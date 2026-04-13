// ============================
// GraceOS cfdisk — Disk I/O Layer
// Wraps kernel disk syscalls
// ============================

#ifndef CFDISK_DISK_H
#define CFDISK_DISK_H

#include "../../../lib/libc/int.h"
#include "cfdisk.h"

/* Read 'count' sectors starting at 'lba' into 'buf'. Returns 0 on success. */
int disk_read(int dev, uint64_t lba, uint32_t count, void* buf);

/* Write 'count' sectors starting at 'lba' from 'buf'. Returns 0 on success. */
int disk_write(int dev, uint64_t lba, uint32_t count, const void* buf);

/* Fill out a disk_info_t for device 'dev'. Returns 0 on success. */
int disk_get_info(int dev, disk_info_t* info);

/* Align lba up to ALIGNMENT_LBA boundary */
uint64_t disk_align_lba(uint64_t lba);

#endif /* CFDISK_DISK_H */
