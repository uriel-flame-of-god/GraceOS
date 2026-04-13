// ============================
// GraceOS cfdisk — Partition Manager
// Userland TUI tool, syscall-based
// ============================

#ifndef GRACEOS_CFDISK_H
#define GRACEOS_CFDISK_H

#include "../../../lib/libc/int.h"

/* ============================
   Geometry constants
   ============================ */

#define SECTOR_SIZE      512
#define ALIGNMENT_LBA    2048       /* 1 MiB alignment */
#define MAX_MBR_PARTS    4
#define MAX_GPT_PARTS    128
#define MAX_PARTITIONS   128        /* Unified in-memory limit */

/* ============================
   Partition types
   ============================ */

#define PART_TYPE_EMPTY    0x00
#define PART_TYPE_FAT16    0x06
#define PART_TYPE_EFI      0xEF
#define PART_TYPE_LINUX    0x83
#define PART_TYPE_SWAP     0x82
#define PART_TYPE_EXTENDED 0x05
#define PART_TYPE_GPT      0xEE     /* Protective MBR GPT marker */
#define PART_TYPE_GRACE    0xAA     /* GraceOS native partition */

/* ============================
   Disk info (from sys_disk_info)
   ============================ */

typedef struct disk_info {
    uint64_t sector_count;
    uint64_t size_bytes;
    uint32_t sector_size;
} disk_info_t;

/* ============================
   In-memory partition descriptor
   ============================ */

typedef struct partition {
    uint64_t start_lba;
    uint64_t end_lba;
    uint64_t size_sectors;

    uint8_t  type;
    char     name[36];

    bool     is_new;        /* Added this session, not yet committed */
    bool     is_deleted;    /* Marked for deletion on commit */
    bool     is_gpt;        /* From a GPT table */
} partition_t;

/* ============================
   cfdisk session state
   ============================ */

#define CFDISK_MAX_FREE_REGIONS 32

typedef struct free_region {
    uint64_t start_lba;
    uint64_t size_sectors;
} free_region_t;

typedef enum table_type {
    TABLE_UNKNOWN = 0,
    TABLE_MBR,
    TABLE_GPT
} table_type_t;

typedef struct cfdisk_state {
    int          dev;               /* Device index (0 = IDE primary) */
    disk_info_t  disk;
    table_type_t table_type;

    partition_t  parts[MAX_PARTITIONS];
    int          part_count;

    free_region_t free[CFDISK_MAX_FREE_REGIONS];
    int           free_count;

    int  selected;              /* Currently selected partition index */
    bool modified;              /* Unsaved changes exist */
    bool running;
} cfdisk_state_t;

/* ============================
   Entry point
   ============================ */

void cfdisk_run(int dev);

#endif /* GRACEOS_CFDISK_H */
