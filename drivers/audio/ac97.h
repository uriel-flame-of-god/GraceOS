// ============================
// GraceOS AC'97 Driver
// QEMU -device AC97 backend
// ============================

#ifndef GRACEOS_AC97_H
#define GRACEOS_AC97_H

#include "../../lib/libc/int.h"

/* Initialize AC'97 controller. Returns 1 if available, 0 otherwise. */
int ac97_init(void);

/* Returns 1 when AC'97 backend is available. */
int ac97_present(void);

/* Configure playback format (best effort). */
int ac97_set_format(uint32_t sample_rate, uint32_t channels, uint32_t bits_per_sample);

/* Stop playback. */
void ac97_stop(void);

/* Play PCM (16-bit stereo path). Returns 0 on success, -1 on error. */
int ac97_play_pcm(const uint8_t* pcm,
                  uint32_t byte_count,
                  uint32_t sample_rate,
                  uint32_t channels,
                  uint32_t bits_per_sample);

/* Set master volume 0-100 (best effort). */
void ac97_set_volume(uint8_t vol);

#endif /* GRACEOS_AC97_H */
