// ============================
// GraceOS VBE Mode Switching
// ============================

#include "vbe.h"
#include "../../../lib/libc/string.h"
#include "../../../lib/libc/int.h"
#include "../../log/klog.h"

#define VBE_RM_DATA_ADDR       0x6000
#define VBE_RM_INFO_ADDR       0x6200
#define VBE_RM_MODEINFO_ADDR   0x6400
#define VBE_RM_MODELIST_ADDR   0x6600
#define VBE_RM_GDT_PTR_ADDR    0x67F0
#define VBE_RM_GDT_ADDR        0x6800
#define VBE_RM_CODE_ADDR       0x7000

struct vbe_rm_data {
    uint16_t op;
    uint16_t mode;
    uint16_t status;
    uint16_t reserved;
} __attribute__((packed));

struct vbe_info_block {
    char signature[4];
    uint16_t version;
    uint32_t oem_string_ptr;
    uint32_t capabilities;
    uint32_t video_mode_ptr;
    uint16_t total_memory;
    uint8_t reserved[236];
} __attribute__((packed));

struct vbe_mode_info_block {
    uint16_t attributes;
    uint8_t win_a;
    uint8_t win_b;
    uint16_t granularity;
    uint16_t win_size;
    uint16_t segment_a;
    uint16_t segment_b;
    uint32_t win_func_ptr;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t w_char;
    uint8_t y_char;
    uint8_t planes;
    uint8_t bpp;
    uint8_t banks;
    uint8_t memory_model;
    uint8_t bank_size;
    uint8_t image_pages;
    uint8_t reserved1;
    uint8_t red_mask;
    uint8_t red_pos;
    uint8_t green_mask;
    uint8_t green_pos;
    uint8_t blue_mask;
    uint8_t blue_pos;
    uint8_t rsv_mask;
    uint8_t rsv_pos;
    uint8_t direct_color;
    uint32_t phys_base;
} __attribute__((packed));

struct gdt_ptr32 {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

extern char __rmcode_start;
extern char __rmcode_end;
extern void vbe_bios_call(void);

static void vbe_build_gdt(void)
{
    volatile uint64_t* gdt = (volatile uint64_t*)(uintptr_t)VBE_RM_GDT_ADDR;
    volatile struct gdt_ptr32* gdt_ptr = (volatile struct gdt_ptr32*)(uintptr_t)VBE_RM_GDT_PTR_ADDR;

    gdt[0] = 0x0000000000000000ULL;
    gdt[1] = 0x00CF9A000000FFFFULL; // 32-bit code
    gdt[2] = 0x00CF92000000FFFFULL; // 32-bit data
    gdt[3] = 0x00209A0000000000ULL; // 64-bit code
    gdt[4] = 0x0000920000000000ULL; // 64-bit data

    gdt_ptr->limit = (uint16_t)((5 * 8) - 1);
    gdt_ptr->base = (uint32_t)VBE_RM_GDT_ADDR;
}

static int vbe_call(uint16_t op, uint16_t mode)
{
    volatile struct vbe_rm_data* data = (volatile struct vbe_rm_data*)(uintptr_t)VBE_RM_DATA_ADDR;

    klog_log("VBE: vbe_call op=");
    klog_hex(op);
    klog_log(" mode=");
    klog_hex(mode);
    klog_log("\\n");

    vbe_build_gdt();

    memset((void*)data, 0, sizeof(*data));
    data->op = op;
    data->mode = mode;

    size_t stub_size = (size_t)(&__rmcode_end - &__rmcode_start);
    memcpy((void*)(uintptr_t)VBE_RM_CODE_ADDR, &__rmcode_start, stub_size);

    klog_log("VBE: Calling BIOS...\\n");
    vbe_bios_call();
    klog_log("VBE: BIOS returned, status=");
    klog_hex(data->status);
    klog_log("\\n");

    return data->status == 0x004F;
}

static int vbe_get_info(struct vbe_info_block* out)
{
    struct vbe_info_block* info = (struct vbe_info_block*)(uintptr_t)VBE_RM_INFO_ADDR;
    memset(info, 0, sizeof(*info));
    info->signature[0] = 'V';
    info->signature[1] = 'B';
    info->signature[2] = 'E';
    info->signature[3] = '2';

    if (!vbe_call(VBE_OP_INFO, 0))
        return 0;

    if (out)
        memcpy(out, info, sizeof(*out));

    return 1;
}

static int vbe_get_mode_info(uint16_t mode, struct vbe_mode_info_block* out)
{
    struct vbe_mode_info_block* info = (struct vbe_mode_info_block*)(uintptr_t)VBE_RM_MODEINFO_ADDR;
    memset(info, 0, sizeof(*info));

    if (!vbe_call(VBE_OP_MODEINFO, mode))
        return 0;

    if (out)
        memcpy(out, info, sizeof(*out));

    return 1;
}

static int vbe_mode_ok(const struct vbe_mode_info_block* info)
{
    if (!(info->attributes & 0x0001))
        return 0;
    if (!(info->attributes & 0x0080))
        return 0;
    if (info->bpp != 32)
        return 0;
    if (info->memory_model != 6)
        return 0;
    if (info->phys_base == 0)
        return 0;

    return 1;
}

int vbe_init_auto(struct vbe_mode_info* out)
{
    klog_log("VBE: Getting VBE info...\n");
    struct vbe_info_block vbe_info;
    if (!vbe_get_info(&vbe_info))
    {
        klog_error("VBE: vbe_get_info failed\n");
        return 0;
    }
    klog_log("VBE: Got info, scanning modes...\n");

    uint16_t* modes = (uint16_t*)(uintptr_t)VBE_RM_MODELIST_ADDR;
    uint16_t best_mode = 0;
    uint32_t best_area = 0;
    uint16_t fallback_mode = 0;
    uint32_t fallback_area = 0;
    struct vbe_mode_info_block best_info;
    struct vbe_mode_info_block fallback_info;

    for (int i = 0; i < 256; i++)
    {
        uint16_t mode = modes[i];
        if (mode == 0xFFFF)
            break;

        struct vbe_mode_info_block info;
        if (!vbe_get_mode_info(mode, &info))
            continue;

        if (!vbe_mode_ok(&info))
            continue;

        uint32_t area = (uint32_t)info.width * (uint32_t)info.height;
        if (info.width >= 1024 && info.height >= 768)
        {
            if (area > best_area)
            {
                best_area = area;
                best_mode = mode;
                best_info = info;
            }
        }
        else if (info.width >= 800 && info.height >= 600)
        {
            if (area > fallback_area)
            {
                fallback_area = area;
                fallback_mode = mode;
                fallback_info = info;
            }
        }
    }

    if (best_mode == 0 && fallback_mode == 0)
    {
        klog_error("VBE: No suitable mode found\\n");
        return 0;
    }

    uint16_t chosen_mode = best_mode ? best_mode : fallback_mode;
    struct vbe_mode_info_block chosen_info = best_mode ? best_info : fallback_info;

    klog_log("VBE: Setting mode ");
    klog_hex(chosen_mode);
    klog_log(" (");
    klog_hex(chosen_info.width);
    klog_log("x");
    klog_hex(chosen_info.height);
    klog_log(")\\n");

    if (!vbe_call(VBE_OP_SETMODE, chosen_mode))
    {
        klog_error("VBE: Mode set failed\\n");
        return 0;
    }

    klog_log("VBE: Mode set success\\n");

    if (out)
    {
        out->phys_base = chosen_info.phys_base;
        out->pitch = chosen_info.pitch;
        out->width = chosen_info.width;
        out->height = chosen_info.height;
        out->bpp = chosen_info.bpp;
    }

    return 1;
}

int vbe_set_mode(uint16_t mode, struct vbe_mode_info* out)
{
    struct vbe_mode_info_block info;

    if (!vbe_call(VBE_OP_SETMODE, mode))
        return 0;

    if (!vbe_get_mode_info(mode, &info))
        return 0;

    if (out)
    {
        out->phys_base = info.phys_base;
        out->pitch = info.pitch;
        out->width = info.width;
        out->height = info.height;
        out->bpp = info.bpp;
    }

    return 1;
}

int vbe_set_vga_text(void)
{
    return vbe_call(VBE_OP_VGA, 0x03);
}
