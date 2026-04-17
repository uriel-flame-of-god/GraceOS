#ifdef ARCH_ARM64

#include "fb_console.h"
#include "../../kernel/arch/arm64/mailbox.h"
#include "../../kernel/font/font.h"
#include "../../lib/libc/string.h"

/* ============================
   Framebuffer Console State
   ============================ */

fb_console_t fb_console = {0};

static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;
static const uint32_t CHAR_SCALE = 2;
static uint32_t char_width = 8 * CHAR_SCALE;
static uint32_t char_height = 8 * CHAR_SCALE;
static uint32_t fg_color = 0xFFFFFFFF;  /* White */
static uint32_t bg_color = 0x00000000;  /* Black */

static struct font fb_font;
static int fb_font_ready = 0;

extern const uint8_t _binary_kernel_font_default_psf2_start[];

/* Simple bitmap font: A (ASCII 65) */
static const uint8_t simple_chars[128][8] = {
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['A'] = {0x18, 0x24, 0x42, 0x7E, 0x81, 0x81, 0x00, 0x00},
    ['B'] = {0x7C, 0x42, 0x42, 0x7C, 0x42, 0x42, 0x7C, 0x00},
    ['C'] = {0x3C, 0x42, 0x80, 0x80, 0x80, 0x42, 0x3C, 0x00},
    ['D'] = {0x7C, 0x42, 0x41, 0x41, 0x41, 0x42, 0x7C, 0x00},
    ['E'] = {0x7E, 0x40, 0x40, 0x78, 0x40, 0x40, 0x7E, 0x00},
    ['F'] = {0x7E, 0x40, 0x40, 0x78, 0x40, 0x40, 0x40, 0x00},
    ['G'] = {0x3C, 0x42, 0x80, 0x8E, 0x82, 0x42, 0x3C, 0x00},
    ['H'] = {0x42, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x42, 0x00},
    ['I'] = {0x7C, 0x10, 0x10, 0x10, 0x10, 0x10, 0x7C, 0x00},
    ['J'] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x44, 0x38, 0x00},
    ['K'] = {0x42, 0x44, 0x48, 0x70, 0x48, 0x44, 0x42, 0x00},
    ['L'] = {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x7E, 0x00},
    ['M'] = {0x81, 0xC3, 0xA5, 0x99, 0x81, 0x81, 0x81, 0x00},
    ['N'] = {0x42, 0x62, 0x52, 0x4A, 0x46, 0x42, 0x41, 0x00},
    ['O'] = {0x3C, 0x42, 0x81, 0x81, 0x81, 0x42, 0x3C, 0x00},
    ['P'] = {0x7C, 0x42, 0x42, 0x7C, 0x40, 0x40, 0x40, 0x00},
    ['Q'] = {0x3C, 0x42, 0x81, 0x81, 0x85, 0x42, 0x3A, 0x00},
    ['R'] = {0x7C, 0x42, 0x42, 0x7C, 0x48, 0x44, 0x42, 0x00},
    ['S'] = {0x3C, 0x42, 0x40, 0x3C, 0x02, 0x42, 0x3C, 0x00},
    ['T'] = {0x7E, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00},
    ['U'] = {0x81, 0x81, 0x81, 0x81, 0x81, 0x42, 0x3C, 0x00},
    ['V'] = {0x81, 0x81, 0x42, 0x42, 0x24, 0x24, 0x18, 0x00},
    ['W'] = {0x81, 0x81, 0x81, 0x99, 0xA5, 0xC3, 0x81, 0x00},
    ['X'] = {0x81, 0x42, 0x24, 0x18, 0x24, 0x42, 0x81, 0x00},
    ['Y'] = {0x81, 0x42, 0x24, 0x18, 0x10, 0x10, 0x10, 0x00},
    ['Z'] = {0x7E, 0x02, 0x04, 0x08, 0x10, 0x20, 0x7E, 0x00},
    ['0'] = {0x3C, 0x42, 0x81, 0x89, 0x81, 0x42, 0x3C, 0x00},
    ['1'] = {0x08, 0x18, 0x08, 0x08, 0x08, 0x08, 0x3E, 0x00},
    ['2'] = {0x3C, 0x42, 0x02, 0x3C, 0x40, 0x40, 0x7E, 0x00},
    ['3'] = {0x3C, 0x42, 0x02, 0x1C, 0x02, 0x42, 0x3C, 0x00},
    ['4'] = {0x08, 0x18, 0x28, 0x48, 0x7E, 0x08, 0x08, 0x00},
    ['5'] = {0x7E, 0x40, 0x40, 0x7C, 0x02, 0x42, 0x3C, 0x00},
    ['6'] = {0x3C, 0x42, 0x40, 0x7C, 0x42, 0x42, 0x3C, 0x00},
    ['7'] = {0x7E, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x00},
    ['8'] = {0x3C, 0x42, 0x42, 0x3C, 0x42, 0x42, 0x3C, 0x00},
    ['9'] = {0x3C, 0x42, 0x42, 0x3E, 0x02, 0x42, 0x3C, 0x00},
};

/* ============================
   Helper: Draw a single pixel
   ============================ */
static void fb_draw_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (x >= fb_console.width || y >= fb_console.height)
        return;

    uint32_t offset = (y * fb_console.pitch) + (x * 4);
    uint32_t* pixel = (uint32_t*)(fb_console.buffer + offset);
    *pixel = color;
}

/* ============================
   Helper: Draw a 8x8 character
   ============================ */
static void fb_draw_char_bitmap(uint32_t x, uint32_t y, char c, const uint8_t* bitmap)
{
    if (x + char_width > fb_console.width || y + char_height > fb_console.height)
        return;

    for (int row = 0; row < 8; row++) {
        uint8_t bits = bitmap[row];
        for (int col = 0; col < 8; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg_color : bg_color;
            uint32_t px = x + (uint32_t)col * CHAR_SCALE;
            uint32_t py = y + (uint32_t)row * CHAR_SCALE;
            for (uint32_t sy = 0; sy < CHAR_SCALE; sy++) {
                for (uint32_t sx = 0; sx < CHAR_SCALE; sx++) {
                    fb_draw_pixel(px + sx, py + sy, color);
                }
            }
        }
    }
}

static void fb_draw_char_font(uint32_t x, uint32_t y, char c)
{
    if (!fb_font_ready)
        return;

    uint32_t fw = font_width(&fb_font);
    uint32_t fh = font_height(&fb_font);
    uint32_t bytes_per_row = (fw + 7) / 8;
    uint8_t* glyph = font_get_glyph(&fb_font, (uint8_t)c);

    if (!glyph)
        return;

    if (x + char_width > fb_console.width || y + char_height > fb_console.height)
        return;

    for (uint32_t row = 0; row < fh; row++) {
        uint8_t* row_data = glyph + row * bytes_per_row;
        for (uint32_t col = 0; col < fw; col++) {
            uint8_t byte = row_data[col / 8];
            uint8_t mask = 0x80 >> (col % 8);
            uint32_t color = (byte & mask) ? fg_color : bg_color;
            uint32_t px = x + col * CHAR_SCALE;
            uint32_t py = y + row * CHAR_SCALE;
            for (uint32_t sy = 0; sy < CHAR_SCALE; sy++) {
                for (uint32_t sx = 0; sx < CHAR_SCALE; sx++) {
                    fb_draw_pixel(px + sx, py + sy, color);
                }
            }
        }
    }
}

/* ============================
   Public API
   ============================ */

int fb_console_init(void)
{
    mbox_message_t __attribute__((aligned(16))) msg;
    memset(&msg, 0, sizeof(msg));

    /* Set physical size to 1024x768 */
    msg.size = 8 * 4;
    msg.code = MBOX_REQUEST;
    msg.tags[0] = MBOX_TAG_SET_PHYSICAL_SIZE;
    msg.tags[1] = 8;   /* value size */
    msg.tags[2] = 8;   /* response size */
    msg.tags[3] = 1024; /* width */
    msg.tags[4] = 768;  /* height */
    msg.tags[5] = MBOX_TAG_END;

    if (mailbox_call(&msg) != 0)
        return -1;

    /* Set virtual size (same as physical) */
    memset(&msg, 0, sizeof(msg));
    msg.size = 8 * 4;
    msg.code = MBOX_REQUEST;
    msg.tags[0] = MBOX_TAG_SET_VIRTUAL_SIZE;
    msg.tags[1] = 8;
    msg.tags[2] = 8;
    msg.tags[3] = 1024;
    msg.tags[4] = 768;
    msg.tags[5] = MBOX_TAG_END;

    if (mailbox_call(&msg) != 0)
        return -1;

    /* Set depth to 32 bits */
    memset(&msg, 0, sizeof(msg));
    msg.size = 7 * 4;
    msg.code = MBOX_REQUEST;
    msg.tags[0] = MBOX_TAG_SET_DEPTH;
    msg.tags[1] = 4;
    msg.tags[2] = 4;
    msg.tags[3] = 32;
    msg.tags[4] = MBOX_TAG_END;

    if (mailbox_call(&msg) != 0)
        return -1;

    /* Set pixel order to RGB */
    memset(&msg, 0, sizeof(msg));
    msg.size = 7 * 4;
    msg.code = MBOX_REQUEST;
    msg.tags[0] = MBOX_TAG_SET_PIXEL_ORDER;
    msg.tags[1] = 4;
    msg.tags[2] = 4;
    msg.tags[3] = 1;  /* 1 = RGB, 0 = BGR */
    msg.tags[4] = MBOX_TAG_END;

    if (mailbox_call(&msg) != 0)
        return -1;

    /* Allocate framebuffer */
    memset(&msg, 0, sizeof(msg));
    msg.size = 8 * 4;
    msg.code = MBOX_REQUEST;
    msg.tags[0] = MBOX_TAG_ALLOCATE_BUFFER;
    msg.tags[1] = 8;
    msg.tags[2] = 8;
    msg.tags[3] = 16;  /* alignment in log2 */
    msg.tags[4] = 0;
    msg.tags[5] = MBOX_TAG_END;

    if (mailbox_call(&msg) != 0)
        return -1;

    uint32_t fb_addr = msg.tags[3];
    uint32_t fb_size = msg.tags[4];

    /* Get pitch (bytes per scanline) */
    memset(&msg, 0, sizeof(msg));
    msg.size = 7 * 4;
    msg.code = MBOX_REQUEST;
    msg.tags[0] = MBOX_TAG_GET_PITCH;
    msg.tags[1] = 4;
    msg.tags[2] = 4;
    msg.tags[3] = 0;
    msg.tags[4] = MBOX_TAG_END;

    if (mailbox_call(&msg) != 0)
        return -1;

    uint32_t pitch = msg.tags[3];

    /* Fill in framebuffer console structure */
    fb_console.width = 1024;
    fb_console.height = 768;
    fb_console.depth = 32;
    fb_console.pitch = pitch;
    fb_console.buffer_size = fb_size;
    /* Convert GPU bus address to ARM physical address */
    fb_console.buffer = (uint8_t*)(uint64_t)(fb_addr & 0x3FFFFFFF);

    fb_font_ready = (font_init(&fb_font, _binary_kernel_font_default_psf2_start) == 0);
    if (fb_font_ready) {
        char_width = font_width(&fb_font) * CHAR_SCALE;
        char_height = font_height(&fb_font) * CHAR_SCALE;
    } else {
        char_width = 8 * CHAR_SCALE;
        char_height = 8 * CHAR_SCALE;
    }

    /* Clear framebuffer */
    fb_console_clear();

    return 0;
}

void fb_console_clear(void)
{
    if (!fb_console.buffer)
        return;

    uint32_t* ptr = (uint32_t*)fb_console.buffer;
    uint32_t count = (fb_console.pitch * fb_console.height) / 4;

    for (uint32_t i = 0; i < count; i++)
        ptr[i] = bg_color;

    cursor_x = 0;
    cursor_y = 0;
}

void fb_console_putchar(char c)
{
    if (!fb_console.buffer)
        return;

    if (c == '\n') {
        cursor_x = 0;
        cursor_y += char_height;
        if (cursor_y + char_height > fb_console.height)
            fb_console_clear();
        return;
    }

    if (c == '\r') {
        cursor_x = 0;
        return;
    }

    if (c == '\b') {
        if (cursor_x >= char_width)
            cursor_x -= char_width;
        if (fb_font_ready)
            fb_draw_char_font(cursor_x, cursor_y, ' ');
        else
            fb_draw_char_bitmap(cursor_x, cursor_y, ' ', simple_chars[' ']);
        return;
    }

    if (c == '\t') {
        for (int i = 0; i < 4; i++)
            fb_console_putchar(' ');
        return;
    }

    /* Draw character at cursor position */
    if (fb_font_ready)
        fb_draw_char_font(cursor_x, cursor_y, c);
    else
        fb_draw_char_bitmap(cursor_x, cursor_y, c, (c >= 32 && c < 127) ? simple_chars[(unsigned char)c] : simple_chars[' ']);

    cursor_x += char_width;
    if (cursor_x + char_width > fb_console.width) {
        cursor_x = 0;
        cursor_y += char_height;
        if (cursor_y + char_height > fb_console.height)
            fb_console_clear();
    }
}

void fb_console_puts(const char* s)
{
    if (!s)
        return;
    while (*s)
        fb_console_putchar(*s++);
}

void fb_console_printf(const char* fmt, ...)
{
    /* Minimal printf implementation for now */
    if (!fmt)
        return;
    fb_console_puts(fmt);
}

#endif /* ARCH_ARM64 */
