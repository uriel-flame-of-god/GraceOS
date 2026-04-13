// ============================
// GraceOS Persistent Memory API
// IDE/NVMe-backed block access
// ============================

#ifndef GRACEOS_PMEM_H
#define GRACEOS_PMEM_H

#include "../../lib/libc/int.h"

#define PMEM_SECTOR_SIZE 512

/* Initialize persistent memory backend */
int pmem_init(void);

/* Check if persistent memory is ready */
int pmem_ready(void);

/* Get persistent memory size in bytes */
uint64_t pmem_size(void);

/* Read from persistent memory (offset/size must be 512-byte aligned) */
int pmem_read(uint64_t offset, void* buffer, uint64_t size);

/* Write to persistent memory (offset/size must be 512-byte aligned) */
int pmem_write(uint64_t offset, const void* buffer, uint64_t size);

/* Get backend name ("NVMe", "IDE", or "None") */
const char* pmem_backend_name(void);

#endif /* GRACEOS_PMEM_H */
