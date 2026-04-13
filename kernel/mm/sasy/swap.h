// ============================
// GraceOS SASY Swap Manager
// Segment swapping to BranchFS
// ============================

#ifndef GRACEOS_SASY_SWAP_H
#define GRACEOS_SASY_SWAP_H

#include "types.h"

/* ============================
   Swap Configuration
   ============================ */

#define MAX_SWAP_ENTRIES    1024
#define SWAP_BLOCK_SIZE     4096

/* ============================
   Swap Entry
   ============================ */

typedef struct {
    uint64_t disk_block;    /* Starting disk block */
    uint64_t pages;         /* Number of pages */
    uint32_t seg_id;        /* Associated segment ID */
    uint8_t in_use;         /* 1 if entry is in use */
} swap_entry_t;

/* ============================
   Swap Statistics
   ============================ */

typedef struct {
    uint32_t total_entries;
    uint32_t used_entries;
    uint64_t bytes_swapped_out;
    uint64_t bytes_swapped_in;
    uint32_t swap_out_count;
    uint32_t swap_in_count;
} swap_stats_t;

/* ============================
   Swap API
   ============================ */

/* Initialize swap subsystem */
void swap_init(void);

/* Configure swap region in persistent memory */
void swap_set_region(uint64_t base, uint64_t size);

/* Check if swap is available */
int swap_available(void);

/* Allocate swap space for a segment */
uint64_t swap_alloc(uint64_t size);

/* Free swap space */
void swap_free(uint64_t swap_id);

/* Write segment to swap */
int swap_out(segment_t* s);

/* Read segment from swap */
int swap_read(segment_t* s);

/* Schedule segment for swapping (async) */
void swap_schedule(segment_t* s);

/* Process pending swap operations */
void swap_process_pending(void);

/* Get swap statistics */
void swap_get_stats(swap_stats_t* stats);

/* Check if segment can be swapped */
int swap_can_swap(segment_t* s);

#endif /* GRACEOS_SASY_SWAP_H */
