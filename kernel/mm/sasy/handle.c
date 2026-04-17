// ============================
// GraceOS SASY Handle Manager
// Implementation
// ============================

#include "handle.h"
#include "../../log/klog.h"
#include "../../../lib/libc/string.h"

/* ============================
   Handle Table
   ============================ */

static handle_t handles[MAX_HANDLES];
static uint32_t handle_count = 0;
static uint32_t next_handle = 0;

/* Simple spinlock for handle operations */
static volatile int handle_lock = 0;

/* ============================
   Lock Helpers
   ============================ */

static inline void lock_handles(void)
{
    while (__sync_lock_test_and_set(&handle_lock, 1))
    {
        #ifdef ARCH_ARM64
        __asm__ volatile ("yield");
        #else
        __asm__ volatile ("pause");
        #endif
    }
}

static inline void unlock_handles(void)
{
    __sync_lock_release(&handle_lock);
}

/* ============================
   Handle API Implementation
   ============================ */

void handle_init(void)
{
    /* Clear all handles */
    for (uint32_t i = 0; i < MAX_HANDLES; i++)
    {
        handles[i].seg = (segment_t*)0;
        handles[i].refs = 0;
        handles[i].owner_pid = 0;
        handles[i].valid = 0;
    }
    
    handle_count = 0;
    next_handle = 0;
    
    klog_init_msg("SASY handle table initialized");
}

seg_handle_t handle_alloc(segment_t* seg, uint32_t owner_pid)
{
    if (!seg)
        return INVALID_HANDLE;
    
    lock_handles();
    
    /* Find a free handle slot */
    uint32_t start = next_handle;
    uint32_t h = start;
    
    do {
        if (!handles[h].valid)
        {
            handles[h].seg = seg;
            handles[h].refs = 1;
            handles[h].owner_pid = owner_pid;
            handles[h].valid = 1;
            
            handle_count++;
            next_handle = (h + 1) % MAX_HANDLES;
            
            unlock_handles();
            return (seg_handle_t)h;
        }
        
        h = (h + 1) % MAX_HANDLES;
    } while (h != start);
    
    unlock_handles();
    
    klog_error("[HANDLE] No free handles available");
    return INVALID_HANDLE;
}

void handle_free(seg_handle_t h)
{
    if (h >= MAX_HANDLES)
        return;
    
    lock_handles();
    
    if (handles[h].valid)
    {
        handles[h].seg = (segment_t*)0;
        handles[h].refs = 0;
        handles[h].owner_pid = 0;
        handles[h].valid = 0;
        
        if (handle_count > 0)
            handle_count--;
    }
    
    unlock_handles();
}

segment_t* handle_get_segment(seg_handle_t h)
{
    if (h >= MAX_HANDLES || !handles[h].valid)
        return (segment_t*)0;
    
    return handles[h].seg;
}

void handle_addref(seg_handle_t h)
{
    if (h >= MAX_HANDLES)
        return;
    
    lock_handles();
    
    if (handles[h].valid)
    {
        handles[h].refs++;
    }
    
    unlock_handles();
}

void handle_release(seg_handle_t h)
{
    if (h >= MAX_HANDLES)
        return;
    
    lock_handles();
    
    if (handles[h].valid && handles[h].refs > 0)
    {
        handles[h].refs--;
    }
    
    unlock_handles();
}

int handle_valid(seg_handle_t h)
{
    if (h >= MAX_HANDLES)
        return 0;
    
    return handles[h].valid;
}

uint32_t handle_count_for_segment(segment_t* seg)
{
    if (!seg)
        return 0;
    
    uint32_t count = 0;
    
    lock_handles();
    
    for (uint32_t i = 0; i < MAX_HANDLES; i++)
    {
        if (handles[i].valid && handles[i].seg == seg)
        {
            count++;
        }
    }
    
    unlock_handles();
    
    return count;
}

seg_handle_t handle_dup(seg_handle_t h, uint32_t new_owner_pid)
{
    if (h >= MAX_HANDLES || !handles[h].valid)
        return INVALID_HANDLE;
    
    segment_t* seg = handles[h].seg;
    
    /* Allocate new handle pointing to same segment */
    seg_handle_t new_h = handle_alloc(seg, new_owner_pid);
    
    if (new_h != INVALID_HANDLE)
    {
        /* Increment segment reference count */
        lock_handles();
        if (seg)
            seg->refcnt++;
        unlock_handles();
    }
    
    return new_h;
}

uint32_t handle_get_total(void)
{
    return handle_count;
}

uint32_t handle_get_free(void)
{
    return MAX_HANDLES - handle_count;
}
