#ifndef GRACEOS_VGA_H
#define GRACEOS_VGA_H

#include "../../lib/libc/int.h"

/* VGA Colors */

enum vga_color
{
    VGA_BLACK = 0,
    VGA_BLUE = 1,
    VGA_GREEN = 2,
    VGA_CYAN = 3,
    VGA_RED = 4,
    VGA_MAGENTA = 5,
    VGA_BROWN = 6,
    VGA_LIGHT_GREY = 7,
    VGA_DARK_GREY = 8,
    VGA_LIGHT_BLUE = 9,
    VGA_LIGHT_GREEN = 10,
    VGA_LIGHT_CYAN = 11,
    VGA_LIGHT_RED = 12,
    VGA_LIGHT_MAGENTA = 13,
    VGA_LIGHT_BROWN = 14,
    VGA_WHITE = 15,
};


/* API */

void vga_init(void);
void vga_clear(void);

void vga_set_color(uint8_t fg, uint8_t bg);

void vga_putchar(char c);
void vga_print(const char* str);

void vga_set_cursor(size_t x, size_t y);

#endif /* GRACEOS_VGA_H */
