// ============================
// GraceOS PMM Bitmap
// Frame allocator bitmap management
// ============================

#ifndef GRACEOS_BITMAP_H
#define GRACEOS_BITMAP_H

#include "../../../lib/libc/int.h"

/* Initialize bitmap with given address and frame count */
void bitmap_init(void* addr, uint64_t frame_count);

/* Set bit (mark frame as used) */
void bitmap_set(uint64_t frame);

/* Clear bit (mark frame as free) */
void bitmap_clear(uint64_t frame);

/* Test bit (check if frame is used) */
int bitmap_test(uint64_t frame);

/* Get total frame count */
uint64_t bitmap_get_total(void);

/* Get bitmap size in bytes */
uint64_t bitmap_get_size(void);

#endif /* GRACEOS_BITMAP_H */
