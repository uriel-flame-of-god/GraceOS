// ============================
// GraceOS NVMe Driver (Stub)
// ============================

#ifndef GRACEOS_NVME_H
#define GRACEOS_NVME_H

#include "../../lib/libc/int.h"

/* Initialize NVMe driver (returns 1 if device is present) */
int nvme_init(void);

/* Check if NVMe device is ready */
int nvme_ready(void);

/* Get device size in bytes */
uint64_t nvme_size(void);

/* Read sectors (512-byte) */
int nvme_read(uint64_t lba, void* buffer, uint32_t sectors);

/* Write sectors (512-byte) */
int nvme_write(uint64_t lba, const void* buffer, uint32_t sectors);

#endif /* GRACEOS_NVME_H */
