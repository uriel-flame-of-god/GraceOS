// ============================
// GraceOS SASY Loader
// Demand paging and code loading
// ============================

#ifndef GRACEOS_SASY_LOADER_H
#define GRACEOS_SASY_LOADER_H

#include "types.h"

/* ============================
   Loader API
   ============================ */

/* Initialize loader subsystem */
void loader_init(void);

/* Page in a segment (load into memory) */
int sasy_pagein(segment_t* s);

/* Page out a segment (unmap but keep in swap) */
int sasy_pageout(segment_t* s);

/* Load code segment from file */
int loader_load_code(segment_t* s, const char* path);

/* Find shared code segment by hash */
segment_t* loader_find_code(uint64_t hash);

/* Compute hash for code file */
uint64_t loader_hash_file(const char* path);

/* Register code segment for sharing */
void loader_register_code(segment_t* s);

/* Unregister code segment from sharing */
void loader_unregister_code(segment_t* s);

/* Handle page fault for segment */
int loader_handle_fault(uint64_t fault_addr);

#endif /* GRACEOS_SASY_LOADER_H */
