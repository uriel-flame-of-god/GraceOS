#ifndef GRACEOS_ARM64_IO_PORT_H
#define GRACEOS_ARM64_IO_PORT_H

// ============================
// GraceOS ARM64 MMIO Helpers
// ============================
//
// AArch64 has no x86-style I/O port instructions (IN/OUT).
// All RPi peripheral access uses Memory-Mapped I/O (MMIO).
//
// These helpers provide:
//   mmio_read8 / mmio_write8    — byte access
//   mmio_read16 / mmio_write16  — halfword access
//   mmio_read32 / mmio_write32  — word access (most common on RPi)
//   mmio_read64 / mmio_write64  — doubleword access
//
// dmb() / dsb() / isb() inline barriers are also provided for
// ordering memory-mapped accesses around peripheral registers.
//
// NOTE: On ARM64, peripheral registers require Device memory ordering.
//       All RPi peripheral regions are mapped as Device-nGnRnE
//       (non-Gathering, non-Reordering, no Early-Write-Ack) by
//       the MMU.  Until the MMU is enabled, the CPU issues all
//       accesses as strongly-ordered Device memory by default.

#include "../../../../lib/libc/int.h"

// ----------------------------------------------------------------
// Memory barrier helpers
// ----------------------------------------------------------------

/* Data Memory Barrier — ensures all previous memory accesses complete
   before any new accesses start in program order. */
static inline void dmb(void)
{
    __asm__ volatile ("dmb sy" ::: "memory");
}

/* Data Synchronization Barrier — stricter than DMB: all preceding
   instructions complete before any new instruction is issued. */
static inline void dsb(void)
{
    __asm__ volatile ("dsb sy" ::: "memory");
}

/* Instruction Synchronization Barrier — flushes the instruction
   pipeline.  Required after changing system registers. */
static inline void isb_barrier(void)
{
    __asm__ volatile ("isb" ::: "memory");
}

// ----------------------------------------------------------------
// MMIO accessors — volatile prevents compiler reordering
// ----------------------------------------------------------------

static inline uint8_t mmio_read8(uint64_t addr)
{
    return *(volatile uint8_t*)addr;
}

static inline void mmio_write8(uint64_t addr, uint8_t val)
{
    *(volatile uint8_t*)addr = val;
}

static inline uint16_t mmio_read16(uint64_t addr)
{
    return *(volatile uint16_t*)addr;
}

static inline void mmio_write16(uint64_t addr, uint16_t val)
{
    *(volatile uint16_t*)addr = val;
}

static inline uint32_t mmio_read32(uint64_t addr)
{
    return *(volatile uint32_t*)addr;
}

static inline void mmio_write32(uint64_t addr, uint32_t val)
{
    *(volatile uint32_t*)addr = val;
}

static inline uint64_t mmio_read64(uint64_t addr)
{
    return *(volatile uint64_t*)addr;
}

static inline void mmio_write64(uint64_t addr, uint64_t val)
{
    *(volatile uint64_t*)addr = val;
}

// ----------------------------------------------------------------
// Compatibility stubs
// Code that is compiled for BOTH x86 and ARM64 (guarded with
// #ifdef ARCH_ARM64) can call these stubs instead of inb/outb so
// the source compiles cleanly on either architecture.
// DO NOT call these from code that needs real x86 port I/O.
// ----------------------------------------------------------------

static inline void outb(uint16_t port, uint8_t val)
{
    (void)port; (void)val;
    /* No-op: x86 port I/O does not exist on AArch64 */
}

static inline uint8_t inb(uint16_t port)
{
    (void)port;
    return 0;
}

static inline void outw(uint16_t port, uint16_t val)
{
    (void)port; (void)val;
}

static inline uint16_t inw(uint16_t port)
{
    (void)port;
    return 0;
}

static inline void outl(uint16_t port, uint32_t val)
{
    (void)port; (void)val;
}

static inline uint32_t inl(uint16_t port)
{
    (void)port;
    return 0;
}

#endif /* GRACEOS_ARM64_IO_PORT_H */
