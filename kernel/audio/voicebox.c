// ============================
// GraceOS VoiceBox Core
// Kernel audio hardware abstraction + SASY ring buffers
// ============================

#include "voicebox.h"
#include "../../drivers/audio/ac97.h"
#include "../../drivers/audio/hda.h"
#include "../../drivers/audio/isa_speaker.h"
#include "../../drivers/audio/sb16.h"
#include "../mm/sasy/sasy.h"
#include "../mm/kheap.h"
#include "../log/klog.h"
#include "../../lib/libc/string.h"
#include "../../include/grace/audio_syscalls.h"

/* ============================
   Global State
   ============================ */

static int g_vbox_ready = 0;
static audio_backend_t g_backend = AUDIO_BACKEND_AUTO;

static audio_stream_t g_streams[VBOX_MAX_STREAMS];
static uint32_t g_stream_count = 0;

static int backend_use_hda(void)
{
    if (g_backend == AUDIO_BACKEND_HDA)
        return hda_present() ? 1 : 0;
    if (g_backend == AUDIO_BACKEND_ISA || g_backend == AUDIO_BACKEND_SB16 || g_backend == AUDIO_BACKEND_AC97)
        return 0;
    return (!sb16_present() && !ac97_present() && hda_present()) ? 1 : 0;
}

static int backend_use_sb16(void)
{
    if (g_backend == AUDIO_BACKEND_HDA || g_backend == AUDIO_BACKEND_AC97)
        return 0;
    if (g_backend == AUDIO_BACKEND_SB16)
        return sb16_present() ? 1 : 0;
    if (g_backend == AUDIO_BACKEND_ISA)
        return 0;
    return sb16_present() ? 1 : 0;
}

static int backend_use_ac97(void)
{
    if (g_backend == AUDIO_BACKEND_HDA || g_backend == AUDIO_BACKEND_SB16)
        return 0;
    if (g_backend == AUDIO_BACKEND_AC97)
        return ac97_present() ? 1 : 0;
    if (g_backend == AUDIO_BACKEND_ISA)
        return 0;
    return (!sb16_present() && ac97_present()) ? 1 : 0;
}

static int backend_use_isa(void)
{
    if (g_backend == AUDIO_BACKEND_HDA)
        return 0;
    if (g_backend == AUDIO_BACKEND_SB16)
        return 0;
    if (g_backend == AUDIO_BACKEND_AC97)
        return 0;
    if (g_backend == AUDIO_BACKEND_ISA)
        return 1;
    return (sb16_present() || ac97_present() || hda_present()) ? 0 : 1;
}

/* ============================
   Lock-Free Ring Buffer
   Single-producer, single-consumer
   Using GCC atomic builtins for memory ordering
   ============================ */

uint32_t audio_ring_avail(const audio_ring_t* r)
{
    uint32_t w = __atomic_load_n(&r->write_pos, __ATOMIC_ACQUIRE);
    uint32_t rd = __atomic_load_n(&r->read_pos, __ATOMIC_ACQUIRE);
    return (w - rd) & (r->capacity - 1);
}

uint32_t audio_ring_free(const audio_ring_t* r)
{
    return r->capacity - 1 - audio_ring_avail(r);
}

int audio_ring_write(audio_ring_t* r, const uint8_t* src, uint32_t len)
{
    if (!r || !r->base || !src)
        return -1;

    uint32_t free_space = audio_ring_free(r);
    if (len > free_space) len = free_space;
    if (len == 0) return 0;

    uint32_t w = __atomic_load_n(&r->write_pos, __ATOMIC_RELAXED);
    uint32_t first = r->capacity - (w & (r->capacity - 1));

    if (first > len) first = len;
    uint32_t second = len - first;

    memcpy(r->base + (w & (r->capacity - 1)), src, first);
    if (second)
        memcpy(r->base, src + first, second);

    __atomic_store_n(&r->write_pos, w + len, __ATOMIC_RELEASE);
    return len;
}

uint32_t audio_ring_read(audio_ring_t* r, uint8_t* dst, uint32_t len)
{
    uint32_t avail = audio_ring_avail(r);
    if (len > avail) len = avail;
    if (len == 0) return 0;

    uint32_t rd = __atomic_load_n(&r->read_pos, __ATOMIC_RELAXED);
    uint32_t first = r->capacity - (rd & (r->capacity - 1));

    if (first > len) first = len;
    uint32_t second = len - first;

    memcpy(dst, r->base + (rd & (r->capacity - 1)), first);
    if (second)
        memcpy(dst + first, r->base, second);

    __atomic_store_n(&r->read_pos, rd + len, __ATOMIC_RELEASE);
    return len;
}

/* ============================
   Stream Helpers
   ============================ */

static audio_stream_t* find_stream(int handle)
{
    if (handle < 1 || handle > (int)VBOX_MAX_STREAMS)
        return 0;
    audio_stream_t* s = &g_streams[handle - 1];
    if (s->state == AUDIO_STREAM_IDLE)
        return 0;
    return s;
}

static int alloc_stream(void)
{
    for (uint32_t i = 0; i < VBOX_MAX_STREAMS; i++)
    {
        if (g_streams[i].state == AUDIO_STREAM_IDLE)
            return (int)(i + 1);  /* 1-based handle */
    }
    return -1;
}

static int ring_init(audio_ring_t* r, uint32_t capacity)
{
    /* Capacity must be a power of 2 */
    uint32_t cap = 1;
    while (cap < capacity) cap <<= 1;

    r->seg = (seg_handle_t)-1;
    r->base = 0;

    /* Try requested size first, then progressively smaller powers of two. */
    for (uint32_t try_cap = cap; try_cap >= (16 * 1024); try_cap >>= 1)
    {
        r->base = (uint8_t*)kmalloc(try_cap);
        if (r->base)
        {
            cap = try_cap;
            break;
        }
    }

    if (!r->base)
        return -1;

    r->capacity = cap;
    __atomic_store_n(&r->write_pos, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&r->read_pos,  0, __ATOMIC_RELAXED);

    memset(r->base, 0, cap);
    return 0;
}

static void ring_destroy(audio_ring_t* r)
{
    if (r->base)
    {
        kfree(r->base);
        r->base = 0;
    }

    r->seg = (seg_handle_t)-1;
}

/* ============================
   VoiceBox Core API
   ============================ */

void voicebox_init(void)
{
    memset(g_streams, 0, sizeof(g_streams));
    for (uint32_t i = 0; i < VBOX_MAX_STREAMS; i++)
    {
        g_streams[i].state  = AUDIO_STREAM_IDLE;
        g_streams[i].handle = (uint32_t)(i + 1);
        g_streams[i].ring.seg = (seg_handle_t)-1;
        g_streams[i].dma_seg  = (seg_handle_t)-1;
    }

    isa_speaker_init();
    sb16_init();
    ac97_init();

    int hda_ok = hda_init();
    if (hda_ok)
        klog_logn("[VoiceBox] HDA controller ready");
    else
        klog_warn("[VoiceBox] No HDA; beeper fallback active");

    g_backend = AUDIO_BACKEND_SB16;

    g_vbox_ready = 1;
    klog_logn("[VoiceBox] Core initialized");
}

int voicebox_ready(void)
{
    return g_vbox_ready;
}

int voicebox_set_backend(audio_backend_t backend)
{
    if (backend != AUDIO_BACKEND_AUTO &&
        backend != AUDIO_BACKEND_HDA &&
        backend != AUDIO_BACKEND_ISA &&
        backend != AUDIO_BACKEND_SB16 &&
        backend != AUDIO_BACKEND_AC97)
        return -1;

    g_backend = backend;
    return 0;
}

audio_backend_t voicebox_get_backend(void)
{
    return g_backend;
}

int audio_out_open(uint32_t sample_rate, uint32_t channels, uint32_t bits)
{
    if (!g_vbox_ready) return -1;

    int handle = alloc_stream();
    if (handle < 0)
    {
        klog_warn("[VoiceBox] No free streams");
        return -1;
    }

    audio_stream_t* s = &g_streams[handle - 1];

    /* Allocate ring buffer via SASY */
    if (ring_init(&s->ring, VBOX_RING_BUFFER_SIZE) < 0)
    {
        klog_warn("[VoiceBox] Ring buffer allocation failed");
        return -1;
    }

    /* Allocate DMA bounce buffer (uncached) */
    s->dma_seg = (seg_handle_t)-1;
    s->dma_buf = (uint8_t*)kmalloc(VBOX_DMA_BUFFER_SIZE);
    if (!s->dma_buf)
    {
        klog_warn("[VoiceBox] DMA buffer allocation failed; using ring only");
        s->dma_size = 0;
    }
    else
    {
        s->dma_size = VBOX_DMA_BUFFER_SIZE;
        memset(s->dma_buf, 0, s->dma_size);
    }

    s->sample_rate = sample_rate;
    s->channels    = channels;
    s->bits        = bits;
    s->volume      = 80;
    s->dma_fill    = 0;
    s->state       = AUDIO_STREAM_OPEN;

    if (backend_use_hda())
        hda_set_format(sample_rate, channels, bits);
    else if (backend_use_ac97())
        ac97_set_format(sample_rate, channels, bits);

    klog_logn("[VoiceBox] Stream opened");
    return handle;
}

int audio_out_write(int handle, seg_handle_t seg_handle, uint32_t byte_count)
{
    audio_stream_t* s = find_stream(handle);
    if (!s) return -1;

    /* Lock the caller's SASY segment to get the PCM data */
    uint8_t* pcm = (uint8_t*)sasy_lock(seg_handle);
    if (!pcm) return -1;

    int written = audio_ring_write(&s->ring, pcm, byte_count);
    sasy_unlock(seg_handle);

    return written;
}

int audio_out_write_raw(int handle, const uint8_t* pcm, uint32_t byte_count)
{
    audio_stream_t* s = find_stream(handle);
    if (!s || !pcm) return -1;

    /*
     * Direct-call playback path with accumulation for audible continuity.
     * AC97 benefits from larger transfers; tiny bursts cause jitter from frequent reprogramming.
     */
    if ((backend_use_hda() || backend_use_ac97()) && s->state == AUDIO_STREAM_RUNNING && s->dma_buf && s->dma_size)
    {
        uint32_t done = 0;
        while (done < byte_count)
        {
            uint32_t space = s->dma_size - s->dma_fill;
            if (space == 0)
            {
                if (backend_use_hda())
                {
                    if (hda_play_buffer_blocking((uint64_t)s->dma_buf, s->dma_fill) < 0)
                        return -1;
                }
                else
                {
                    if (ac97_play_pcm(s->dma_buf, s->dma_fill, s->sample_rate, s->channels, s->bits) < 0)
                        return -1;
                }
                s->dma_fill = 0;
                space = s->dma_size;
            }

            uint32_t n = byte_count - done;
            if (n > space)
                n = space;

            memcpy(s->dma_buf + s->dma_fill, pcm + done, n);
            s->dma_fill += n;
            done += n;

            if (s->dma_fill >= (s->dma_size / 2))
            {
                if (backend_use_hda())
                {
                    if (hda_play_buffer_blocking((uint64_t)s->dma_buf, s->dma_fill) < 0)
                        return -1;
                }
                else
                {
                    if (ac97_play_pcm(s->dma_buf, s->dma_fill, s->sample_rate, s->channels, s->bits) < 0)
                        return -1;
                }
                s->dma_fill = 0;
            }
        }
        return (int)byte_count;
    }

    if (backend_use_isa() && s->state == AUDIO_STREAM_RUNNING)
    {
        if (isa_speaker_play_pcm(pcm, byte_count, s->sample_rate, s->channels, s->bits) < 0)
            return -1;
        return (int)byte_count;
    }

    if (backend_use_sb16() && s->state == AUDIO_STREAM_RUNNING)
    {
        if (sb16_play_pcm(pcm, byte_count, s->sample_rate, s->channels, s->bits) < 0)
            return -1;
        return (int)byte_count;
    }

    if (backend_use_ac97() && s->state == AUDIO_STREAM_RUNNING)
    {
        if (ac97_play_pcm(pcm, byte_count, s->sample_rate, s->channels, s->bits) < 0)
            return -1;
        return (int)byte_count;
    }

    int written = audio_ring_write(&s->ring, pcm, byte_count);
    if (written < 0)
        return -1;

    /* Feed hardware immediately in direct-call mode so audio is not left queued. */
    if (s->state == AUDIO_STREAM_RUNNING)
        voicebox_hw_feed();

    return written;
}

int audio_out_start(int handle)
{
    audio_stream_t* s = find_stream(handle);
    if (!s) return -1;
    if (s->state == AUDIO_STREAM_RUNNING) return 0;

    s->state = AUDIO_STREAM_RUNNING;

    if (backend_use_hda())
        hda_start();
    else if (backend_use_sb16())
        klog_log("[VoiceBox] Playback started (SB16 DMA backend)");
    else if (backend_use_ac97())
        klog_log("[VoiceBox] Playback started (AC97 DMA backend)");
    else if (backend_use_isa())
        klog_log("[VoiceBox] Playback started (ISA speaker fallback)");
    else
        return -1;

    return 0;
}

int audio_out_stop(int handle)
{
    audio_stream_t* s = find_stream(handle);
    if (!s) return -1;

    if (backend_use_hda() && s->state == AUDIO_STREAM_RUNNING && s->dma_buf && s->dma_fill)
    {
        if (hda_play_buffer_blocking((uint64_t)s->dma_buf, s->dma_fill) < 0)
            return -1;
        s->dma_fill = 0;
    }

    if (backend_use_ac97() && s->state == AUDIO_STREAM_RUNNING && s->dma_buf && s->dma_fill)
    {
        if (ac97_play_pcm(s->dma_buf, s->dma_fill, s->sample_rate, s->channels, s->bits) < 0)
            return -1;
        s->dma_fill = 0;
    }

    s->state = AUDIO_STREAM_STOPPED;

    if (backend_use_hda())
        hda_stop();
    else if (backend_use_sb16())
        sb16_stop();
    else if (backend_use_ac97())
        ac97_stop();
    else if (backend_use_isa())
        isa_speaker_stop();

    s->dma_fill = 0;

    return 0;
}

int audio_out_flush(int handle)
{
    audio_stream_t* s = find_stream(handle);
    if (!s) return -1;

    if (backend_use_hda() && s->state == AUDIO_STREAM_RUNNING && s->dma_buf && s->dma_fill)
    {
        if (hda_play_buffer_blocking((uint64_t)s->dma_buf, s->dma_fill) < 0)
            return -1;
        s->dma_fill = 0;
    }

    if (backend_use_ac97() && s->state == AUDIO_STREAM_RUNNING && s->dma_buf && s->dma_fill)
    {
        if (ac97_play_pcm(s->dma_buf, s->dma_fill, s->sample_rate, s->channels, s->bits) < 0)
            return -1;
        s->dma_fill = 0;
    }

    return 0;
}

int audio_out_close(int handle)
{
    audio_stream_t* s = find_stream(handle);
    if (!s) return -1;

    if (s->state == AUDIO_STREAM_RUNNING)
        audio_out_stop(handle);

    ring_destroy(&s->ring);

    if (s->dma_buf)
    {
        kfree(s->dma_buf);
        s->dma_buf = 0;
    }

    s->dma_seg = (seg_handle_t)-1;
    s->dma_fill = 0;

    s->state = AUDIO_STREAM_IDLE;
    return 0;
}

int audio_out_ctl(int handle, uint32_t cmd, uint32_t value)
{
    audio_stream_t* s = find_stream(handle);
    if (!s) return -1;

    switch (cmd)
    {
        case AUDIO_CTL_SET_VOLUME:
            if (value > 100) value = 100;
            s->volume = (uint8_t)value;
            if (backend_use_hda()) hda_set_volume((uint8_t)value);
            if (backend_use_ac97()) ac97_set_volume((uint8_t)value);
            return 0;
        case AUDIO_CTL_MUTE:
            if (backend_use_hda()) hda_set_volume(value ? 0 : s->volume);
            if (backend_use_ac97()) ac97_set_volume(value ? 0 : s->volume);
            return 0;
        case AUDIO_CTL_SET_RATE:
            s->sample_rate = value;
            if (backend_use_hda()) hda_set_format(value, s->channels, s->bits);
            if (backend_use_ac97()) ac97_set_format(value, s->channels, s->bits);
            return 0;
        default:
            return -1;
    }
}

/* ============================
   Hardware Feed Task
   Drains the ring buffer into the HDA DMA engine.
   Called periodically (e.g., every 10ms from scheduler tick).
   ============================ */

void voicebox_hw_feed(void)
{
    if (!g_vbox_ready || !backend_use_hda()) return;

    for (uint32_t i = 0; i < VBOX_MAX_STREAMS; i++)
    {
        audio_stream_t* s = &g_streams[i];
        if (s->state != AUDIO_STREAM_RUNNING) continue;
        if (!s->dma_buf) continue;

        uint32_t avail = audio_ring_avail(&s->ring);
        if (avail == 0) continue;

        if (avail > s->dma_size) avail = s->dma_size;
        uint32_t n = audio_ring_read(&s->ring, s->dma_buf, avail);

        if (n > 0)
        {
            /* Submit to HDA DMA — physical address is identity-mapped */
            uint64_t phys = (uint64_t)s->dma_buf;
            hda_submit_buffer(phys, n);
        }
    }
}
