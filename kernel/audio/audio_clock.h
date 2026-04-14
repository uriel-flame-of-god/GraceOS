// ============================
// GraceOS Audio Clock Service
// Background monotonic clock for audio pacing
// ============================

#ifndef GRACEOS_AUDIO_CLOCK_H
#define GRACEOS_AUDIO_CLOCK_H

#include "../../lib/libc/int.h"

/* minit service entry */
void audio_clock_main(void);

/* Current monotonic audio clock in milliseconds. */
uint64_t audio_clock_now_ms(void);

/* 1 if background audio clock service has started. */
int audio_clock_ready(void);

#endif /* GRACEOS_AUDIO_CLOCK_H */
