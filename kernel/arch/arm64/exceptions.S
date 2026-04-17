// ============================
// GraceOS ARM64 Exception Vectors
// ============================
//
// AArch64 exception vector table layout (VMSA spec):
//
//  Offset   Group                              Entry
//  +0x000   Current EL, SP0   Synchronous
//  +0x080   Current EL, SP0   IRQ / vIRQ
//  +0x100   Current EL, SP0   FIQ / vFIQ
//  +0x180   Current EL, SP0   SError / vSError
//  +0x200   Current EL, SPx   Synchronous
//  +0x280   Current EL, SPx   IRQ / vIRQ
//  +0x300   Current EL, SPx   FIQ / vFIQ
//  +0x380   Current EL, SPx   SError / vSError
//  +0x400   Lower EL AArch64  Synchronous
//  +0x480   Lower EL AArch64  IRQ / vIRQ
//  +0x500   Lower EL AArch64  FIQ / vFIQ
//  +0x580   Lower EL AArch64  SError / vSError
//  +0x600   Lower EL AArch32  Synchronous
//  +0x680   Lower EL AArch32  IRQ / vIRQ
//  +0x700   Lower EL AArch32  FIQ / vFIQ
//  +0x780   Lower EL AArch32  SError / vSError
//
// Each slot is 128 bytes (32 instructions maximum).
// VBAR_EL1 must be 2 KB aligned — enforced by .text.vectors in linker.ld.
//
// All handlers are stubs for now (halt loop).
// Real handlers will be wired in when interrupts are needed.

// Helper macro: emit one 128-byte exception vector entry
.macro vector_entry name
.balign 128
\name:
    // Save minimal context for debugging (x0, x1 scratch)
    b       \name           // Hang — prevents silent corruption
.endm

.section ".text.vectors", "ax"
.balign 2048
.global exception_vectors

exception_vectors:

    // ---- Current EL with SP0 ----
    vector_entry exc_sp0_sync
    vector_entry exc_sp0_irq
    vector_entry exc_sp0_fiq
    vector_entry exc_sp0_serror

    // ---- Current EL with SPx ----
    vector_entry exc_spx_sync
    vector_entry exc_spx_irq
    vector_entry exc_spx_fiq
    vector_entry exc_spx_serror

    // ---- Lower EL (AArch64) ----
    vector_entry exc_el0_64_sync
    vector_entry exc_el0_64_irq
    vector_entry exc_el0_64_fiq
    vector_entry exc_el0_64_serror

    // ---- Lower EL (AArch32) ----
    vector_entry exc_el0_32_sync
    vector_entry exc_el0_32_irq
    vector_entry exc_el0_32_fiq
    vector_entry exc_el0_32_serror
