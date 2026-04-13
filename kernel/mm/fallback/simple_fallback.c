// ============================
// GraceOS Simple Fallback Allocator
// Emergency allocation when PMM is offline
// ============================

#include "simple_fallback.h"
#include "../../log/klog.h"
#include "../kheap.h"

#define PAGE_SIZE 4096

/* Allocate a page using the simple fallback allocator */
uint64_t simple_fallback_alloc(void)
{
    klog_warn("Fallback allocator used");

    void* p = kmalloc(PAGE_SIZE);

    if (!p)
    {
        klog_fail("Fallback allocation failed");
        return 0;
    }

    return (uint64_t)p;
}
