// ============================
// GraceOS I/O Port Abstraction
// 8/16/32-bit port operations
// ============================

#ifndef GRACEOS_PORT_H
#define GRACEOS_PORT_H

#include "../../../../lib/libc/int.h"

/* 8-bit I/O */
void outb(uint16_t port, uint8_t val);
uint8_t inb(uint16_t port);

/* 16-bit I/O */
void outw(uint16_t port, uint16_t val);
uint16_t inw(uint16_t port);

/* 32-bit I/O */
void outl(uint16_t port, uint32_t val);
uint32_t inl(uint16_t port);

/* I/O wait (for slow devices) */
void io_wait(void);

#endif /* GRACEOS_PORT_H */
