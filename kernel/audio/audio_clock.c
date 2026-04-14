// ============================
// GraceOS Audio Clock Service
// Background monotonic clock for audio pacing
// ============================

#include "audio_clock.h"
#include "../include/time.h"
#include "../log/klog.h"

static volatile uint64_t g_audio_clock_ms = 0;
static volatile uint8_t g_audio_clock_live = 0;

uint64_t audio_clock_now_ms(void)
{
    uint64_t bg = g_audio_clock_ms;
    uint64_t pit = timer_get_ms();
    return (pit > bg) ? pit : bg;
}

int audio_clock_ready(void)
{
    return g_audio_clock_live ? 1 : 0;
}

void audio_clock_main(void)
{
    g_audio_clock_ms = timer_get_ms();
    g_audio_clock_live = 1;
    klog_log("[AudioClock] Started");

    for (;;)
    {
        uint64_t now = timer_get_ms();
        if (now > g_audio_clock_ms)
            g_audio_clock_ms = now;

        __asm__ volatile ("sti; hlt");
    }
}
