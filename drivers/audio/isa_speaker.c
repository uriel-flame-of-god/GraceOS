// ============================
// GraceOS ISA Speaker Audio Fallback
// Legacy PC speaker playback backend
// ============================

#include "isa_speaker.h"
#include "../../kernel/arch/x86_64/io/port.h"

#define PIT_BASE_HZ 1193180u

#define ISA_PCM_OUT_RATE 6000u
#define ISA_PWM_STEPS    16u
#define ISA_PWM_DELAY_ITERS 24u

static uint8_t g_isa_ready = 0;

static void isa_speaker_program_tone(uint32_t freq_hz)
{
    if (freq_hz < 20) freq_hz = 20;
    if (freq_hz > 20000) freq_hz = 20000;

    uint32_t divisor = PIT_BASE_HZ / freq_hz;
    if (divisor == 0) divisor = 1;

    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(divisor & 0xFF));
    outb(0x42, (uint8_t)((divisor >> 8) & 0xFF));

    uint8_t gate = inb(0x61);
    outb(0x61, (uint8_t)(gate | 0x03));
}

static inline void isa_speaker_set_level(uint8_t on)
{
    uint8_t gate = inb(0x61);
    gate &= 0xFC;
    if (on)
        gate |= 0x03;  /* gate + data */
    else
        gate |= 0x01;  /* gate only (quiet) */
    outb(0x61, gate);
}

void isa_speaker_init(void)
{
    g_isa_ready = 1;
    isa_speaker_stop();
}

void isa_speaker_stop(void)
{
    uint8_t gate = inb(0x61);
    outb(0x61, (uint8_t)(gate & 0xFC));
}

static int32_t pcm_sample_to_s16(const uint8_t* p, uint32_t bits)
{
    switch (bits)
    {
        case 8:
        {
            int32_t s = (int32_t)p[0] - 128;
            return s << 8;
        }
        case 16:
            return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
        case 24:
        {
            int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
            if (v & 0x800000)
                v |= ~0xFFFFFF;
            return v >> 8;
        }
        case 32:
        {
            int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                                  ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
            return v >> 16;
        }
        default:
            return 0;
    }
}

int isa_speaker_play_pcm(const uint8_t* pcm,
                         uint32_t byte_count,
                         uint32_t sample_rate,
                         uint32_t channels,
                         uint32_t bits_per_sample)
{
    if (!g_isa_ready || !pcm || byte_count == 0 || channels == 0 || sample_rate == 0)
        return -1;

    if (!(bits_per_sample == 8 || bits_per_sample == 16 || bits_per_sample == 24 || bits_per_sample == 32))
        return -1;

    uint32_t bytes_per_sample = bits_per_sample / 8;
    uint32_t frame_bytes = channels * bytes_per_sample;
    if (frame_bytes == 0)
        return -1;

    uint32_t total_frames = byte_count / frame_bytes;
    if (total_frames == 0)
        return 0;

    /* Software PWM renderer:
       - Downsample to ~6 kHz
       - Convert PCM amplitude to pulse duty over 16 slots */
    uint32_t out_rate = ISA_PCM_OUT_RATE;
    if (sample_rate < out_rate)
        out_rate = sample_rate;
    if (out_rate == 0)
        out_rate = 1000;

    uint32_t src_step_q16 = (uint32_t)(((uint64_t)sample_rate << 16) / out_rate);
    if (src_step_q16 == 0)
        src_step_q16 = 1;

    uint64_t out_samples = ((uint64_t)total_frames * out_rate) / sample_rate;
    if (out_samples == 0)
        out_samples = 1;

    uint32_t src_pos_q16 = 0;

    /* Keep PIT running at an ultrasonic-ish carrier; we only gate output level. */
    isa_speaker_program_tone(18000);

    for (uint64_t oi = 0; oi < out_samples; oi++)
    {
        uint32_t src_idx = (src_pos_q16 >> 16);
        if (src_idx >= total_frames)
            src_idx = total_frames - 1;

        const uint8_t* fp = pcm + (uint64_t)src_idx * frame_bytes;

        int32_t mono = 0;
        if (channels == 1)
        {
            mono = pcm_sample_to_s16(fp, bits_per_sample);
        }
        else
        {
            int64_t mix = 0;
            for (uint32_t c = 0; c < channels; c++)
            {
                const uint8_t* sp = fp + c * bytes_per_sample;
                mix += pcm_sample_to_s16(sp, bits_per_sample);
            }
            mono = (int32_t)(mix / (int64_t)channels);
        }

        /* Map signed sample to duty 0..ISA_PWM_STEPS. */
        uint32_t u = (uint32_t)(mono + 32768);
        if (u > 65535) u = 65535;
        uint32_t duty = (u * ISA_PWM_STEPS) / 65535u;

        for (uint32_t k = 0; k < ISA_PWM_STEPS; k++)
        {
            isa_speaker_set_level((k < duty) ? 1 : 0);
            for (volatile uint32_t d = 0; d < ISA_PWM_DELAY_ITERS; d++)
                __asm__ volatile("nop");
        }

        src_pos_q16 += src_step_q16;
    }

    isa_speaker_stop();
    return 0;
}
