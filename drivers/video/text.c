#include "text.h"
#include "../../kernel/font/font.h"
#include "../../drivers/video/fb.h"
#include "../../drivers/video/serial.h"

extern const uint8_t _binary_kernel_font_default_psf2_start[];

static struct font sys_font;
static int font_ready = 0;

void text_init(void)
{
    if (font_ready)
        return;

    if (font_init(&sys_font,
        _binary_kernel_font_default_psf2_start) == 0)
    {
        font_ready = 1;
        serial_log("Text: System font loaded");
    }
    else
    {
        serial_log("Text: Font init failed");
    }
}

static void draw_glyph(
    int px, int py,
    uint8_t* glyph,
    uint32_t fg,
    uint32_t bg,
    int scale)
{
    uint32_t w = font_width(&sys_font);
    uint32_t h = font_height(&sys_font);

    for (uint32_t y = 0; y < h; y++)
    {
        uint8_t row = glyph[y];

        for (uint32_t x = 0; x < w; x++)
        {
            uint32_t col =
                (row & (0x80 >> x)) ? fg : bg;

            for (int sy = 0; sy < scale; sy++)
            for (int sx = 0; sx < scale; sx++)
            {
                fb_put_pixel(
                    px + x*scale + sx,
                    py + y*scale + sy,
                    col
                );
            }
        }
    }
}

void text_draw(
    const char* str,
    int x,
    int y,
    uint32_t fg,
    uint32_t bg,
    int scale)
{
    if (!font_ready || !str)
        return;

    int cx = x;
    int cy = y;

    int cw = font_width(&sys_font) * scale;
    int ch = font_height(&sys_font) * scale;

    while (*str)
    {
        char c = *str++;

        if (c == '\n')
        {
            cx = x;
            cy += ch;
            continue;
        }

        uint8_t* g = font_get_glyph(&sys_font, c);

        if (g)
            draw_glyph(cx, cy, g, fg, bg, scale);

        cx += cw;
    }
}
