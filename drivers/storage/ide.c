// ============================
// GraceOS IDE (ATA PIO) Driver
// Primary master, 28-bit LBA
// ============================

#include "ide.h"
#include "../../kernel/arch/x86_64/io/port.h"
#include "../../kernel/log/klog.h"
#include "../video/tty.h"

#define ATA_PRIMARY_IO     0x1F0
#define ATA_PRIMARY_CTRL   0x3F6

#define ATA_REG_DATA       0x00
#define ATA_REG_ERROR      0x01
#define ATA_REG_SECCOUNT0  0x02
#define ATA_REG_LBA0       0x03
#define ATA_REG_LBA1       0x04
#define ATA_REG_LBA2       0x05
#define ATA_REG_HDDEVSEL   0x06
#define ATA_REG_COMMAND    0x07
#define ATA_REG_STATUS     0x07

#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30
#define ATA_CMD_CACHE_FLUSH 0xE7
#define ATA_CMD_IDENTIFY   0xEC

#define ATA_SR_BSY         0x80
#define ATA_SR_DRDY        0x40
#define ATA_SR_DF          0x20
#define ATA_SR_DRQ         0x08
#define ATA_SR_ERR         0x01

#define IDE_SECTOR_SIZE    512

typedef struct {
    uint16_t io_base;
    uint16_t ctrl_base;
    uint8_t present;
    uint64_t total_sectors;
} ide_device_t;

static ide_device_t ide_dev = {0};

static inline void ide_delay_400ns(void)
{
    (void)inb(ide_dev.ctrl_base);
    (void)inb(ide_dev.ctrl_base);
    (void)inb(ide_dev.ctrl_base);
    (void)inb(ide_dev.ctrl_base);
}

#define IDE_POLL_TIMEOUT 10000000  /* ~1-2 seconds at typical CPU speed */

static int ide_poll(int check_drq)
{
    uint8_t status = 0;
    uint64_t timeout = IDE_POLL_TIMEOUT;

    /* Wait for BSY to clear with timeout */
    while ((status = inb(ide_dev.io_base + ATA_REG_STATUS)) & ATA_SR_BSY)
    {
        if (--timeout == 0)
            return -1;  /* Timeout */
        __asm__ volatile ("pause");
    }

    if (status & ATA_SR_ERR)
        return -1;
    if (status & ATA_SR_DF)
        return -1;
    if (check_drq && !(status & ATA_SR_DRQ))
        return -1;

    return 0;
}

static int ide_identify_primary_master(void)
{
    uint16_t data[256];

    outb(ide_dev.io_base + ATA_REG_HDDEVSEL, 0xA0);
    ide_delay_400ns();

    outb(ide_dev.io_base + ATA_REG_SECCOUNT0, 0);
    outb(ide_dev.io_base + ATA_REG_LBA0, 0);
    outb(ide_dev.io_base + ATA_REG_LBA1, 0);
    outb(ide_dev.io_base + ATA_REG_LBA2, 0);

    outb(ide_dev.io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    if (inb(ide_dev.io_base + ATA_REG_STATUS) == 0)
        return 0;

    if (ide_poll(0) != 0)
        return 0;

    if (ide_poll(1) != 0)
        return 0;

    for (int i = 0; i < 256; i++)
        data[i] = inw(ide_dev.io_base + ATA_REG_DATA);

    ide_dev.total_sectors = ((uint32_t)data[61] << 16) | data[60];

    if (ide_dev.total_sectors == 0)
        return 0;

    return 1;
}

int ide_init(void)
{
    ide_dev.io_base = ATA_PRIMARY_IO;
    ide_dev.ctrl_base = ATA_PRIMARY_CTRL;
    ide_dev.present = 0;
    ide_dev.total_sectors = 0;

    klog_log("IDE: Probing primary master...");
    
    if (!ide_identify_primary_master())
    {
        klog_warn("IDE: No primary master detected");
        return 0;
    }

    ide_dev.present = 1;
    klog_log("IDE: Primary master online");
    
    // Log the disk size in sectors
    tty_print("[IDE] Total sectors: ");
    tty_print_hex(ide_dev.total_sectors);
    tty_print(" (");
    tty_print_hex(ide_dev.total_sectors * IDE_SECTOR_SIZE);
    tty_print(" bytes)\n");
    
    return 1;
}

int ide_ready(void)
{
    return ide_dev.present;
}

uint64_t ide_size(void)
{
    return ide_dev.total_sectors * IDE_SECTOR_SIZE;
}

int ide_read(uint64_t lba, void* buffer, uint32_t sectors)
{
    tty_print("R");
    if (!ide_dev.present || !buffer || sectors == 0)
    {
        klog_warn("IDE read: invalid params");
        return -1;
    }

    if (lba + sectors > ide_dev.total_sectors)
    {
        klog_warn("IDE read: out of bounds");
        return -1;
    }

    uint16_t* buf = (uint16_t*)buffer;

    while (sectors > 0)
    {
        uint8_t chunk = (sectors > 255) ? 255 : (uint8_t)sectors;

        outb(ide_dev.io_base + ATA_REG_HDDEVSEL, 0xE0 | ((lba >> 24) & 0x0F));
        ide_delay_400ns();

        outb(ide_dev.io_base + ATA_REG_SECCOUNT0, chunk);
        outb(ide_dev.io_base + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
        outb(ide_dev.io_base + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
        outb(ide_dev.io_base + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
        outb(ide_dev.io_base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

        for (uint32_t s = 0; s < chunk; s++)
        {
            if (ide_poll(1) != 0)
                return -1;

            for (int i = 0; i < 256; i++)
                buf[i] = inw(ide_dev.io_base + ATA_REG_DATA);

            buf += 256;
        }

        lba += chunk;
        sectors -= chunk;
    }

    tty_print("r");
    return 0;
}

int ide_write(uint64_t lba, const void* buffer, uint32_t sectors)
{
    tty_print("W");
    if (!ide_dev.present || !buffer || sectors == 0)
    {
        klog_warn("IDE write: invalid params");
        return -1;
    }

    if (lba + sectors > ide_dev.total_sectors)
    {
        klog_warn("IDE write: out of bounds");
        return -1;
    }

    const uint16_t* buf = (const uint16_t*)buffer;

    while (sectors > 0)
    {
        uint8_t chunk = (sectors > 255) ? 255 : (uint8_t)sectors;

        outb(ide_dev.io_base + ATA_REG_HDDEVSEL, 0xE0 | ((lba >> 24) & 0x0F));
        ide_delay_400ns();

        outb(ide_dev.io_base + ATA_REG_SECCOUNT0, chunk);
        outb(ide_dev.io_base + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
        outb(ide_dev.io_base + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
        outb(ide_dev.io_base + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
        outb(ide_dev.io_base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

        for (uint32_t s = 0; s < chunk; s++)
        {
            /* Wait for DRQ - drive ready to accept data */
            if (ide_poll(1) != 0)
                return -1;

            /* Write 256 words (512 bytes = 1 sector) */
            for (int i = 0; i < 256; i++)
                outw(ide_dev.io_base + ATA_REG_DATA, buf[i]);

            buf += 256;
            
            /* Small delay after writing sector */
            ide_delay_400ns();
        }

        /* Flush cache and wait for completion */
        outb(ide_dev.io_base + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
        if (ide_poll(0) != 0)
            return -1;

        lba += chunk;
        sectors -= chunk;
    }

    tty_print("w");
    return 0;
}
