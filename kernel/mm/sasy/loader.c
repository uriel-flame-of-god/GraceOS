// ============================
// GraceOS SASY Loader
// Demand paging and code loading
// ============================

#include "loader.h"
#include "sasy.h"
#include "swap.h"
#include "../vmm/vmm.h"
#include "../pmm/pmm.h"
#include "../../log/klog.h"
#include "../../../drivers/storage/bfs.h"
#include "../../../lib/libc/string.h"

/* ============================
   Code Segment Registry
   ============================ */

#define MAX_CODE_SEGMENTS 256

typedef struct {
    uint64_t hash;
    segment_t* seg;
    uint32_t refs;
    uint8_t valid;
} code_registry_t;

static code_registry_t code_registry[MAX_CODE_SEGMENTS];

/* Simple spinlock */
static volatile int loader_lock = 0;

/* External BFS instance */
extern struct bfs_instance g_bfs;

/* ============================
   Lock Helpers
   ============================ */

static inline void lock_loader(void)
{
    while (__sync_lock_test_and_set(&loader_lock, 1))
    {
        __asm__ volatile ("pause");
    }
}

static inline void unlock_loader(void)
{
    __sync_lock_release(&loader_lock);
}

/* ============================
   Loader API Implementation
   ============================ */

void loader_init(void)
{
    /* Clear code registry */
    for (uint32_t i = 0; i < MAX_CODE_SEGMENTS; i++)
    {
        code_registry[i].hash = 0;
        code_registry[i].seg = (segment_t*)0;
        code_registry[i].refs = 0;
        code_registry[i].valid = 0;
    }
    
    klog_init_msg("SASY loader initialized");
}

int sasy_pagein(segment_t* s)
{
    if (!s)
        return -1;
    
    if (s->present)
        return 0;  /* Already resident */
    
    /* Allocate physical memory */
    uint64_t pages = s->size / PAGE_SIZE;
    uint64_t first_page = 0;
    
    for (uint64_t i = 0; i < pages; i++)
    {
        uint64_t page = pmm_alloc_page();
        
        if (!page)
        {
            klog_error("[LOAD] Out of physical memory");
            
            /* Free already allocated pages */
            for (uint64_t j = 0; j < i; j++)
            {
                pmm_free_page(first_page + j * PAGE_SIZE);
            }
            return -1;
        }
        
        if (i == 0)
        {
            first_page = page;
        }
        else if (page != (first_page + i * PAGE_SIZE))
        {
            klog_error("[LOAD] Non-contiguous physical allocation");

            /* Free already allocated contiguous prefix and current page. */
            for (uint64_t j = 0; j < i; j++)
            {
                pmm_free_page(first_page + j * PAGE_SIZE);
            }
            pmm_free_page(page);
            return -1;
        }
    }
    
    s->phys = first_page;
    
    /* Map pages into virtual address space */
    uint64_t virt = s->base;
    uint64_t phys = s->phys;
    uint64_t flags = VMM_PRESENT;
    
    if (s->flags & SEG_FLAG_WRITE)
        flags |= VMM_WRITE;
    if (s->flags & SEG_FLAG_USER)
        flags |= VMM_USER;
    if (!(s->flags & SEG_FLAG_EXEC))
        flags |= VMM_NOEXEC;
    if (s->flags & SEG_FLAG_NOCACHE)
        flags |= (VMM_PWT | VMM_PCD);
    
    for (uint64_t i = 0; i < pages; i++)
    {
        uint64_t vaddr = virt + i * PAGE_SIZE;
        uint64_t paddr = phys + i * PAGE_SIZE;

        vmm_map(vaddr, paddr, flags);

        if (vmm_translate(vaddr) != paddr)
        {
            klog_error("[LOAD] Virtual mapping failed");

            for (uint64_t j = 0; j <= i; j++)
            {
                vmm_unmap(virt + j * PAGE_SIZE);
            }

            for (uint64_t j = 0; j < pages; j++)
            {
                pmm_free_page(first_page + j * PAGE_SIZE);
            }

            s->phys = 0;
            return -1;
        }
    }
    
    /* Load data depending on segment type */
    if (s->type == SEG_CODE)
    {
        /* Code would be loaded from file here */
        klog_log("[LOAD] Code segment paged in");
    }
    else if (s->swap_id)
    {
        /* Read from swap */
        if (swap_read(s) != 0)
            return -1;
        klog_log("[LOAD] Segment read from swap");
    }
    else if (s->flags & SEG_FLAG_ZEROED)
    {
        /* Zero-fill the segment */
        memset((void*)s->base, 0, s->size);
    }
    
    s->present = 1;
    
    klog_log("[LOAD] Segment paged in");
    
    return 0;
}

int sasy_pageout(segment_t* s)
{
    if (!s)
        return -1;
    
    if (!s->present)
        return 0;  /* Already not resident */
    
    if (s->locked > 0)
    {
        klog_warn("[LOAD] Cannot page out locked segment");
        return -1;
    }
    
    /* Check if segment is discardable and clean */
    if ((s->flags & SEG_FLAG_DISCARD) && !s->dirty && s->refcnt == 0)
    {
        /* Discardable - just free without swapping */
        uint64_t addr = s->base;
        uint64_t end = s->base + s->size;
        
        while (addr < end)
        {
            vmm_unmap(addr);
            addr += PAGE_SIZE;
        }
        
        if (s->phys)
        {
            uint64_t pages = s->size / PAGE_SIZE;
            for (uint64_t i = 0; i < pages; i++)
            {
                pmm_free_page(s->phys + i * PAGE_SIZE);
            }
            s->phys = 0;
        }
        
        s->present = 0;
        klog_log("[LOAD] Discardable segment freed");
        
        return 0;
    }
    
    /* Otherwise, swap out */
    return swap_out(s);
}

int loader_load_code(segment_t* s, const char* path)
{
    if (!s || !path)
        return -1;
    
    /* Find file in BFS */
    struct bfs_file_entry entry;
    int result = bfs_find(&g_bfs, path, &entry);
    
    if (result != 0)
    {
        klog_error("[LOAD] Code file not found");
        return -1;
    }
    
    /* Read file content into segment */
    /* For now, this is a placeholder - actual file reading would happen here */
    
    /* Set code hash for sharing */
    s->code_hash = loader_hash_file(path);
    
    klog_log("[LOAD] Code loaded from file");
    
    return 0;
}

segment_t* loader_find_code(uint64_t hash)
{
    if (!hash)
        return (segment_t*)0;
    
    lock_loader();
    
    for (uint32_t i = 0; i < MAX_CODE_SEGMENTS; i++)
    {
        if (code_registry[i].valid && code_registry[i].hash == hash)
        {
            segment_t* seg = code_registry[i].seg;
            unlock_loader();
            return seg;
        }
    }
    
    unlock_loader();
    return (segment_t*)0;
}

uint64_t loader_hash_file(const char* path)
{
    if (!path)
        return 0;
    
    /* Simple DJB2 hash */
    uint64_t hash = 5381;
    int c;
    
    while ((c = *path++))
    {
        hash = ((hash << 5) + hash) + c;
    }
    
    return hash;
}

void loader_register_code(segment_t* s)
{
    if (!s || !s->code_hash)
        return;
    
    lock_loader();
    
    /* Check if already registered */
    for (uint32_t i = 0; i < MAX_CODE_SEGMENTS; i++)
    {
        if (code_registry[i].valid && code_registry[i].hash == s->code_hash)
        {
            code_registry[i].refs++;
            unlock_loader();
            return;
        }
    }
    
    /* Find free slot */
    for (uint32_t i = 0; i < MAX_CODE_SEGMENTS; i++)
    {
        if (!code_registry[i].valid)
        {
            code_registry[i].hash = s->code_hash;
            code_registry[i].seg = s;
            code_registry[i].refs = 1;
            code_registry[i].valid = 1;
            break;
        }
    }
    
    unlock_loader();
}

void loader_unregister_code(segment_t* s)
{
    if (!s || !s->code_hash)
        return;
    
    lock_loader();
    
    for (uint32_t i = 0; i < MAX_CODE_SEGMENTS; i++)
    {
        if (code_registry[i].valid && code_registry[i].hash == s->code_hash)
        {
            if (code_registry[i].refs > 0)
                code_registry[i].refs--;
            
            if (code_registry[i].refs == 0)
            {
                code_registry[i].hash = 0;
                code_registry[i].seg = (segment_t*)0;
                code_registry[i].valid = 0;
            }
            break;
        }
    }
    
    unlock_loader();
}

int loader_handle_fault(uint64_t fault_addr)
{
    /* Check if fault is in segment space */
    if (!sasy_is_segment_addr(fault_addr))
        return -1;  /* Not our fault */
    
    /* Find the segment */
    segment_t* seg = sasy_find_segment(fault_addr);
    
    if (!seg)
    {
        klog_error("[LOAD] Page fault: segment not found");
        return -1;
    }
    
    /* Page in the segment */
    int result = sasy_pagein(seg);
    
    if (result == 0)
    {
        klog_log("[LOAD] Demand page-in successful");
    }
    
    return result;
}
