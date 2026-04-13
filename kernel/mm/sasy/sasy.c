// ============================
// GraceOS SASY - Segment Allocator System
// Main Implementation
// ============================

#include "sasy.h"
#include "../vmm/vmm.h"
#include "../pmm/pmm.h"
#include "../../log/klog.h"
#include "../../../lib/libc/string.h"

/* ============================
   Segment Table
   ============================ */

static segment_t segments[MAX_SEGMENTS];
static uint32_t segment_count = 0;
static uint32_t next_segment_id = 1;
static uint64_t next_segment_base = SASY_BASE_ADDR;

/* Simple spinlock */
static volatile int sasy_spinlock = 0;

/* Memory pressure threshold */
static uint64_t pressure_threshold = 0x100000;  /* 1MB default */

/* ============================
   Lock Helpers
   ============================ */

static inline void lock_sasy(void)
{
    while (__sync_lock_test_and_set(&sasy_spinlock, 1))
    {
        __asm__ volatile ("pause");
    }
}

static inline void unlock_sasy(void)
{
    __sync_lock_release(&sasy_spinlock);
}

/* ============================
   Internal Helpers
   ============================ */

static inline uint64_t align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1) & ~(align - 1);
}

static segment_t* alloc_segment(void)
{
    for (uint32_t i = 0; i < MAX_SEGMENTS; i++)
    {
        if (segments[i].id == 0)
        {
            segments[i].id = next_segment_id++;
            segment_count++;
            return &segments[i];
        }
    }
    
    return (segment_t*)0;
}

static void free_segment(segment_t* s)
{
    if (!s)
        return;
    
    /* Clear segment */
    s->id = 0;
    s->type = SEG_FIXED;
    s->base = 0;
    s->size = 0;
    s->phys = 0;
    s->refcnt = 0;
    s->flags = 0;
    s->locked = 0;
    s->present = 0;
    s->dirty = 0;
    s->shared = 0;
    s->swap_id = 0;
    s->code_hash = 0;
    
    if (segment_count > 0)
        segment_count--;
}

static int sasy_map_now(segment_t* s)
{
    if (!s)
        return -1;
    
    /* Allocate physical pages */
    uint64_t pages = s->size / PAGE_SIZE;
    uint64_t first_page = 0;
    
    for (uint64_t i = 0; i < pages; i++)
    {
        uint64_t page = pmm_alloc_page();
        
        if (!page)
        {
            klog_error("[SEG] Out of physical memory");
            
            /* Free already allocated */
            for (uint64_t j = 0; j < i; j++)
            {
                pmm_free_page(first_page + j * PAGE_SIZE);
            }
            return -1;
        }
        
        if (i == 0)
            first_page = page;
    }
    
    s->phys = first_page;
    
    /* Map into virtual address space */
    uint64_t virt = s->base;
    uint64_t phys = s->phys;
    uint64_t flags = VMM_PRESENT;
    
    if (s->flags & SEG_FLAG_WRITE)
        flags |= VMM_WRITE;
    if (s->flags & SEG_FLAG_USER)
        flags |= VMM_USER;
    if (!(s->flags & SEG_FLAG_EXEC))
        flags |= VMM_NOEXEC;
    
    for (uint64_t i = 0; i < pages; i++)
    {
        vmm_map(virt + i * PAGE_SIZE, phys + i * PAGE_SIZE, flags);
    }
    
    /* Zero-fill if requested */
    if (s->flags & SEG_FLAG_ZEROED)
    {
        memset((void*)s->base, 0, s->size);
    }
    
    s->present = 1;
    
    return 0;
}

/* ============================
   SASY Public API
   ============================ */

void sasy_init(void)
{
    /* Clear segment table */
    for (uint32_t i = 0; i < MAX_SEGMENTS; i++)
    {
        segments[i].id = 0;
        segments[i].type = SEG_FIXED;
        segments[i].base = 0;
        segments[i].size = 0;
        segments[i].phys = 0;
        segments[i].refcnt = 0;
        segments[i].flags = 0;
        segments[i].locked = 0;
        segments[i].present = 0;
        segments[i].dirty = 0;
        segments[i].shared = 0;
        segments[i].swap_id = 0;
        segments[i].code_hash = 0;
    }
    
    segment_count = 0;
    next_segment_id = 1;
    next_segment_base = SASY_BASE_ADDR;
    
    /* Initialize subsystems */
    handle_init();
    swap_init();
    loader_init();
    
    klog_init_msg("SASY segment allocator initialized");
}

seg_handle_t sasy_create(uint64_t size, seg_type_t type, uint32_t flags)
{
    return sasy_create_ex(size, type, flags, 0);  /* Kernel owns it */
}

seg_handle_t sasy_create_ex(
    uint64_t size,
    seg_type_t type,
    uint32_t flags,
    uint32_t owner_pid)
{
    if (size == 0)
        return INVALID_HANDLE;
    
    lock_sasy();
    
    /* Allocate segment descriptor */
    segment_t* s = alloc_segment();
    
    if (!s)
    {
        unlock_sasy();
        klog_error("[SEG] No free segment slots");
        return INVALID_HANDLE;
    }
    
    /* Initialize segment */
    s->size = align_up(size, PAGE_SIZE);
    s->type = type;
    s->flags = flags;
    s->refcnt = 1;
    s->locked = 0;
    s->present = 0;
    s->dirty = 0;
    s->shared = (type == SEG_CODE) ? 1 : 0;
    s->swap_id = 0;
    s->code_hash = 0;
    
    /* Assign virtual address */
    s->base = next_segment_base;
    next_segment_base += s->size;
    
    /* Ensure we don't exceed segment space */
    if (next_segment_base >= SASY_BASE_ADDR + SASY_MAX_SIZE)
    {
        free_segment(s);
        unlock_sasy();
        klog_error("[SEG] Segment space exhausted");
        return INVALID_HANDLE;
    }
    
    /* Map immediately for fixed/physical segments */
    if (type == SEG_FIXED || type == SEG_PHYSICAL)
    {
        if (sasy_map_now(s) != 0)
        {
            free_segment(s);
            unlock_sasy();
            return INVALID_HANDLE;
        }
    }
    
    /* Allocate handle */
    seg_handle_t h = handle_alloc(s, owner_pid);
    
    if (h == INVALID_HANDLE)
    {
        free_segment(s);
        unlock_sasy();
        klog_error("[SEG] Failed to allocate handle");
        return INVALID_HANDLE;
    }
    
    unlock_sasy();
    
    klog_log("[SEG] Created segment");
    
    return h;
}

void* sasy_lock(seg_handle_t h)
{
    if (!handle_valid(h))
        return (void*)0;
    
    lock_sasy();
    
    segment_t* s = handle_get_segment(h);
    
    if (!s)
    {
        unlock_sasy();
        return (void*)0;
    }
    
    /* Page in if not present */
    if (!s->present)
    {
        if (sasy_pagein(s) != 0)
        {
            unlock_sasy();
            klog_error("[SEG] Failed to page in segment");
            return (void*)0;
        }
    }
    
    /* Lock the segment */
    s->locked++;
    s->refcnt++;
    
    void* addr = (void*)s->base;
    
    unlock_sasy();
    
    return addr;
}

void sasy_unlock(seg_handle_t h)
{
    if (!handle_valid(h))
        return;
    
    lock_sasy();
    
    segment_t* s = handle_get_segment(h);
    
    if (!s)
    {
        unlock_sasy();
        return;
    }
    
    /* Decrement lock count */
    if (s->locked > 0)
        s->locked--;
    
    /* If dirty and unlocked, schedule for swap */
    if (!s->locked && s->dirty)
    {
        swap_schedule(s);
    }
    
    unlock_sasy();
}

void sasy_free(seg_handle_t h)
{
    if (!handle_valid(h))
        return;
    
    lock_sasy();
    
    segment_t* s = handle_get_segment(h);
    
    if (!s)
    {
        unlock_sasy();
        return;
    }
    
    /* Decrement reference count */
    if (s->refcnt > 0)
        s->refcnt--;
    
    /* Free handle */
    handle_free(h);
    
    /* If no more references, free segment */
    if (s->refcnt == 0)
    {
        /* Unmap from virtual memory */
        if (s->present)
        {
            uint64_t addr = s->base;
            uint64_t end = s->base + s->size;
            
            while (addr < end)
            {
                vmm_unmap(addr);
                addr += PAGE_SIZE;
            }
        }
        
        /* Free physical pages */
        if (s->phys)
        {
            uint64_t pages = s->size / PAGE_SIZE;
            for (uint64_t i = 0; i < pages; i++)
            {
                pmm_free_page(s->phys + i * PAGE_SIZE);
            }
        }
        
        /* Free swap space */
        if (s->swap_id)
        {
            swap_free(s->swap_id);
        }
        
        /* Unregister code sharing */
        if (s->type == SEG_CODE && s->code_hash)
        {
            loader_unregister_code(s);
        }
        
        /* Free segment descriptor */
        free_segment(s);
        
        klog_log("[SEG] Segment freed");
    }
    
    unlock_sasy();
}

int sasy_resize(seg_handle_t h, uint64_t new_size)
{
    if (!handle_valid(h))
        return -1;
    
    lock_sasy();
    
    segment_t* s = handle_get_segment(h);
    
    if (!s)
    {
        unlock_sasy();
        return -1;
    }
    
    /* Can't resize locked segments */
    if (s->locked > 0)
    {
        unlock_sasy();
        klog_warn("[SEG] Cannot resize locked segment");
        return -1;
    }
    
    /* Can't resize fixed segments */
    if (s->type == SEG_FIXED || s->type == SEG_PHYSICAL)
    {
        unlock_sasy();
        klog_warn("[SEG] Cannot resize fixed segment");
        return -1;
    }
    
    uint64_t new_aligned = align_up(new_size, PAGE_SIZE);
    uint64_t old_size = s->size;
    
    if (new_aligned == old_size)
    {
        unlock_sasy();
        return 0;  /* No change needed */
    }
    
    if (new_aligned > old_size)
    {
        /* Growing - need to remap */
        /* For now, we don't support growing resident segments */
        if (s->present)
        {
            unlock_sasy();
            klog_warn("[SEG] Cannot grow resident segment");
            return -1;
        }
        
        s->size = new_aligned;
    }
    else
    {
        /* Shrinking */
        if (s->present)
        {
            /* Unmap excess pages */
            uint64_t old_pages = old_size / PAGE_SIZE;
            uint64_t new_pages = new_aligned / PAGE_SIZE;
            
            for (uint64_t i = new_pages; i < old_pages; i++)
            {
                vmm_unmap(s->base + i * PAGE_SIZE);
                pmm_free_page(s->phys + i * PAGE_SIZE);
            }
        }
        
        s->size = new_aligned;
    }
    
    unlock_sasy();
    
    klog_log("[SEG] Segment resized");
    
    return 0;
}

void sasy_mark_dirty(seg_handle_t h)
{
    if (!handle_valid(h))
        return;
    
    segment_t* s = handle_get_segment(h);
    
    if (s)
    {
        s->dirty = 1;
    }
}

int sasy_get_info(seg_handle_t h, segment_t* info)
{
    if (!handle_valid(h) || !info)
        return -1;
    
    segment_t* s = handle_get_segment(h);
    
    if (!s)
        return -1;
    
    lock_sasy();
    *info = *s;
    unlock_sasy();
    
    return 0;
}

uint64_t sasy_get_size(seg_handle_t h)
{
    if (!handle_valid(h))
        return 0;
    
    segment_t* s = handle_get_segment(h);
    
    return s ? s->size : 0;
}

seg_type_t sasy_get_type(seg_handle_t h)
{
    if (!handle_valid(h))
        return SEG_FIXED;
    
    segment_t* s = handle_get_segment(h);
    
    return s ? s->type : SEG_FIXED;
}

/* ============================
   Memory Pressure Handling
   ============================ */

void sasy_pressure(void)
{
    klog_warn("[SEG] Memory pressure triggered");
    
    lock_sasy();
    
    /* First pass: free discardable segments */
    for (uint32_t i = 0; i < MAX_SEGMENTS; i++)
    {
        segment_t* s = &segments[i];
        
        if (s->id == 0)
            continue;
        
        /* Check if discardable */
        if ((s->flags & SEG_FLAG_DISCARD) && 
            !s->locked && !s->dirty && s->refcnt == 0)
        {
            sasy_pageout(s);
        }
    }
    
    /* Second pass: swap out unlocked segments */
    for (uint32_t i = 0; i < MAX_SEGMENTS; i++)
    {
        segment_t* s = &segments[i];
        
        if (s->id == 0)
            continue;
        
        if (swap_can_swap(s))
        {
            swap_out(s);
        }
    }
    
    unlock_sasy();
}

void sasy_set_pressure_threshold(uint64_t bytes)
{
    pressure_threshold = bytes;
}

/* ============================
   Code Sharing API
   ============================ */

seg_handle_t sasy_load_code(const char* path, uint32_t owner_pid)
{
    if (!path)
        return INVALID_HANDLE;
    
    /* Compute hash */
    uint64_t hash = loader_hash_file(path);
    
    /* Check if already loaded */
    segment_t* existing = loader_find_code(hash);
    
    if (existing)
    {
        /* Share existing code segment */
        lock_sasy();
        
        existing->refcnt++;
        seg_handle_t h = handle_alloc(existing, owner_pid);
        
        unlock_sasy();
        
        klog_log("[SEG] Sharing existing code segment");
        
        return h;
    }
    
    /* Create new code segment */
    seg_handle_t h = sasy_create_ex(
        PAGE_SIZE,  /* Initial size - will be updated by loader */
        SEG_CODE,
        SEG_FLAGS_RX | SEG_FLAG_SHARED,
        owner_pid
    );
    
    if (h == INVALID_HANDLE)
        return INVALID_HANDLE;
    
    segment_t* s = handle_get_segment(h);
    
    if (s)
    {
        s->code_hash = hash;
        
        /* Load code */
        if (loader_load_code(s, path) != 0)
        {
            sasy_free(h);
            return INVALID_HANDLE;
        }
        
        /* Register for sharing */
        loader_register_code(s);
    }
    
    return h;
}

/* ============================
   Process Integration
   ============================ */

seg_handle_t sasy_create_heap(uint32_t pid, uint64_t initial_size)
{
    return sasy_create_ex(
        initial_size,
        SEG_DATA_AUTO,
        SEG_FLAGS_USER_RW | SEG_FLAG_ZEROED,
        pid
    );
}

seg_handle_t sasy_create_stack(uint32_t pid, uint64_t size)
{
    return sasy_create_ex(
        size,
        SEG_DATA_INST,
        SEG_FLAGS_USER_RW | SEG_FLAG_ZEROED | SEG_FLAG_NOSWAP,
        pid
    );
}

seg_handle_t sasy_clone(seg_handle_t h, uint32_t new_pid)
{
    if (!handle_valid(h))
        return INVALID_HANDLE;
    
    segment_t* s = handle_get_segment(h);
    
    if (!s)
        return INVALID_HANDLE;
    
    /* For code segments, just share */
    if (s->type == SEG_CODE)
    {
        return handle_dup(h, new_pid);
    }
    
    /* For data segments, create copy */
    seg_handle_t new_h = sasy_create_ex(
        s->size,
        s->type,
        s->flags,
        new_pid
    );
    
    if (new_h == INVALID_HANDLE)
        return INVALID_HANDLE;
    
    segment_t* new_s = handle_get_segment(new_h);
    
    if (!new_s)
    {
        sasy_free(new_h);
        return INVALID_HANDLE;
    }
    
    /* Lock both segments and copy data */
    void* src = sasy_lock(h);
    void* dst = sasy_lock(new_h);
    
    if (src && dst)
    {
        memcpy(dst, src, s->size);
    }
    
    sasy_unlock(new_h);
    sasy_unlock(h);
    
    klog_log("[SEG] Segment cloned for process");
    
    return new_h;
}

void sasy_release_process(uint32_t pid)
{
    (void)pid;  /* TODO: Implement when handle table supports PID filtering */
    
    klog_log("[SEG] Releasing process segments");
    
    /* This would need handle table access to find all handles for a process */
    /* For now, this is a placeholder */
}

/* ============================
   Statistics and Debug
   ============================ */

void sasy_get_stats(seg_stats_t* stats)
{
    if (!stats)
        return;
    
    lock_sasy();
    
    stats->total_segments = 0;
    stats->resident_segments = 0;
    stats->swapped_segments = 0;
    stats->locked_segments = 0;
    stats->total_virtual_bytes = 0;
    stats->total_physical_bytes = 0;
    stats->total_swapped_bytes = 0;
    
    for (uint32_t i = 0; i < MAX_SEGMENTS; i++)
    {
        segment_t* s = &segments[i];
        
        if (s->id == 0)
            continue;
        
        stats->total_segments++;
        stats->total_virtual_bytes += s->size;
        
        if (s->present)
        {
            stats->resident_segments++;
            stats->total_physical_bytes += s->size;
        }
        
        if (s->swap_id)
        {
            stats->swapped_segments++;
            stats->total_swapped_bytes += s->size;
        }
        
        if (s->locked > 0)
        {
            stats->locked_segments++;
        }
    }
    
    unlock_sasy();
}

void sasy_dump(void)
{
    klog_log("[SEG] Segment table dump:");
    
    lock_sasy();
    
    for (uint32_t i = 0; i < MAX_SEGMENTS; i++)
    {
        segment_t* s = &segments[i];
        
        if (s->id == 0)
            continue;
        
        /* Would print segment info here */
    }
    
    unlock_sasy();
}

int sasy_check(void)
{
    /* Verify segment table integrity */
    lock_sasy();
    
    int errors = 0;
    
    for (uint32_t i = 0; i < MAX_SEGMENTS; i++)
    {
        segment_t* s = &segments[i];
        
        if (s->id == 0)
            continue;
        
        /* Check for invalid states */
        if (s->present && !s->phys && s->type != SEG_PHYSICAL)
        {
            klog_error("[SEG] Segment present but no phys");
            errors++;
        }
        
        if (s->locked > 0 && !s->present)
        {
            klog_error("[SEG] Segment locked but not present");
            errors++;
        }
    }
    
    unlock_sasy();
    
    return errors;
}

/* ============================
   Segment Lookup
   ============================ */

segment_t* sasy_find_segment(uint64_t addr)
{
    if (!sasy_is_segment_addr(addr))
        return (segment_t*)0;
    
    lock_sasy();
    
    for (uint32_t i = 0; i < MAX_SEGMENTS; i++)
    {
        segment_t* s = &segments[i];
        
        if (s->id == 0)
            continue;
        
        if (addr >= s->base && addr < s->base + s->size)
        {
            unlock_sasy();
            return s;
        }
    }
    
    unlock_sasy();
    return (segment_t*)0;
}

int sasy_is_segment_addr(uint64_t addr)
{
    return (addr >= SASY_BASE_ADDR && 
            addr < SASY_BASE_ADDR + SASY_MAX_SIZE);
}
