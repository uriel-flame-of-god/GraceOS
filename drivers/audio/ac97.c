// ============================
// GraceOS AC'97 Driver
// QEMU -device AC97 backend
// ============================

#include "ac97.h"
#include "../../kernel/arch/x86_64/io/port.h"
#include "../../kernel/mm/vmm/vmm.h"
#include "../../kernel/audio/audio_clock.h"
#include "../../kernel/log/klog.h"
#include "../../lib/libc/string.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define AC97_NAM_MASTER_VOL      0x02
#define AC97_NAM_PCM_OUT_VOL     0x18
#define AC97_NAM_PCM_FRONT_DAC   0x2C

#define AC97_PO_BDBAR            0x10
#define AC97_PO_LVI              0x15
#define AC97_PO_SR               0x16
#define AC97_PO_CR               0x1B

#define AC97_PO_CR_RPBM          0x01
#define AC97_PO_CR_RR            0x02

#define AC97_PO_SR_BCIS          0x08
#define AC97_PO_SR_LVBCI         0x04
#define AC97_PO_SR_FIFOE         0x10

#define AC97_DMA_BUF_BYTES       (64 * 1024)

typedef struct __attribute__((packed)) ac97_bdl_entry {
    uint32_t addr;
    uint32_t ctl_len;
} ac97_bdl_entry_t;

static uint8_t g_ac97_ready = 0;
static uint16_t g_ac97_nam = 0;
static uint16_t g_ac97_nabm = 0;

static ac97_bdl_entry_t g_po_bdl[1] __attribute__((aligned(16)));
static uint8_t g_dma_buf[AC97_DMA_BUF_BYTES] __attribute__((aligned(16)));

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg)
{
    uint32_t addr = (1U << 31)
                  | ((uint32_t)bus << 16)
                  | ((uint32_t)dev << 11)
                  | ((uint32_t)fn << 8)
                  | (reg & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint32_t val)
{
    uint32_t addr = (1U << 31)
                  | ((uint32_t)bus << 16)
                  | ((uint32_t)dev << 11)
                  | ((uint32_t)fn << 8)
                  | (reg & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}

static uint64_t ac97_phys_addr(const void* ptr)
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

static void ac97_delay(uint32_t count)
{
    for (volatile uint32_t i = 0; i < count; i++)
        __asm__ volatile("nop");
}

static int ac97_pci_probe(void)
{
    for (uint8_t bus = 0; bus < 8; bus++)
    {
        for (uint8_t dev = 0; dev < 32; dev++)
        {
            uint32_t id = pci_read32(bus, dev, 0, 0x00);
            uint16_t vendor = (uint16_t)(id & 0xFFFF);
            if (vendor == 0xFFFF)
                continue;

            uint32_t class_code = pci_read32(bus, dev, 0, 0x08);
            uint8_t base_class = (uint8_t)(class_code >> 24);
            uint8_t sub_class = (uint8_t)(class_code >> 16);

            if (base_class != 0x04 || sub_class != 0x01)
                continue;

            uint32_t nam = pci_read32(bus, dev, 0, 0x10);
            uint32_t nabm = pci_read32(bus, dev, 0, 0x14);

            if ((nam & 1) == 0 || (nabm & 1) == 0)
                continue;

            g_ac97_nam = (uint16_t)(nam & ~0x1);
            g_ac97_nabm = (uint16_t)(nabm & ~0x1);

            uint32_t cmd = pci_read32(bus, dev, 0, 0x04);
            cmd |= 0x00000005U; /* I/O space + bus master */
            pci_write32(bus, dev, 0, 0x04, cmd);

            return 1;
        }
    }

    return 0;
}

int ac97_init(void)
{
    g_ac97_ready = 0;
    g_ac97_nam = 0;
    g_ac97_nabm = 0;

    if (!ac97_pci_probe())
        return 0;

    /* Reset PCM out engine */
    outb((uint16_t)(g_ac97_nabm + AC97_PO_CR), AC97_PO_CR_RR);
    ac97_delay(10000);
    outb((uint16_t)(g_ac97_nabm + AC97_PO_CR), 0x00);

    /* Unmute master and PCM out. */
    outw((uint16_t)(g_ac97_nam + AC97_NAM_MASTER_VOL), 0x0000);
    outw((uint16_t)(g_ac97_nam + AC97_NAM_PCM_OUT_VOL), 0x0000);

    /* Default to 44.1 kHz request (QEMU generally accepts common rates). */
    outw((uint16_t)(g_ac97_nam + AC97_NAM_PCM_FRONT_DAC), 44100);

    g_ac97_ready = 1;
    klog_log("[AC97] Ready");
    return 1;
}

int ac97_present(void)
{
    return g_ac97_ready ? 1 : 0;
}

int ac97_set_format(uint32_t sample_rate, uint32_t channels, uint32_t bits_per_sample)
{
    if (!g_ac97_ready)
        return -1;

    if (channels != 2 || bits_per_sample != 16)
        return -1;

    if (sample_rate < 8000) sample_rate = 8000;
    if (sample_rate > 48000) sample_rate = 48000;

    outw((uint16_t)(g_ac97_nam + AC97_NAM_PCM_FRONT_DAC), (uint16_t)sample_rate);
    return 0;
}

void ac97_set_volume(uint8_t vol)
{
    if (!g_ac97_ready)
        return;

    if (vol > 100) vol = 100;

    /* AC97 volume attenuation: 0=loudest, 31=quietest (per channel). */
    uint8_t att = (uint8_t)((31U * (100U - vol)) / 100U);
    uint16_t reg = (uint16_t)(att | (att << 8));
    outw((uint16_t)(g_ac97_nam + AC97_NAM_MASTER_VOL), reg);
}

void ac97_stop(void)
{
    if (!g_ac97_ready)
        return;

    uint8_t cr = inb((uint16_t)(g_ac97_nabm + AC97_PO_CR));
    cr &= (uint8_t)~AC97_PO_CR_RPBM;
    outb((uint16_t)(g_ac97_nabm + AC97_PO_CR), cr);
}

int ac97_play_pcm(const uint8_t* pcm,
                  uint32_t byte_count,
                  uint32_t sample_rate,
                  uint32_t channels,
                  uint32_t bits_per_sample)
{
    if (!g_ac97_ready || !pcm || byte_count == 0)
        return -1;

    if (channels != 2 || bits_per_sample != 16)
        return -1;

    if (ac97_set_format(sample_rate, channels, bits_per_sample) < 0)
        return -1;

    uint32_t done = 0;
    while (done < byte_count)
    {
        uint32_t chunk = byte_count - done;
        if (chunk > AC97_DMA_BUF_BYTES)
            chunk = AC97_DMA_BUF_BYTES;

        /* AC97 PCM out length field is in 16-bit samples. */
        chunk &= ~1u;
        if (chunk < 2)
            break;

        memcpy(g_dma_buf, pcm + done, chunk);

        uint64_t bdl_pa = ac97_phys_addr(&g_po_bdl[0]);
        uint64_t dma_pa = ac97_phys_addr(&g_dma_buf[0]);

        g_po_bdl[0].addr = (uint32_t)dma_pa;
        g_po_bdl[0].ctl_len = ((chunk >> 1) & 0xFFFF) | (1u << 31); /* IOC */

        outl((uint16_t)(g_ac97_nabm + AC97_PO_BDBAR), (uint32_t)bdl_pa);
        outb((uint16_t)(g_ac97_nabm + AC97_PO_LVI), 0);

        outw((uint16_t)(g_ac97_nabm + AC97_PO_SR), (AC97_PO_SR_BCIS | AC97_PO_SR_LVBCI | AC97_PO_SR_FIFOE));

        uint8_t cr = inb((uint16_t)(g_ac97_nabm + AC97_PO_CR));
        cr |= AC97_PO_CR_RPBM;
        outb((uint16_t)(g_ac97_nabm + AC97_PO_CR), cr);

        uint32_t expected_ms = (uint32_t)(((uint64_t)chunk * 1000ULL) /
                                          (uint64_t)(sample_rate * channels * 2));
        /* Descriptor setup overhead per chunk otherwise accumulates jitter/drift. */
        if (expected_ms > 2)
            expected_ms = (expected_ms * 85U) / 100U;
        if (expected_ms < 4)
            expected_ms = 4;

        uint64_t start = audio_clock_now_ms();
        uint64_t deadline = start + expected_ms;
        while (audio_clock_now_ms() < deadline)
        {
            uint16_t st = inw((uint16_t)(g_ac97_nabm + AC97_PO_SR));
            if (st & AC97_PO_SR_BCIS)
                break;
            __asm__ volatile("pause");
        }

        outw((uint16_t)(g_ac97_nabm + AC97_PO_SR), (AC97_PO_SR_BCIS | AC97_PO_SR_LVBCI | AC97_PO_SR_FIFOE));
        ac97_stop();

        done += chunk;
    }

    return 0;
}
