// ============================
// GraceOS Physical Memory Manager
// Bitmap-based PMM with fallback
// ============================

#ifndef GRACEOS_PMM_H
#define GRACEOS_PMM_H

#include "../../../lib/libc/int.h"

#define PAGE_SIZE 4096
#define PMM_MAX_PHYS_BYTES (64ULL * 1024 * 1024 * 1024) /* 64 GiB */

/* Initialize PMM with multiboot info */
void pmm_init(void* mb_info);

/* Allocate a physical page (returns physical address) */
uint64_t pmm_alloc_page(void);

/* Allocate a page below 16MB (for ISA DMA constraints). */
uint64_t pmm_alloc_page_low(void);

/* Free a physical page */
void pmm_free_page(uint64_t phys_addr);

/* Reserve a physical range (mark frames used) */
void pmm_reserve_range(uint64_t phys_start, uint64_t size);

/* Get total physical memory in bytes */
uint64_t pmm_total(void);

/* Get free physical memory in bytes */
uint64_t pmm_free(void);

/* Check if PMM is ready (1 = ready, 0 = using fallback) */
int pmm_ready(void);

/* Get end of kernel-reserved region (including bitmap) */
uint64_t pmm_kernel_reserved_end(void);

#endif /* GRACEOS_PMM_H */
