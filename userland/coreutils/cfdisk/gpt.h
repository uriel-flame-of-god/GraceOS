// ============================
// GraceOS cfdisk — GPT Handling
// ============================

#ifndef CFDISK_GPT_H
#define CFDISK_GPT_H

#include "../../../lib/libc/int.h"
#include "cfdisk.h"

/* ============================
   GPT on-disk structures
   ============================ */

#define GPT_HEADER_SIGNATURE  0x5452415020494645ULL  /* "EFI PART" LE */
#define GPT_HEADER_REVISION   0x00010000
#define GPT_ENTRY_SIZE        128
#define GPT_ENTRIES_PER_LBA   4   /* 4 x 128 = 512 bytes */

typedef struct __attribute__((packed)) gpt_header {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t my_lba;
    uint64_t alternate_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t partition_entry_lba;
    uint32_t num_partition_entries;
    uint32_t sizeof_partition_entry;
    uint32_t partition_entry_array_crc32;
} gpt_header_t;

typedef struct __attribute__((packed)) gpt_entry {
    uint8_t  type_guid[16];
    uint8_t  unique_guid[16];
    uint64_t start_lba;
    uint64_t end_lba;
    uint64_t attributes;
    uint16_t name[36];   /* UTF-16LE, 36 chars */
} gpt_entry_t;

/* ============================
   GPT API
   ============================ */

/* Read GPT and populate state->parts. Returns 0 on success. */
int gpt_read(cfdisk_state_t* state);

/* Write primary and backup GPT. Returns 0 on success. */
int gpt_write(cfdisk_state_t* state);

/* Validate CRC32 of header and partition array. Returns 0 if valid. */
int gpt_validate(cfdisk_state_t* state);

/* CRC32 helper (used internally and by tests) */
uint32_t crc32_compute(const void* data, uint32_t length);

#endif /* CFDISK_GPT_H */
