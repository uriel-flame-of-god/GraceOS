// ============================
// GraceOS VBE Mode Switching
// ============================

#ifndef GRACEOS_VBE_H
#define GRACEOS_VBE_H

#include "../../../lib/libc/int.h"

#define VBE_MODE_800x600x32   0x115
#define VBE_MODE_1024x768x32  0x118

#define VBE_OP_INFO     1
#define VBE_OP_MODEINFO 2
#define VBE_OP_SETMODE  3
#define VBE_OP_VGA      4

struct vbe_mode_info {
    uint32_t phys_base;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
};

int vbe_set_mode(uint16_t mode, struct vbe_mode_info* out);
int vbe_init_auto(struct vbe_mode_info* out);
int vbe_set_vga_text(void);

#endif /* GRACEOS_VBE_H */
