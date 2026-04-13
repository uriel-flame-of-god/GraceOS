// ============================
// GraceOS Multiboot2 Parser
// ============================
#include "multiboot.h"
#include "../log/klog.h"

static uint64_t total_memory_kb = 0;
static uint64_t multiboot_addr = 0;

void multiboot_init(uint64_t multiboot_info_addr) {
    multiboot_addr = multiboot_info_addr;
}

uint64_t multiboot_get_total_memory(void) {
    if (!multiboot_addr) return 512 * 1024;

    struct multiboot_tag* tag = (struct multiboot_tag*)(multiboot_addr + 8);

    while (tag->type != MULTIBOOT_TAG_TYPE_END) {
        if (tag->type == MULTIBOOT_TAG_TYPE_BASIC_MEMINFO) {
            struct multiboot_tag_basic_meminfo* meminfo =
                (struct multiboot_tag_basic_meminfo*)tag;
            total_memory_kb = meminfo->mem_lower + meminfo->mem_upper;
            return total_memory_kb * 1024;
        }
        tag = (struct multiboot_tag*)((uint64_t)tag + ((tag->size + 7) & ~7));
    }

    return 512 * 1024 * 1024;
}

int multiboot_get_framebuffer(struct framebuffer_info* info) {
    if (!multiboot_addr || !info) {
        klog_warn("Multiboot: no address or info pointer");
        return 0;
    }

    struct multiboot_tag* tag = (struct multiboot_tag*)(multiboot_addr + 8);
    int tag_count = 0;

    while (tag->type != MULTIBOOT_TAG_TYPE_END && tag_count < 50) {
        if (tag->type == MULTIBOOT_TAG_TYPE_FRAMEBUFFER) {
            struct multiboot_tag_framebuffer* fb =
                (struct multiboot_tag_framebuffer*)tag;

            info->addr   = fb->framebuffer_addr;
            info->pitch  = fb->framebuffer_pitch;
            info->width  = fb->framebuffer_width;
            info->height = fb->framebuffer_height;
            info->bpp    = fb->framebuffer_bpp;
            info->type   = fb->framebuffer_type;

            return 1;
        }
        tag = (struct multiboot_tag*)((uint64_t)tag + ((tag->size + 7) & ~7));
        tag_count++;
    }

    klog_warn("Multiboot: no framebuffer tag found");
    return 0;
}

void multiboot_parse_memory_map(void) {
    (void)multiboot_addr;
}
