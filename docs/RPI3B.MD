# GraceOS — Raspberry Pi 3B (ARM64) Port

> **Status as of Kernel v0.3.5 (2026-04-17): WORK IN PROGRESS — currently broken.**
> Boots to banner and accepts UART I/O in QEMU (`make ARCH=arm64 run-arm64`).
> Real-hardware input, full framebuffer stability, interrupts, and the syscall
> layer are not yet implemented. Do not use this port in production.

This document covers every detail of the Raspberry Pi 3B bring-up: hardware assumptions,
new source files, changes to shared files, the build system, and what remains
x86\_64-only. Read [ARCHITECTURE.MD](ARCHITECTURE.MD) for the overall kernel design.

---

## Table of Contents

1. [Target Hardware](#1-target-hardware)
2. [High-Level Architecture](#2-high-level-architecture)
3. [New Files](#3-new-files)
4. [Changed Shared Files](#4-changed-shared-files)
5. [Boot Sequence](#5-boot-sequence)
6. [Linker Script](#6-linker-script)
7. [UART Driver](#7-uart-driver)
8. [Framebuffer Console](#8-framebuffer-console)
9. [Mailbox Interface](#9-mailbox-interface)
10. [Power Management](#10-power-management)
11. [Exception Vectors](#11-exception-vectors)
12. [Context Switch](#12-context-switch)
13. [MMIO Helpers](#13-mmio-helpers)
14. [TTY Layer](#14-tty-layer)
15. [Keyboard / Input](#15-keyboard--input)
16. [Kernel Entry (kmain\_arm64)](#16-kernel-entry-kmain_arm64)
17. [Build System](#17-build-system)
18. [SD Card Setup](#18-sd-card-setup)
19. [config.txt Reference](#19-configtxt-reference)
20. [QEMU Testing](#20-qemu-testing)
21. [What is Not Yet Ported](#21-what-is-not-yet-ported)
22. [Pi 4 Differences](#22-pi-4-differences)

---

## 1. Target Hardware

| Property | Value |
|---|---|
| Board | Raspberry Pi 3 Model B / 3B+ |
| SoC | Broadcom BCM2837 |
| CPU | Cortex-A53, quad-core, AArch64 |
| Peripheral base | `0x3F000000` |
| Firmware load address | `0x80000` |
| Output binary | `kernel8.img` (flat binary, no ELF header) |
| Default console | UART (mini-UART on GPIO 14/15) |
| Optional console | PL011 UART (requires Bluetooth disabled in config.txt) |
| Framebuffer | Via GPU mailbox at 1024×768, 32 bpp |

The RPi firmware (`start.elf`) executes at power-on, loads `kernel8.img` from the
SD card into RAM at physical address `0x80000`, and jumps to it simultaneously on
all four Cortex-A53 cores.

---

## 2. High-Level Architecture

```
boot/arm64/boot.S          — AArch64 entry, EL2→EL1 drop, BSS zero, stack setup
kernel/core/kmain_arm64.c  — ARM64 kernel main (replaces kmain.c on this arch)
kernel/arch/arm64/         — Architecture-specific: linker, exceptions, context, mailbox, power, MMIO
drivers/video/uart_rpi.c   — mini-UART / PL011 UART driver
drivers/video/fb_console.c — Framebuffer text console via GPU mailbox
drivers/video/tty.c        — Unified TTY (dual UART + FB output on ARM64)
drivers/input/keyboard.c   — UART-backed keyboard (no PS/2 on RPi)
```

The ARM64 build is a **minimal subset** of the full kernel.  The goal of this port is
to establish a working serial console, framebuffer, heap, and process/scheduler
infrastructure — enough to prove correctness on real hardware before adding more
subsystems.

---

## 3. New Files

All files listed here are new additions that exist solely to support the ARM64/RPi port.

### `boot/arm64/boot.S`
AArch64 assembly entry point.  Replaces `boot/boot.asm` (x86 Multiboot2) for this
architecture.  Details in [§5 Boot Sequence](#5-boot-sequence).

### `kernel/arch/arm64/linker.ld`
Linker script for the ARM64 flat binary.  Details in [§6 Linker Script](#6-linker-script).

### `kernel/arch/arm64/exceptions.S` / `exceptions.h`
Full 2 KB-aligned AArch64 exception vector table.  All 16 entries are stub hang
loops for now.  Details in [§11 Exception Vectors](#11-exception-vectors).

### `kernel/arch/arm64/context.S`
Assembly implementation of `ctx_switch()` using AArch64 callee-saved register
conventions.  Details in [§12 Context Switch](#12-context-switch).

### `kernel/arch/arm64/io/port.h`
MMIO helpers (volatile pointer reads/writes) and memory barrier macros (`dmb`,
`dsb`, `isb`).  Also provides no-op stubs for `inb`/`outb` etc. so shared code
compiles on both architectures.  Details in [§13 MMIO Helpers](#13-mmio-helpers).

### `kernel/arch/arm64/mailbox.h` / `mailbox.c`
Raspberry Pi VideoCore mailbox channel 8 (property interface).  Used by the
framebuffer console to negotiate a framebuffer with the GPU.
Details in [§9 Mailbox Interface](#9-mailbox-interface).

### `kernel/arch/arm64/power.h` / `power.c`
Watchdog-based reboot and WFE-loop shutdown for RPi.
Details in [§10 Power Management](#10-power-management).

### `drivers/video/uart_rpi.h` / `uart_rpi.c`
Dual-mode UART driver (mini-UART default, PL011 optional).
Details in [§7 UART Driver](#7-uart-driver).

### `drivers/video/fb_console.h` / `fb_console.c`
Framebuffer text console.  Negotiates a 1024×768×32 framebuffer via the mailbox,
then renders characters using the embedded PSF2 font (falls back to a built-in
8×8 bitmap font if PSF2 init fails).
Details in [§8 Framebuffer Console](#8-framebuffer-console).

### `kernel/core/kmain_arm64.c`
ARM64-specific kernel main.  Guarded with `#ifdef ARCH_ARM64`.  Replaces the
x86 `kmain.c` boot path on this architecture.
Details in [§16 Kernel Entry](#16-kernel-entry-kmain_arm64).

---

## 4. Changed Shared Files

These files already existed for x86\_64 and were modified with `#ifdef ARCH_ARM64`
guards to support both architectures.

### `drivers/video/tty.c`
An `#ifdef ARCH_ARM64` block was added at the top.  On ARM64, `tty_init()` calls
`uart_init()` then `fb_console_init()`.  Every output function routes characters to
both the UART and (if the framebuffer initialized successfully) the framebuffer.
Color set/get calls are accepted but UART output remains plain text.
See [§14 TTY Layer](#14-tty-layer).

### `drivers/input/keyboard.c`
An `#ifdef ARCH_ARM64` block replaces the PS/2 keyboard driver with a UART-backed
implementation.  A 256-byte circular software buffer bridges the UART RX path with
the `keyboard_haschar()` / `keyboard_getchar()` / `keyboard_getchar_nonblocking()`
API.  `\r` is silently converted to `\n`.
See [§15 Keyboard / Input](#15-keyboard--input).

### `kernel/proc/context.h`
The `struct cpu_context` definition is now split by architecture:
- **ARM64**: `x19`–`x28`, `fp` (x29), `lr` (x30), `sp` — callee-saved registers
  per the AArch64 PCS.
- **x86\_64**: `rip`, `rsp`, `rflags`, callee-saved GPRs, CS, SS, CR3 (unchanged).

Functions that are x86-only (`ctx_switch_to_user`, `ctx_init_user`,
`ctx_save_fpu`, `ctx_restore_fpu`) are now wrapped in `#ifndef ARCH_ARM64`.

### `Makefile`
Extended to support `ARCH=arm64`.  See [§17 Build System](#17-build-system) for the
full list of new variables, targets, and object lists.

---

## 5. Boot Sequence

**Source**: [boot/arm64/boot.S](../boot/arm64/boot.S)

```
Power-on / firmware jump to 0x80000
         |
         v
_start   — read MPIDR_EL1 Aff0 field
         |
         +-- Non-zero core ID → .park_core (WFE loop, SMP parking)
         |
         v  (Core 0 only)
Read CurrentEL
         |
         +-- EL2 → .drop_to_el1
         |         set HCR_EL2.RW = 1  (EL1 runs AArch64)
         |         clear SCTLR_EL1     (MMU off, caches off)
         |         SPSR_EL2 = 0x3c5   (EL1h, DAIF masked)
         |         ELR_EL2  → .at_el1
         |         ic iallu, tlbi vmalle1, dsb sy, isb
         |         eret → EL1
         |
         +-- EL1 → .at_el1 directly
         |
         v
.at_el1  — SPSel = 1        (use SP_EL1 not SP_EL0)
         — sp = stack_top   (32 KB stack defined in linker script)
         — VBAR_EL1 = exception_vectors   (2 KB aligned)
         — CPACR_EL1.FPEN = 0b11         (enable FP/NEON)
         — zero BSS section (8 bytes at a time with str xzr)
         — bl kmain
         |
         v
.hang    — wfe loop (should never be reached)
```

**Key decisions:**

- All four Cortex-A53 cores receive the firmware jump.  Only core 0 (MPIDR Aff0 == 0)
  continues; the others spin in `wfe` indefinitely.  SMP wakeup is not implemented yet.
- The EL2→EL1 drop is mandatory on typical RPi 3 firmware.  Unusual EL1 entry (e.g.
  some custom firmware builds) is handled by jumping directly to `.at_el1`.
- BSS is zeroed in boot.S before any C code runs.  The C compiler assumes BSS starts
  zeroed.
- The FP/NEON enable step (`CPACR_EL1.FPEN`) is critical: without it, any floating-
  point instruction triggers a fault even inside the kernel.

---

## 6. Linker Script

**Source**: [kernel/arch/arm64/linker.ld](../kernel/arch/arm64/linker.ld)

```
. = 0x80000                 RPi firmware load address

.text.boot                  _start must be the very first byte
.text.vectors  ALIGN(2048)  VBAR_EL1 requires 2 KB alignment
.text          ALIGN(4096)  Remaining kernel code
.rodata        ALIGN(4096)
.data          ALIGN(4096)
.bss           ALIGN(4096)  __bss_start / __bss_end symbols for boot.S
.stack         ALIGN(4096)  32 KB, 16-byte aligned; exports stack_top
__kernel_end
```

The output format is `elf64-littleaarch64`.  `llvm-objcopy -O binary` strips the
ELF headers to produce the flat `kernel8.img` that the RPi firmware expects.

---

## 7. UART Driver

**Source**: [drivers/video/uart_rpi.h](../drivers/video/uart_rpi.h),
[drivers/video/uart_rpi.c](../drivers/video/uart_rpi.c)

The driver supports two UART variants selectable at build time:

### mini-UART (AUX UART) — default

| Property | Value |
|---|---|
| Selection | `enable_uart=1` in `config.txt` (default) |
| Peripheral offset | `RPI_BASE + 0x215000` |
| GPIO pins | 14 (TX), 15 (RX) via ALT5 |
| Baud | 115200 — `AUX_MU_BAUD = (250 MHz / (8 × 115200)) − 1 = 270` |
| Mode | 8-bit, no parity, polling (no interrupts) |

### PL011 UART — optional

| Property | Value |
|---|---|
| Selection | `UART=pl011` on make command line; `dtoverlay=disable-bt` in `config.txt` |
| Peripheral offset | `RPI_BASE + 0x201000` |
| GPIO pins | 14 (TX), 15 (RX) via ALT0 |
| Clock | 48 MHz UART clock (`init_uart_clock=48000000`) |
| Baud | 115200 — IBRD=26, FBRD=3 |
| Mode | 8-bit, no parity, 1 stop bit, FIFO enabled, polling |

On Pi 3, PL011 is wired to the Bluetooth module by default; `dtoverlay=disable-bt`
frees it but disables on-board Bluetooth.

### API

```c
void uart_init(void);           /* Calls mini or PL011 init based on USE_PL011 */
void uart_putchar(char c);      /* Inserts \r before \n automatically */
char uart_getchar(void);        /* Blocking read */
int  uart_rx_ready(void);       /* Non-blocking: 1 if byte available */
char uart_getchar_nb(void);     /* Non-blocking: returns 0 if empty */
void uart_print(const char* s);
```

Low-level variants (`uart_mini_*`, `uart_pl011_*`) are also exported for code that
needs to select a UART explicitly.

### GPIO setup

Both modes configure GPIO 14 and 15 via `GPFSEL1` (covers GPIO 10–19):
- Pin 14 at bits `[14:12]`, pin 15 at bits `[17:15]`
- mini-UART: ALT5 = `0b010`
- PL011: ALT0 = `0b100`

Pull-up/down is disabled using the BCM2837 three-step method (`GPPUD` → delay →
`GPPUDCLK0` → delay → `GPPUDCLK0 = 0`).

---

## 8. Framebuffer Console

**Source**: [drivers/video/fb_console.h](../drivers/video/fb_console.h),
[drivers/video/fb_console.c](../drivers/video/fb_console.c)

The framebuffer console negotiates a display with the VideoCore GPU via the
[mailbox property interface](#9-mailbox-interface), then renders text directly into
the returned buffer.

### Initialization sequence

`fb_console_init()` makes six sequential mailbox calls:

1. `MBOX_TAG_SET_PHYSICAL_SIZE` — 1024×768
2. `MBOX_TAG_SET_VIRTUAL_SIZE` — 1024×768
3. `MBOX_TAG_SET_DEPTH` — 32 bpp
4. `MBOX_TAG_SET_PIXEL_ORDER` — RGB (1)
5. `MBOX_TAG_ALLOCATE_BUFFER` — 16-byte alignment request; receives `(address, size)`
6. `MBOX_TAG_GET_PITCH` — bytes per scanline

The GPU returns a **bus address** (top bits set for GPU cache coherence).  The driver
strips these bits with `& 0x3FFFFFFF` to get the ARM physical address.

### Font rendering

The console prefers the embedded PSF2 font (`kernel/font/default.psf2`, linked via
`llvm-objcopy` into the `.font` section).  If PSF2 init fails, it falls back to a
hardcoded 8×8 bitmap covering ASCII printable characters.

All characters are drawn at `CHAR_SCALE = 2` (each bitmap pixel → 2×2 screen pixels),
giving a nominal 16×16 cell size.

### Scrolling

When the cursor reaches the bottom of the framebuffer, `fb_console_clear()` is called
(clears the entire buffer and resets cursor to 0,0).  Full scrolling is not yet
implemented.

### Control character handling

| Char | Action |
|---|---|
| `\n` | Move cursor to start of next row; clear if at bottom |
| `\r` | Return cursor to column 0 |
| `\b` | Move cursor left one cell, erase with background color |
| `\t` | Advance 4 spaces |

---

## 9. Mailbox Interface

**Source**: [kernel/arch/arm64/mailbox.h](../kernel/arch/arm64/mailbox.h),
[kernel/arch/arm64/mailbox.c](../kernel/arch/arm64/mailbox.c)

The Raspberry Pi uses a hardware mailbox at `RPI_BASE + 0xB880` for ARM↔GPU
communication.  Channel 8 carries "property" (tagged) messages.

### Protocol

```
1. Prepare a 16-byte-aligned mbox_message_t in RAM
2. Convert ARM physical address → GPU bus address:
      addr = (arm_phys & 0x3FFFFFFF) | 0xC0000000
3. OR in channel number: addr |= 0x8
4. Wait until MAILBOX_STATUS bit 31 (FULL) is clear
5. Write addr to MAILBOX_WRITE
6. Poll MAILBOX_READ until lower 4 bits == 8 (our channel)
7. Check msg.code == MBOX_RESPONSE_OK (0x80000000)
```

Each step uses a timeout of 100,000 iterations to prevent infinite hangs on
hardware faults.

### Supported property tags

| Tag constant | ID | Purpose |
|---|---|---|
| `MBOX_TAG_SET_PHYSICAL_SIZE` | `0x00048003` | Set display physical resolution |
| `MBOX_TAG_SET_VIRTUAL_SIZE` | `0x00048004` | Set display virtual resolution |
| `MBOX_TAG_SET_DEPTH` | `0x00048005` | Set bits per pixel |
| `MBOX_TAG_SET_PIXEL_ORDER` | `0x00048006` | RGB (1) or BGR (0) |
| `MBOX_TAG_ALLOCATE_BUFFER` | `0x00040001` | Allocate framebuffer |
| `MBOX_TAG_GET_PITCH` | `0x00040008` | Get bytes per scanline |

---

## 10. Power Management

**Source**: [kernel/arch/arm64/power.h](../kernel/arch/arm64/power.h),
[kernel/arch/arm64/power.c](../kernel/arch/arm64/power.c)

The BCM2837 provides a watchdog timer at `RPI_BASE + 0x100000` (PM block).

### Reboot — `rpi_reboot()`

```c
mmio_write32(PM_RSTS, PM_PASSWORD);                           // Clear restart flags
mmio_write32(PM_WDOG, PM_PASSWORD | 1);                       // 1-tick timeout
mmio_write32(PM_RSTC, PM_PASSWORD | PM_RSTC_WRCFG_FULL_RESET); // Trigger watchdog
for (;;) __asm__ volatile ("wfe");                            // Spin until reset fires
```

`PM_PASSWORD = 0x5A000000` is the magic value required by the BCM2837 PM block to
accept writes.

### Shutdown — `rpi_shutdown()`

There is no hardware power-off path on the BCM2837 accessible without the GPU
firmware.  `rpi_shutdown()` spins in `wfe` indefinitely, stopping the CPU with minimal
power draw.

---

## 11. Exception Vectors

**Source**: [kernel/arch/arm64/exceptions.S](../kernel/arch/arm64/exceptions.S),
[kernel/arch/arm64/exceptions.h](../kernel/arch/arm64/exceptions.h)

The AArch64 exception vector table is 2 KB in size and must be 2 KB aligned
(required by the `VBAR_EL1` register).  The linker script places it in `.text.vectors
ALIGN(2048)`.

```
Offset   Group                          Entries
+0x000   Current EL, SP0               Sync, IRQ, FIQ, SError
+0x200   Current EL, SPx               Sync, IRQ, FIQ, SError
+0x400   Lower EL (AArch64)            Sync, IRQ, FIQ, SError
+0x600   Lower EL (AArch32)            Sync, IRQ, FIQ, SError
```

Each slot is 128 bytes (32 instructions).  All 16 entries are currently **stub hang
loops** (`b .`).  This is intentional for the bring-up phase — an unexpected exception
halts the CPU at the faulting vector rather than silently corrupting state.

The vector table address is installed in `boot.S`:
```asm
ldr x0, =exception_vectors
msr vbar_el1, x0
isb
```

Real interrupt handlers will be wired here when the ARM Generic Interrupt Controller
(GIC-400 on BCM2837) is brought up.

---

## 12. Context Switch

**Source**: [kernel/arch/arm64/context.S](../kernel/arch/arm64/context.S),
[kernel/proc/context.h](../kernel/proc/context.h)

### `struct cpu_context` — ARM64 layout

```c
struct cpu_context {
    uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
    uint64_t fp;   /* x29 — frame pointer */
    uint64_t lr;   /* x30 — link register, used as instruction pointer */
    uint64_t sp;   /* stack pointer */
};
```

Only **callee-saved** registers are preserved across `ctx_switch()`.  Caller-saved
registers (x0–x18) are not stored because the AArch64 PCS guarantees they may be
clobbered across any function call boundary.

### Memory layout (offsets in `context.S`)

| Offset | Register |
|---|---|
| +0x00 | x19 |
| +0x08 | x20 |
| +0x10 | x21 |
| +0x18 | x22 |
| +0x20 | x23 |
| +0x28 | x24 |
| +0x30 | x25 |
| +0x38 | x26 |
| +0x40 | x27 |
| +0x48 | x28 |
| +0x50 | x29 (fp) |
| +0x58 | x30 (lr) |
| +0x60 | sp |

### First-time thread switch

`ctx_init_kernel(ctx, entry, stack_top)` sets `ctx->lr = entry` and `ctx->sp = stack_top`.
When `ctx_switch(NULL, new_ctx)` restores this context, `ret` jumps to the entry
function — the thread starts running.

### Resume

On subsequent switches, `lr` was saved as the return address inside `ctx_switch`
itself.  Restoring it and calling `ret` resumes execution at the instruction
immediately after `bl ctx_switch` in the previously-running thread.

---

## 13. MMIO Helpers

**Source**: [kernel/arch/arm64/io/port.h](../kernel/arch/arm64/io/port.h)

AArch64 has no x86-style `IN`/`OUT` port instructions.  All BCM2837 peripheral
access uses Memory-Mapped I/O (MMIO) via volatile pointer reads/writes.

### MMIO accessors

```c
uint8_t  mmio_read8 (uint64_t addr);
void     mmio_write8(uint64_t addr, uint8_t val);
uint16_t mmio_read16(uint64_t addr);
void     mmio_write16(uint64_t addr, uint16_t val);
uint32_t mmio_read32(uint64_t addr);    /* Most common on RPi */
void     mmio_write32(uint64_t addr, uint32_t val);
uint64_t mmio_read64(uint64_t addr);
void     mmio_write64(uint64_t addr, uint64_t val);
```

All accessors use `volatile` casts to prevent the compiler from reordering or
caching the accesses.

### Memory barriers

```c
void dmb(void);        /* Data Memory Barrier — all prior accesses complete */
void dsb(void);        /* Data Sync Barrier — all prior instructions complete */
void isb_barrier(void); /* Instruction Sync Barrier — flush pipeline */
```

These are essential when writing peripheral registers that must be sequenced
(e.g. configuring a UART before enabling it).

### x86 compatibility stubs

`inb`, `outb`, `inw`, `outw`, `inl`, `outl` are provided as no-ops so shared
kernel code that includes this header compiles cleanly.  **Do not call these from
any code path that actually needs to perform x86 I/O.**

---

## 14. TTY Layer

**Source**: [drivers/video/tty.c](../drivers/video/tty.c)

On ARM64, the TTY layer is rewritten inside an `#ifdef ARCH_ARM64` block.  The public
API (`tty_init`, `tty_putchar`, `tty_print`, `tty_clear`, `tty_set_color`, etc.)
is identical to the x86 version.

### Dual output

All character output goes to **both** the UART and the framebuffer (if available):

```c
static inline void tty_putc_both(char c) {
    uart_putchar(c);
    if (fb_console_ready)
        fb_console_putchar(c);
}
```

`tty_init()` calls `uart_init()` first (UART is always available), then
`fb_console_init()`.  If the mailbox call fails (e.g. in QEMU without proper
framebuffer support), `fb_console_ready` stays 0 and output falls back to UART only.

### Colors

`tty_set_color()` and `tty_get_color()` accept and store color values but UART output
is always plain text.  The framebuffer console uses fixed white-on-black colors
for the bring-up phase.

---

## 15. Keyboard / Input

**Source**: [drivers/input/keyboard.c](../drivers/input/keyboard.c)

There is no PS/2 controller on the Raspberry Pi.  Inside `#ifdef ARCH_ARM64`, the
keyboard driver is replaced with a UART-backed implementation.

### Architecture

```
UART RX register
      |
      v
keyboard_haschar() / keyboard_getchar() / keyboard_getchar_nonblocking()
      |                 |
      v                 v
256-byte circular    uart_getchar() / uart_rx_ready()
software buffer      (direct UART access)
```

The software buffer allows future interrupt-driven UART filling without changing the
keyboard API.  Currently the buffer is only filled on demand from `keyboard_getchar()`.

### Key translations

| Input | Output |
|---|---|
| `\r` (0x0D) | `\n` (0x0A) — normalize CR to LF |
| `0x03` (Ctrl+C) | Sets `kb_ctrl_c_pending` flag, returns `\x03` |

Escape sequences (arrow keys, function keys) are passed through raw as multi-byte
sequences.  `kmain_arm64.c`'s console loop recognizes `ESC [ A` (up) and `ESC [ B`
(down) for command history navigation.

---

## 16. Kernel Entry (kmain\_arm64)

**Source**: [kernel/core/kmain_arm64.c](../kernel/core/kmain_arm64.c)

This file is compiled only when `ARCH_ARM64` is defined.  It contains a self-contained
minimal kernel — enough to validate the bring-up without pulling in the full x86
shell and subsystems.

### Boot sequence inside kmain

```
tty_init()          — UART + framebuffer
klog_init()         — kernel logger
print banner        — "Lumina Kernel v0.3.5  [ARM64/RPi]"
kheap_init()        — kernel heap
sysinfo_init()      — system information
keyboard_init()     — UART keyboard
console_loop()      — interactive minimal shell
```

### Minimal console loop

The console loop (`console_loop()`) provides:

| Command | Action |
|---|---|
| `help` | List available commands |
| `sysinfo` | Display system information |
| `echo <text>` | Print text |
| `clear` | Clear the screen |
| `shutdown` | Call `rpi_shutdown()` |
| `reboot` | Call `rpi_reboot()` |

The readline implementation (`console_readline()`) supports:
- Backspace / DEL (character erase)
- Ctrl+C (clear current line)
- Up/Down arrow keys (command history, 8 entries)
- Tab (4-space expansion)
- CR/LF normalization

---

## 17. Build System

**Source**: [Makefile](../Makefile)

### Architecture selection

```makefile
ARCH      ?= x86_64       # Default
RPI_MODEL ?= 3            # 3 or 4
UART      ?= mini         # mini or pl011
```

### Targets

| Make invocation | Result |
|---|---|
| `make` | x86\_64 ISO (default) |
| `make ARCH=arm64` | `kernel8.img` for RPi 3B |
| `make ARCH=arm64 RPI_MODEL=4` | `kernel8.img` for RPi 4 |
| `make ARCH=arm64 UART=pl011` | RPi 3B with PL011 UART |
| `make run-arm64` | Build + run in QEMU (`-M raspi3b`) |
| `make clean-arm64` | Remove `build_arm64/` and `kernel8.img` |

### Compiler / linker

| Tool | Value |
|---|---|
| Compiler | `clang --target=aarch64-elf` |
| Linker | `ld.lld` |
| Flat binary | `llvm-objcopy -O binary` |

### Preprocessor defines injected by the Makefile

| Define | Value |
|---|---|
| `ARCH_ARM64` | Defined for all ARM64 builds |
| `RPI_MODEL` | `3` or `4` (integer) |
| `RPI_BASE` | `0x3F000000` (Pi 3) or `0xFE000000` (Pi 4) |
| `USE_PL011` | Defined when `UART=pl011` |

### CFLAGS (ARM64)

```makefile
CFLAGS := -ffreestanding -fno-stack-protector -fno-pic \
           -O2 -Wall -Wextra \
           -nostdlib -nostdinc \
           -Ilib/libc -Ilib/libtranslate -Iinclude \
           -DARCH_ARM64 -DRPI_MODEL=$(RPI_MODEL) -DRPI_BASE=$(RPI_BASE)
```

### Build output directory

ARM64 objects go into `build_arm64/` (separate from the x86 `build/` directory)
to prevent object file collisions when building both architectures on the same
working tree.

### ARM64 object list (minimal subset)

```
boot/arm64/boot.S
kernel/arch/arm64/context.S
kernel/arch/arm64/exceptions.S
kernel/arch/arm64/mailbox.c
kernel/arch/arm64/power.c
kernel/core/kmain_arm64.c
kernel/core/multiboot.c       (stub — no multiboot on RPi)
kernel/core/sysinfo.c
kernel/core/time.c
kernel/log/klog.c
kernel/mm/kheap.c
kernel/mm/pmm/pmm.c
kernel/mm/pmm/bitmap.c
kernel/mm/fallback/simple_fallback.c
kernel/proc/proc.c
kernel/proc/table.c
kernel/proc/sched.c
kernel/proc/pipe.c
kernel/proc/context.c
kernel/proc/wait.c
kernel/proc/zombie.c
kernel/proc/job.c
kernel/proc/security.c
kernel/spm/spm.c
kernel/font/font.c
kernel/font/default_font.o    (embedded PSF2 via llvm-objcopy)
drivers/video/uart_rpi.c
drivers/video/tty.c
drivers/video/fb_console.c
drivers/input/keyboard.c
lib/libc/string.c
lib/libc/float.c
lib/libc/array.c
lib/libtranslate/stdio.c
lib/libtranslate/ctype.c
lib/libtranslate/math.c
```

### Font object (ARM64)

The PSF2 font is embedded using `llvm-objcopy` with an AArch64-specific ELF target:

```makefile
$(OBJCOPY) \
  -I binary \
  -O elf64-littleaarch64 \
  --rename-section .data=.font,alloc,load,readonly,data,contents \
  kernel/font/default.psf2 $(BUILD_ARM64)/kernel/font/default_font.o
```

---

## 18. SD Card Setup

1. Format an SD card with a FAT32 partition (first partition).

2. Copy the official Raspberry Pi firmware files onto the partition:
   ```
   bootcode.bin
   start.elf
   fixup.dat
   ```
   Download these from the [raspberrypi/firmware](https://github.com/raspberrypi/firmware)
   repository (`boot/` directory).

3. Build GraceOS for ARM64:
   ```sh
   make ARCH=arm64
   ```

4. Copy the output to the SD card root:
   ```
   kernel8.img
   config.txt        (see §19 below)
   ```

5. Insert the SD card, connect a USB-to-serial adapter to GPIO 14 (TX) and 15 (RX),
   open a terminal at 115200 8N1, and power on.

---

## 19. config.txt Reference

Place `config.txt` in the root of the FAT32 partition alongside `kernel8.img`.

### Minimum config (mini-UART)

```ini
# Enable the UART (maps mini-UART to GPIO 14/15)
enable_uart=1

# Boot in 64-bit mode — required for kernel8.img
arm_64bit=1
```

### Alternative config (PL011 UART)

```ini
# Disable Bluetooth to free PL011 for the kernel
dtoverlay=disable-bt

# Set PL011 clock (required for baud rate accuracy with PL011)
init_uart_clock=48000000

# Boot in 64-bit mode
arm_64bit=1
```

Build with `make ARCH=arm64 UART=pl011` when using PL011.

### Optional display settings

```ini
# Force HDMI output (useful when monitor doesn't auto-detect)
hdmi_force_hotplug=1

# Set display resolution (default is 1024x768 requested via mailbox)
framebuffer_width=1024
framebuffer_height=768
framebuffer_depth=32
```

---

## 20. QEMU Testing

The Makefile includes a pre-configured QEMU target:

```makefile
run-arm64: kernel8
    qemu-system-aarch64 \
        -M raspi3b \
        -kernel kernel8.img \
        -serial stdio \
        -m 1024M
```

Run with:
```sh
make ARCH=arm64 run-arm64
```

**Notes:**
- `-M raspi3b` emulates the BCM2837 peripheral map (`RPI_BASE = 0x3F000000`).
- `-serial stdio` routes the emulated mini-UART to the host terminal.
- The framebuffer mailbox is supported in recent QEMU raspi3b machines.
  If the mailbox init fails silently, UART output still works.
- QEMU does not emulate the BCM2837 watchdog, so `reboot` will spin in `wfe`
  rather than resetting.

---

## 21. What is Not Yet Ported

The following subsystems are x86\_64-only and have not been brought to the ARM64
port yet.

| Subsystem | Reason / Notes |
|---|---|
| VMM / MMU / paging | No page tables set up; kernel runs with MMU off |
| Multiboot / PMM from memory map | RPi firmware provides no memory map at boot |
| IDT / interrupt routing | Exception vector stubs only; GIC-400 not wired |
| VBE graphics | Replaced by mailbox framebuffer — no VBE on RPi |
| Syscall table | No `svc` handler; `syscall.asm` is x86-only |
| Full userland shell | `kmain_arm64.c` has a minimal console instead |
| ChibiVM | Not compiled for ARM64 |
| Audio (HDA / SB16 / AC97 / VoiceBox) | PC audio hardware not present on RPi |
| Network stack / virtio-net | virtio NIC is QEMU/x86 only |
| IDE / NVMe / PMEM storage | Not available on RPi |
| cfdisk / disk utilities | Depend on storage drivers |
| Ports (7zip, sqlite, libsodium) | Not built for `ARCH=arm64` |
| LLM runtime | Not included in ARM64 object list |
| Snake game / GUI / raylib | Not compiled for ARM64 |
| minit / process logger | Not compiled for ARM64 |
| SPM CLI / doas | Not compiled for ARM64 |

---

## 22. Pi 4 Differences

The build system supports Raspberry Pi 4 (BCM2711) with a single variable:

```sh
make ARCH=arm64 RPI_MODEL=4
```

This changes `RPI_BASE` from `0x3F000000` to `0xFE000000`.  Every peripheral
offset (UART, GPIO, mailbox, power management) is computed relative to `RPI_BASE`,
so all drivers automatically target the correct addresses.

Additional Pi 4 considerations not yet implemented:

| Item | Notes |
|---|---|
| GIC-400 vs GIC-500 | BCM2711 uses a different interrupt controller layout |
| XHCI USB | Pi 4 uses a different USB host controller |
| PCIe | Available on Pi 4 only |
| 4 GB RAM | PMM will need to handle RAM above 4 GB (requires LPAE or 64-bit paging) |
| PL011 default | On Pi 4, PL011 is the primary UART by default (no Bluetooth conflict) |

The output binary is still named `kernel8.img` for Pi 4.  The RPi firmware selects
the correct kernel file based on the SoC detected at boot.
