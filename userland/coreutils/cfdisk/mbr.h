// ============================
// GraceOS cfdisk — MBR Handling
// ============================

#ifndef CFDISK_MBR_H
#define CFDISK_MBR_H

#include "../../../lib/libc/int.h"
#include "cfdisk.h"

/* ============================
   On-disk MBR structures
   ============================ */

#define MBR_SIGNATURE    0xAA55
#define MBR_BOOTCODE_SIZE 446

typedef struct __attribute__((packed)) mbr_entry {
    uint8_t  status;
    uint8_t  chs_first[3];
    uint8_t  type;
    uint8_t  chs_last[3];
    uint32_t lba_start;
    uint32_t lba_size;
} mbr_entry_t;

typedef struct __attribute__((packed)) mbr {
    uint8_t     bootcode[MBR_BOOTCODE_SIZE];
    mbr_entry_t entries[4];
    uint16_t    signature;
} mbr_t;

/* ============================
   MBR API
   ============================ */

/* Read and parse MBR into state->parts. Returns 0 on success. */
int mbr_read(cfdisk_state_t* state);

/* Write state->parts back to MBR, preserving bootcode. Returns 0 on success. */
int mbr_write(cfdisk_state_t* state);

/* Validate that partitions do not overlap and fit within the disk. */
int mbr_validate(cfdisk_state_t* state);

#endif /* CFDISK_MBR_H */
