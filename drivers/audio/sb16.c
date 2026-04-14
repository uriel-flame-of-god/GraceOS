// ============================
// GraceOS Sound Blaster 16 Driver
// ISA DMA backend (QEMU -device sb16)
// ============================

#include "sb16.h"
#include "../../kernel/arch/x86_64/io/port.h"
#include "../../kernel/mm/vmm/vmm.h"
#include "../../kernel/audio/audio_clock.h"
#include "../../kernel/include/time.h"
#include "../../kernel/log/klog.h"
#include "../../lib/libc/string.h"

#define SB16_BASE              0x220
#define SB16_RESET             (SB16_BASE + 0x6)
#define SB16_READ              (SB16_BASE + 0xA)
#define SB16_WRITE             (SB16_BASE + 0xC)
#define SB16_WRITE_STATUS      (SB16_BASE + 0xC)
#define SB16_READ_STATUS       (SB16_BASE + 0xE)

/* 16-bit DMA channel 5 (secondary DMA controller). */
#define DMA2_MASK_REG          0xD4
#define DMA2_MODE_REG          0xD6
#define DMA2_CLEAR_FF          0xD8
#define DMA2_CH5_ADDR          0xC4
#define DMA2_CH5_COUNT         0xC6
#define DMA2_CH5_PAGE          0x8B

#define SB16_DMA_CHUNK         4096

static uint8_t g_sb16_ready = 0;
static uint8_t* g_sb16_dma_buf = 0;
static uint64_t g_sb16_dma_phys = 0;
static uint8_t g_sb16_dma_boot_buf[SB16_DMA_CHUNK]
    __attribute__((section(".sb16dma"), aligned(4096)));

static uint64_t sb16_dma_phys_addr(const void* ptr)
{
    uint64_t va = (uint64_t)(uintptr_t)ptr;
    uint64_t pa = vmm_translate(va);
    if (pa == 0)
    {
        if (va >= KERNEL_BASE)
            pa = va - KERNEL_BASE;
        else
            pa = va;
    }
    return pa;
}

static void sb16_delay(uint32_t count)
{
    for (volatile uint32_t i = 0; i < count; i++)
        __asm__ volatile("nop");
}

static int sb16_dsp_wait_write_ready(void)
{
    for (uint32_t t = 0; t < 200000; t++)
    {
        if ((inb(SB16_WRITE_STATUS) & 0x80) == 0)
            return 0;
    }
    return -1;
}

static int sb16_dsp_wait_read_ready(void)
{
    for (uint32_t t = 0; t < 200000; t++)
    {
        if (inb(SB16_READ_STATUS) & 0x80)
            return 0;
    }
    return -1;
}

static int sb16_dsp_write(uint8_t v)
{
    if (sb16_dsp_wait_write_ready() < 0)
        return -1;
    outb(SB16_WRITE, v);
    return 0;
}

static int sb16_dsp_read(uint8_t* out)
{
    if (!out) return -1;
    if (sb16_dsp_wait_read_ready() < 0)
        return -1;
    *out = inb(SB16_READ);
    return 0;
}

static int sb16_reset_dsp(void)
{
    outb(SB16_RESET, 1);
    sb16_delay(5000);
    outb(SB16_RESET, 0);

    uint8_t ack = 0;
    if (sb16_dsp_read(&ack) < 0)
        return -1;
    return (ack == 0xAA) ? 0 : -1;
}

static int sb16_dma_program_ch5(uint64_t pa, uint32_t byte_count)
{
    if (byte_count < 2)
        return -1;

    if (pa >= 0x1000000ULL)
    {
        klog_warn("[SB16] DMA buffer above 16MB; ISA DMA cannot reach");
        return -1;
    }

    uint32_t words = byte_count >> 1;
    if (words == 0)
        return -1;
    if (words > 0x10000)
        words = 0x10000;

    uint16_t count = (uint16_t)(words - 1);
    uint32_t addr_word = (uint32_t)(pa >> 1);
    uint16_t addr16 = (uint16_t)(addr_word & 0xFFFF);
    uint8_t page = (uint8_t)((pa >> 16) & 0xFF);

    /* Mask channel 5 (index 1 on DMA2), clear flip-flop, then program mode/address/count/page. */
    outb(DMA2_MASK_REG, 0x05);
    outb(DMA2_CLEAR_FF, 0x00);
    outb(DMA2_MODE_REG, 0x59);   /* single, address increment, mem->device, channel index 1 */

    outb(DMA2_CH5_ADDR, (uint8_t)(addr16 & 0xFF));
    outb(DMA2_CH5_ADDR, (uint8_t)(addr16 >> 8));

    outb(DMA2_CH5_COUNT, (uint8_t)(count & 0xFF));
    outb(DMA2_CH5_COUNT, (uint8_t)(count >> 8));

    outb(DMA2_CH5_PAGE, page);

    outb(DMA2_MASK_REG, 0x01);   /* unmask channel index 1 */
    return 0;
}

static void sb16_wait_ms(uint32_t ms)
{
    uint64_t start_ms = audio_clock_now_ms();
    uint64_t target_ms = start_ms + (uint64_t)ms;

    while (audio_clock_now_ms() < target_ms)
    {
        __asm__ volatile ("pause");
    }
}

int sb16_init(void)
{
    g_sb16_ready = 0;
    g_sb16_dma_buf = 0;
    g_sb16_dma_phys = 0;

    if (sb16_reset_dsp() < 0)
    {
        klog_warn("[SB16] DSP reset/ack failed");
        return 0;
    }

    g_sb16_dma_buf = g_sb16_dma_boot_buf;
    g_sb16_dma_phys = sb16_dma_phys_addr(g_sb16_dma_boot_buf);
    if (g_sb16_dma_phys >= 0x1000000ULL)
    {
        klog_warn("[SB16] Boot DMA page above 16MB; SB16 disabled");
        return 0;
    }

    memset(g_sb16_dma_buf, 0, SB16_DMA_CHUNK);

    g_sb16_ready = 1;
    klog_log("[SB16] Ready");
    return 1;
}

int sb16_present(void)
{
    return g_sb16_ready ? 1 : 0;
}

void sb16_stop(void)
{
    if (!g_sb16_ready)
        return;
    (void)sb16_dsp_write(0xD5); /* Pause 16-bit DMA mode */
}

int sb16_play_pcm(const uint8_t* pcm,
                  uint32_t byte_count,
                  uint32_t sample_rate,
                  uint32_t channels,
                  uint32_t bits_per_sample)
{
    if (!g_sb16_ready || !pcm || byte_count == 0)
        return -1;
    if (!g_sb16_dma_buf || g_sb16_dma_phys == 0)
        return -1;

    if (sample_rate < 5000) sample_rate = 5000;
    if (sample_rate > 44100) sample_rate = 44100;

    /* Keep initial implementation strict to SB16 16-bit stereo output path. */
    if (channels != 2 || bits_per_sample != 16)
    {
        klog_warn("[SB16] Unsupported format (requires 16-bit stereo)");
        return -1;
    }

    uint32_t done = 0;
    while (done < byte_count)
    {
        uint32_t chunk = byte_count - done;
        if (chunk > SB16_DMA_CHUNK)
            chunk = SB16_DMA_CHUNK;
        chunk &= ~1u;
        if (chunk < 2)
            break;

        memcpy(g_sb16_dma_buf, pcm + done, chunk);

        uint64_t pa = g_sb16_dma_phys;
        if (sb16_dma_program_ch5(pa, chunk) < 0)
            return -1;

        /* Set output sample rate. */
        if (sb16_dsp_write(0x41) < 0) return -1;
        if (sb16_dsp_write((uint8_t)((sample_rate >> 8) & 0xFF)) < 0) return -1;
        if (sb16_dsp_write((uint8_t)(sample_rate & 0xFF)) < 0) return -1;

        /* 16-bit single-cycle DAC output, signed stereo mode. */
        if (sb16_dsp_write(0xB0) < 0) return -1;
        if (sb16_dsp_write(0x30) < 0) return -1; /* bit5 stereo, bit4 signed */

        /* For 16-bit DMA commands, SB16 transfer length is in words-1. */
        uint32_t words = chunk >> 1;
        if (words == 0)
            return -1;

        uint16_t words_minus_1 = (uint16_t)(words - 1);
        if (sb16_dsp_write((uint8_t)(words_minus_1 & 0xFF)) < 0) return -1;
        if (sb16_dsp_write((uint8_t)((words_minus_1 >> 8) & 0xFF)) < 0) return -1;

        /* Wait for DMA completion (TC) to keep playback paced correctly. */
        uint32_t expected_ms = (uint32_t)(((uint64_t)chunk * 1000ULL) /
                                          (uint64_t)(sample_rate * channels * 2));
        /* Single-cycle setup overhead per chunk otherwise accumulates drift. */
        if (expected_ms > 2)
            expected_ms = (expected_ms * 85U) / 100U;
        if (expected_ms < 4)
            expected_ms = 4;

        sb16_wait_ms(expected_ms);

        done += chunk;
    }

    return 0;
}
