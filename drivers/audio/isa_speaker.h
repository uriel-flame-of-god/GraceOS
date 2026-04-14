// ============================
// GraceOS ISA Speaker Audio Fallback
// Legacy PC speaker playback backend
// ============================

#ifndef GRACEOS_ISA_SPEAKER_H
#define GRACEOS_ISA_SPEAKER_H

#include "../../lib/libc/int.h"

/* Initialize ISA speaker backend. */
void isa_speaker_init(void);

/* Force speaker off/silent. */
void isa_speaker_stop(void);

/*
 * Play PCM data using a coarse legacy speaker approximation.
 * This is a fallback path for systems where HDA is unavailable.
 * Returns 0 on success, -1 on invalid input.
 */
int isa_speaker_play_pcm(const uint8_t* pcm,
                         uint32_t byte_count,
                         uint32_t sample_rate,
                         uint32_t channels,
                         uint32_t bits_per_sample);

#endif /* GRACEOS_ISA_SPEAKER_H */
