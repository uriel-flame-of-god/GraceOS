// ============================
// GraceOS cfdisk — Partition Operations
// ============================

#ifndef CFDISK_OPERATIONS_H
#define CFDISK_OPERATIONS_H

#include "../../../lib/libc/int.h"
#include "cfdisk.h"

/* Add a new partition using the first available free region.
   Returns 0 on success, -1 if no free space. */
int ops_new_partition(cfdisk_state_t* state, uint64_t size_sectors, uint8_t type);

/* Mark the selected partition as deleted (pending commit). */
int ops_delete_partition(cfdisk_state_t* state);

/* Commit all in-memory changes to disk.
   Validates layout, then writes MBR or GPT. Returns 0 on success. */
int ops_commit(cfdisk_state_t* state);

/* Create a GPT layout with EFI + BranchFS partitions. */
int ops_make_efi_branchfs(cfdisk_state_t* state, uint64_t efi_size_mb);

/* Rebuild the free region list from the current partition table. */
void ops_rebuild_free(cfdisk_state_t* state);

/* Sort partitions by start_lba ascending. */
void ops_sort_partitions(cfdisk_state_t* state);

/* Move selection up/down */
void ops_select_prev(cfdisk_state_t* state);
void ops_select_next(cfdisk_state_t* state);

#endif /* CFDISK_OPERATIONS_H */
