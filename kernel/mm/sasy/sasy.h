// ============================
// GraceOS SASY - Segment Allocator System
// NT/MS-DOS 4 Inspired Segment Manager
// ============================

#ifndef GRACEOS_SASY_H
#define GRACEOS_SASY_H

#include "types.h"
#include "handle.h"
#include "swap.h"
#include "loader.h"

/* ============================
   SASY Configuration
   ============================ */

#define MAX_SEGMENTS        4096
#define SASY_BASE_ADDR      0x0000100000000000ULL  /* Segment virtual base */
#define SASY_MAX_SIZE       0x0000010000000000ULL  /* 1TB max segment space */

/* ============================
   SASY Public API
   ============================ */

/* Initialize SASY subsystem */
void sasy_init(void);

/* Create a new segment */
seg_handle_t sasy_create(
    uint64_t size,
    seg_type_t type,
    uint32_t flags
);

/* Create segment with specific attributes */
seg_handle_t sasy_create_ex(
    uint64_t size,
    seg_type_t type,
    uint32_t flags,
    uint32_t owner_pid
);

/* Lock a segment for access (returns virtual address) */
void* sasy_lock(seg_handle_t h);

/* Unlock a segment */
void sasy_unlock(seg_handle_t h);

/* Free a segment */
void sasy_free(seg_handle_t h);

/* Resize a segment */
int sasy_resize(seg_handle_t h, uint64_t new_size);

/* Mark segment as dirty */
void sasy_mark_dirty(seg_handle_t h);

/* Get segment information */
int sasy_get_info(seg_handle_t h, segment_t* info);

/* Get segment size */
uint64_t sasy_get_size(seg_handle_t h);

/* Get segment type */
seg_type_t sasy_get_type(seg_handle_t h);

/* ============================
   Memory Pressure Handling
   ============================ */

/* Called by PMM when memory is low */
void sasy_pressure(void);

/* Set memory pressure threshold */
void sasy_set_pressure_threshold(uint64_t bytes);

/* ============================
   Code Sharing API
   ============================ */

/* Load or share code segment */
seg_handle_t sasy_load_code(const char* path, uint32_t owner_pid);

/* ============================
   Process Integration
   ============================ */

/* Create heap segment for process */
seg_handle_t sasy_create_heap(uint32_t pid, uint64_t initial_size);

/* Create stack segment for process */
seg_handle_t sasy_create_stack(uint32_t pid, uint64_t size);

/* Clone segment for fork (copy-on-write) */
seg_handle_t sasy_clone(seg_handle_t h, uint32_t new_pid);

/* Release all segments for a process */
void sasy_release_process(uint32_t pid);

/* ============================
   Statistics and Debug
   ============================ */

/* Get SASY statistics */
void sasy_get_stats(seg_stats_t* stats);

/* Print segment table (debug) */
void sasy_dump(void);

/* Check SASY health */
int sasy_check(void);

/* ============================
   Segment Lookup
   ============================ */

/* Find segment containing address */
segment_t* sasy_find_segment(uint64_t addr);

/* Check if address is in segment space */
int sasy_is_segment_addr(uint64_t addr);

#endif /* GRACEOS_SASY_H */
