// ============================
// GraceOS SASY Types
// Segment Allocator System Types
// ============================

#ifndef GRACEOS_SASY_TYPES_H
#define GRACEOS_SASY_TYPES_H

#include "../../../lib/libc/int.h"

/* ============================
   Segment Types
   ============================ */

typedef enum {
    SEG_FIXED,          /* Fixed segment - never moved or swapped */
    SEG_MANUAL,         /* Manually managed segment */
    SEG_CODE,           /* Shared code segment (executable) */
    SEG_DATA_AUTO,      /* Automatic data segment (per-process) */
    SEG_DATA_INST,      /* Instance data segment (copy-on-write) */
    SEG_PHYSICAL        /* Direct physical memory mapping */
} seg_type_t;

/* ============================
   Segment Flags
   ============================ */

#define SEG_FLAG_NONE       0x00000000
#define SEG_FLAG_READ       0x00000001  /* Segment is readable */
#define SEG_FLAG_WRITE      0x00000002  /* Segment is writable */
#define SEG_FLAG_EXEC       0x00000004  /* Segment is executable */
#define SEG_FLAG_USER       0x00000008  /* User-accessible segment */
#define SEG_FLAG_NOCACHE    0x00000010  /* Disable caching */
#define SEG_FLAG_DISCARD    0x00000020  /* Discardable when under pressure */
#define SEG_FLAG_NOSWAP     0x00000040  /* Never swap to disk */
#define SEG_FLAG_ZEROED     0x00000080  /* Zero-fill on allocation */
#define SEG_FLAG_SHARED     0x00000100  /* Shared between processes */

/* Combined flag sets */
#define SEG_FLAGS_RW        (SEG_FLAG_READ | SEG_FLAG_WRITE)
#define SEG_FLAGS_RX        (SEG_FLAG_READ | SEG_FLAG_EXEC)
#define SEG_FLAGS_RWX       (SEG_FLAG_READ | SEG_FLAG_WRITE | SEG_FLAG_EXEC)
#define SEG_FLAGS_USER_RW   (SEG_FLAGS_RW | SEG_FLAG_USER)
#define SEG_FLAGS_USER_RX   (SEG_FLAGS_RX | SEG_FLAG_USER)

/* ============================
   Segment Descriptor
   ============================ */

typedef struct segment {
    uint32_t id;            /* Unique segment ID */
    seg_type_t type;        /* Segment type */

    uint64_t base;          /* Virtual base address */
    uint64_t size;          /* Size in bytes (page-aligned) */

    uint64_t phys;          /* Physical address (if resident) */

    uint32_t refcnt;        /* Reference count */
    uint32_t flags;         /* Segment flags */

    uint8_t locked;         /* Lock count (prevents swapping) */
    uint8_t present;        /* 1 if resident in memory */
    uint8_t dirty;          /* 1 if modified since last swap */
    uint8_t shared;         /* 1 if shared between processes */

    uint64_t swap_id;       /* Swap file block ID (0 if not swapped) */
    uint64_t code_hash;     /* Hash for code sharing (SEG_CODE only) */
} segment_t;

/* ============================
   Segment Statistics
   ============================ */

typedef struct seg_stats {
    uint32_t total_segments;
    uint32_t resident_segments;
    uint32_t swapped_segments;
    uint32_t locked_segments;
    uint64_t total_virtual_bytes;
    uint64_t total_physical_bytes;
    uint64_t total_swapped_bytes;
} seg_stats_t;

#endif /* GRACEOS_SASY_TYPES_H */
