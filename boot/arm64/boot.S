// ============================
// GraceOS ARM64 Boot Entry
// Raspberry Pi 3 / 4 (AArch64)
// ============================
//
// The RPi firmware (start.elf) loads kernel8.img at physical 0x80000
// and jumps to it on ALL 4 cores simultaneously.
//
// On entry from firmware:
//   x0  = DTB pointer (or 0 on older builds — ignored here)
//   EL  = EL2 (typical) or EL1 depending on firmware
//   MMU = off, caches = off, interrupts = masked
//
// Core 0 becomes the boot core; cores 1-3 are parked in WFE.

.section ".text.boot", "ax"
.global _start

_start:
    // Which core are we?  MPIDR_EL1 Aff0 = bits [7:0]
    mrs     x1, mpidr_el1
    and     x1, x1, #0xFF
    cbnz    x1, .park_core      // Non-zero core ID → park it

    // ----------------------------------------------------------------
    // Core 0: determine current exception level
    // CurrentEL bits [3:2]: 2 = EL1, 4 = EL2 (after >> 2: 1 or 2)
    // ----------------------------------------------------------------
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #2
    b.eq    .drop_to_el1
    b       .at_el1             // Already EL1 (unusual but handle it)

.drop_to_el1:
    // HCR_EL2.RW = 1  →  EL1 runs in AArch64
    mov     x0, #(1 << 31)
    msr     hcr_el2, x0

    // SCTLR_EL1: clear all bits (MMU off, caches off, no alignment check)
    msr     sctlr_el1, xzr
    isb

    // SPSR_EL2: M[3:0]=0101 (EL1h) | DAIF masked = 0x3c5
    mov     x0, #0x3c5
    msr     spsr_el2, x0

    // ELR_EL2 points to .at_el1 so eret drops us there
    adr     x0, .at_el1
    msr     elr_el2, x0

    // Flush caches / TLBs before the exception level change
    ic      iallu
    tlbi    vmalle1
    dsb     sy
    isb

    eret                        // → EL1

.at_el1:
    // ----------------------------------------------------------------
    // EL1 setup
    // ----------------------------------------------------------------

    // Use SP_EL1 (not SP_EL0) — set SPSel = 1
    msr     spsel, #1

    // Load the kernel stack (defined by the linker script)
    ldr     x0, =stack_top
    mov     sp, x0

    // Install the exception vector table (defined in exceptions.S)
    ldr     x0, =exception_vectors
    msr     vbar_el1, x0
    isb

    // Enable FP / SIMD (NEON) — CPACR_EL1.FPEN = 0b11 (bits [21:20])
    mov     x0, #(3 << 20)
    msr     cpacr_el1, x0
    isb

    // Zero the BSS section (boot.S is responsible before C runs)
    ldr     x1, =__bss_start
    ldr     x2, =__bss_end
.zero_bss:
    cmp     x1, x2
    b.ge    .bss_done
    str     xzr, [x1], #8      // Zero 8 bytes, advance pointer
    b       .zero_bss
.bss_done:

    // x0 = 0  (no multiboot struct on RPi)
    mov     x0, xzr

    // Jump into the C kernel — must not return
    bl      kmain

.hang:
    wfe
    b       .hang

// ----------------------------------------------------------------
// Secondary cores: spin in WFE forever (SMP parking)
// Could be woken later for SMP use — not implemented yet
// ----------------------------------------------------------------
.park_core:
    wfe
    b       .park_core
