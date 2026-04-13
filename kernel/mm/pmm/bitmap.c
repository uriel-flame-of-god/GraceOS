// ============================
// GraceOS PMM Bitmap
// Frame allocator bitmap management
// ============================

#include "bitmap.h"

static uint8_t* bitmap = 0;
static uint64_t frames = 0;

/* Initialize bitmap with given address and frame count */
void bitmap_init(void* addr, uint64_t frame_count)
{
    bitmap = (uint8_t*)addr;
    frames = frame_count;

    uint64_t size = (frames + 7) / 8;

    /* Mark all frames as used initially */
    for (uint64_t i = 0; i < size; i++)
        bitmap[i] = 0xFF;
}

/* Set bit (mark frame as used) */
void bitmap_set(uint64_t frame)
{
    if (frame >= frames)
        return;

    uint64_t byte = frame / 8;
    uint64_t bit = frame % 8;

    bitmap[byte] |= (1 << bit);
}

/* Clear bit (mark frame as free) */
void bitmap_clear(uint64_t frame)
{
    if (frame >= frames)
        return;

    uint64_t byte = frame / 8;
    uint64_t bit = frame % 8;

    bitmap[byte] &= ~(1 << bit);
}

/* Test bit (check if frame is used) */
int bitmap_test(uint64_t frame)
{
    if (frame >= frames)
        return 1;  /* Out of range = used */

    uint64_t byte = frame / 8;
    uint64_t bit = frame % 8;

    return (bitmap[byte] & (1 << bit)) != 0;
}

/* Get total frame count */
uint64_t bitmap_get_total(void)
{
    return frames;
}

/* Get bitmap size in bytes */
uint64_t bitmap_get_size(void)
{
    return (frames + 7) / 8;
}
