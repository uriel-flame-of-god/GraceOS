// ============================
// GraceOS TTY Driver
// ============================

#include "tty.h"

#ifdef ARCH_ARM64
// ================================================================
// ARM64 / Raspberry Pi TTY
// Routes all output to the RPi UART (mini-UART or PL011).
// Colors are accepted by the API but ignored — UART is plain text.
// tty_readline uses the UART directly for line input.
// ================================================================

#include "uart_rpi.h"
#include "fb_console.h"
#include "../input/keyboard.h"
#include "../../lib/libc/string.h"

/* Color is ignored on UART — just track it to satisfy callers */
static uint8_t cur_fg = 7;
static uint8_t cur_bg = 0;
static int fb_console_ready = 0;

static inline void tty_putc_both(char c)
{
    uart_putchar(c);
    if (fb_console_ready)
        fb_console_putchar(c);
}

static inline void tty_puts_both(const char* s)
{
    uart_print(s);
    if (fb_console_ready)
        fb_console_puts(s);
}

void tty_init(void)
{
    uart_init();
    fb_console_ready = (fb_console_init() == 0);
}

void tty_clear(void)
{
    /* Send ANSI clear-screen sequence — most serial terminals support it */
    tty_puts_both("\033[2J\033[H");
    if (fb_console_ready)
        fb_console_clear();
}

void tty_set_color(uint8_t fg, uint8_t bg)
{
    cur_fg = fg & 0x0F;
    cur_bg = bg & 0x0F;
}

void tty_get_color(uint8_t* fg, uint8_t* bg)
{
    if (fg) *fg = cur_fg;
    if (bg) *bg = cur_bg;
}

void tty_putchar(char c)
{
    tty_putc_both(c);
}

void tty_write(const char* str, size_t len)
{
    for (size_t i = 0; i < len; i++)
        tty_putc_both(str[i]);
}

void tty_print(const char* str)
{
    tty_puts_both(str);
}

void tty_print_hex(uint64_t value)
{
    const char* hex = "0123456789ABCDEF";
    tty_puts_both("0x");
    int started = 0;
    for (int i = 60; i >= 0; i -= 4)
    {
        int digit = (int)((value >> i) & 0xF);
        if (digit || started || i == 0)
        {
            tty_putc_both(hex[digit]);
            started = 1;
        }
    }
}

void tty_newline(void)       { tty_putc_both('\n'); }
void tty_scroll(void)        { /* no-op: UART terminal scrolls itself */ }
void tty_begin_batch(void)   { /* no-op */ }
void tty_end_batch(void)     { /* no-op */ }
void tty_set_cursor(size_t x, size_t y) { (void)x; (void)y; }
void tty_get_cursor(size_t* x, size_t* y) { if (x) *x = 0; if (y) *y = 0; }
void tty_show_cursor(void)   { /* no-op */ }
void tty_hide_cursor(void)   { /* no-op */ }

/* Logging helpers — just print the tag prefix */
void log_init(const char* msg)    { tty_print("[INIT]    "); tty_print(msg); tty_putchar('\n'); }
void log_debug(const char* msg)   { tty_print("[DEBUG]   "); tty_print(msg); tty_putchar('\n'); }
void log_set(const char* msg)     { tty_print("[SET]     "); tty_print(msg); tty_putchar('\n'); }
void log_get(const char* msg)     { tty_print("[GET]     "); tty_print(msg); tty_putchar('\n'); }
void log_success(const char* msg) { tty_print("[SUCCESS] "); tty_print(msg); tty_putchar('\n'); }
void log_warn(const char* msg)    { tty_print("[WARN]    "); tty_print(msg); tty_putchar('\n'); }
void log_error(const char* msg)   { tty_print("[ERROR]   "); tty_print(msg); tty_putchar('\n'); }

/* Simple line reader over UART — no history or arrow key support yet */
char* tty_readline(char* buffer, size_t max_len)
{
    size_t pos = 0;

    while (1)
    {
        char c = keyboard_getchar();

        if (c == '\r' || c == '\n')
        {
            buffer[pos] = '\0';
            tty_putchar('\n');
            return buffer;
        }
        else if (c == '\b' || c == 127)
        {
            if (pos > 0)
            {
                pos--;
                tty_putchar('\b');
                tty_putchar(' ');
                tty_putchar('\b');
            }
        }
        else if (c == 0x03)
        {
            /* Ctrl+C */
            buffer[0] = '\0';
            tty_print("^C\n");
            return buffer;
        }
        else if (c >= 32 && c < 127)
        {
            if (pos < max_len - 1)
            {
                buffer[pos++] = c;
                tty_putchar(c);
            }
        }
    }
}

#else  /* !ARCH_ARM64 — original x86 VGA implementation */

#include "fb.h"
#include "../../lib/libc/string.h"
#include "../input/keyboard.h"
#include "../../kernel/arch/x86_64/io/port.h"

#include "../../kernel/font/font.h"

#include "../../kernel/include/time.h"

extern const uint8_t _binary_kernel_font_default_psf2_start[];
extern const uint8_t _binary_kernel_font_default_psf2_end[];

/* VGA Text Mode Constants */
#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_ADDR    0xB8000

/* Hardware cursor I/O ports */
#define VGA_CTRL_PORT   0x3D4
#define VGA_DATA_PORT   0x3D5

/* Font */
static struct font tty_font;

/* Font scaling for HD support */
static int use_hd_font = 0;  // 0 = 8x16 (800x600), 1 = 16x32 (1280x720)
static uint32_t font_scale = 1;  // Pixel scaling factor


/* VGA Buffer */
static volatile uint16_t* const vga_buffer = (uint16_t*)VGA_ADDR;

/* Framebuffer mode */
static int use_fb = 0;
static uint32_t fb_cols = 80;   // Text columns
static uint32_t fb_rows = 25;   // Text rows

/* Text buffer for framebuffer mode (char + color for each cell) */
#define FB_TEXT_MAX_COLS 128
#define FB_TEXT_MAX_ROWS 64
static char fb_text[FB_TEXT_MAX_ROWS][FB_TEXT_MAX_COLS];
static uint8_t fb_color_buf[FB_TEXT_MAX_ROWS][FB_TEXT_MAX_COLS];

/* VGA color to 32-bit ARGB palette */
static const uint32_t tty_palette[16] = {
    0xFF000000, // Black
    0xFF0000AA, // Blue
    0xFF00AA00, // Green
    0xFF00AAAA, // Cyan
    0xFFAA0000, // Red
    0xFFAA00AA, // Magenta
    0xFFAA5500, // Brown
    0xFFAAAAAA, // Light Grey
    0xFF555555, // Dark Grey
    0xFF5555FF, // Light Blue
    0xFF55FF55, // Light Green
    0xFF55FFFF, // Light Cyan
    0xFFFF5555, // Light Red
    0xFFFF55FF, // Light Magenta
    0xFFFFFF55, // Yellow
    0xFFFFFFFF, // White
};

/* TTY State */
static size_t cursor_x = 0;
static size_t cursor_y = 0;
static uint8_t cur_fg = TTY_LIGHT_GREY;
static uint8_t cur_bg = TTY_BLACK;
static uint8_t cur_color = 0x07;
static int cursor_visible = 1;
static int batch_mode = 0;

#define CURSOR_BLINK_MS 500
static uint64_t cursor_blink_last_ms = 0;
static int cursor_blink_on = 1;
static int fb_cursor_drawn = 0;
static size_t fb_cursor_drawn_x = 0;
static size_t fb_cursor_drawn_y = 0;

/* Command History */
#define HISTORY_SIZE 32
#define HISTORY_LINE_LEN 256
static char history[HISTORY_SIZE][HISTORY_LINE_LEN];
static int history_count = 0;
static int history_write_idx = 0;
static int history_browse_idx = 0;


/* Create VGA color byte */
static inline uint8_t make_color(uint8_t fg, uint8_t bg)
{
    return fg | (bg << 4);
}


/* Create VGA entry */
static inline uint16_t make_entry(char c, uint8_t color)
{
    return (uint16_t)c | ((uint16_t)color << 8);
}


/* Update hardware cursor position */
static void update_hw_cursor(void)
{
    if (use_fb)
        return;  // No hardware cursor in FB mode
    
    if (!cursor_visible)
        return;
    
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;
    
    outb(VGA_CTRL_PORT, 0x0F);
    outb(VGA_DATA_PORT, (uint8_t)(pos & 0xFF));
    outb(VGA_CTRL_PORT, 0x0E);
    outb(VGA_DATA_PORT, (uint8_t)((pos >> 8) & 0xFF));
}

static void fb_draw_glyph(int px, int py,
                          uint8_t* glyph,
                          uint32_t fg,
                          uint32_t bg)
{
    uint32_t base_w = font_width(&tty_font);
    uint32_t base_h = font_height(&tty_font);
    
    // For HD mode, we scale 2x
    uint32_t w = base_w * font_scale;
    uint32_t h = base_h * font_scale;

    if (font_scale == 1)
    {
        // Standard 1x rendering (800x600 with 8x16)
        for (uint32_t y = 0; y < base_h; y++)
        {
            uint8_t row = glyph[y];

            for (uint32_t x = 0; x < base_w; x++)
            {
                uint32_t col = (row & (0x80 >> x)) ? fg : bg;
                fb_put_pixel(px + x, py + y, col);
            }
        }
    }
    else
    {
        // 2x scaled rendering (1280x720 with 8x16 scaled to 16x32)
        for (uint32_t y = 0; y < base_h; y++)
        {
            uint8_t row = glyph[y];

            for (uint32_t x = 0; x < base_w; x++)
            {
                uint32_t col = (row & (0x80 >> x)) ? fg : bg;
                
                // Draw 2x2 block for each pixel
                fb_put_pixel(px + (x * 2),     py + (y * 2),     col);
                fb_put_pixel(px + (x * 2) + 1, py + (y * 2),     col);
                fb_put_pixel(px + (x * 2),     py + (y * 2) + 1, col);
                fb_put_pixel(px + (x * 2) + 1, py + (y * 2) + 1, col);
            }
        }
    }
}


/* Render a single cell in framebuffer mode */
static void fb_render_cell(size_t x, size_t y)
{
    if (!use_fb || x >= fb_cols || y >= fb_rows)
        return;

    char c = fb_text[y][x];
    uint8_t color = fb_color_buf[y][x];
    uint32_t fg = tty_palette[color & 0x0F];
    uint32_t bg = tty_palette[(color >> 4) & 0x0F];

    uint32_t char_width = font_width(&tty_font) * font_scale;
    uint32_t char_height = font_height(&tty_font) * font_scale;
    
    int px = (int)(x * char_width);
    int py = (int)(y * char_height);
    uint8_t* glyph = font_get_glyph(&tty_font, (uint32_t)c);
    fb_draw_glyph(px, py, glyph, fg, bg);
}

static void fb_render_cursor_cell(size_t x, size_t y)
{
    if (!use_fb || x >= fb_cols || y >= fb_rows)
        return;

    char c = fb_text[y][x];
    uint8_t color = fb_color_buf[y][x];
    uint32_t fg = tty_palette[(color >> 4) & 0x0F];
    uint32_t bg = tty_palette[color & 0x0F];

    uint32_t char_width = font_width(&tty_font) * font_scale;
    uint32_t char_height = font_height(&tty_font) * font_scale;

    int px = (int)(x * char_width);
    int py = (int)(y * char_height);
    uint8_t* glyph = font_get_glyph(&tty_font, (uint32_t)c);
    fb_draw_glyph(px, py, glyph, fg, bg);
}

static void tty_cursor_erase_fb(void)
{
    if (!use_fb || !fb_cursor_drawn)
        return;

    fb_render_cell(fb_cursor_drawn_x, fb_cursor_drawn_y);
    fb_cursor_drawn = 0;
}

static void tty_cursor_refresh(int present)
{
    if (use_fb)
    {
        tty_cursor_erase_fb();

        if (cursor_visible && cursor_blink_on && cursor_x < fb_cols && cursor_y < fb_rows)
        {
            fb_render_cursor_cell(cursor_x, cursor_y);
            fb_cursor_drawn = 1;
            fb_cursor_drawn_x = cursor_x;
            fb_cursor_drawn_y = cursor_y;
        }

        if (present)
            fb_present();
        return;
    }

    update_hw_cursor();
}

static void tty_cursor_activity(void)
{
    cursor_blink_on = 1;
    cursor_blink_last_ms = timer_get_ms();
    tty_cursor_refresh(1);
}

static void tty_cursor_blink_tick(void)
{
    if (!cursor_visible)
        return;

    uint64_t now = timer_get_ms();
    if (cursor_blink_last_ms == 0)
        cursor_blink_last_ms = now;

    if ((now - cursor_blink_last_ms) >= CURSOR_BLINK_MS)
    {
        cursor_blink_last_ms = now;
        cursor_blink_on = !cursor_blink_on;
        tty_cursor_refresh(1);
    }
}


/* Render entire screen in framebuffer mode */
static void fb_render_all(void)
{
    if (!use_fb)
        return;

    for (size_t y = 0; y < fb_rows; y++)
    {
        for (size_t x = 0; x < fb_cols; x++)
        {
            fb_render_cell(x, y);
        }
    }
    fb_present();
}


/* Initialize TTY */
void tty_init(void)
{
    cur_fg = TTY_LIGHT_GREY;
    cur_bg = TTY_BLACK;
    cur_color = make_color(cur_fg, cur_bg);
    cursor_x = 0;
    cursor_y = 0;
    cursor_visible = 1;
    cursor_blink_last_ms = 0;
    cursor_blink_on = 1;
    fb_cursor_drawn = 0;

    int font_result = font_init(&tty_font, _binary_kernel_font_default_psf2_start);

    if (fb_init())
    {
        use_fb = 1;
        struct fb_state* state = fb_get_state();

        if (font_result == 0)
        {
            if (state->width >= 1280 && state->height >= 720)
            {
                use_hd_font = 1;
                font_scale = 2;
            }
            else
            {
                use_hd_font = 0;
                font_scale = 1;
            }

            uint32_t char_width  = font_width(&tty_font)  * font_scale;
            uint32_t char_height = font_height(&tty_font) * font_scale;
            fb_cols = state->width  / char_width;
            fb_rows = state->height / char_height;
        }
        else
        {
            fb_cols = state->width  / 8;
            fb_rows = state->height / 8;
        }

        if (fb_cols > FB_TEXT_MAX_COLS)
            fb_cols = FB_TEXT_MAX_COLS;
        if (fb_rows > FB_TEXT_MAX_ROWS)
            fb_rows = FB_TEXT_MAX_ROWS;
    }
    else
    {
        use_fb = 0;
    }

    tty_clear();
    if (!use_fb)
        tty_show_cursor();
}

/* Clear screen */
void tty_clear(void)
{
    cursor_x = 0;
    cursor_y = 0;
    cursor_blink_on = 1;
    cursor_blink_last_ms = timer_get_ms();

    if (use_fb)
    {
        // Clear text buffer
        for (size_t y = 0; y < fb_rows; y++)
        {
            for (size_t x = 0; x < fb_cols; x++)
            {
                fb_text[y][x] = ' ';
                fb_color_buf[y][x] = cur_color;
            }
        }
        // Clear framebuffer to background color
        fb_clear(tty_palette[(cur_color >> 4) & 0x0F]);
        if (!batch_mode) fb_present();
    }
    else
    {
        uint16_t blank = make_entry(' ', cur_color);
        
        for (size_t y = 0; y < VGA_HEIGHT; y++)
        {
            for (size_t x = 0; x < VGA_WIDTH; x++)
            {
                vga_buffer[y * VGA_WIDTH + x] = blank;
            }
        }
        tty_cursor_refresh(0);
    }
}


/* Set color */
void tty_set_color(uint8_t fg, uint8_t bg)
{
    cur_fg = fg & 0x0F;
    cur_bg = bg & 0x0F;
    cur_color = make_color(cur_fg, cur_bg);
}


/* Get color */
void tty_get_color(uint8_t* fg, uint8_t* bg)
{
    if (fg) *fg = cur_fg;
    if (bg) *bg = cur_bg;
}


/* Scroll screen up by one line */
void tty_scroll(void)
{
    if (use_fb)
    {
        size_t max_y = fb_rows - 1;
        
        // Move all lines up in text buffer
        for (size_t y = 1; y < fb_rows; y++)
        {
            for (size_t x = 0; x < fb_cols; x++)
            {
                fb_text[y - 1][x] = fb_text[y][x];
                fb_color_buf[y - 1][x] = fb_color_buf[y][x];
            }
        }
        
        // Clear bottom line in text buffer
        for (size_t x = 0; x < fb_cols; x++)
        {
            fb_text[max_y][x] = ' ';
            fb_color_buf[max_y][x] = cur_color;
        }
        
        cursor_y = max_y;
        
        // Fast scroll: use memmove on backbuffer directly
        struct fb_state* state = fb_get_state();
        if (state && state->backbuffer)
        {
            uint32_t pitch_pixels = state->pitch / 4;
            uint32_t line_height = font_height(&tty_font) * font_scale;
            uint32_t scroll_pixels = line_height * pitch_pixels;
            uint32_t total_pixels = (state->height - line_height) * pitch_pixels;
            
            // Move framebuffer content up by one text line
            memmove(state->backbuffer, 
                    state->backbuffer + scroll_pixels,
                    total_pixels * sizeof(uint32_t));
            
            // Clear the bottom line (last line_height rows)
            uint32_t bg_color = tty_palette[(cur_color >> 4) & 0x0F];
            uint32_t* bottom = state->backbuffer + (state->height - line_height) * pitch_pixels;
            for (uint32_t i = 0; i < line_height * pitch_pixels; i++)
            {
                bottom[i] = bg_color;
            }
        }
        
        fb_present();
    }
    else
    {
        // Move all lines up
        for (size_t y = 1; y < VGA_HEIGHT; y++)
        {
            for (size_t x = 0; x < VGA_WIDTH; x++)
            {
                vga_buffer[(y - 1) * VGA_WIDTH + x] = 
                    vga_buffer[y * VGA_WIDTH + x];
            }
        }
        
        // Clear bottom line
        uint16_t blank = make_entry(' ', cur_color);
        for (size_t x = 0; x < VGA_WIDTH; x++)
        {
            vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
        }
        
        cursor_y = VGA_HEIGHT - 1;
    }
}


/* Move to new line */
void tty_newline(void)
{
    tty_cursor_erase_fb();

    cursor_x = 0;
    cursor_y++;
    
    size_t max_y = use_fb ? fb_rows : VGA_HEIGHT;
    
    if (cursor_y >= max_y)
    {
        tty_scroll();
    }
    
    tty_cursor_activity();
}


/* Put a character */
void tty_putchar(char c)
{
    tty_cursor_erase_fb();

    size_t max_x = use_fb ? fb_cols : VGA_WIDTH;
    
    // Handle special characters
    switch (c)
    {
        case '\n':
            tty_newline();
            return;
            
        case '\r':
            cursor_x = 0;
            tty_cursor_activity();
            return;
            
        case '\t':
            // Tab = 4 spaces
            for (int i = 0; i < 4; i++)
                tty_putchar(' ');
            return;
            
        case '\b':
            // Backspace
            if (cursor_x > 0)
            {
                cursor_x--;
                if (use_fb)
                {
                    fb_text[cursor_y][cursor_x] = ' ';
                    fb_color_buf[cursor_y][cursor_x] = cur_color;
                    fb_render_cell(cursor_x, cursor_y);
                    fb_present();
                }
                else
                {
                    vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = 
                        make_entry(' ', cur_color);
                }
                tty_cursor_activity();
            }
            return;
    }
    
    // Regular character
    if (use_fb)
    {
        fb_text[cursor_y][cursor_x] = c;
        fb_color_buf[cursor_y][cursor_x] = cur_color;
        fb_render_cell(cursor_x, cursor_y);
        if (!batch_mode) fb_present();
    }
    else
    {
        vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = make_entry(c, cur_color);
    }
    cursor_x++;
    
    // Wrap to next line
    if (cursor_x >= max_x)
    {
        tty_newline();
    }
    else
    {
        tty_cursor_activity();
    }
}


/* Write buffer */
void tty_write(const char* str, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        tty_putchar(str[i]);
    }
}


/* Print string */
void tty_print(const char* str)
{
    while (*str)
    {
        tty_putchar(*str++);
    }
    
    if (use_fb && !batch_mode)
        fb_present();
}


/* Print a 64-bit hex value */
void tty_print_hex(uint64_t value)
{
    char hex[] = "0123456789ABCDEF";
    tty_print("0x");
    
    int started = 0;
    for (int i = 60; i >= 0; i -= 4)
    {
        int digit = (value >> i) & 0xF;
        if (digit != 0 || started || i == 0)
        {
            tty_putchar(hex[digit]);
            started = 1;
        }
    }
    
    if (use_fb && !batch_mode)
        fb_present();
}


/* Batch drawing mode — suppresses per-character fb_present() calls */
void tty_begin_batch(void)
{
    batch_mode = 1;
}

void tty_end_batch(void)
{
    batch_mode = 0;
    if (use_fb)
        fb_present();
}


/* Fancy logging functions */
void log_init(const char* msg)
{
    uint8_t old_fg = cur_fg;
    tty_set_color(TTY_LIGHT_CYAN, cur_bg);
    tty_print("[INIT]    ");
    tty_set_color(old_fg, cur_bg);
    tty_print(msg);
    tty_print("\n");
}

void log_debug(const char* msg)
{
    uint8_t old_fg = cur_fg;
    tty_set_color(TTY_LIGHT_GREY, cur_bg);
    tty_print("[DEBUG]   ");
    tty_set_color(old_fg, cur_bg);
    tty_print(msg);
    tty_print("\n");
}

void log_set(const char* msg)
{
    uint8_t old_fg = cur_fg;
    tty_set_color(TTY_MAGENTA, cur_bg);
    tty_print("[SET]     ");
    tty_set_color(old_fg, cur_bg);
    tty_print(msg);
    tty_print("\n");
}

void log_get(const char* msg)
{
    uint8_t old_fg = cur_fg;
    tty_set_color(TTY_CYAN, cur_bg);
    tty_print("[GET]     ");
    tty_set_color(old_fg, cur_bg);
    tty_print(msg);
    tty_print("\n");
}

void log_success(const char* msg)
{
    uint8_t old_fg = cur_fg;
    tty_set_color(TTY_GREEN, cur_bg);
    tty_print("[SUCCESS] ");
    tty_set_color(old_fg, cur_bg);
    tty_print(msg);
    tty_print("\n");
}

void log_warn(const char* msg)
{
    uint8_t old_fg = cur_fg;
    tty_set_color(TTY_BROWN, cur_bg);
    tty_print("[WARN]    ");
    tty_set_color(old_fg, cur_bg);
    tty_print(msg);
    tty_print("\n");
}

void log_error(const char* msg)
{
    uint8_t old_fg = cur_fg;
    tty_set_color(TTY_RED, cur_bg);
    tty_print("[ERROR]   ");
    tty_set_color(old_fg, cur_bg);
    tty_print(msg);
    tty_print("\n");
}


/* Set cursor position */
void tty_set_cursor(size_t x, size_t y)
{
    size_t max_x = use_fb ? fb_cols : VGA_WIDTH;
    size_t max_y = use_fb ? fb_rows : VGA_HEIGHT;

    if (x < max_x)
        cursor_x = x;
    
    if (y < max_y)
        cursor_y = y;

    tty_cursor_activity();
}


/* Get cursor position */
void tty_get_cursor(size_t* x, size_t* y)
{
    if (x) *x = cursor_x;
    if (y) *y = cursor_y;
}


/* Show hardware cursor */
void tty_show_cursor(void)
{
    cursor_visible = 1;
    cursor_blink_on = 1;
    cursor_blink_last_ms = timer_get_ms();
    
    // Enable cursor (scan line 0-15)
    outb(VGA_CTRL_PORT, 0x0A);
    outb(VGA_DATA_PORT, (inb(VGA_DATA_PORT) & 0xC0) | 0x00);
    
    outb(VGA_CTRL_PORT, 0x0B);
    outb(VGA_DATA_PORT, (inb(VGA_DATA_PORT) & 0xE0) | 0x0F);
    
    tty_cursor_refresh(1);
}


/* Hide hardware cursor */
void tty_hide_cursor(void)
{
    tty_cursor_erase_fb();
    cursor_visible = 0;
    
    // Disable cursor
    outb(VGA_CTRL_PORT, 0x0A);
    outb(VGA_DATA_PORT, 0x20);

    if (use_fb)
        fb_present();
}


/* Add command to history */
static void history_add(const char* cmd)
{
    // Don't add empty commands or duplicates of last command
    if (!cmd || cmd[0] == '\0')
        return;
    
    // Check for duplicate of last command
    if (history_count > 0)
    {
        int last_idx = (history_write_idx - 1 + HISTORY_SIZE) % HISTORY_SIZE;
        int same = 1;
        for (int i = 0; cmd[i] != '\0' || history[last_idx][i] != '\0'; i++)
        {
            if (cmd[i] != history[last_idx][i])
            {
                same = 0;
                break;
            }
        }
        if (same)
            return;
    }
    
    // Copy command to history
    int i;
    for (i = 0; i < HISTORY_LINE_LEN - 1 && cmd[i] != '\0'; i++)
        history[history_write_idx][i] = cmd[i];
    history[history_write_idx][i] = '\0';
    
    history_write_idx = (history_write_idx + 1) % HISTORY_SIZE;
    if (history_count < HISTORY_SIZE)
        history_count++;
}


/* Clear current line from start position */
static void clear_input_line(size_t start_x, size_t start_y, size_t len)
{
    /* Backend-safe clear path for both VGA and framebuffer modes. */
    tty_set_cursor(start_x, start_y);
    for (size_t i = 0; i < len; i++)
        tty_putchar(' ');
    tty_set_cursor(start_x, start_y);
}


/* Read a line of input with history support */
char* tty_readline(char* buffer, size_t max_len)
{
    size_t pos = 0;
    size_t start_x = cursor_x;
    size_t start_y = cursor_y;
    
    // Reset history browse position
    history_browse_idx = history_write_idx;
    
    // Temporary buffer for current input (before browsing history)
    char temp_buffer[HISTORY_LINE_LEN];
    temp_buffer[0] = '\0';
    int saved_current = 0;
    
    while (1)
    {
        tty_cursor_blink_tick();

        unsigned char c = (unsigned char)keyboard_getchar_nonblocking();
        if (c == 0)
        {
            __asm__ volatile ("sti; hlt");
            continue;
        }

        tty_cursor_activity();

        if (c == 0x03)
        {
            /* Ctrl+C: abort current line input and return empty command. */
            buffer[0] = '\0';
            tty_print("^C");
            tty_newline();
            return buffer;
        }
        
        if (c == '\n')
        {
            // Enter pressed - end input
            buffer[pos] = '\0';
            
            // Add to history if non-empty
            if (pos > 0)
                history_add(buffer);
            
            tty_newline();
            return buffer;
        }
        else if (c == '\b')
        {
            // Backspace - delete character
            if (pos > 0)
            {
                pos--;
                tty_putchar('\b');
            }
        }
        else if (c == KEY_SPECIAL_UP)
        {
            // Up arrow - previous command in history
            if (history_count > 0)
            {
                // Save current input if not already browsing
                if (!saved_current)
                {
                    for (size_t i = 0; i < pos && i < HISTORY_LINE_LEN - 1; i++)
                        temp_buffer[i] = buffer[i];
                    temp_buffer[pos] = '\0';
                    saved_current = 1;
                }
                
                // Calculate previous index
                int prev_idx = (history_browse_idx - 1 + HISTORY_SIZE) % HISTORY_SIZE;
                
                // Check if we have more history to browse
                int start_idx = (history_write_idx - history_count + HISTORY_SIZE) % HISTORY_SIZE;
                if (history_browse_idx != start_idx)
                {
                    history_browse_idx = prev_idx;
                    
                    // Clear current line
                    clear_input_line(start_x, start_y, pos);
                    
                    // Copy history entry to buffer
                    pos = 0;
                    while (history[history_browse_idx][pos] != '\0' && pos < max_len - 1)
                    {
                        buffer[pos] = history[history_browse_idx][pos];
                        tty_putchar(buffer[pos]);
                        pos++;
                    }
                    buffer[pos] = '\0';
                }
            }
        }
        else if (c == KEY_SPECIAL_DOWN)
        {
            // Down arrow - next command in history
            if (saved_current)
            {
                if (history_browse_idx != history_write_idx)
                {
                    int next_idx = (history_browse_idx + 1) % HISTORY_SIZE;
                    
                    // Clear current line
                    clear_input_line(start_x, start_y, pos);
                    
                    if (next_idx == history_write_idx)
                    {
                        // Restore original input
                        history_browse_idx = history_write_idx;
                        pos = 0;
                        while (temp_buffer[pos] != '\0' && pos < max_len - 1)
                        {
                            buffer[pos] = temp_buffer[pos];
                            tty_putchar(buffer[pos]);
                            pos++;
                        }
                        buffer[pos] = '\0';
                        saved_current = 0;
                    }
                    else
                    {
                        history_browse_idx = next_idx;
                        
                        // Copy history entry to buffer
                        pos = 0;
                        while (history[history_browse_idx][pos] != '\0' && pos < max_len - 1)
                        {
                            buffer[pos] = history[history_browse_idx][pos];
                            tty_putchar(buffer[pos]);
                            pos++;
                        }
                        buffer[pos] = '\0';
                    }
                }
            }
        }
        else if (c >= 32 && c < 127)
        {
            // Printable character
            if (pos < max_len - 1)
            {
                buffer[pos++] = c;
                tty_putchar(c);
            }
        }
        // Ignore other characters
    }
}

#endif /* ARCH_ARM64 */
