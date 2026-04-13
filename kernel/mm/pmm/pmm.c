// ============================
// GraceOS Physical Memory Manager
// Bitmap-based PMM with fallback
// ============================

#include "pmm.h"
#include "bitmap.h"
#include "../../log/klog.h"
#include "../fallback/simple_fallback.h"
#include "../../core/multiboot.h"

/* Kernel end symbol from linker */
extern char end;

/* PMM state */
static uint64_t max_frames = 0;
static uint64_t used_frames = 0;
static int ready = 0;
static uint64_t kernel_reserved_end = 0;

/* Align up macro (SSE-safe 16-byte alignment) */
#define ALIGN_UP(x, a) (((x) + (a) - 1) & ~((a) - 1))

/* Memory map iteration helper */
static struct multiboot_mmap_entry* get_first_mmap_entry(uint64_t mb_addr)
{
    struct multiboot_tag* tag = (struct multiboot_tag*)(mb_addr + 8);

    while (tag->type != MULTIBOOT_TAG_TYPE_END)
    {
        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP)
        {
            struct multiboot_tag_mmap* mmap_tag = (struct multiboot_tag_mmap*)tag;
            return (struct multiboot_mmap_entry*)((uint64_t)mmap_tag + 16);
        }

        tag = (struct multiboot_tag*)((uint64_t)tag + ((tag->size + 7) & ~7));
    }

    return 0;
}

static struct multiboot_tag_mmap* get_mmap_tag(uint64_t mb_addr)
{
    struct multiboot_tag* tag = (struct multiboot_tag*)(mb_addr + 8);

    while (tag->type != MULTIBOOT_TAG_TYPE_END)
    {
        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP)
            return (struct multiboot_tag_mmap*)tag;

        tag = (struct multiboot_tag*)((uint64_t)tag + ((tag->size + 7) & ~7));
    }

    return 0;
}

static struct multiboot_mmap_entry* get_mmap_end(uint64_t mb_addr)
{
    struct multiboot_tag* tag = (struct multiboot_tag*)(mb_addr + 8);

    while (tag->type != MULTIBOOT_TAG_TYPE_END)
    {
        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP)
        {
            struct multiboot_tag_mmap* mmap_tag = (struct multiboot_tag_mmap*)tag;
            return (struct multiboot_mmap_entry*)((uint64_t)mmap_tag + mmap_tag->size);
        }

        tag = (struct multiboot_tag*)((uint64_t)tag + ((tag->size + 7) & ~7));
    }

    return 0;
}

static uint32_t get_mmap_entry_size(uint64_t mb_addr)
{
    struct multiboot_tag* tag = (struct multiboot_tag*)(mb_addr + 8);

    while (tag->type != MULTIBOOT_TAG_TYPE_END)
    {
        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP)
        {
            struct multiboot_tag_mmap* mmap_tag = (struct multiboot_tag_mmap*)tag;
            return mmap_tag->entry_size;
        }

        tag = (struct multiboot_tag*)((uint64_t)tag + ((tag->size + 7) & ~7));
    }

    return sizeof(struct multiboot_mmap_entry);
}

/* Initialize PMM with multiboot info */
void pmm_init(void* mb_info)
{
    uint64_t mb_addr = (uint64_t)mb_info;

    klog_init_msg("Starting PMM initialization");

    /* Validate multiboot address */
    if (!mb_addr || mb_addr < 0x1000)
    {
        klog_fail("Invalid multiboot info address");
        return;
    }

    /* Conservative memory sizing: prefer basic meminfo for boot stability. */
    uint64_t max_addr = multiboot_get_total_memory();

    if (!max_addr)
    {
        klog_warn("No usable memory regions, using default 128MB");
        max_addr = 0x8000000ULL;  /* 128MB default */
    }

    /* Clamp to 64GB hard limit */
    if (max_addr > 0x1000000000ULL)
        max_addr = 0x1000000000ULL;

    max_frames = max_addr / PAGE_SIZE;
    used_frames = max_frames;  /* All frames start as used */


    /* Place bitmap after kernel end (16-byte aligned for SSE safety) */
    uint8_t* bitmap_area = (uint8_t*)ALIGN_UP((uint64_t)&end, 16);

    bitmap_init(bitmap_area, max_frames);

    uint64_t bitmap_size = (max_frames + 7) / 8;

    /* Reserve kernel and bitmap area */
    uint64_t kernel_end = (uint64_t)bitmap_area + bitmap_size;
    uint64_t kernel_end_frame = (kernel_end + PAGE_SIZE - 1) / PAGE_SIZE;
    kernel_reserved_end = kernel_end_frame * PAGE_SIZE;

    /* Conservative free policy: only free memory above kernel+bitmap. */
    for (uint64_t i = kernel_end_frame; i < max_frames; i++)
    {
        if (bitmap_test(i))
        {
            bitmap_clear(i);
            used_frames--;
        }
    }


    for (uint64_t i = 0; i < kernel_end_frame; i++)
    {
        if (!bitmap_test(i))
        {
            bitmap_set(i);
            used_frames++;
        }
    }

    klog_set("Kernel memory reserved");

    ready = 1;

    klog_init_msg("PMM online");
}

/* Allocate a physical page (returns physical address) */
uint64_t pmm_alloc_page(void)
{
    if (!ready)
    {
        klog_warn("PMM offline, using fallback");
        return simple_fallback_alloc();
    }

    for (uint64_t i = 0; i < max_frames; i++)
    {
        if (!bitmap_test(i))
        {
            bitmap_set(i);
            used_frames++;

            return i * PAGE_SIZE;
        }
    }

    klog_error("Out of physical memory");
    return simple_fallback_alloc();
}

/* Free a physical page */
void pmm_free_page(uint64_t phys_addr)
{
    if (!ready)
    {
        klog_warn("Free called while PMM offline");
        return;
    }

    uint64_t frame = phys_addr / PAGE_SIZE;

    if (frame >= max_frames)
    {
        klog_warn("Attempted to free invalid frame");
        return;
    }

    if (!bitmap_test(frame))
    {
        klog_warn("Double free detected");
        return;
    }

    bitmap_clear(frame);
    used_frames--;

}

uint64_t pmm_kernel_reserved_end(void)
{
    return kernel_reserved_end;
}

/* Reserve a physical range (mark frames used) */
void pmm_reserve_range(uint64_t phys_start, uint64_t size)
{
    if (!ready || size == 0)
        return;

    uint64_t start = phys_start / PAGE_SIZE;
    uint64_t end = (phys_start + size + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint64_t frame = start; frame < end; frame++)
    {
        if (!bitmap_test(frame))
        {
            bitmap_set(frame);
            used_frames++;
        }
    }
}

/* Get total physical memory in bytes */
uint64_t pmm_total(void)
{
    return max_frames * PAGE_SIZE;
}

/* Get free physical memory in bytes */
uint64_t pmm_free(void)
{
    return (max_frames - used_frames) * PAGE_SIZE;
}

/* Check if PMM is ready */
int pmm_ready(void)
{
    return ready;
}
