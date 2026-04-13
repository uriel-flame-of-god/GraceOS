// ============================
// GraceOS Framebuffer Driver
// ============================
//
// NASA-Style Design Contract:
// - Single Responsibility: Pixel storage and presentation
// - Ownership: fb_state is owned by fb.c module (static)
// - Lifetime: Valid after fb_init() until system shutdown
// - Thread Safety: Not thread-safe (single-core kernel)
//

#ifndef GRACEOS_FB_H
#define GRACEOS_FB_H

#include "../../lib/libc/int.h"
#include "../../kernel/core/multiboot.h"

/**
 * fb_state - Framebuffer hardware state
 *
 * Owned by fb.c module. Do not modify externally.
 */
struct fb_state {
    uint64_t addr;          // Physical address of hardware framebuffer
    uint32_t width;         // Screen width in pixels
    uint32_t height;        // Screen height in pixels
    uint32_t pitch;         // Bytes per row (may exceed width*4 due to alignment)
    uint8_t bpp;            // Bits per pixel (only 32 supported)
    uint8_t type;           // Framebuffer type (1 = RGB)
    uint8_t ready;          // 1 if initialized and usable, 0 otherwise
    uint8_t reserved;       // Padding for alignment
    uint32_t* backbuffer;   // Pointer to backbuffer (owned by fb.c)
};

/**
 * fb_matrix - 2D array view of pixel buffer
 *
 * Provides row/col addressable access to pixel data.
 */
struct fb_matrix {
    uint32_t* data;         // Pointer to pixel data
    uint32_t width;         // Width in pixels
    uint32_t height;        // Height in pixels
    uint32_t pitch_pixels;  // Pixels per row (may exceed width)
};

// ============================
// Initialization
// ============================

// Initialize framebuffer from multiboot/VBE. Returns 1 on success.
int fb_init(void);

// Check if framebuffer is ready. Returns 1 if usable.
int fb_ready(void);

// Get pointer to framebuffer state (do not free).
struct fb_state* fb_get_state(void);

// ============================
// Matrix Operations
// ============================

// Get a matrix view of the backbuffer.
void fb_matrix_get(struct fb_matrix* out);

// Fill entire matrix with color.
void fb_matrix_clear(struct fb_matrix* mat, uint32_t color);

// Set a single pixel. Clips to bounds.
void fb_matrix_put_pixel(struct fb_matrix* mat, int x, int y, uint32_t color);

// Fill a rectangle. Clips to bounds.
void fb_matrix_fill_rect(struct fb_matrix* mat, int x, int y, int w, int h, uint32_t color);

// Copy a rectangular region from src to dst. Clips to bounds.
void fb_matrix_blit(struct fb_matrix* dst, const struct fb_matrix* src,
                    int dst_x, int dst_y, int src_x, int src_y, int w, int h);

// ============================
// Convenience Functions
// ============================

// Clear backbuffer to color.
void fb_clear(uint32_t color);

// Draw pixel to backbuffer. Clips to bounds.
void fb_put_pixel(int x, int y, uint32_t color);

// Draw filled rectangle to backbuffer. Clips to bounds.
void fb_fill_rect(int x, int y, int w, int h, uint32_t color);

// Copy backbuffer to hardware framebuffer.
void fb_present(void);


#endif /* GRACEOS_FB_H */
