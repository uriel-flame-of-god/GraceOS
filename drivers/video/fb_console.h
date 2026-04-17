#ifndef GRACEOS_DRIVERS_FB_CONSOLE_H
#define GRACEOS_DRIVERS_FB_CONSOLE_H

#include "../../lib/libc/int.h"

/* ============================
   Framebuffer Console for RPi
   ============================

   Minimal console implementation using RPi framebuffer.
   Supports basic text output with a simple fixed-width font.
*/

typedef struct {
    uint32_t width;         /* Framebuffer width in pixels */
    uint32_t height;        /* Framebuffer height in pixels */
    uint32_t pitch;         /* Bytes per scanline */
    uint32_t depth;         /* Bits per pixel (32 = RGBA) */
    uint32_t x_offset;      /* Virtual offset X */
    uint32_t y_offset;      /* Virtual offset Y */
    uint8_t* buffer;        /* Framebuffer virtual address */
    uint32_t buffer_size;   /* Total buffer size */
} fb_console_t;

extern fb_console_t fb_console;

/* Framebuffer API */
int fb_console_init(void);
void fb_console_clear(void);
void fb_console_putchar(char c);
void fb_console_puts(const char* s);
void fb_console_printf(const char* fmt, ...);

#endif /* GRACEOS_DRIVERS_FB_CONSOLE_H */
