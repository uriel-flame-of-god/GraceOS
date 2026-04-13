// ============================
// Lumina Kernel Logger
// Standardized logging system
// ============================

#include "klog.h"
#include "../../drivers/video/tty.h"
#include "../arch/x86_64/io/port.h"

// Serial port (COM1) for debug output
#define SERIAL_PORT 0x3F8

static int serial_initialized = 0;

/* Initialize serial port */
static void serial_init(void)
{
    if (serial_initialized)
        return;
    
    outb(SERIAL_PORT + 1, 0x00);  // Disable interrupts
    outb(SERIAL_PORT + 3, 0x80);  // Enable DLAB
    outb(SERIAL_PORT + 0, 0x03);  // Set divisor to 3 (38400 baud)
    outb(SERIAL_PORT + 1, 0x00);  // Hi byte
    outb(SERIAL_PORT + 3, 0x03);  // 8 bits, no parity, one stop bit
    outb(SERIAL_PORT + 2, 0xC7);  // Enable FIFO
    outb(SERIAL_PORT + 4, 0x0B);  // IRQs enabled, RTS/DSR set
    
    serial_initialized = 1;
}

/* Write a character to serial */
static void serial_putchar(char c)
{
    if (!serial_initialized)
        serial_init();
    
    // Wait for transmit buffer empty
    while ((inb(SERIAL_PORT + 5) & 0x20) == 0) {}
    outb(SERIAL_PORT, c);
}

/* Write a string to serial */
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

/* Write hex to serial */
static void serial_hex(uint64_t value)
{
    const char* hex = "0123456789ABCDEF";
    serial_print("0x");
    
    int started = 0;
    for (int i = 60; i >= 0; i -= 4)
    {
        int digit = (value >> i) & 0xF;
        if (digit != 0 || started || i == 0)
        {
            serial_putchar(hex[digit]);
            started = 1;
        }
    }
}

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
}

/* Allocation messages */
void klog_get(const char* msg)
{
    print_tag("GET  ");
    tty_print(msg);
    tty_print("\n");
    serial_print(msg);
    serial_print("\n");
}

/* Free / Mark used messages */
void klog_set(const char* msg)
{
    print_tag("SET  ");
    tty_print(msg);
    tty_print("\n");
    serial_print(msg);
    serial_print("\n");
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
}

/* Debug info messages */
void klog_log(const char* msg)
{
    tty_print(msg);
    serial_print(msg);
}

/* Debug info messages with newline */
void klog_logn(const char* msg)
{
    tty_print(msg);
    tty_print("\n");
    serial_print(msg);
    serial_print("\n");
}

/* Print hex value inline (no newline) */
void klog_hex(uint64_t value)
{
    tty_print_hex(value);
    serial_hex(value);
}
