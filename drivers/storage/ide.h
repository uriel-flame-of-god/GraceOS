// ============================
// GraceOS IDE (ATA PIO) Driver
// Primary master, 28-bit LBA
// ============================

#ifndef GRACEOS_IDE_H
#define GRACEOS_IDE_H

#include "../../lib/libc/int.h"

/* Initialize IDE driver (returns 1 if device is present) */
int ide_init(void);

/* Check if IDE device is ready */
int ide_ready(void);

/* Get device size in bytes */
uint64_t ide_size(void);

/* Read sectors (512-byte) */
int ide_read(uint64_t lba, void* buffer, uint32_t sectors);

/* Write sectors (512-byte) */
int ide_write(uint64_t lba, const void* buffer, uint32_t sectors);

#endif /* GRACEOS_IDE_H */
