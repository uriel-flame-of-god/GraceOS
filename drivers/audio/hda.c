// ============================
// GraceOS Intel HDA Driver
// Intel High Definition Audio Controller
// Supports: QEMU -device intel-hda + ich9-intel-hda
// ============================

#include "hda.h"
#include "../../kernel/arch/x86_64/io/port.h"
#include "../../kernel/mm/vmm/vmm.h"
#include "../../kernel/log/klog.h"
#include "../../lib/libc/string.h"

/* ============================
   PCI Access Helpers
   ============================ */

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg)
{
    uint32_t addr = (1U << 31)
                  | ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)fn   <<  8)
                  | (reg & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint32_t val)
{
    uint32_t addr = (1U << 31)
                  | ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)fn   <<  8)
                  | (reg & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}

/* ============================
   MMIO Helpers
   ============================ */

static inline uint32_t hda_read32(volatile uint8_t* base, uint32_t reg)
{
    return *(volatile uint32_t*)(base + reg);
}

static inline uint16_t hda_read16(volatile uint8_t* base, uint32_t reg)
{
    return *(volatile uint16_t*)(base + reg);
}

static inline uint8_t hda_read8(volatile uint8_t* base, uint32_t reg)
{
    return *(volatile uint8_t*)(base + reg);
}

static inline void hda_write32(volatile uint8_t* base, uint32_t reg, uint32_t val)
{
    *(volatile uint32_t*)(base + reg) = val;
}

static inline void hda_write16(volatile uint8_t* base, uint32_t reg, uint16_t val)
{
    *(volatile uint16_t*)(base + reg) = val;
}

static inline void hda_write8(volatile uint8_t* base, uint32_t reg, uint8_t val)
{
    *(volatile uint8_t*)(base + reg) = val;
}

/* ============================
   Global Driver State
   ============================ */

static hda_state_t g_hda;
static uint16_t g_codec_mask = 0;
static uint32_t g_hda_init_calls = 0;

static void hda_delay(uint32_t count);
static int hda_corb_rirb_init(void);
static void hda_log_hex_msg(const char* prefix, uint64_t value);
static int hda_stream_reset(void);

/* CORB/RIRB rings for codec verb transport. */
static uint32_t g_corb[256] __attribute__((aligned(128)));
static uint64_t g_rirb[256] __attribute__((aligned(128)));
static uint16_t g_rirb_last_wp = 0;
static uint16_t g_corb_entries = 256;
static uint16_t g_rirb_entries = 256;
static uint16_t g_codec_fmt = 0;
static uint32_t g_out_sd_base = HDA_SD0_BASE;

/* QEMU hda-duplex common path (DAC nid 0x02 -> pin nid 0x05). */
static uint8_t g_out_dac_nid = 0x02;
static uint8_t g_out_pin_nid = 0x05;

#define HDA_SD_CTL_RUN         (1U << 1)
#define HDA_SD_CTL_SRST        (1U << 0)
#define HDA_SD_CTL_STREAM_TAG_SHIFT 20
#define HDA_SD_STS_BCIS        (1U << 2)

static uint16_t hda_ring_entries_from_sel(uint8_t sel)
{
    if (sel == 0x02) return 256;
    if (sel == 0x01) return 16;
    return 2;
}

static uint64_t hda_dma_phys_addr(const void* ptr)
{
    uint64_t va = (uint64_t)(uintptr_t)ptr;
    uint64_t pa = vmm_translate(va);
    if (pa == 0)
    {
        hda_log_hex_msg("[HDA] dma va=", va);
        if (va >= KERNEL_BASE)
            pa = va - KERNEL_BASE;
        else
            pa = va;
        hda_log_hex_msg("[HDA] dma fallback pa=", pa);
    }
    return pa;
}

static void hda_recover_corb_rirb_if_needed(uint8_t corbctl, uint8_t rirbctl, uint8_t corbsts)
{
    /* If CORB/RIRB DMA is stopped or controller reports CORB memory error, rearm rings. */
    if (((corbctl & 0x02) == 0) || ((rirbctl & 0x02) == 0) || (corbsts & 0x01))
    {
        klog_warn("[HDA] CORB/RIRB unhealthy, reinitializing rings");
        (void)hda_corb_rirb_init();
    }
}

static void hda_log_hex_msg(const char* prefix, uint64_t value)
{
    klog_log(prefix);
    klog_hex(value);
    klog_logn("");
}

static int hda_detect_codecs(void)
{
    uint16_t statests = hda_read16(g_hda.mmio, HDA_REG_STATESTS);
    g_codec_mask = (uint16_t)(statests & 0x0F);

    hda_log_hex_msg("[HDA] STATESTS=", statests);

    /* Ack state-change bits we observed. */
    hda_write16(g_hda.mmio, HDA_REG_STATESTS, statests);

    if (g_codec_mask == 0)
    {
        klog_warn("[HDA] No codecs reported by STATESTS");
        return -1;
    }

    return 0;
}

static int hda_corb_rirb_init(void)
{
    volatile uint8_t* b = g_hda.mmio;

    hda_write8(b, HDA_REG_CORBCTL, 0x00);
    hda_write8(b, HDA_REG_RIRBCTL, 0x00);

    uint8_t corbsize = hda_read8(b, HDA_REG_CORBSIZE);
    uint8_t rirbsize = hda_read8(b, HDA_REG_RIRBSIZE);

    /* Choose 256 entries when supported, else fall back to 16/2. */
    uint8_t corb_sel = 0x00;
    uint8_t rirb_sel = 0x00;

    if (corbsize & 0x40)      corb_sel = 0x02;
    else if (corbsize & 0x20) corb_sel = 0x01;

    if (rirbsize & 0x40)      rirb_sel = 0x02;
    else if (rirbsize & 0x20) rirb_sel = 0x01;

    hda_write8(b, HDA_REG_CORBSIZE, corb_sel);
    hda_write8(b, HDA_REG_RIRBSIZE, rirb_sel);

    g_corb_entries = hda_ring_entries_from_sel((uint8_t)(hda_read8(b, HDA_REG_CORBSIZE) & 0x03));
    g_rirb_entries = hda_ring_entries_from_sel((uint8_t)(hda_read8(b, HDA_REG_RIRBSIZE) & 0x03));

    memset(g_corb, 0, sizeof(g_corb));
    memset(g_rirb, 0, sizeof(g_rirb));

    uint64_t corb_phys = hda_dma_phys_addr(&g_corb[0]);
    uint64_t rirb_phys = hda_dma_phys_addr(&g_rirb[0]);

    hda_write32(b, HDA_REG_CORBLBASE, (uint32_t)(corb_phys & 0xFFFFFFFF));
    hda_write32(b, HDA_REG_CORBUBASE, (uint32_t)(corb_phys >> 32));
    hda_write32(b, HDA_REG_RIRBLBASE, (uint32_t)(rirb_phys & 0xFFFFFFFF));
    hda_write32(b, HDA_REG_RIRBUBASE, (uint32_t)(rirb_phys >> 32));

    /* Reset CORB RP: set reset bit, wait set, clear reset bit, wait clear. */
    hda_write16(b, HDA_REG_CORBRP, 0x8000);
    for (uint32_t t = 0; t < 100000; t++)
    {
        if (hda_read16(b, HDA_REG_CORBRP) & 0x8000)
            break;
        hda_delay(10);
    }
    hda_write16(b, HDA_REG_CORBRP, 0x0000);
    for (uint32_t t = 0; t < 100000; t++)
    {
        if ((hda_read16(b, HDA_REG_CORBRP) & 0x8000) == 0)
            break;
        hda_delay(10);
    }
    hda_write16(b, HDA_REG_CORBWP, 0x0000);

    /* Reset RIRB WP (write 1 to bit15). */
    hda_write16(b, HDA_REG_RIRBWP, 0x8000);
    hda_delay(1000);
    /* In polling mode, avoid throttling on every response interrupt. */
    hda_write16(b, HDA_REG_RINTCNT, 0xFF);

    hda_write8(b, HDA_REG_CORBSTS, 0xFF);
    hda_write8(b, HDA_REG_RIRBSTS, 0xFF);

    /* CORB: RUN, RIRB: DMA enable. */
    hda_write8(b, HDA_REG_CORBCTL, 0x02);
    hda_write8(b, HDA_REG_RIRBCTL, 0x02);

    hda_delay(10000);

    g_rirb_last_wp = (uint16_t)(hda_read16(b, HDA_REG_RIRBWP) & (g_rirb_entries - 1));

    return 0;
}

static int hda_wait_rirb_response(uint32_t* out_resp)
{
    volatile uint8_t* b = g_hda.mmio;
    uint32_t tries = 300000;

    while (tries--)
    {
        uint8_t st = hda_read8(b, HDA_REG_RIRBSTS);
        uint16_t wp = (uint16_t)(hda_read16(b, HDA_REG_RIRBWP) & (g_rirb_entries - 1));

        if ((st & 0x01) || (wp != g_rirb_last_wp))
        {
            g_rirb_last_wp = wp;
            if (out_resp)
                *out_resp = (uint32_t)(g_rirb[wp] & 0xFFFFFFFFu);
            hda_write8(b, HDA_REG_RIRBSTS, st);
            return 0;
        }
        hda_delay(50);
    }

    return -1;
}

static int hda_send_verb(uint8_t codec, uint8_t nid, uint16_t verb, uint8_t payload, uint32_t* out_resp)
{
    if (!g_hda.mmio)
    {
        klog_warn("[HDA] send_verb: mmio null");
        return -1;
    }

    if ((g_codec_mask & (1u << codec)) == 0)
    {
        klog_warn("[HDA] send_verb: codec missing in mask");
        hda_log_hex_msg("[HDA] codec_mask=", g_codec_mask);
        return -1;
    }

    volatile uint8_t* b = g_hda.mmio;

    hda_recover_corb_rirb_if_needed(hda_read8(b, HDA_REG_CORBCTL),
                                    hda_read8(b, HDA_REG_RIRBCTL),
                                    hda_read8(b, HDA_REG_CORBSTS));

    /* Clear any stale RIRB status before queueing a new command. */
    hda_write8(b, HDA_REG_RIRBSTS, 0xFF);

    uint32_t cmd = ((uint32_t)(codec & 0x0F) << 28)
                 | ((uint32_t)(nid   & 0x7F) << 20)
                 | ((uint32_t)(verb  & 0x0FFF) << 8)
                 | ((uint32_t)payload);

    uint16_t wp = (uint16_t)(hda_read16(b, HDA_REG_CORBWP) & (g_corb_entries - 1));
    uint16_t next = (uint16_t)((wp + 1) & (g_corb_entries - 1));
    g_corb[next] = cmd;
    __asm__ volatile ("mfence" ::: "memory");
    hda_write16(b, HDA_REG_CORBWP, next);

    if (hda_wait_rirb_response(out_resp) < 0)
    {
        klog_warn("[HDA] send_verb: no RIRB response");
        hda_log_hex_msg("[HDA] codec_mask=", g_codec_mask);
        return -1;
    }

    return 0;
}

/* 4-bit verb + 16-bit payload format (needed by some codec verbs). */
static int hda_send_verb16(uint8_t codec, uint8_t nid, uint8_t verb4, uint16_t payload16, uint32_t* out_resp)
{
    if (!g_hda.mmio)
    {
        klog_warn("[HDA] send_verb16: mmio null");
        return -1;
    }

    if ((g_codec_mask & (1u << codec)) == 0)
    {
        klog_warn("[HDA] send_verb16: codec missing in mask");
        hda_log_hex_msg("[HDA] codec_mask=", g_codec_mask);
        return -1;
    }

    volatile uint8_t* b = g_hda.mmio;

    hda_recover_corb_rirb_if_needed(hda_read8(b, HDA_REG_CORBCTL),
                                    hda_read8(b, HDA_REG_RIRBCTL),
                                    hda_read8(b, HDA_REG_CORBSTS));

    /* Clear any stale RIRB status before queueing a new command. */
    hda_write8(b, HDA_REG_RIRBSTS, 0xFF);

    uint32_t cmd = ((uint32_t)(codec & 0x0F) << 28)
                 | ((uint32_t)(nid   & 0x7F) << 20)
                 | ((uint32_t)(verb4 & 0x0F) << 16)
                 | ((uint32_t)payload16);

    uint16_t wp = (uint16_t)(hda_read16(b, HDA_REG_CORBWP) & (g_corb_entries - 1));
    uint16_t next = (uint16_t)((wp + 1) & (g_corb_entries - 1));
    g_corb[next] = cmd;
    __asm__ volatile ("mfence" ::: "memory");
    hda_write16(b, HDA_REG_CORBWP, next);

    if (hda_wait_rirb_response(out_resp) < 0)
    {
        klog_warn("[HDA] send_verb16: no RIRB response");
        hda_log_hex_msg("[HDA] codec_mask=", g_codec_mask);
        return -1;
    }

    return 0;
}

static void hda_codec_setup_output_path(void)
{
    if ((g_codec_mask & 0x1) == 0)
        return;

    uint32_t resp = 0;

    /* Bind converter to controller stream tag 1, channel 0. */
    if (hda_send_verb(0, g_out_dac_nid, 0x706, 0x10, &resp) < 0)
        klog_warn("[HDA] SET_CONVERTER_STREAM_CHANNEL failed");

    /* Program converter sample format directly on codec. */
    if (hda_send_verb16(0, g_out_dac_nid, 0x2, g_codec_fmt, &resp) < 0)
        klog_warn("[HDA] SET_CONVERTER_FORMAT failed");

    /* Unmute DAC output amplifier (output+left+right, mute=0, gain=0). */
    if (hda_send_verb16(0, g_out_dac_nid, 0x3, 0xB000, &resp) < 0)
        klog_warn("[HDA] DAC amp unmute failed");

    /* Enable pin output and EAPD on the output pin widget. */
    if (hda_send_verb(0, g_out_pin_nid, 0x707, 0x40, &resp) < 0)
        klog_warn("[HDA] SET_PIN_WIDGET_CONTROL failed");

    if (hda_send_verb(0, g_out_pin_nid, 0x70C, 0x02, &resp) < 0)
        klog_warn("[HDA] SET_EAPD_BTLENABLE failed");

    /* Unmute pin output amplifier. */
    if (hda_send_verb16(0, g_out_pin_nid, 0x3, 0xB000, &resp) < 0)
        klog_warn("[HDA] Pin amp unmute failed");
}

/* ============================
   PCI Probe
   ============================ */

/* Scan PCI bus for Intel HDA controller.
   Returns 1 if found and sets g_hda.mmio_phys. */
static int hda_pci_probe(void)
{
    for (uint8_t bus = 0; bus < 8; bus++)
    {
        for (uint8_t dev = 0; dev < 32; dev++)
        {
            uint32_t id = pci_read32(bus, dev, 0, 0x00);
            uint16_t vendor = (uint16_t)(id & 0xFFFF);
            uint16_t device = (uint16_t)(id >> 16);

            if (vendor == 0xFFFF) continue;

            /* Check class code: 0x04 (Multimedia), subclass 0x03 (HDA) */
            uint32_t class = pci_read32(bus, dev, 0, 0x08);
            uint8_t base_class = (uint8_t)(class >> 24);
            uint8_t sub_class  = (uint8_t)(class >> 16);

            if (base_class != 0x04 || sub_class != 0x03)
                continue;

            /* Found HDA controller — read BAR0 (MMIO) */
            uint32_t bar0 = pci_read32(bus, dev, 0, 0x10);
            if (bar0 & 1) continue;  /* I/O BAR, skip */

            uint64_t mmio_phys = bar0 & ~0xFUL;

            /* Enable Bus Master + Memory Space in command register */
            uint32_t cmd = pci_read32(bus, dev, 0, 0x04);
            pci_write32(bus, dev, 0, 0x04, cmd | 0x06);

            g_hda.mmio_phys = mmio_phys;
            g_hda.mmio_size = 0x4000;  /* 16KB typical */

            klog_log("[HDA] PCI device found");
            (void)vendor; (void)device;
            return 1;
        }
    }
    return 0;
}

/* ============================
   Controller Reset
   ============================ */

static void hda_delay(uint32_t count)
{
    for (volatile uint32_t i = 0; i < count; i++)
        __asm__ volatile("nop");
}

static int hda_reset(void)
{
    volatile uint8_t* b = g_hda.mmio;

    /* Assert reset by clearing CRST */
    uint32_t gctl = hda_read32(b, HDA_REG_GCTL);
    hda_write32(b, HDA_REG_GCTL, gctl & ~(uint32_t)HDA_GCTL_CRST);
    hda_delay(100000);

    /* Deassert reset */
    gctl = hda_read32(b, HDA_REG_GCTL);
    hda_write32(b, HDA_REG_GCTL, gctl | HDA_GCTL_CRST);

    /* Wait for controller ready (CRST reads back 1) */
    uint32_t tries = 100000;
    while (tries--)
    {
        if (hda_read32(b, HDA_REG_GCTL) & HDA_GCTL_CRST)
            break;
        hda_delay(100);
    }

    if (!(hda_read32(b, HDA_REG_GCTL) & HDA_GCTL_CRST))
    {
        klog_warn("[HDA] Controller reset timed out");
        return -1;
    }

    /* Allow codecs time to enumerate */
    hda_delay(500000);
    return 0;
}

/* ============================
   Stream Format Encoding
   ============================ */

static uint16_t hda_encode_fmt(uint32_t rate, uint32_t channels, uint32_t bits)
{
    /* PCM flag (bit 15 = 0 for PCM, bit 14 = 1 for non-PCM — we use PCM) */
    uint16_t fmt = 0;

    /* Sample rate: encode as BASE * MULT / DIV
       For 48kHz: BASE=48kHz, MULT=1, DIV=1 → bits[14:11]=000, bits[10:8]=001 */
    switch (rate)
    {
        case 8000:  fmt |= (0 << 11) | (6 << 8); break;  /* 48000/6 */
        case 11025: fmt |= (1 << 11) | (4 << 8); break;  /* 44100*1/4 */
        case 16000: fmt |= (0 << 11) | (3 << 8); break;  /* 48000/3 */
        case 22050: fmt |= (1 << 11) | (2 << 8); break;  /* 44100*1/2 */
        case 44100: fmt |= (1 << 11) | (0 << 8); break;  /* 44100*1/1 */
        case 48000:
        default:    fmt |= (0 << 11) | (0 << 8); break;  /* 48000*1/1 */
    }

    /* Bits per sample: bits[6:4] */
    switch (bits)
    {
        case 8:  fmt |= (0 << 4); break;
        case 16: fmt |= (1 << 4); break;
        case 20: fmt |= (2 << 4); break;
        case 24: fmt |= (3 << 4); break;
        case 32: fmt |= (4 << 4); break;
        default: fmt |= (1 << 4); break;  /* default 16-bit */
    }

    /* Channels: bits[3:0] = (channels - 1) */
    if (channels < 1) channels = 1;
    if (channels > 8) channels = 8;
    fmt |= (uint16_t)((channels - 1) & 0xF);

    return fmt;
}

/* ============================
   Public API
   ============================ */

int hda_init(void)
{
    g_hda_init_calls++;
    hda_log_hex_msg("[HDA] init calls=", g_hda_init_calls);

    memset(&g_hda, 0, sizeof(g_hda));
    g_hda.volume = 75;
    g_hda.sample_rate = 48000;
    g_hda.channels = 2;
    g_hda.bits = 16;

    if (!hda_pci_probe())
    {
        klog_warn("[HDA] No Intel HDA controller found (use -device intel-hda in QEMU)");
        return 0;
    }

    /* Identity-map MMIO region (uncacheable: PWT + PCD flags) */
    g_hda.mmio = (volatile uint8_t*)g_hda.mmio_phys;
    vmm_map_range(g_hda.mmio_phys, g_hda.mmio_phys, g_hda.mmio_size,
                  VMM_PRESENT | VMM_WRITE | VMM_PWT | VMM_PCD);

    if (hda_reset() < 0)
    {
        klog_warn("[HDA] Reset failed — driver disabled");
        g_hda.present = 0;
        return 0;
    }

    /* Read capabilities */
    uint32_t gcap = (uint32_t)hda_read16(g_hda.mmio, HDA_REG_GCAP);
    uint8_t vmaj = hda_read8(g_hda.mmio, HDA_REG_VMAJ);
    uint8_t vmin = hda_read8(g_hda.mmio, HDA_REG_VMIN);

    hda_log_hex_msg("[HDA] GCAP=", gcap);
    hda_log_hex_msg("[HDA] VMAJ=", vmaj);
    hda_log_hex_msg("[HDA] VMIN=", vmin);

    g_hda.num_out_streams = (gcap >> 12) & 0xF;
    uint32_t num_in_streams = (gcap >> 8) & 0xF;
    g_out_sd_base = HDA_SD0_BASE + (num_in_streams * HDA_SD_SIZE);

    if (hda_detect_codecs() < 0)
    {
        g_hda.present = 0;
        return 0;
    }

    if (hda_corb_rirb_init() < 0)
    {
        klog_warn("[HDA] CORB/RIRB init failed");
        g_hda.present = 0;
        return 0;
    }

    /* Probe root node (NID 0) vendor ID via verb GET_PARAMETER (0xF00, payload 0x00). */
    uint32_t vendor_param = 0;
    if ((g_codec_mask & 0x1) && hda_send_verb(0, 0, 0xF00, 0x00, &vendor_param) == 0)
        hda_log_hex_msg("[HDA] Codec0 vendor param=", vendor_param);
    else
        klog_warn("[HDA] No valid CORB/RIRB response from codec0");

    hda_log_hex_msg("[HDA] Input streams=", num_in_streams);
    hda_log_hex_msg("[HDA] Output streams=", g_hda.num_out_streams);
    hda_log_hex_msg("[HDA] Out SD base=", g_out_sd_base);
    g_hda.present = 1;
    return 1;
}

int hda_present(void)
{
    return g_hda.present;
}

int hda_set_format(uint32_t sample_rate, uint32_t channels, uint32_t bits_per_sample)
{
    if (!g_hda.present) return -1;

    g_hda.sample_rate = sample_rate;
    g_hda.channels    = channels;
    g_hda.bits        = bits_per_sample;

    uint16_t fmt = hda_encode_fmt(sample_rate, channels, bits_per_sample);
    g_codec_fmt = fmt;
    volatile uint8_t* sd = g_hda.mmio + g_out_sd_base;
    hda_write16(sd, HDA_SD_FMT, fmt);
    return 0;
}

int hda_submit_buffer(uint64_t phys_addr, uint32_t byte_count)
{
    if (!g_hda.present) return -1;
    if (g_hda.bdl_entry_count >= HDA_BDL_MAX_ENTRIES) return -1;

    /* Accept either VA or PA from callers; normalize to physical for DMA. */
    phys_addr = hda_dma_phys_addr((const void*)(uintptr_t)phys_addr);

    uint32_t idx = g_hda.bdl_entry_count++;
    g_hda.bdl[idx].addr   = phys_addr;
    g_hda.bdl[idx].length = byte_count;
    g_hda.bdl[idx].ioc    = 1;  /* Interrupt on completion */

    volatile uint8_t* sd = g_hda.mmio + g_out_sd_base;

    /* Program BDL address */
    uint64_t bdl_phys = hda_dma_phys_addr(&g_hda.bdl[0]);
    hda_write32(sd, HDA_SD_BDLPL, (uint32_t)(bdl_phys & 0xFFFFFFFF));
    hda_write32(sd, HDA_SD_BDLPU, (uint32_t)(bdl_phys >> 32));

    /* Set cyclic buffer length and last valid index */
    uint32_t total_bytes = 0;
    for (uint32_t i = 0; i < g_hda.bdl_entry_count; i++)
        total_bytes += g_hda.bdl[i].length;

    hda_write32(sd, HDA_SD_CBL, total_bytes);
    hda_write16(sd, HDA_SD_LVI, (uint16_t)(g_hda.bdl_entry_count - 1));
    return 0;
}

int hda_start(void)
{
    if (!g_hda.present) return -1;

    volatile uint8_t* sd = g_hda.mmio + g_out_sd_base;

    /* Program stream tag=1 and enable stream RUN. */
    uint32_t ctl = hda_read32(sd, HDA_SD_CTL);
    ctl &= ~(0xFU << HDA_SD_CTL_STREAM_TAG_SHIFT);
    ctl |=  (1U << HDA_SD_CTL_STREAM_TAG_SHIFT);
    ctl &= ~HDA_SD_CTL_SRST;
    ctl |=  HDA_SD_CTL_RUN;
    hda_write32(sd, HDA_SD_CTL, ctl);

    hda_codec_setup_output_path();

    g_hda.running = 1;
    return 0;
}

static int hda_stream_reset(void)
{
    if (!g_hda.present) return -1;

    volatile uint8_t* sd = g_hda.mmio + g_out_sd_base;

    uint32_t ctl = hda_read32(sd, HDA_SD_CTL);
    ctl &= ~(0xFU << HDA_SD_CTL_STREAM_TAG_SHIFT);
    ctl &= ~HDA_SD_CTL_RUN;
    ctl |= HDA_SD_CTL_SRST;
    hda_write32(sd, HDA_SD_CTL, ctl);

    for (uint32_t t = 0; t < 100000; t++)
    {
        if (hda_read32(sd, HDA_SD_CTL) & HDA_SD_CTL_SRST)
            break;
        hda_delay(10);
    }

    ctl = hda_read32(sd, HDA_SD_CTL);
    ctl &= ~HDA_SD_CTL_SRST;
    hda_write32(sd, HDA_SD_CTL, ctl);

    for (uint32_t t = 0; t < 100000; t++)
    {
        if ((hda_read32(sd, HDA_SD_CTL) & HDA_SD_CTL_SRST) == 0)
            break;
        hda_delay(10);
    }

    return 0;
}

int hda_stop(void)
{
    if (!g_hda.present) return -1;

    volatile uint8_t* sd = g_hda.mmio + g_out_sd_base;

    uint32_t ctl = hda_read32(sd, HDA_SD_CTL);
    ctl &= ~(1U << 1);  /* Clear RUN */
    hda_write32(sd, HDA_SD_CTL, ctl);

    g_hda.running = 0;
    g_hda.bdl_entry_count = 0;
    return 0;
}

void hda_set_volume(uint8_t vol)
{
    if (vol > 100) vol = 100;
    g_hda.volume = vol;
    /* Real volume control goes through the codec via CORB/RIRB commands.
       Left as future work — codec enumeration required. */
}

void hda_poll_complete(void)
{
    if (!g_hda.present || !g_hda.running) return;

    volatile uint8_t* sd = g_hda.mmio + g_out_sd_base;
    uint32_t total_bytes = hda_read32(sd, HDA_SD_CBL);

    /* Wait for completion IRQ status or LPIB reaching end of cyclic buffer. */
    uint32_t tries = 0x02000000;
    while (tries--)
    {
        uint8_t st = hda_read8(sd, HDA_SD_STS);
        uint32_t lpib = hda_read32(sd, HDA_SD_LPIB);

        if (st & HDA_SD_STS_BCIS)
        {
            hda_write8(sd, HDA_SD_STS, HDA_SD_STS_BCIS);
            break;
        }

        if (total_bytes > 0 && lpib >= (total_bytes - 1))
            break;

        __asm__ volatile("pause");
    }
}

int hda_play_buffer_blocking(uint64_t phys_addr, uint32_t byte_count)
{
    if (!g_hda.present || byte_count == 0)
        return -1;

    /* Always restart from a clean descriptor state for long streams. */
    hda_stop();

    if (hda_stream_reset() < 0)
        return -1;

    if (hda_submit_buffer(phys_addr, byte_count) < 0)
        return -1;

    if (hda_start() < 0)
        return -1;

    hda_poll_complete();
    hda_stop();

    return 0;
}

/* ============================
   PC Speaker Beep Fallback
   Used when HDA is absent (e.g., vanilla QEMU without -device intel-hda)
   ============================ */

void hda_beep(uint32_t freq_hz, uint32_t duration_ms)
{
    if (freq_hz == 0)
    {
        /* Silence */
        outb(0x61, inb(0x61) & 0xFC);
        return;
    }

    /* PIT channel 2 drives the PC speaker */
    uint32_t divisor = 1193180 / freq_hz;
    outb(0x43, 0xB6);                      /* PIT: channel 2, square wave */
    outb(0x42, (uint8_t)(divisor & 0xFF));
    outb(0x42, (uint8_t)(divisor >> 8));

    /* Enable speaker */
    uint8_t tmp = inb(0x61);
    outb(0x61, tmp | 0x03);

    /* Busy-wait for duration (~1 ms ≈ 400000 NOPs at typical kernel entry speed) */
    for (uint32_t ms = 0; ms < duration_ms; ms++)
        for (volatile uint32_t j = 0; j < 80000; j++)
            __asm__ volatile("nop");

    /* Disable speaker */
    outb(0x61, inb(0x61) & 0xFC);
}
