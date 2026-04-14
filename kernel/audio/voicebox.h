// ============================
// GraceOS VoiceBox Core
// Kernel audio hardware abstraction + SASY ring buffers
// Placement: kernel/audio/
// ============================

#ifndef GRACEOS_VOICEBOX_H
#define GRACEOS_VOICEBOX_H

#include "../../lib/libc/int.h"
#include "../mm/sasy/sasy.h"

/* ============================
   Configuration
   ============================ */

#define VBOX_MAX_STREAMS        4       /* Maximum concurrent audio streams */
#define VBOX_RING_BUFFER_SIZE   (256 * 1024)  /* 256 KB ring buffer per stream */
#define VBOX_DMA_BUFFER_SIZE    (64  * 1024)  /* 64 KB DMA transfer buffer */

/* ============================
   Audio Ring Buffer
   SASY-backed lock-free SPSC ring buffer.
   Producer: VoiceBox Server (decoder output)
   Consumer: Hardware feed task (DMA submission)
   ============================ */

typedef struct audio_ring {
    seg_handle_t    seg;            /* SASY segment backing the buffer */
    uint8_t*        base;           /* Locked virtual address */
    uint32_t        capacity;       /* Total capacity in bytes */
    volatile uint32_t write_pos;   /* Producer write position (atomic) */
    volatile uint32_t read_pos;    /* Consumer read position (atomic) */
} audio_ring_t;

/* Write bytes to ring buffer.  Returns bytes written, or -1 on fatal mapping error. */
int audio_ring_write(audio_ring_t* r, const uint8_t* src, uint32_t len);

/* Read bytes from ring buffer.  Returns bytes actually read. */
uint32_t audio_ring_read(audio_ring_t* r, uint8_t* dst, uint32_t len);

/* Bytes available to read */
uint32_t audio_ring_avail(const audio_ring_t* r);

/* Free space for writing */
uint32_t audio_ring_free(const audio_ring_t* r);

/* ============================
   Audio Stream
   ============================ */

typedef enum audio_stream_state {
    AUDIO_STREAM_IDLE    = 0,
    AUDIO_STREAM_OPEN    = 1,
    AUDIO_STREAM_RUNNING = 2,
    AUDIO_STREAM_STOPPED = 3,
} audio_stream_state_t;

typedef struct audio_stream {
    uint32_t            handle;         /* Stream handle (1-based) */
    audio_stream_state_t state;
    uint32_t            sample_rate;
    uint32_t            channels;
    uint32_t            bits;
    uint8_t             volume;         /* 0–100 */
    audio_ring_t        ring;           /* PCM ring buffer */
    seg_handle_t        dma_seg;        /* DMA bounce buffer (SASY, NOCACHE) */
    uint8_t*            dma_buf;        /* DMA buffer virtual address */
    uint32_t            dma_size;       /* DMA buffer size */
   uint32_t            dma_fill;       /* bytes accumulated in dma_buf */
} audio_stream_t;

typedef enum audio_backend {
   AUDIO_BACKEND_AUTO = 0,
   AUDIO_BACKEND_HDA  = 1,
   AUDIO_BACKEND_ISA  = 2,
   AUDIO_BACKEND_SB16 = 3,
   AUDIO_BACKEND_AC97 = 4,
} audio_backend_t;

/* ============================
   VoiceBox Core API
   (called by VoiceBox Server and syscall bridge)
   ============================ */

/* Initialize VoiceBox Core (called from kmain after sasy_init) */
void voicebox_init(void);

/* Check if core is ready */
int voicebox_ready(void);

/* Set active audio backend preference (AUTO/HDA/ISA). */
int voicebox_set_backend(audio_backend_t backend);

/* Get current backend preference. */
audio_backend_t voicebox_get_backend(void);

/* Open an output stream.
   Returns stream handle (> 0) or -1 on failure. */
int audio_out_open(uint32_t sample_rate, uint32_t channels, uint32_t bits);

/* Submit PCM data from a SASY segment to the ring buffer.
   seg_handle: SASY segment containing PCM data.
   byte_count: how many bytes to transfer from segment.
   Returns bytes enqueued, or -1 on error. */
int audio_out_write(int handle, seg_handle_t seg_handle, uint32_t byte_count);

/* Submit PCM data from a raw kernel pointer (server-internal use). */
int audio_out_write_raw(int handle, const uint8_t* pcm, uint32_t byte_count);

/* Flush backend buffered PCM for this stream (if any). */
int audio_out_flush(int handle);

/* Start playback */
int audio_out_start(int handle);

/* Stop playback */
int audio_out_stop(int handle);

/* Close stream and free resources */
int audio_out_close(int handle);

/* Control stream parameters */
int audio_out_ctl(int handle, uint32_t cmd, uint32_t value);

/* Hardware feed task — called periodically to push ring buffer → DMA.
   Registered in the scheduler as a short audio task. */
void voicebox_hw_feed(void);

/* ============================
   Embedded Sample Symbols
   Defined by linker script from kernel/samples/sample.mp3
   ============================ */

extern uint8_t _sample_mp3_start[];
extern uint8_t _sample_mp3_end[];

/* Size of embedded MP3 sample in bytes */
static inline uint32_t voicebox_sample_mp3_size(void)
{
    return (uint32_t)(_sample_mp3_end - _sample_mp3_start);
}

/* Generate a sine-wave test tone (440 Hz, 1s, 48kHz/16-bit/stereo).
   Returns 0 on success, -1 on write failure. */
int voicebox_gen_test_tone(int stream_handle);

#endif /* GRACEOS_VOICEBOX_H */
