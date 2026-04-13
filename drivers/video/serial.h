// serial.h
#ifndef SERIAL_H
#define SERIAL_H

#include "../../lib/libc/int.h"

void serial_init(void);
void serial_putchar(char c);
void serial_write(const char* str);
void serial_hex(uint64_t value);
void serial_int(int64_t value);
void serial_log(const char* str);
void serial_error(const char* str); 
void serial_warn(const char* str);

static inline void serial_log_hex(const char* label, uint64_t value) {
    serial_write(label);
    serial_write(": 0x");
    serial_hex(value);
    serial_write("\n");
}

static inline void serial_log_int(const char* label, uint64_t value) {
    serial_write(label);
    serial_write(": ");
    serial_int(value);
    serial_write("\n");
}

static inline void serial_log_str(const char* label, const char* str) {
    serial_write(label);
    serial_write(": ");
    serial_write(str);
    serial_write("\n");
}

// Convenience macros for logging
#define SERIAL_LOG(msg) serial_write("[LOG] "); serial_write(msg); serial_write("\n")
#define SERIAL_WARN(msg) serial_write("[WARN] "); serial_write(msg); serial_write("\n")
#define SERIAL_ERROR(msg) serial_write("[ERROR] "); serial_write(msg); serial_write("\n")
#define SERIAL_HEX(label, value) serial_write(label); serial_write(": 0x"); serial_hex(value); serial_write("\n")
#define SERIAL_INT(label, value) serial_write(label); serial_write(": "); serial_int(value); serial_write("\n")

#endif