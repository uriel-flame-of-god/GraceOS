// ============================
// GraceOS Sound Blaster 16 Driver
// ISA DMA audio backend for QEMU -device sb16
// ============================

#ifndef GRACEOS_SB16_H
#define GRACEOS_SB16_H

#include "../../lib/libc/int.h"

/* Initialize SB16 at the default base (0x220). Returns 1 if ready, 0 otherwise. */
int sb16_init(void);

/* Returns 1 when SB16 backend is available. */
int sb16_present(void);

/* Stop/pause current DMA playback if active. */
void sb16_stop(void);

/* Play PCM through SB16 DMA (16-bit stereo path). Returns 0 on success, -1 on error. */
int sb16_play_pcm(const uint8_t* pcm,
                  uint32_t byte_count,
                  uint32_t sample_rate,
                  uint32_t channels,
                  uint32_t bits_per_sample);

#endif /* GRACEOS_SB16_H */
