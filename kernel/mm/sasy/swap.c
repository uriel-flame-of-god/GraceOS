// ============================
// GraceOS SASY Swap Manager
// Implementation
// ============================

#include "swap.h"
#include "sasy.h"
#include "../vmm/vmm.h"
#include "../pmm/pmm.h"
#include "../../log/klog.h"
#include "../../../drivers/storage/pmem.h"
#include "../../../lib/libc/string.h"

/* ============================
   Swap Table
   ============================ */

static swap_entry_t swap_table[MAX_SWAP_ENTRIES];
static uint32_t swap_entry_count = 0;
static uint64_t next_swap_block = 1;  /* Block 0 reserved */

/* Swap statistics */
static swap_stats_t swap_stats;

/* Simple spinlock */
static volatile int swap_lock = 0;

/* Swap availability flag */
static int swap_ready = 0;

/* Swap region in persistent memory */
static uint64_t swap_base = 0;
static uint64_t swap_size = 0;

void swap_set_region(uint64_t base, uint64_t size)
{
    swap_base = base;
    swap_size = size;
}

/* ============================
   Lock Helpers
   ============================ */

static inline void lock_swap(void)
{
    while (__sync_lock_test_and_set(&swap_lock, 1))
    {
        #ifdef ARCH_ARM64
        __asm__ volatile ("yield");
        #else
        __asm__ volatile ("pause");
        #endif
    }
}

static inline void unlock_swap(void)
{
    __sync_lock_release(&swap_lock);
}

/* ============================
   Swap API Implementation
   ============================ */

void swap_init(void)
{
    /* Clear swap table */
    for (uint32_t i = 0; i < MAX_SWAP_ENTRIES; i++)
    {
        swap_table[i].disk_block = 0;
        swap_table[i].pages = 0;
        swap_table[i].seg_id = 0;
        swap_table[i].in_use = 0;
    }
    
    swap_entry_count = 0;
    next_swap_block = 1;
    
    /* Clear statistics */
    swap_stats.total_entries = MAX_SWAP_ENTRIES;
    swap_stats.used_entries = 0;
    swap_stats.bytes_swapped_out = 0;
    swap_stats.bytes_swapped_in = 0;
    swap_stats.swap_out_count = 0;
    swap_stats.swap_in_count = 0;
    
    /* Check if PMEM region is available for swapping */
    if (pmem_ready() && swap_size >= SWAP_BLOCK_SIZE)
    {
        swap_ready = 1;
        klog_init_msg("SASY swap initialized (PMEM backend)");
    }
    else
    {
        swap_ready = 0;
        klog_warn("[SWAP] PMEM not available, swap disabled");
    }
}

int swap_available(void)
{
    return swap_ready;
}

uint64_t swap_alloc(uint64_t size)
{
    if (!swap_ready)
        return 0;
    
    uint64_t pages = (size + SWAP_BLOCK_SIZE - 1) / SWAP_BLOCK_SIZE;
    
    lock_swap();
    
    uint64_t max_blocks = swap_size / SWAP_BLOCK_SIZE;
    if (next_swap_block + pages > max_blocks)
    {
        unlock_swap();
        klog_error("[SWAP] PMEM swap region full");
        return 0;
    }

    /* Find a free swap entry */
    for (uint32_t i = 0; i < MAX_SWAP_ENTRIES; i++)
    {
        if (!swap_table[i].in_use)
        {
            swap_table[i].disk_block = next_swap_block;
            swap_table[i].pages = pages;
            swap_table[i].in_use = 1;
            
            next_swap_block += pages;
            swap_entry_count++;
            swap_stats.used_entries++;
            
            uint64_t swap_id = swap_table[i].disk_block;
            
            unlock_swap();
            return swap_id;
        }
    }
    
    unlock_swap();
    
    klog_error("[SWAP] No free swap entries");
    return 0;
}

void swap_free(uint64_t swap_id)
{
    if (!swap_id)
        return;
    
    lock_swap();
    
    for (uint32_t i = 0; i < MAX_SWAP_ENTRIES; i++)
    {
        if (swap_table[i].in_use && swap_table[i].disk_block == swap_id)
        {
            swap_table[i].disk_block = 0;
            swap_table[i].pages = 0;
            swap_table[i].seg_id = 0;
            swap_table[i].in_use = 0;
            
            if (swap_entry_count > 0)
                swap_entry_count--;
            if (swap_stats.used_entries > 0)
                swap_stats.used_entries--;
            
            break;
        }
    }
    
    unlock_swap();
}

int swap_out(segment_t* s)
{
    if (!s || !swap_ready)
        return -1;
    
    if (!s->present)
        return 0;  /* Already swapped or not resident */
    
    if (s->locked > 0)
    {
        klog_warn("[SWAP] Cannot swap locked segment");
        return -1;
    }
    
    if (s->flags & SEG_FLAG_NOSWAP)
    {
        klog_warn("[SWAP] Segment marked NOSWAP");
        return -1;
    }
    
    /* Allocate swap space if needed */
    if (!s->swap_id)
    {
        s->swap_id = swap_alloc(s->size);
        if (!s->swap_id)
        {
            klog_error("[SWAP] Failed to allocate swap space");
            return -1;
        }
    }
    
    /* Write segment data to swap */
    uint64_t swap_offset = swap_base + (s->swap_id * SWAP_BLOCK_SIZE);
    if (swap_offset + s->size > swap_base + swap_size)
    {
        klog_error("[SWAP] Swap write out of bounds");
        return -1;
    }
    if (pmem_write(swap_offset, (void*)s->base, s->size) != 0)
    {
        klog_error("[SWAP] Failed to write PMEM swap block");
        return -1;
    }
    
    /* Unmap segment from virtual memory */
    uint64_t addr = s->base;
    uint64_t end = s->base + s->size;
    
    while (addr < end)
    {
        vmm_unmap(addr);
        addr += PAGE_SIZE;
    }
    
    /* Free physical memory */
    if (s->phys)
    {
        uint64_t phys = s->phys;
        uint64_t pages = s->size / PAGE_SIZE;
        
        for (uint64_t i = 0; i < pages; i++)
        {
            pmm_free_page(phys + i * PAGE_SIZE);
        }
        
        s->phys = 0;
    }
    
    s->present = 0;
    
    /* Update statistics */
    swap_stats.bytes_swapped_out += s->size;
    swap_stats.swap_out_count++;
    
    klog_log("[SWAP] Segment swapped out");
    
    return 0;
}

int swap_read(segment_t* s)
{
    if (!s || !swap_ready)
        return -1;
    
    if (!s->swap_id)
        return -1;  /* Not in swap */
    
    if (s->present)
        return 0;  /* Already resident */
    
    /* Read from swap into segment */
    uint64_t swap_offset = swap_base + (s->swap_id * SWAP_BLOCK_SIZE);
    if (swap_offset + s->size > swap_base + swap_size)
    {
        klog_error("[SWAP] Swap read out of bounds");
        return -1;
    }
    if (pmem_read(swap_offset, (void*)s->base, s->size) != 0)
    {
        klog_error("[SWAP] Failed to read PMEM swap block");
        return -1;
    }
    
    /* Update statistics */
    swap_stats.bytes_swapped_in += s->size;
    swap_stats.swap_in_count++;
    
    /* Clear swap ID (data now in memory) */
    uint64_t old_swap_id = s->swap_id;
    s->swap_id = 0;
    
    /* Free swap entry */
    swap_free(old_swap_id);
    
    klog_log("[SWAP] Segment read from swap");
    
    return 0;
}

void swap_schedule(segment_t* s)
{
    /* For now, just do immediate swap */
    /* In a full implementation, this would queue for async swap */
    if (swap_can_swap(s))
    {
        swap_out(s);
    }
}

void swap_process_pending(void)
{
    /* Process any pending swap operations */
    /* Placeholder for async swap processing */
}

void swap_get_stats(swap_stats_t* stats)
{
    if (stats)
    {
        lock_swap();
        *stats = swap_stats;
        unlock_swap();
    }
}

int swap_can_swap(segment_t* s)
{
    if (!s || !swap_ready)
        return 0;
    
    if (!s->present)
        return 0;  /* Already not resident */
    
    if (s->locked > 0)
        return 0;  /* Locked */
    
    if (s->flags & SEG_FLAG_NOSWAP)
        return 0;  /* Not swappable */
    
    if (s->type == SEG_FIXED || s->type == SEG_PHYSICAL)
        return 0;  /* Fixed segments can't swap */
    
    return 1;
}
