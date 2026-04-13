// drivers/serial/serial.c - Simple serial port driver for debugging
#include "serial.h"
#include "../../kernel/arch/x86_64/io/port.h"

#define COM1_PORT 0x3F8

// Initialize serial port
void serial_init(void)
{
    outb(COM1_PORT + 1, 0x00);    // Disable interrupts
    outb(COM1_PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(COM1_PORT + 0, 0x03);    // Set divisor to 3 (38400 baud)
    outb(COM1_PORT + 1, 0x00);    // High byte of divisor
    outb(COM1_PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(COM1_PORT + 2, 0xC7);    // Enable FIFO, clear, 14-byte threshold
    outb(COM1_PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

// Check if transmitter is empty
static int serial_is_transmit_empty(void)
{
    return inb(COM1_PORT + 5) & 0x20;
}

// Write a character to serial port
void serial_putchar(char c)
{
    while (serial_is_transmit_empty() == 0);
    outb(COM1_PORT, c);
}

// Write a string to serial port
void serial_write(const char* str)
{
    while (*str) {
        serial_putchar(*str++);
    }
}

void serial_log(const char* str) {
    serial_write("[LOG] ");
    serial_write(str);
    serial_write("\n");
}

void serial_error(const char* str) {
    serial_write("[ERROR] ");
    serial_write(str);
    serial_write("\n");
}

void serial_warn(const char* str) {
    serial_write("[WARN] ");
    serial_write(str);
    serial_write("\n");
}

// Convert integer to hex string and send
void serial_hex(uint64_t value)
{
    char hex_chars[] = "0123456789ABCDEF";
    char buffer[19] = "0x";
    
    for (int i = 15; i >= 0; i--) {
        buffer[17 - i] = hex_chars[(value >> (i * 4)) & 0xF];
    }
    buffer[18] = '\0';
    
    serial_write(buffer);
}

// Convert integer to decimal and send
void serial_int(int64_t value)
{
    char buffer[21];
    int i = 0;
    int is_negative = 0;
    
    if (value < 0) {
        is_negative = 1;
        value = -value;
    }
    
    // Handle zero specially
    if (value == 0) {
        buffer[i++] = '0';
    } else {
        // Generate digits in reverse order
        while (value > 0) {
            buffer[i++] = '0' + (value % 10);
            value /= 10;
        }
    }
    
    if (is_negative) {
        buffer[i++] = '-';
    }
    
    // Reverse the string
    for (int j = 0; j < i / 2; j++) {
        char temp = buffer[j];
        buffer[j] = buffer[i - j - 1];
        buffer[i - j - 1] = temp;
    }
    
    buffer[i] = '\0';
    serial_write(buffer);
}