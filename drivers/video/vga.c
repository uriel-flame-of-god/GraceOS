// ============================
// GraceOS VGA Driver
// ============================

#include "vga.h"
#include "../../lib/libc/string.h"


#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_ADDR    0xB8000


static volatile uint16_t* const vga_buffer =
    (uint16_t*)VGA_ADDR;

static size_t cursor_x = 0;
static size_t cursor_y = 0;

static uint8_t cur_color = 0x07;


/* Create color byte */
static inline uint8_t vga_color(uint8_t fg, uint8_t bg)
{
    return fg | (bg << 4);
}


/* Create entry */
static inline uint16_t vga_entry(char c, uint8_t color)
{
    return (uint16_t)c | (uint16_t)color << 8;
}


/* Initialize VGA */
void vga_init(void)
{
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
}


/* Set colors */
void vga_set_color(uint8_t fg, uint8_t bg)
{
    cur_color = vga_color(fg, bg);
}


/* Clear screen */
void vga_clear(void)
{
    uint16_t blank = vga_entry(' ', cur_color);

    for (size_t y = 0; y < VGA_HEIGHT; y++)
    {
        for (size_t x = 0; x < VGA_WIDTH; x++)
        {
            vga_buffer[y * VGA_WIDTH + x] = blank;
        }
    }

    cursor_x = 0;
    cursor_y = 0;
}


/* Scroll */
static void vga_scroll(void)
{
    size_t line_size = VGA_WIDTH * sizeof(uint16_t);

    for (size_t y = 1; y < VGA_HEIGHT; y++)
    {
        memcpy(
            (void*)&vga_buffer[(y - 1) * VGA_WIDTH],
            (void*)&vga_buffer[y * VGA_WIDTH],
            line_size
        );
    }

    uint16_t blank = vga_entry(' ', cur_color);

    for (size_t x = 0; x < VGA_WIDTH; x++)
    {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
    }

    cursor_y = VGA_HEIGHT - 1;
}


/* Set cursor (software) */
void vga_set_cursor(size_t x, size_t y)
{
    if (x < VGA_WIDTH)
        cursor_x = x;

    if (y < VGA_HEIGHT)
        cursor_y = y;
}


/* Put char */
void vga_putchar(char c)
{
    if (c == '\n')
    {
        cursor_x = 0;
        cursor_y++;

        if (cursor_y >= VGA_HEIGHT)
            vga_scroll();

        return;
    }

    vga_buffer[cursor_y * VGA_WIDTH + cursor_x] =
        vga_entry(c, cur_color);

    cursor_x++;

    if (cursor_x >= VGA_WIDTH)
    {
        cursor_x = 0;
        cursor_y++;

        if (cursor_y >= VGA_HEIGHT)
            vga_scroll();
    }
}


/* Print string */
void vga_print(const char* str)
{
    while (*str)
        vga_putchar(*str++);
}
