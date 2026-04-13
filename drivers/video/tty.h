#ifndef GRACEOS_TTY_H
#define GRACEOS_TTY_H

#include "../../lib/libc/int.h"

/* ============================
   TTY (Terminal) Driver
   ============================ */

/* TTY Colors */
enum tty_color
{
    TTY_BLACK = 0,
    TTY_BLUE = 1,
    TTY_GREEN = 2,
    TTY_CYAN = 3,
    TTY_RED = 4,
    TTY_MAGENTA = 5,
    TTY_BROWN = 6,
    TTY_LIGHT_GREY = 7,
    TTY_DARK_GREY = 8,
    TTY_LIGHT_BLUE = 9,
    TTY_LIGHT_GREEN = 10,
    TTY_LIGHT_CYAN = 11,
    TTY_LIGHT_RED = 12,
    TTY_LIGHT_MAGENTA = 13,
    TTY_YELLOW = 14,
    TTY_WHITE = 15,
};

/* API Functions */
void tty_init(void);
void tty_clear(void);

void tty_set_color(uint8_t fg, uint8_t bg);
void tty_get_color(uint8_t* fg, uint8_t* bg);

void tty_putchar(char c);
void tty_write(const char* str, size_t len);
void tty_print(const char* str);
void tty_print_hex(uint64_t value);

/* Fancy logging functions */
void log_init(const char* msg);
void log_debug(const char* msg);
void log_set(const char* msg);
void log_get(const char* msg);
void log_success(const char* msg);
void log_warn(const char* msg);
void log_error(const char* msg);

void tty_set_cursor(size_t x, size_t y);
void tty_get_cursor(size_t* x, size_t* y);
void tty_show_cursor(void);
void tty_hide_cursor(void);

/* Batch drawing — suppresses per-character fb_present() calls in framebuffer mode.
   Call tty_begin_batch() before bulk output, tty_end_batch() to flush once. */
void tty_begin_batch(void);
void tty_end_batch(void);

void tty_scroll(void);
void tty_newline(void);

/* Input functions */
char* tty_readline(char* buffer, size_t max_len);

#endif /* GRACEOS_TTY_H */
