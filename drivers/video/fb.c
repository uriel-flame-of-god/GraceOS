// ============================
// GraceOS Framebuffer Driver
// ============================
//
// NASA-Style Design Contract:
// - Single Responsibility: Pixel storage and presentation
// - Deterministic Execution: No timing/random dependencies
// - Explicit Initialization: All structures zero-initialized
// - Fail Loud: Invalid states cause immediate return with error
// - Bounded Complexity: Simple scalar loops for all operations
//
// Interface Contract:
// - fb_init(): Must be called before any other function
// - fb_ready(): Returns 1 if usable, 0 otherwise
// - All draw functions silently clip to bounds (defensive)
// - fb_present(): Copies backbuffer to hardware (single memcpy)
//

#include "fb.h"
#include "../../kernel/mm/vmm/vmm.h"



// ============================
// Module State (Single Owner)
// ============================

static struct fb_state fb = {0};

// ============================
// Static Backbuffer
// ============================
// Pre-allocated in BSS to avoid kmalloc (which can't handle multi-MB allocations)
// Supports up to 1024x768x32bpp = 3MB, we allocate 4MB to be safe
#define STATIC_BACKBUFFER_SIZE (4 * 1024 * 1024)
static uint32_t static_backbuffer[STATIC_BACKBUFFER_SIZE / sizeof(uint32_t)] 
    __attribute__((aligned(4096)));

// ============================
// Internal Helpers
// ============================

/**
 * fb_fill32 - Fill a contiguous array of 32-bit pixels with a color.
 *
 * Inputs:
 *   dst   - Pointer to destination buffer (must be valid if count > 0)
 *   count - Number of pixels to fill
 *   color - 32-bit ARGB color value
 *
 * Outputs: None
 * Errors:  Returns immediately if dst is NULL or count is 0
 * Side effects: Writes to dst[0..count-1]
 */
static void fb_fill32(uint32_t* dst, uint64_t count, uint32_t color)
{
    if (dst == NULL || count == 0)
        return;

    for (uint64_t i = 0; i < count; i++)
    {
        dst[i] = color;
    }
}

/**
 * fb_copy32 - Copy a contiguous array of 32-bit pixels.
 *
 * Inputs:
 *   dst   - Pointer to destination buffer
 *   src   - Pointer to source buffer
 *   count - Number of pixels to copy
 *
 * Outputs: None
 * Errors:  Returns immediately if dst/src is NULL or count is 0
 * Side effects: Writes to dst[0..count-1]
 */
static void fb_copy32(uint32_t* dst, const uint32_t* src, uint64_t count)
{
    if (dst == NULL || src == NULL || count == 0)
        return;

    for (uint64_t i = 0; i < count; i++)
    {
        dst[i] = src[i];
    }
}

/**
 * fb_copy8 - Copy a contiguous array of bytes.
 *
 * Inputs:
 *   dst   - Pointer to destination buffer
 *   src   - Pointer to source buffer
 *   count - Number of bytes to copy
 *
 * Outputs: None
 * Errors:  Returns immediately if dst/src is NULL or count is 0
 * Side effects: Writes to dst[0..count-1]
 */
static void fb_copy8(uint8_t* dst, const uint8_t* src, uint64_t count)
{
    if (dst == NULL || src == NULL || count == 0)
        return;

    for (uint64_t i = 0; i < count; i++)
    {
        dst[i] = src[i];
    }
}

// ============================
// Public Interface
// ============================

/**
 * fb_init - Initialize the framebuffer subsystem.
 *
 * Inputs:  None (reads from multiboot or VBE)
 * Outputs: Returns 1 on success, 0 on failure
 * Errors:  Logs warnings on failure, sets fb.ready = 0
 * Side effects:
 *   - Maps framebuffer physical memory
 *   - Allocates backbuffer from kernel heap
 *   - Clears screen to black
 */

/**
 * fb_init - Initialize the framebuffer subsystem.
 */
int fb_init(void)
{
    struct framebuffer_info info = {0};

    if (!multiboot_get_framebuffer(&info))
    {
        fb.ready = 0;
        return 0;
    }

    if (info.type != 1 || info.bpp != 32)
    {
        fb.ready = 0;
        return 0;
    }

    if (info.width == 0 || info.height == 0 || info.pitch == 0)
    {
        fb.ready = 0;
        return 0;
    }

    fb.addr   = info.addr;
    fb.width  = info.width;
    fb.height = info.height;
    fb.pitch  = info.pitch;
    fb.bpp    = info.bpp;
    fb.type   = info.type;
    fb.ready  = 0;

    if (fb.pitch < fb.width * 4)
    {
        fb.ready = 0;
        return 0;
    }

    // Map the framebuffer physical address
    uint64_t fb_size = (uint64_t)fb.pitch * fb.height;
    uint64_t fb_pages = (fb_size + PAGE_SIZE_4K - 1) / PAGE_SIZE_4K;
    uint64_t map_flags = VMM_KERNEL_RW | VMM_PWT | VMM_PCD;

    for (uint64_t i = 0; i < fb_pages; i++)
    {
        uint64_t page_addr = (fb.addr & ~0xFFFULL) + i * PAGE_SIZE_4K;
        vmm_map(page_addr, page_addr, map_flags);
    }

    uint64_t bytes = (uint64_t)fb.pitch * fb.height;
    if (bytes > STATIC_BACKBUFFER_SIZE)
        fb.backbuffer = (uint32_t*)(uintptr_t)fb.addr;
    else
        fb.backbuffer = static_backbuffer;

    fb.ready = 1;

    fb_clear(0xFF000000);
    fb_present();

    return 1;
}
/**
 * fb_ready - Check if framebuffer is initialized and usable.
 *
 * Inputs:  None
 * Outputs: Returns 1 if ready, 0 otherwise
 * Errors:  None
 * Side effects: None
 */
int fb_ready(void)
{
    return fb.ready;
}

/**
 * fb_get_state - Get pointer to framebuffer state structure.
 *
 * Inputs:  None
 * Outputs: Returns pointer to static fb_state
 * Errors:  None
 * Side effects: None
 *
 * WARNING: Caller must not free returned pointer.
 */
struct fb_state* fb_get_state(void)
{
    return &fb;
}

/**
 * fb_matrix_get - Get a matrix view of the backbuffer.
 *
 * Inputs:
 *   out - Pointer to fb_matrix structure to fill
 *
 * Outputs: Fills out with backbuffer dimensions
 * Errors:  Returns immediately if out is NULL
 * Side effects: None
 */
void fb_matrix_get(struct fb_matrix* out)
{
    if (out == NULL)
        return;

    out->data = fb.backbuffer;
    out->width = fb.width;
    out->height = fb.height;
    out->pitch_pixels = fb.pitch / 4;
}

/**
 * fb_matrix_clear - Fill entire matrix with a single color.
 *
 * Inputs:
 *   mat   - Pointer to fb_matrix
 *   color - 32-bit ARGB color
 *
 * Outputs: None
 * Errors:  Returns immediately if invalid state/params
 * Side effects: Writes to mat->data
 */
void fb_matrix_clear(struct fb_matrix* mat, uint32_t color)
{
    // Validate state
    if (!fb.ready)
        return;

    // Validate parameters
    if (mat == NULL || mat->data == NULL)
        return;

    uint64_t pixels = (uint64_t)mat->pitch_pixels * mat->height;
    fb_fill32(mat->data, pixels, color);
}

/**
 * fb_matrix_put_pixel - Set a single pixel in the matrix.
 *
 * Inputs:
 *   mat   - Pointer to fb_matrix
 *   x, y  - Pixel coordinates (0-indexed)
 *   color - 32-bit ARGB color
 *
 * Outputs: None
 * Errors:  Returns immediately if out of bounds
 * Side effects: Writes to mat->data[y * pitch + x]
 */
void fb_matrix_put_pixel(struct fb_matrix* mat, int x, int y, uint32_t color)
{
    // Validate state
    if (!fb.ready)
        return;

    // Validate parameters
    if (mat == NULL || mat->data == NULL)
        return;

    // Bounds checking (defensive)
    if (x < 0 || y < 0)
        return;

    if ((uint32_t)x >= mat->width || (uint32_t)y >= mat->height)
        return;

    // Write pixel
    uint32_t* row = mat->data + (uint64_t)y * mat->pitch_pixels;
    row[x] = color;
}

/**
 * fb_matrix_fill_rect - Fill a rectangle with a single color.
 *
 * Inputs:
 *   mat       - Pointer to fb_matrix
 *   x, y      - Top-left corner coordinates
 *   w, h      - Width and height of rectangle
 *   color     - 32-bit ARGB color
 *
 * Outputs: None
 * Errors:  Returns immediately if invalid params; clips to bounds
 * Side effects: Writes to mat->data
 */
void fb_matrix_fill_rect(struct fb_matrix* mat, int x, int y, int w, int h, uint32_t color)
{
    // Validate state
    if (!fb.ready)
        return;

    // Validate parameters
    if (mat == NULL || mat->data == NULL)
        return;

    // Clip to bounds (defensive)
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }

    if (x + w > (int)mat->width)
        w = (int)mat->width - x;

    if (y + h > (int)mat->height)
        h = (int)mat->height - y;

    if (w <= 0 || h <= 0)
        return;

    // Fill each row
    for (int row = 0; row < h; row++)
    {
        uint32_t* dst = mat->data + (uint64_t)(y + row) * mat->pitch_pixels + x;
        fb_fill32(dst, (uint64_t)w, color);
    }
}

/**
 * fb_matrix_blit - Copy a rectangular region from src to dst matrix.
 *
 * Inputs:
 *   dst              - Destination matrix
 *   src              - Source matrix
 *   dst_x, dst_y     - Destination top-left corner
 *   src_x, src_y     - Source top-left corner
 *   w, h             - Width and height of region
 *
 * Outputs: None
 * Errors:  Returns immediately if invalid params; clips to bounds
 * Side effects: Writes to dst->data
 */
void fb_matrix_blit(struct fb_matrix* dst, const struct fb_matrix* src,
                    int dst_x, int dst_y, int src_x, int src_y, int w, int h)
{
    // Validate state
    if (!fb.ready)
        return;

    // Validate parameters
    if (dst == NULL || dst->data == NULL)
        return;

    if (src == NULL || src->data == NULL)
        return;

    // Clip source region
    if (src_x < 0) { w += src_x; dst_x -= src_x; src_x = 0; }
    if (src_y < 0) { h += src_y; dst_y -= src_y; src_y = 0; }

    if (src_x + w > (int)src->width)
        w = (int)src->width - src_x;

    if (src_y + h > (int)src->height)
        h = (int)src->height - src_y;

    // Clip dest region
    if (dst_x < 0) { w += dst_x; src_x -= dst_x; dst_x = 0; }
    if (dst_y < 0) { h += dst_y; src_y -= dst_y; dst_y = 0; }

    if (dst_x + w > (int)dst->width)
        w = (int)dst->width - dst_x;

    if (dst_y + h > (int)dst->height)
        h = (int)dst->height - dst_y;

    if (w <= 0 || h <= 0)
        return;

    // Copy each row
    for (int row = 0; row < h; row++)
    {
        uint32_t* d = dst->data + (uint64_t)(dst_y + row) * dst->pitch_pixels + dst_x;
        const uint32_t* s = src->data + (uint64_t)(src_y + row) * src->pitch_pixels + src_x;
        fb_copy32(d, s, (uint64_t)w);
    }
}

/**
 * fb_clear - Clear the backbuffer to a single color.
 *
 * Inputs:
 *   color - 32-bit ARGB color
 *
 * Outputs: None
 * Errors:  Returns immediately if not ready
 * Side effects: Writes to backbuffer
 */
void fb_clear(uint32_t color)
{
    struct fb_matrix mat = {0};
    fb_matrix_get(&mat);
    fb_matrix_clear(&mat, color);
}

/**
 * fb_put_pixel - Set a single pixel in the backbuffer.
 *
 * Inputs:
 *   x, y  - Pixel coordinates (0-indexed)
 *   color - 32-bit ARGB color
 *
 * Outputs: None
 * Errors:  Returns immediately if out of bounds
 * Side effects: Writes to backbuffer
 */
void fb_put_pixel(int x, int y, uint32_t color)
{
    struct fb_matrix mat = {0};
    fb_matrix_get(&mat);
    fb_matrix_put_pixel(&mat, x, y, color);
}

/**
 * fb_fill_rect - Fill a rectangle in the backbuffer.
 *
 * Inputs:
 *   x, y  - Top-left corner coordinates
 *   w, h  - Width and height
 *   color - 32-bit ARGB color
 *
 * Outputs: None
 * Errors:  Returns immediately if invalid; clips to bounds
 * Side effects: Writes to backbuffer
 */
void fb_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    struct fb_matrix mat = {0};
    fb_matrix_get(&mat);
    fb_matrix_fill_rect(&mat, x, y, w, h, color);
}

/**
 * fb_present - Copy backbuffer to hardware framebuffer.
 *
 * Inputs:  None
 * Outputs: None
 * Errors:  Returns immediately if not ready
 * Side effects: Writes to hardware framebuffer memory
 */
void fb_present(void)
{
    // Validate state
    if (!fb.ready)
        return;

    if (fb.backbuffer == NULL)
        return;

    // Copy backbuffer to hardware framebuffer
    uint8_t* dst = (uint8_t*)(uintptr_t)fb.addr;
    uint8_t* src = (uint8_t*)fb.backbuffer;
    uint64_t bytes = (uint64_t)fb.pitch * fb.height;

    fb_copy8(dst, src, bytes);
}

// ============================
// Text Rendering
// ============================

// 8x8 bitmap font (ASCII 32-127)
static const uint8_t fb_font8x8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // '!'
    {0x36,0x36,0x24,0x00,0x00,0x00,0x00,0x00}, // '"'
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, // '#'
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, // '$'
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, // '%'
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, // '&'
    {0x06,0x06,0x04,0x00,0x00,0x00,0x00,0x00}, // '\''
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, // '('
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, // ')'
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // '*'
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, // '+'
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, // ','
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, // '-'
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, // '.'
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, // '/'
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, // '0'
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, // '1'
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, // '2'
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, // '3'
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, // '4'
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, // '5'
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, // '6'
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, // '7'
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, // '8'
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, // '9'
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, // ':'
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, // ';'
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, // '<'
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, // '='
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, // '>'
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, // '?'
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, // '@'
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, // 'A'
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, // 'B'
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, // 'C'
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, // 'D'
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, // 'E'
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, // 'F'
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, // 'G'
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, // 'H'
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 'I'
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, // 'J'
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, // 'K'
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, // 'L'
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, // 'M'
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, // 'N'
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, // 'O'
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, // 'P'
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, // 'Q'
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, // 'R'
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, // 'S'
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 'T'
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, // 'U'
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, // 'V'
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // 'W'
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, // 'X'
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, // 'Y'
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, // 'Z'
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, // '['
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, // '\\'
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, // ']'
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, // '^'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // '_'
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, // '`'
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, // 'a'
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, // 'b'
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, // 'c'
    {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00}, // 'd'
    {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00}, // 'e'
    {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00}, // 'f'
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, // 'g'
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, // 'h'
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, // 'i'
    {0x30,0x00,0x38,0x30,0x30,0x33,0x33,0x1E}, // 'j'
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, // 'k'
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 'l'
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, // 'm'
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, // 'n'
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, // 'o'
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, // 'p'
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, // 'q'
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, // 'r'
    {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, // 's'
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, // 't'
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, // 'u'
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, // 'v'
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, // 'w'
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, // 'x'
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, // 'y'
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, // 'z'
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, // '{'
    {0x0C,0x0C,0x0C,0x00,0x0C,0x0C,0x0C,0x00}, // '|'
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, // '}'
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, // '~'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}  // DEL
};
