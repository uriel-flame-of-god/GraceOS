// ============================
// GraceOS Virtual Memory Manager
// x86_64 4-level paging
// ============================

#ifndef GRACEOS_VMM_H
#define GRACEOS_VMM_H

#include "../../../lib/libc/int.h"

/* Kernel virtual base address (higher half) */
#define KERNEL_BASE     0xFFFF800000000000ULL

/* Page sizes */
#define PAGE_SIZE_4K    0x1000ULL
#define PAGE_SIZE_2M    0x200000ULL
#define PAGE_SIZE_4M    0x400000ULL
#define PAGE_SIZE_1G    0x40000000ULL

/* Page table entry flags */
#define VMM_PRESENT     (1ULL << 0)     /* Page is present */
#define VMM_WRITE       (1ULL << 1)     /* Page is writable */
#define VMM_USER        (1ULL << 2)     /* User-accessible */
#define VMM_PWT         (1ULL << 3)     /* Page write-through */
#define VMM_PCD         (1ULL << 4)     /* Page cache disable */
#define VMM_ACCESSED    (1ULL << 5)     /* Page accessed */
#define VMM_DIRTY       (1ULL << 6)     /* Page dirty */
#define VMM_HUGE        (1ULL << 7)     /* Huge page (2MB/1GB) */
#define VMM_GLOBAL      (1ULL << 8)     /* Global page */
#define VMM_NOEXEC      (1ULL << 63)    /* No execute */

/* Combined flag sets */
#define VMM_KERNEL_RW   (VMM_PRESENT | VMM_WRITE)
#define VMM_KERNEL_RO   (VMM_PRESENT)
#define VMM_USER_RW     (VMM_PRESENT | VMM_WRITE | VMM_USER)
#define VMM_USER_RO     (VMM_PRESENT | VMM_USER)

/* Address masks */
#define VMM_ADDR_MASK   0x000FFFFFFFFFF000ULL

/* Initialize VMM with identity + higher-half mapping */
void vmm_init(void);

/* Map a virtual address to a physical address */
void vmm_map(uint64_t virt, uint64_t phys, uint64_t flags);

/* Map a range of addresses */
void vmm_map_range(uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags);

/* Unmap a virtual address */
void vmm_unmap(uint64_t virt);

/* Translate virtual address to physical */
uint64_t vmm_translate(uint64_t virt);

/* Switch to a different page table */
void vmm_switch(uint64_t pml4_phys);

/* Get current CR3 */
uint64_t vmm_get_cr3(void);

/* Check if VMM is initialized */
int vmm_ready(void);

/* Get VMM statistics */
uint64_t vmm_mapped_pages(void);

#endif /* GRACEOS_VMM_H */
