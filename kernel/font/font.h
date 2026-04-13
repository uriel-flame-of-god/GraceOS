#ifndef _KERNEL_FONT_H
#define _KERNEL_FONT_H

#include "../../lib/libc/int.h"

/* Opaque font structure */
struct font{
    uint32_t width;         // Glyph width in pixels
    uint32_t height;        // Glyph height in pixels
    uint32_t glyph_count;   // Total number of glyphs in font
    uint32_t bytes_per_glyph; // Number of bytes for each glyph bitmap

    uint8_t* glyphs;        // Pointer to glyph bitmap data (glyph_count * bytes_per_glyph bytes)
};

/* Initialize font from PSF2 data */
int font_init(struct font* font, const uint8_t* font_data);

/* Get glyph bitmap for codepoint */
uint8_t* font_get_glyph(struct font* font, uint32_t codepoint);

/* Font metrics */
uint32_t font_width(struct font* font);
uint32_t font_height(struct font* font);

#endif
