// ============================
// GraceOS Intel HDA Driver
// Intel High Definition Audio Controller
// Targets QEMU -device intel-hda
// ============================

#ifndef GRACEOS_HDA_H
#define GRACEOS_HDA_H

#include "../../lib/libc/int.h"

/* ============================
   Intel HDA PCI Identity
   ============================ */

#define HDA_VENDOR_INTEL    0x8086
#define HDA_DEVICE_ICH6     0x2668  /* ICH6 HDA (QEMU default) */
#define HDA_DEVICE_ICH9     0x293E  /* ICH9 HDA */

/* ============================
   Intel HDA MMIO Registers (offset from BAR0)
   ============================ */

#define HDA_REG_GCAP        0x00    /* Global Capabilities */
#define HDA_REG_VMIN        0x02    /* Minor Version */
#define HDA_REG_VMAJ        0x03    /* Major Version */
#define HDA_REG_OUTPAY      0x04    /* Output Payload Capability */
#define HDA_REG_INPAY       0x06    /* Input Payload Capability */
#define HDA_REG_GCTL        0x08    /* Global Control */
#define HDA_REG_WAKEEN      0x0C    /* Wake Enable */
#define HDA_REG_STATESTS    0x0E    /* State Change Status */
#define HDA_REG_GSTS        0x10    /* Global Status */
#define HDA_REG_INTCTL      0x20    /* Interrupt Control */
#define HDA_REG_INTSTS      0x24    /* Interrupt Status */
#define HDA_REG_WALCLK      0x30    /* Wall Clock Counter */
#define HDA_REG_SSYNC       0x34    /* Stream Synchronisation */
#define HDA_REG_CORBLBASE   0x40    /* CORB Lower Base Address */
#define HDA_REG_CORBUBASE   0x44    /* CORB Upper Base Address */
#define HDA_REG_CORBWP      0x48    /* CORB Write Pointer */
#define HDA_REG_CORBRP      0x4A    /* CORB Read Pointer */
#define HDA_REG_CORBCTL     0x4C    /* CORB Control */
#define HDA_REG_CORBSTS     0x4D    /* CORB Status */
#define HDA_REG_CORBSIZE    0x4E    /* CORB Size */
#define HDA_REG_RIRBLBASE   0x50    /* RIRB Lower Base Address */
#define HDA_REG_RIRBUBASE   0x54    /* RIRB Upper Base Address */
#define HDA_REG_RIRBWP      0x58    /* RIRB Write Pointer */
#define HDA_REG_RINTCNT     0x5A    /* Response Interrupt Count */
#define HDA_REG_RIRBCTL     0x5C    /* RIRB Control */
#define HDA_REG_RIRBSTS     0x5D    /* RIRB Status */
#define HDA_REG_RIRBSIZE    0x5E    /* RIRB Size */
#define HDA_REG_DPLBASE     0x70    /* DMA Position Lower Base */
#define HDA_REG_DPUBASE     0x74    /* DMA Position Upper Base */

/* Output Stream Descriptor base (first output stream) */
#define HDA_SD0_BASE        0x80
#define HDA_SD_SIZE         0x20    /* Per-stream register block size */

/* Per-stream register offsets */
#define HDA_SD_CTL          0x00    /* Stream Descriptor Control */
#define HDA_SD_STS          0x03    /* Stream Descriptor Status */
#define HDA_SD_LPIB         0x04    /* Link Position in Buffer */
#define HDA_SD_CBL          0x08    /* Cyclic Buffer Length */
#define HDA_SD_LVI          0x0C    /* Last Valid Index */
#define HDA_SD_FIFOW        0x0E    /* FIFO Watermark */
#define HDA_SD_FMT          0x12    /* Stream Format */
#define HDA_SD_BDLPL        0x18    /* BDL Lower Base Address */
#define HDA_SD_BDLPU        0x1C    /* BDL Upper Base Address */

/* GCTL bits */
#define HDA_GCTL_CRST       (1 << 0)   /* Controller Reset */
#define HDA_GCTL_FCNTRL     (1 << 1)   /* Flush Control */
#define HDA_GCTL_UNSOL      (1 << 8)   /* Accept Unsolicited Responses */

/* Stream format encoding */
/* bits[14]:  1=PCM, 0=non-PCM
   bits[13:11]: sample rate multiplier
   bits[10:8]:  sample rate divisor
   bits[7]:    reserved
   bits[6:4]:  bits per sample (000=8,001=16,010=20,011=24,100=32)
   bits[3:0]:  channels-1 */
#define HDA_FMT_48KHZ_16BIT_STEREO   0x0011   /* 48kHz, 16-bit, stereo */
#define HDA_FMT_44KHZ_16BIT_STEREO   0x4011   /* 44.1kHz, 16-bit, stereo */

/* ============================
   BDL (Buffer Descriptor List) Entry
   ============================ */

typedef struct __attribute__((packed)) hda_bdl_entry {
    uint64_t addr;      /* Physical address of buffer */
    uint32_t length;    /* Buffer length in bytes */
    uint32_t ioc;       /* Interrupt On Completion (bit 0) */
} hda_bdl_entry_t;

#define HDA_BDL_MAX_ENTRIES 256

/* ============================
   Driver State
   ============================ */

typedef struct hda_state {
    uint8_t     present;        /* 1 if HDA controller found */
    volatile uint8_t* mmio;    /* MMIO base (mapped) */
    uint64_t    mmio_phys;     /* Physical address of MMIO */
    uint64_t    mmio_size;     /* MMIO region size (16KB typical) */
    uint32_t    num_out_streams;/* Number of output streams */
    uint32_t    sample_rate;   /* Current sample rate */
    uint32_t    channels;      /* Current channel count */
    uint32_t    bits;          /* Bits per sample */
    uint8_t     volume;        /* Current volume (0-100) */
    uint8_t     running;       /* 1 if output stream is active */

    /* BDL for output stream 0 */
    hda_bdl_entry_t bdl[HDA_BDL_MAX_ENTRIES] __attribute__((aligned(128)));
    uint32_t    bdl_entry_count;
} hda_state_t;

/* ============================
   Driver API
   ============================ */

/* Initialize HDA controller (PCI probe + reset + codec enum).
   Returns 1 if HDA found and initialized, 0 if absent/unsupported. */
int hda_init(void);

/* Check if HDA is available */
int hda_present(void);

/* Configure output stream format */
int hda_set_format(uint32_t sample_rate, uint32_t channels, uint32_t bits_per_sample);

/* Submit a buffer for playback via DMA.
   phys_addr: physical address of the PCM buffer.
   byte_count: size in bytes.
   Returns 0 on success. */
int hda_submit_buffer(uint64_t phys_addr, uint32_t byte_count);

/* Start/stop playback */
int hda_start(void);
int hda_stop(void);

/* Set volume (0–100) */
void hda_set_volume(uint8_t vol);

/* Poll for completion of current buffer (blocking for simple use) */
void hda_poll_complete(void);

/* Play one PCM buffer synchronously.
   Resets stream state for each buffer to avoid BDL overflow on long tracks.
   Returns 0 on success, -1 on error. */
int hda_play_buffer_blocking(uint64_t phys_addr, uint32_t byte_count);

/* PC speaker fallback beep (for systems without HDA) */
void hda_beep(uint32_t freq_hz, uint32_t duration_ms);

#endif /* GRACEOS_HDA_H */
