// ============================
// GraceOS VoiceBox Server
// minit-managed audio decoding service
// Implements: WAV decoder, MP3 wrapper, embedded sample playback
// ============================

#include "voicebox_server.h"
#include "voicebox.h"
#include "../../include/grace/audio_syscalls.h"
#include "../../drivers/audio/hda.h"
#include "../../drivers/storage/bfs.h"
#include "../../drivers/input/keyboard.h"
#include "../mm/kheap.h"
#include "../log/klog.h"
#include "../../lib/libc/string.h"

/* ============================
   minimp3 Integration
   ============================ */

/* Redirect stdlib/string includes to our kernel implementations */
#define memcpy   __builtin_memcpy
#define memset   __builtin_memset
#define memcmp   __builtin_memcmp
#define abs(x)   ((x) < 0 ? -(x) : (x))

/* Tell minimp3 to compile its implementation here */
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3        /* Skip Layer 1/2 support */
#define MINIMP3_NO_SIMD         /* No intrinsic headers available with -nostdinc */
#include "minimp3.h"

#undef memcpy
#undef memset
#undef memcmp
#undef abs

/* Restore our versions */
#include "../../lib/libc/string.h"

/* ============================
   External BFS instance
   ============================ */

extern struct bfs_instance g_bfs;

/* ============================
   Server Global State
   ============================ */

static int g_server_playing  = 0;
static int g_server_stream   = -1;
static int g_server_ready    = 0;

/* Static mp3 decoder state (avoid heap for reliability) */
static mp3dec_t g_mp3dec;

/* PCM output buffer for one frame (max 2304 samples * 2 channels * 2 bytes) */
static int16_t g_pcm_frame[MINIMP3_MAX_SAMPLES_PER_FRAME];

/* ============================
   WAV Decoder
   ============================
   WAV RIFF structure:
   "RIFF" [4] + size[4] + "WAVE"[4]
   "fmt "[4] + chunk_size[4] + audio_format[2] + channels[2]
              + sample_rate[4] + byte_rate[4] + block_align[2]
              + bits_per_sample[2]
   "data"[4] + data_size[4] + [PCM bytes]
   ============================ */

static uint16_t wav_read16(const uint8_t* p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t wav_read32(const uint8_t* p)
{
    return (uint32_t)(p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24));
}

int wav_open(voicebox_unit_t* u, const uint8_t* data, uint32_t size)
{
    if (!data || size < 44) return -1;

    /* Check RIFF+WAVE magic */
    if (data[0]!='R'||data[1]!='I'||data[2]!='F'||data[3]!='F') return -1;
    if (data[8]!='W'||data[9]!='A'||data[10]!='V'||data[11]!='E') return -1;

    /* Find fmt chunk */
    uint32_t offset = 12;
    uint32_t fmt_offset = 0;
    uint32_t data_offset = 0;
    uint32_t data_size   = 0;

    while (offset + 8 <= size)
    {
        uint32_t chunk_size = wav_read32(data + offset + 4);

        if (data[offset]=='f' && data[offset+1]=='m' &&
            data[offset+2]=='t' && data[offset+3]==' ')
        {
            fmt_offset = offset + 8;
        }
        else if (data[offset]=='d' && data[offset+1]=='a' &&
                 data[offset+2]=='t' && data[offset+3]=='a')
        {
            data_offset = offset + 8;
            data_size   = chunk_size;
        }

        offset += 8 + chunk_size;
        if (chunk_size & 1) offset++;  /* Word-align */
    }

    if (!fmt_offset || !data_offset) return -1;

    const uint8_t* fmt = data + fmt_offset;
    uint16_t audio_fmt     = wav_read16(fmt + 0);
    uint16_t channels      = wav_read16(fmt + 2);
    uint32_t sample_rate   = wav_read32(fmt + 4);
    uint16_t bits          = wav_read16(fmt + 14);

    if (audio_fmt != 1) return -1;  /* PCM only */
    if (channels < 1 || channels > 8) return -1;
    if (bits != 8 && bits != 16 && bits != 24) return -1;

    u->wav.data          = data + data_offset;
    u->wav.size          = data_size;
    u->wav.pos           = 0;
    u->wav.sample_rate   = sample_rate;
    u->wav.channels      = channels;
    u->wav.bits_per_sample = bits;

    klog_log("[VoiceBox] WAV opened");
    return 0;
}

uint32_t wav_decode_chunk(voicebox_unit_t* u, int stream_handle, uint32_t max_bytes)
{
    if (!u || u->wav.pos >= u->wav.size) return 0;

    uint32_t remaining = u->wav.size - u->wav.pos;
    uint32_t to_write  = remaining < max_bytes ? remaining : max_bytes;

    int written = audio_out_write_raw(stream_handle,
                                      u->wav.data + u->wav.pos,
                                      to_write);
    if (written > 0)
        u->wav.pos += (uint32_t)written;

    return (uint32_t)(written > 0 ? written : 0);
}

/* ============================
   MP3 Decoder (minimp3 wrapper)
   ============================ */

int mp3_open(voicebox_unit_t* u, const uint8_t* data, uint32_t size)
{
    if (!data || size < 4) return -1;

    mp3dec_init(&g_mp3dec);

    u->mp3.data      = data;
    u->mp3.size      = size;
    u->mp3.pos       = 0;
    u->mp3.dec_state = (void*)&g_mp3dec;

    klog_log("[VoiceBox] MP3 stream opened");
    return 0;
}

int mp3_decode_frame(voicebox_unit_t* u, int stream_handle)
{
    if (!u || u->mp3.pos >= u->mp3.size) return 0;

    mp3dec_t* dec = (mp3dec_t*)u->mp3.dec_state;
    mp3dec_frame_info_t info;

    const uint8_t* mp3_ptr   = u->mp3.data + u->mp3.pos;
    int            mp3_bytes = (int)(u->mp3.size - u->mp3.pos);

    int samples = mp3dec_decode_frame(dec, mp3_ptr, mp3_bytes,
                                      g_pcm_frame, &info);

    if (info.frame_bytes <= 0) return -1;  /* End or error */
    u->mp3.pos += (uint32_t)info.frame_bytes;

    if (samples > 0)
    {
        uint32_t byte_count = (uint32_t)(samples * info.channels * sizeof(int16_t));
        if (audio_out_write_raw(stream_handle, (uint8_t*)g_pcm_frame, byte_count) < 0)
            return -1;
    }

    return samples;
}

/* ============================
   Sine-wave Test Tone
   44100 Hz, 16-bit, stereo, 440 Hz, 1 second
   ============================ */

int voicebox_gen_test_tone(int stream_handle)
{
    /* 44100 * 16-bit * 2ch = 176400 bytes for 1 second.
       Generate in 4KB chunks to avoid a huge stack buffer. */
    #define TONE_RATE   44100
    #define TONE_FREQ   440
    #define TONE_CHUNK  2048  /* samples per chunk */

    int16_t buf[TONE_CHUNK * 2];  /* stereo */
    uint32_t total_samples = TONE_RATE;  /* 1 second */
    uint32_t written = 0;

    /* Use a fixed-point sine approximation (parabola):
       sin(x) ≈ 4x(pi-x)/pi^2 for x in [0, pi]
       We use Q16 fixed-point arithmetic. */

    /* Phase accumulator: increments by freq/rate per sample */
    /* period = TONE_RATE / TONE_FREQ = 100.22... samples  */
    uint32_t phase     = 0;
    uint32_t phase_inc = (TONE_FREQ * 65536u) / TONE_RATE;

    while (written < total_samples)
    {
        uint32_t chunk = total_samples - written;
        if (chunk > TONE_CHUNK) chunk = TONE_CHUNK;

        for (uint32_t i = 0; i < chunk; i++)
        {
            /* Simple triangle wave (no transcendentals needed) */
            uint32_t p = phase & 0xFFFF;           /* 0..65535 */
            int32_t  s;
            if (p < 32768)
                s = (int32_t)(p * 2) - 32768;      /* rising */
            else
                s = 32768 - (int32_t)((p - 32768) * 2);  /* falling */

            /* Scale to 16-bit range at ~75% volume */
            int16_t sample = (int16_t)((s * 24576) >> 15);
            buf[i * 2 + 0] = sample;  /* left */
            buf[i * 2 + 1] = sample;  /* right */

            phase += phase_inc;
        }

        if (audio_out_write_raw(stream_handle, (uint8_t*)buf,
                                chunk * 2 * sizeof(int16_t)) < 0)
            return -1;
        written += chunk;
    }
    #undef TONE_RATE
    #undef TONE_FREQ
    #undef TONE_CHUNK
    return 0;
}

/* ============================
   File Playback
   ============================ */

int voicebox_play_file(const char* path)
{
    if (!path) return -1;

    /* Read file from BranchFS */
    uint64_t fsize64 = 0;
    if (bfs_file_size(&g_bfs, path, &fsize64) < 0 || fsize64 == 0)
    {
        klog_warn("[VoiceBox] File not found");
        return -1;
    }
    uint32_t fsize = (uint32_t)fsize64;

    uint8_t* fbuf = (uint8_t*)kmalloc(fsize);
    if (!fbuf) { klog_warn("[VoiceBox] Out of memory for file"); return -1; }

    uint64_t out_size = 0;
    if (bfs_read_file(&g_bfs, path, fbuf, fsize, &out_size) < 0)
    {
        kfree(fbuf);
        klog_warn("[VoiceBox] Read failed");
        return -1;
    }

    /* Detect format by extension */
    uint32_t plen = 0;
    while (path[plen]) plen++;
    int is_mp3 = (plen >= 4 &&
                  path[plen-3]=='m' && path[plen-2]=='p' && path[plen-1]=='3');
    int is_wav = (plen >= 3 &&
                  path[plen-3]=='w' && path[plen-2]=='a' && path[plen-1]=='v');

    /* Detect by magic if extension unclear */
    if (!is_mp3 && !is_wav)
    {
        if (fbuf[0]=='R'&&fbuf[1]=='I'&&fbuf[2]=='F'&&fbuf[3]=='F') is_wav = 1;
        else is_mp3 = 1;  /* Assume MP3 */
    }

    /* Open audio stream */
    uint32_t rate = 44100, ch = 2, bits = 16;
    int handle = audio_out_open(rate, ch, bits);
    if (handle < 0)
    {
        kfree(fbuf);
        klog_warn("[VoiceBox] Cannot open audio stream");
        return -1;
    }

    g_server_stream  = handle;
    g_server_playing = 1;
    audio_out_start(handle);

    voicebox_unit_t unit;
    memset(&unit, 0, sizeof(unit));
    unit.type = VBOX_SOURCE;
    unit.stream_handle = handle;

    if (is_wav)
    {
        if (wav_open(&unit, fbuf, fsize) < 0)
        {
            kfree(fbuf);
            audio_out_close(handle);
            g_server_playing = 0;
            return -1;
        }

        /* Reconfigure stream with detected format */
        audio_out_ctl(handle, AUDIO_CTL_SET_RATE, unit.wav.sample_rate);
        /* Drain WAV in one shot */
        if (wav_decode_chunk(&unit, handle, unit.wav.size) == 0 && unit.wav.size > 0)
        {
            klog_error("[VoiceBox] WAV write failed");
            audio_out_close(handle);
            g_server_playing = 0;
            g_server_stream  = -1;
            kfree(fbuf);
            return -1;
        }
    }
    else if (is_mp3)
    {
        if (mp3_open(&unit, fbuf, fsize) < 0)
        {
            kfree(fbuf);
            audio_out_close(handle);
            g_server_playing = 0;
            return -1;
        }
        /* Decode all frames */
        int decode_failed = 0;
        while (unit.mp3.pos < unit.mp3.size)
        {
            if (keyboard_consume_ctrl_c())
            {
                klog_warn("[VoiceBox] Playback interrupted (Ctrl+C)");
                audio_out_close(handle);
                g_server_playing = 0;
                g_server_stream  = -1;
                kfree(fbuf);
                return -1;
            }

            int rc = mp3_decode_frame(&unit, handle);
            if (rc < 0)
            {
                decode_failed = 1;
                break;
            }
        }

        if (decode_failed)
        {
            klog_error("[VoiceBox] MP3 decode/write failed");
            audio_out_close(handle);
            g_server_playing = 0;
            g_server_stream  = -1;
            kfree(fbuf);
            return -1;
        }
    }

    /* Flush pending backend buffer and wait for completion then clean up. */
    (void)audio_out_flush(handle);

    if (hda_present())
        hda_poll_complete();
    else
    {
        /* Fallback: beep to indicate something played */
        hda_beep(440, 500);
    }

    audio_out_close(handle);
    g_server_playing = 0;
    g_server_stream  = -1;
    kfree(fbuf);
    return 0;
}

int voicebox_play_sample(void)
{
    uint32_t mp3_size = voicebox_sample_mp3_size();

    if (mp3_size == 0)
    {
        /* No embedded sample — play a synthesised test tone */
        klog_log("[VoiceBox] No embedded sample; playing test tone");
        int handle = audio_out_open(44100, 2, 16);
        if (handle < 0) return -1;

        g_server_stream  = handle;
        g_server_playing = 1;
        audio_out_start(handle);

        if (voicebox_gen_test_tone(handle) < 0)
        {
            klog_error("[VoiceBox] Test tone write failed");
            audio_out_close(handle);
            g_server_playing = 0;
            g_server_stream  = -1;
            return -1;
        }

        if (hda_present())
            hda_poll_complete();
        else
            hda_beep(440, 1000);

        audio_out_close(handle);
        g_server_playing = 0;
        g_server_stream  = -1;
        return 0;
    }

    /* Embedded MP3 sample */
    klog_log("[VoiceBox] Playing embedded MP3 sample");
    int handle = audio_out_open(44100, 2, 16);
    if (handle < 0) return -1;

    g_server_stream  = handle;
    g_server_playing = 1;
    audio_out_start(handle);

    voicebox_unit_t unit;
    memset(&unit, 0, sizeof(unit));
    unit.type = VBOX_SOURCE;
    unit.stream_handle = handle;

    if (mp3_open(&unit, _sample_mp3_start, mp3_size) == 0)
    {
        int decode_failed = 0;

        while (unit.mp3.pos < unit.mp3.size)
        {
            if (keyboard_consume_ctrl_c())
            {
                klog_warn("[VoiceBox] Playback interrupted (Ctrl+C)");
                audio_out_close(handle);
                g_server_playing = 0;
                g_server_stream  = -1;
                return -1;
            }

            int rc = mp3_decode_frame(&unit, handle);
            if (rc < 0)
            {
                decode_failed = 1;
                break;
            }
        }

        if (decode_failed)
        {
            klog_error("[VoiceBox] MP3 decode/write failed");
            audio_out_close(handle);
            g_server_playing = 0;
            g_server_stream  = -1;
            return -1;
        }
    }

    (void)audio_out_flush(handle);

    if (hda_present())
        hda_poll_complete();
    else
        hda_beep(440, 1000);

    audio_out_close(handle);
    g_server_playing = 0;
    g_server_stream  = -1;
    return 0;
}

void voicebox_stop(void)
{
    if (g_server_playing && g_server_stream >= 0)
        audio_out_stop(g_server_stream);
    g_server_playing = 0;
}

int voicebox_is_playing(void)
{
    return g_server_playing;
}

int voicebox_server_ready(void)
{
    return g_server_ready;
}

/* ============================
   Server Main (minit entry)
   ============================ */

void voicebox_server_main(void)
{
    klog_log("[VoiceBox] Server starting");

    /* Nothing to do until a client calls voicebox_play_* directly.
       In a full design this would block on an IPC port / message queue.
       For Phase 1 the server just initialises and returns — clients call
       the voicebox_play_* helpers directly from kernel mode. */

    g_server_ready = 1;
    klog_log("[VoiceBox] Server ready (direct-call mode)");
}
