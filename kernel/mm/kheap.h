// ============================
// Lumina Kernel Heap
// Rock-solid block allocator
// ============================
//
// Allocation Policy:
// - kmalloc: max 64KB (structs, metadata, tables)
// - Larger buffers: use PMM + VMM directly
//

#ifndef GRACEOS_KHEAP_H
#define GRACEOS_KHEAP_H

#include "../../lib/libc/int.h"

#define KHEAP_START 0x400000        // 4MB (page-aligned)
#define KHEAP_INITIAL_SIZE 0x2000000 // 32MB
#define KHEAP_MAX_SIZE 0x4000000     // 64MB (future expansion)
#define KHEAP_MAX_ALLOC (64 * 1024)  // 64KB max per allocation

// Initialize kernel heap
void kheap_init(void);

// Allocate memory (max 64KB)
void* kmalloc(size_t size);

// Free memory
void kfree(void* ptr);

// Get heap statistics
uint64_t kheap_get_used(void);
uint64_t kheap_get_free(void);

// Validate heap integrity (debug)
int kheap_validate(void);

#endif
