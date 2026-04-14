// ============================
// GraceOS VoiceBox Server
// minit-managed audio decoding service
// Placement: kernel/audio/
// ============================

#ifndef GRACEOS_VOICEBOX_SERVER_H
#define GRACEOS_VOICEBOX_SERVER_H

#include "../../lib/libc/int.h"

/* ============================
   VBox Unit Types
   ============================ */

typedef enum vbox_unit_type {
    VBOX_SOURCE,    /* Audio source (file decoder) */
    VBOX_FILTER,    /* Audio filter / effect */
    VBOX_SINK,      /* Output sink (PCM to kernel) */
} vbox_unit_type_t;

/* ============================
   VBox Unit Descriptor
   ============================ */

typedef struct voicebox_unit {
    char            name[32];
    vbox_unit_type_t type;
    int             stream_handle;  /* VoiceBox Core stream handle */

    /* Decoder state (union covers WAV and MP3) */
    union {
        struct {
            const uint8_t*  data;       /* PCM data pointer */
            uint32_t        size;       /* Total bytes */
            uint32_t        pos;        /* Read position */
            uint32_t        sample_rate;
            uint16_t        channels;
            uint16_t        bits_per_sample;
        } wav;

        struct {
            const uint8_t*  data;       /* MP3 stream pointer */
            uint32_t        size;       /* Total bytes */
            uint32_t        pos;        /* Read position */
            void*           dec_state;  /* mp3dec_t* (opaque) */
        } mp3;
    };

    /* Chain: next unit in graph */
    struct voicebox_unit* next;
} voicebox_unit_t;

/* ============================
   VoiceBox Server API
   ============================ */

/* Server main entry point — registered with minit */
void voicebox_server_main(void);

/* Returns 1 once server init path has completed. */
int voicebox_server_ready(void);

/* Request the server to play a file from BranchFS */
int voicebox_play_file(const char* path);

/* Request playback of the embedded MP3 sample */
int voicebox_play_sample(void);

/* Stop current playback */
void voicebox_stop(void);

/* Get playback state (0 = idle, 1 = playing) */
int voicebox_is_playing(void);

/* ============================
   Decoder helpers (internal)
   ============================ */

/* Parse WAV header, set up unit, return 0 on success */
int wav_open(voicebox_unit_t* u, const uint8_t* data, uint32_t size);

/* Decode next chunk of WAV into PCM.  Returns bytes decoded. */
uint32_t wav_decode_chunk(voicebox_unit_t* u, int stream_handle, uint32_t max_bytes);

/* Open MP3 stream for decoding */
int mp3_open(voicebox_unit_t* u, const uint8_t* data, uint32_t size);

/* Decode next MP3 frame into PCM.  Returns samples decoded. */
int mp3_decode_frame(voicebox_unit_t* u, int stream_handle);

/* Generate a sine-wave test tone (440 Hz, 1s, 48kHz/16-bit/stereo).
    Returns 0 on success, -1 on write failure. */
int voicebox_gen_test_tone(int stream_handle);

#endif /* GRACEOS_VOICEBOX_SERVER_H */
