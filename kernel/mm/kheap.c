// ============================
// Lumina Kernel Heap
// Rock-solid block allocator
// ============================
//
// Design Rules:
// - Max allocation: 64KB (larger buffers use PMM+VMM)
// - 8-byte aligned allocations
// - Page-aligned heap region
// - Magic numbers detect corruption
// - Boundary tags enable safe coalescing
//

#include "kheap.h"
#include "../../lib/libc/string.h"
#include "pmm/pmm.h"

// Configuration
#define KHEAP_MAGIC       0xDEADBEEF
#define KHEAP_MAX_ALLOC   (64 * 1024)  // 64KB max per allocation
#define KHEAP_ALIGN       8

// Alignment macros
#define ALIGN_UP(x, a)    (((x) + (a) - 1) & ~((a) - 1))
#define ALIGN_DOWN(x, a)  ((x) & ~((a) - 1))

// Block header (at start of each block)
struct heap_block {
    uint32_t magic;              // Corruption detection
    uint32_t free;               // 1 = free, 0 = allocated
    uint64_t size;               // Total block size (header + payload + footer)
    struct heap_block* next;     // Next block in list
    struct heap_block* prev;     // Previous block in list
};

// Block footer (at end of each block, for backward coalescing)
struct heap_footer {
    uint64_t size;               // Mirrors header size
    uint32_t magic;              // Corruption detection
};

// Heap state
static struct heap_block* heap_start = 0;
static uint64_t heap_base = 0;
static uint64_t heap_limit = 0;

// Get footer from header
static inline struct heap_footer* get_footer(struct heap_block* block)
{
    return (struct heap_footer*)((uint8_t*)block + block->size - sizeof(struct heap_footer));
}

// Get header from footer
static inline struct heap_block* get_header_from_footer(struct heap_footer* footer)
{
    return (struct heap_block*)((uint8_t*)footer + sizeof(struct heap_footer) - footer->size);
}

// Validate a block's integrity
static inline int block_valid(struct heap_block* block)
{
    if (!block) return 0;
    if (block->magic != KHEAP_MAGIC) return 0;
    
    struct heap_footer* footer = get_footer(block);
    if (footer->magic != KHEAP_MAGIC) return 0;
    if (footer->size != block->size) return 0;
    
    return 1;
}

// Initialize heap
void kheap_init(void)
{
    // Heap region: starts after kernel+bitmap, page-aligned
    uint64_t raw_start = KHEAP_START;
    uint64_t raw_size  = KHEAP_INITIAL_SIZE;

    uint64_t reserved_end = pmm_kernel_reserved_end();
    if (reserved_end > raw_start)
        raw_start = reserved_end;

    uint64_t phys_total = pmm_total();
    if (phys_total && raw_start + raw_size > phys_total)
    {
        if (phys_total > raw_start)
            raw_size = phys_total - raw_start;
        else
            raw_size = 0;
    }
    
    // Page-align boundaries
    heap_base  = ALIGN_UP(raw_start, PAGE_SIZE);
    heap_limit = ALIGN_DOWN(raw_start + raw_size, PAGE_SIZE);
    
    // Minimum block size
    uint64_t min_block = sizeof(struct heap_block) + sizeof(struct heap_footer);
    uint64_t total_size = heap_limit - heap_base;
    
    if (total_size < min_block)
        return;  // Not enough space
    
    // Initialize first block spanning entire heap
    heap_start = (struct heap_block*)heap_base;
    heap_start->magic = KHEAP_MAGIC;
    heap_start->free = 1;
    heap_start->size = total_size;
    heap_start->next = 0;
    heap_start->prev = 0;
    
    // Initialize footer
    struct heap_footer* footer = get_footer(heap_start);
    footer->size = total_size;
    footer->magic = KHEAP_MAGIC;

    /* Prevent PMM from handing out heap pages. */
    pmm_reserve_range(heap_base, total_size);
}

// Allocate memory
void* kmalloc(size_t size)
{
    if (!heap_start || size == 0)
        return 0;
    
    // Enforce max allocation size - large buffers must use PMM+VMM
    if (size > KHEAP_MAX_ALLOC)
        return 0;
    
    // Calculate total needed: header + aligned payload + footer
    size_t payload = ALIGN_UP(size, KHEAP_ALIGN);
    size_t needed = sizeof(struct heap_block) + payload + sizeof(struct heap_footer);
    
    // Minimum block size for splitting
    size_t min_split = sizeof(struct heap_block) + KHEAP_ALIGN + sizeof(struct heap_footer);
    
    // First-fit search
    struct heap_block* block = heap_start;
    while (block)
    {
        // Validate block integrity
        if (!block_valid(block))
            return 0;  // Corruption detected - fail safely
        
        if (block->free && block->size >= needed)
        {
            // Check if we can split
            uint64_t remaining = block->size - needed;
            
            if (remaining >= min_split)
            {
                // Split block
                struct heap_block* new_block = (struct heap_block*)((uint8_t*)block + needed);
                new_block->magic = KHEAP_MAGIC;
                new_block->free = 1;
                new_block->size = remaining;
                new_block->next = block->next;
                new_block->prev = block;
                
                // Update new block's footer
                struct heap_footer* new_footer = get_footer(new_block);
                new_footer->size = remaining;
                new_footer->magic = KHEAP_MAGIC;
                
                // Update original block
                block->size = needed;
                block->next = new_block;
                
                // Update original block's footer
                struct heap_footer* footer = get_footer(block);
                footer->size = needed;
                footer->magic = KHEAP_MAGIC;
                
                // Fix next block's prev pointer
                if (new_block->next)
                    new_block->next->prev = new_block;
            }
            
            // Mark allocated
            block->free = 0;
            
            // Return pointer to payload (after header)
            return (void*)((uint8_t*)block + sizeof(struct heap_block));
        }
        
        block = block->next;
    }
    
    return 0;  // Out of memory
}

// Free memory
void kfree(void* ptr)
{
    if (!ptr || !heap_start)
        return;
    
    // Get block header
    struct heap_block* block = (struct heap_block*)((uint8_t*)ptr - sizeof(struct heap_block));
    
    // Validate
    if (!block_valid(block))
        return;  // Corruption - ignore silently
    
    if (block->free)
        return;  // Double free - ignore
    
    // Mark as free
    block->free = 1;
    
    // Coalesce with next block if free
    if (block->next && block_valid(block->next) && block->next->free)
    {
        struct heap_block* next = block->next;
        block->size += next->size;
        block->next = next->next;
        
        if (block->next)
            block->next->prev = block;
        
        // Update footer
        struct heap_footer* footer = get_footer(block);
        footer->size = block->size;
        footer->magic = KHEAP_MAGIC;
    }
    
    // Coalesce with previous block if free
    if (block->prev && block_valid(block->prev) && block->prev->free)
    {
        struct heap_block* prev = block->prev;
        prev->size += block->size;
        prev->next = block->next;
        
        if (prev->next)
            prev->next->prev = prev;
        
        // Update footer
        struct heap_footer* footer = get_footer(prev);
        footer->size = prev->size;
        footer->magic = KHEAP_MAGIC;
    }
}

// Get used memory
uint64_t kheap_get_used(void)
{
    if (!heap_start)
        return 0;
    
    uint64_t used = 0;
    struct heap_block* block = heap_start;
    
    while (block && block_valid(block))
    {
        if (!block->free)
            used += block->size;
        block = block->next;
    }
    
    return used;
}

// Get free memory
uint64_t kheap_get_free(void)
{
    if (!heap_start)
        return 0;
    
    uint64_t free = 0;
    struct heap_block* block = heap_start;
    
    while (block && block_valid(block))
    {
        if (block->free)
            free += block->size;
        block = block->next;
    }
    
    return free;
}

// Validate entire heap (debug)
int kheap_validate(void)
{
    if (!heap_start)
        return 0;
    
    struct heap_block* block = heap_start;
    int count = 0;
    
    while (block)
    {
        if (!block_valid(block))
            return 0;  // Corruption found
        
        // Sanity check: block within heap bounds
        uint64_t block_addr = (uint64_t)block;
        if (block_addr < heap_base || block_addr >= heap_limit)
            return 0;
        
        block = block->next;
        count++;
        
        // Prevent infinite loop
        if (count > 100000)
            return 0;
    }
    
    return 1;  // Heap is valid
}
