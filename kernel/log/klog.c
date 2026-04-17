// ============================
// Lumina Kernel Logger
// Standardized logging system
// ============================

#include "klog.h"
#include "../../drivers/video/tty.h"

#ifndef ARCH_ARM64
#include "../arch/x86_64/io/port.h"
// COM1 serial port for x86 debug output
#define SERIAL_PORT 0x3F8
static int serial_initialized = 0;
#else
#include "../../drivers/video/uart_rpi.h"
#endif

/* ============================
   Kernel Log Ring Buffer
   Circular character buffer.  All klog_* functions write here in addition
   to the TTY and serial port.  The logger service drains it every 5 s.

   No locking is used — a concurrent write may produce a garbled line, which
   is acceptable for a diagnostic log.  klog_write_pos is a monotonically
   increasing byte counter; the actual ring index is pos % KLOG_RING_SIZE.
   ============================ */

static char     klog_ring[KLOG_RING_SIZE];
static uint64_t klog_write_pos = 0;

static void ring_putchar(char c)
{
    klog_ring[klog_write_pos % KLOG_RING_SIZE] = c;
    klog_write_pos++;
}

static void ring_puts(const char* s)
{
    while (*s)
        ring_putchar(*s++);
}

/* Write one complete log entry ("[TAG] msg\n") to the ring. */
static void ring_write_entry(const char* tag, const char* msg)
{
    ring_putchar('[');
    ring_puts(tag);
    ring_putchar(']');
    ring_putchar(' ');
    ring_puts(msg);
    ring_putchar('\n');
}

/* ============================
   Public ring buffer API
   ============================ */

int klog_read_new(uint64_t* pos, char* buf, int size)
{
    if (!pos || !buf || size < 2)
        return 0;

    uint64_t write = klog_write_pos;

    if (write <= *pos)
        return 0;   /* no new data */

    uint64_t available = write - *pos;

    /* If we've fallen a full ring behind, skip to oldest available data */
    if (available > (uint64_t)KLOG_RING_SIZE)
    {
        *pos      = write - (uint64_t)KLOG_RING_SIZE;
        available = (uint64_t)KLOG_RING_SIZE;
    }

    int to_copy = (int)(available < (uint64_t)(size - 1)
                        ? available
                        : (uint64_t)(size - 1));

    for (int i = 0; i < to_copy; i++)
        buf[i] = klog_ring[(*pos + (uint64_t)i) % KLOG_RING_SIZE];

    buf[to_copy] = '\0';
    *pos        += (uint64_t)to_copy;
    return to_copy;
}

uint64_t klog_get_write_pos(void)
{
    return klog_write_pos;
}

#ifndef ARCH_ARM64
/* Initialize COM1 serial port (x86 only) */
static void serial_init(void)
{
    if (serial_initialized)
        return;

    outb(SERIAL_PORT + 1, 0x00);  // Disable interrupts
    outb(SERIAL_PORT + 3, 0x80);  // Enable DLAB
    outb(SERIAL_PORT + 0, 0x03);  // Divisor = 3 (38400 baud)
    outb(SERIAL_PORT + 1, 0x00);  // Hi byte
    outb(SERIAL_PORT + 3, 0x03);  // 8 bits, no parity, one stop bit
    outb(SERIAL_PORT + 2, 0xC7);  // Enable FIFO
    outb(SERIAL_PORT + 4, 0x0B);  // IRQs enabled, RTS/DSR set

    serial_initialized = 1;
}

static void serial_putchar(char c)
{
    if (!serial_initialized)
        serial_init();
    while ((inb(SERIAL_PORT + 5) & 0x20) == 0) {}
    outb(SERIAL_PORT, c);
}

static void serial_print(const char* str)
{
    if (!serial_initialized)
        serial_init();
    while (*str)
    {
        if (*str == '\n')
            serial_putchar('\r');
        serial_putchar(*str++);
    }
}

static void serial_hex(uint64_t value)
{
    const char* hex = "0123456789ABCDEF";
    serial_print("0x");
    int started = 0;
    for (int i = 60; i >= 0; i -= 4)
    {
        int digit = (value >> i) & 0xF;
        if (digit || started || i == 0)
        {
            serial_putchar(hex[digit]);
            started = 1;
        }
    }
}

#else  /* ARCH_ARM64 — serial = RPi UART */

static void serial_init(void)    { /* uart_init() called by tty_init() */ }
static void serial_putchar(char c)        { uart_putchar(c); }
static void serial_print(const char* s)   { uart_print(s); }
static void serial_hex(uint64_t value)
{
    const char* hex = "0123456789ABCDEF";
    uart_print("0x");
    int started = 0;
    for (int i = 60; i >= 0; i -= 4)
    {
        int digit = (int)((value >> i) & 0xF);
        if (digit || started || i == 0)
        {
            uart_putchar(hex[digit]);
            started = 1;
        }
    }
}

#endif /* ARCH_ARM64 */

/* Print a colored tag */
static void print_tag(const char* tag)
{
    tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
    tty_print("[");
    tty_print(tag);
    tty_print("] ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    
    // Also to serial
    serial_print("[");
    serial_print(tag);
    serial_print("] ");
}

/* Initialize logging system */
void klog_init(void)
{
    serial_init();
}

/* Startup messages */
void klog_init_msg(const char* msg)
{
    print_tag("INIT ");
    tty_print(msg);
    tty_print("\n");
    serial_print(msg);
    serial_print("\n");
    ring_write_entry("INIT ", msg);
}

/* Allocation messages */
void klog_get(const char* msg)
{
    print_tag("GET  ");
    tty_print(msg);
    tty_print("\n");
    serial_print(msg);
    serial_print("\n");
    ring_write_entry("GET  ", msg);
}

/* Free / Mark used messages */
void klog_set(const char* msg)
{
    print_tag("SET  ");
    tty_print(msg);
    tty_print("\n");
    serial_print(msg);
    serial_print("\n");
    ring_write_entry("SET  ", msg);
}

/* Warning messages */
void klog_warn(const char* msg)
{
    tty_set_color(TTY_YELLOW, TTY_BLACK);
    tty_print("[WARN ] ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    tty_print(msg);
    tty_print("\n");
    serial_print("[WARN ] ");
    serial_print(msg);
    serial_print("\n");
    ring_write_entry("WARN ", msg);
}

/* Error messages (recoverable) */
void klog_error(const char* msg)
{
    tty_set_color(TTY_LIGHT_RED, TTY_BLACK);
    tty_print("[ERROR] ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    tty_print(msg);
    tty_print("\n");
    serial_print("[ERROR] ");
    serial_print(msg);
    serial_print("\n");
    ring_write_entry("ERROR", msg);
}

/* Fatal failure messages */
void klog_fail(const char* msg)
{
    tty_set_color(TTY_RED, TTY_BLACK);
    tty_print("[FAIL ] ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    tty_print(msg);
    tty_print("\n");
    serial_print("[FAIL ] ");
    serial_print(msg);
    serial_print("\n");
    ring_write_entry("FAIL ", msg);
}

/* Debug info messages */
void klog_log(const char* msg)
{
    tty_print(msg);
    serial_print(msg);
    ring_puts(msg);   /* no tag, no newline — raw inline text */
}

/* Debug info messages with newline */
void klog_logn(const char* msg)
{
    tty_print(msg);
    tty_print("\n");
    serial_print(msg);
    serial_print("\n");
    ring_write_entry("LOG  ", msg);
}

/* Print hex value inline (no newline) */
void klog_hex(uint64_t value)
{
    tty_print_hex(value);
    serial_hex(value);
    /* Emit hex into ring as well */
    const char* hex_chars = "0123456789ABCDEF";
    ring_putchar('0'); ring_putchar('x');
    int started = 0;
    for (int i = 60; i >= 0; i -= 4) {
        int digit = (int)((value >> i) & 0xF);
        if (digit != 0 || started || i == 0) {
            ring_putchar(hex_chars[digit]);
            started = 1;
        }
    }
}
