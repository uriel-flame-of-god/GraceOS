# GraceOS (Lumina Kernel)

**Version**: v0.6.1 (Kernel v0.3.5)

A minimal 64-bit operating system built on the **Lumina Kernel**, inspired by Windows NT and MS-DOS 4.0.

## Overview

GraceOS (formerly Grace-Kernel) is a hobby operating system built from scratch for x86\_64 with an active ARM64/Raspberry Pi 3B bring-up in progress. It features true preemptive multitasking, segment-based memory management, a B+ tree filesystem, and a syscall-based userland interface.

## Features

### Kernel

- **Preemptive Multitasking**: Hybrid SJF/FCFS scheduler with aging and IRQ0-driven tick
- **Lumina Kernel**: NT-style executive with full PCB, job trees, and CSID job control
- **Memory Management**: SASY segment allocator with handle-based access, PMM bitmap, 4-level VMM paging
- **Kernel Logger (klog)**: Multi-level ring-buffer logger (`INIT`, `INFO`, `WARN`, `ERROR`) routed to serial and memory
- **Service Orchestration (minit)**: Service tree with dependency ordering, restart policy, and exhaustion handling
- **Security**: SPM policy manager with user and permission tables; userland `spm` and `doas` tools
- **LLM Runtime**: In-kernel GGUF inference engine — transformer forward pass, quantization, chat templates, LoRA adapters

### Drivers & Hardware

- **Graphics**: VBE Framebuffer with Raylib compatibility layer; TTY text console
- **Input**: PS/2 keyboard driver with IRQ1 + polling fallback; mouse stub
- **Audio**: VoiceBox playback subsystem — SB16 (default), AC97 (secondary), ISA speaker (fallback), HDA (experimental)
- **Networking**: virtio-net PCI driver, Ethernet / ARP / IPv4 / ICMP / TCP stack, HTTP/1.1 client
- **Storage**: BranchFS (in-memory B+ tree), IDE, NVMe, PMEM, RTC
- **Time**: PIT-based timekeeping at 1000 Hz, RTC, sub-millisecond precision

### Userland

- **Shell**: Interactive shell with command history, tab completion stubs, 30+ built-in commands
- **Filesystem**: `ls`, `mkfile`, `find`, `rm`, `cat`, `mkdir`, `cd`, `quill` text editor
- **Networking**: `netstat`, `ifconfig`, `ping`, `fetch` (HTTP GET to BranchFS)
- **ChibiVM** *(Stage 2)*: Lightweight VM — REPL, `chivm build` compiler, `run` executor; `int`, `bool`, `string`, heterogeneous and homogeneous arrays, `if`/`else if`/`else` conditionals
- **Media**: `mplayer` (WAV/MP3 via VoiceBox), backend switching at runtime
- **Utilities**: `now`, `snake`, `7c` (ZIP store/extract), `spm`, `doas`
- **libtranslate**: POSIX-style libc shim (stdlib / stdio / ctype / math)

### ARM64 / Raspberry Pi 3B (Work in Progress)

> **Currently broken** — boots to banner, UART I/O works in QEMU.
> Real-hardware input, interrupts, syscall layer, and MMU not yet implemented.

- AArch64 boot assembly (EL2 → EL1 drop, SMP core parking)
- mini-UART and PL011 UART drivers (selectable at build time)
- Framebuffer console via VideoCore GPU mailbox (1024 × 768 × 32 bpp)
- BCM2837 watchdog reboot, WFE-loop shutdown
- AArch64 exception vector table (16 stub entries)
- Context switch using callee-saved register save/restore
- MIDR\_EL1 CPU identification (PartNum → core name, Implementer → vendor)
- See [docs/RPI3B.MD](docs/RPI3B.MD) for the full bring-up guide

---

## Quick Start

### Requirements

- Windows 10/11 with WSL (for GRUB2)
- QEMU (`qemu-system-x86_64` for x86; `qemu-system-aarch64` for ARM64)
- FASM and Clang/LLVM — see [docs/BUILD.MD](docs/BUILD.MD)

### Build and Run (x86\_64)

```cmd
make clean
make run
```

Builds the kernel, creates a bootable ISO, and launches QEMU.

### Build and Run (Raspberry Pi 3B — QEMU)

```cmd
make ARCH=arm64
make ARCH=arm64 run-arm64
```

Produces `kernel8.img` and runs it under `qemu-system-aarch64 -M raspi3b -serial stdio`.

### Build for Raspberry Pi 4

```cmd
make ARCH=arm64 RPI_MODEL=4
```

---

## Shell Commands

| Command | Description |
|---|---|
| `help` | Show available commands |
| `sysinfo` | Display system and kernel information |
| `ls`, `mkfile`, `find`, `rm`, `cat` | BranchFS file operations |
| `cd [path]`, `mkdir <name>` | Directory navigation and creation |
| `now` | Show current date and time |
| `netstat` | Show active network device status |
| `ifconfig [ip gw mask]` | Show or set static IP config |
| `ping <ip> [count]` | ICMP echo test |
| `fetch <ip> [path] [outfile]` | HTTP GET, optionally save to BranchFS |
| `quill <file>` | Text editor |
| `chivm` / `chivm build` / `run` | ChibiVM REPL, compiler, and program launcher |
| `mplayer <file>` | Play WAV/MP3 via VoiceBox |
| `mplayer --switch <sb16\|ac97\|hda\|isa\|auto>` | Select audio backend |
| `7c encode` / `7c decode` | ZIP store-only tool |
| `snake` | Snake game |
| `spm ...` | Security Policy Manager user/permission commands |
| `shutdown`, `reboot`, `halt` | Power controls |

---

## Architecture

```
User Applications (Shell, Snake, ChibiVM, mplayer)
            |
    Syscall Interface (INT 0x80, 80+ calls)
            |
    Lumina Kernel Executive
    ├── Process Manager (PCB, CSID, fork/exec/wait/pipe/job)
    ├── Memory Manager (SASY segments, PMM bitmap, VMM paging)
    ├── Scheduler (Hybrid SJF/FCFS with aging)
    ├── minit (service tree, restart policy, exhaustion handler)
    ├── klog (ring-buffer kernel logger, multi-level)
    ├── SPM (Security Policy Manager)
    ├── Graphics (VBE Framebuffer, TTY)
    ├── LLM Runtime (GGUF inference, 13 syscalls)
    └── Network Stack (Ethernet/ARP/IPv4/ICMP/TCP/HTTP)
            |
    Device Drivers
    ├── Video: VGA, TTY, VBE framebuffer, serial
    ├── Input: PS/2 keyboard (IRQ1 + poll), mouse stub
    ├── Audio: SB16, AC97, HDA, ISA speaker, VoiceBox
    ├── Net: virtio-net PCI
    └── Storage: BranchFS, RTC, IDE, NVMe, PMEM
            |
    Hardware Abstraction
    └── PMM bitmap, 4-level paging, IDT/ISR, GDT
```

---

## Project Structure

```
GraceOS/
  boot/
    boot.asm              x86_64 Multiboot2 entry (FASM)
    arm64/boot.S          AArch64 entry (EL2→EL1, SMP parking)

  kernel/                 Lumina Kernel source
    arch/
      x86_64/             IDT, VBE, I/O ports, linker script
        io/               inb/outb port helpers
      arm64/              AArch64 arch layer
        io/               MMIO helpers, memory barrier macros
        exceptions.S      2 KB-aligned AArch64 vector table
        context.S         Callee-saved register context switch
        mailbox.c/h       VideoCore GPU mailbox (property channel 8)
        power.c/h         BCM2837 watchdog reboot / WFE shutdown
        linker.ld         ARM64 linker script (load at 0x80000)
    audio/                VoiceBox core, server, audio clock
    core/
      kmain.c             x86_64 kernel entry
      kmain_arm64.c       ARM64 kernel entry (minimal console)
      sysinfo.c           System information (CPUID / MIDR_EL1)
      multiboot.c         Multiboot2 memory map parser
      time.c              Timekeeping
    font/                 PSF2 font renderer + embedded default.psf2
    include/              Internal kernel headers (syscall.h, time.h)
    llm/                  In-kernel GGUF inference engine
    log/                  klog — ring-buffer kernel logger
    mm/
      pmm/                Physical Memory Manager (bitmap)
      vmm/                Virtual Memory Manager (4-level paging)
      sasy/               Segment Allocator (handle-based)
      fallback/           Bootstrap bump allocator
    net/
      arp/                ARP layer (32-entry cache)
      ethernet/           Ethernet frame dispatch
      http/               HTTP/1.1 client
      icmp/               ICMP echo / echo-reply
      ip/                 IPv4 layer
      tcp/                TCP client state machine
    proc/
      proc.c/table.c      PCB creation and process table
      sched.c             Hybrid SJF/FCFS scheduler
      fork.c/exec.c       fork and exec paths
      wait.c/zombie.c     Wait/zombie reaping
      pipe.c              Kernel pipes
      job.c               CSID job control
      security.c          Per-process security context
      context.c/.h        cpu_context (arch-split: x86_64 / AArch64)
      minit/              Service orchestration layer
        tree.c            Service dependency tree
        orchestration.c   Boot sequencing and restart logic
        exhaustion.c      Restart exhaustion and policy enforcement
      logger/             Background kernel log service (minit-managed)
    samples/              Embedded audio sample (sample.mp3)
    spm/                  Security Policy Manager
    sys/                  Syscall dispatcher + per-subsystem handlers

  drivers/
    audio/                SB16, AC97, HDA, ISA speaker
    input/                PS/2 keyboard (x86) / UART keyboard (ARM64), mouse
    net/                  virtio-net PCI driver
    storage/              BranchFS, RTC, IDE, NVMe, PMEM
    video/
      tty.c               Unified TTY (VGA on x86, UART+FB on ARM64)
      uart_rpi.c/h        RPi mini-UART / PL011 driver
      fb_console.c/h      RPi framebuffer text console (mailbox)
      vga.c               VGA text mode (x86)
      fb.c                VBE framebuffer (x86)

  lib/
    libc/                 string, float, array, time
    libtranslate/         POSIX shim: stdlib / stdio / ctype / math
    libgrace/             Syscall wrappers + LLM userland API

  userland/
    chivm/                ChibiVM runtime + CMDT compiler
    coreutils/            now, timestat
    doas/                 Privilege escalation stub
    gui/                  GUI application
    mplayer/              Audio player CLI
    quill/                Text editor
    raylib/ raygui/       Raylib / RayGUI compatibility shims
    shell/                Interactive shell + history
    snake/                Snake game
    spm/                  SPM CLI tool

  ports/                  BSD-style ports tree
  grub/                   GRUB2 config and themes
  iso/                    Bootable ISO staging area
  docs/                   Documentation (21 files)
```

---

## Changelog

### v0.6.1 / Kernel v0.3.5 — 2026-04-17

- **Logger (klog)**: Kernel logger now fully operational. `INIT` / `INFO` / `WARN` / `ERROR`
  levels route correctly to serial console and in-memory ring buffer. Fixed silent
  init-ordering bug where `klog_init()` fired before the UART was ready.
- **minit**: Service orchestration layer fixed. Tree construction, restart policy, and
  exhaustion handling now work correctly. Previously minit could deadlock at boot if
  a service exited before its dependents were registered.
- **sysinfo (ARM64)**: MIDR\_EL1 PartNum now decoded to exact core name (Cortex-A53,
  Cortex-A72, …). Implementer byte decoded to vendor string (ARM Ltd, Broadcom, …).
  Total RAM derived from `RPI_MODEL` at compile time.
- **Console readline (ARM64)**: Fixed blind pre-classification echo (double-newline on
  Enter, double-erase on Backspace, ESC leaking `[A` into the buffer).
- **Keyboard driver (ARM64)**: Removed dead 256-byte circular buffer and
  `keyboard_service_run()` polling thread. ARM64 path is now thin UART wrappers —
  one input system only.
- **ESC handling (ARM64)**: Replaced 50 000-NOP timing busy-wait with ANSI CSI
  sequence drain using `uart_rx_ready()`. Reliable on both QEMU and real hardware.
- **ARM64 / Raspberry Pi 3B**: Port builds and boots in QEMU (`make ARCH=arm64 run-arm64`).
  Input and output work over UART. Active development — see [docs/RPI3B.MD](docs/RPI3B.MD).

---

## Ports

GraceOS includes a BSD-style ports tree. See [docs/PORTS.MD](docs/PORTS.MD) for details.

```
make PORTS=1           # build all ports
make port-7zip         # build a single port
make port-libsodium
```

Current ports: `7zip` (C-only decoder + `7c` store tool), `libsodium` (static crypto), `sqlite`

---

## ChibiVM (Stage 2)

ChibiVM is a lightweight VM toolchain with three shell entry points:

- `chivm` — interactive REPL (multi-line accumulation, `help`, `reset`, `run`)
- `chivm build <src> <out.cmdt>` — compile source to CMDT (Intel-HEX-like) format
- `run <file.cmdt>` — execute a CMDT program via the process manager exec path

**Stage 2 language features:**

| Feature | Syntax |
|---|---|
| Integer | `int x = 42;` |
| Boolean | `bool flag = true;` |
| String | `string s = "hello";` |
| Heterogeneous array (default) | `array a = [1, "two", true];` |
| Homogeneous int array | `int[] n = [10, 20, 30];` |
| Homogeneous bool/string arrays | `bool[] b = [true, false];` / `string[] ss = ["a", "b"];` |
| Array element access | `nums[0]`, `nums[1] = 99;` |
| If / else if / else | `if (x >= 90) { ... } else if (...) { ... } else { ... }` |
| Conditions | `==` `!=` `<` `>` `<=` `>=`, truthy check, `!` negation |
| Output | `cprintf("text ${var}\n");` |
| Input | `dinput("prompt: ", varname);` |

```text
chivm build hello.chi hello.cmdt
run hello.cmdt
```

Full reference: [docs/CHIVM.MD](docs/CHIVM.MD)

---

## Documentation

| File | Contents |
|---|---|
| [API.MD](docs/API.MD) | Syscall reference (80+ calls) |
| [ARCHITECTURE.MD](docs/ARCHITECTURE.MD) | System architecture overview |
| [AUDIO.MD](docs/AUDIO.MD) | Audio backend status and playback |
| [CHIVM.MD](docs/CHIVM.MD) | ChibiVM language, REPL, CMDT workflow |
| [CONTRIBUTORS.MD](docs/CONTRIBUTORS.MD) | Project credits |
| [DEBUGGING.MD](docs/DEBUGGING.MD) | Debugging guide |
| [FILESYSTEM.MD](docs/FILESYSTEM.MD) | BranchFS internals |
| [KERNEL.MD](docs/KERNEL.MD) | Kernel internals |
| [LLM.MD](docs/LLM.MD) | In-kernel LLM runtime reference |
| [MEMORY.MD](docs/MEMORY.MD) | Memory subsystem |
| [PORTS.MD](docs/PORTS.MD) | Ports tree guide |
| [PROC.MD](docs/PROC.MD) | Process manager |
| [RPI3B.MD](docs/RPI3B.MD) | Raspberry Pi 3B bring-up guide |
| [SASY.MD](docs/SASY.MD) | Segment Allocator reference |
| [SPM.MD](docs/SPM.MD) | Security Policy Manager |
| [STATUS.MD](docs/STATUS.MD) | Feature matrix and changelog |
| [TIME_SYSTEM.MD](docs/TIME_SYSTEM.MD) | Timekeeping |
| [VIDEO.MD](docs/VIDEO.MD) | Graphics subsystem |

---

## Code Size

`cloc kernel userland drivers` — April 17, 2026

| Language | Files | Blank | Comment | Code |
|---|---:|---:|---:|---:|
| C | 110 | 5764 | 4238 | 25358 |
| C/C++ Header | 94 | 1423 | 1998 | 4973 |
| Assembly | 9 | 182 | 274 | 573 |
| Linker Script | 2 | 23 | 46 | 83 |
| **Total** | **215** | **7392** | **6556** | **30987** |

---

## License

MIT

## Acknowledgments

Design inspiration from Windows NT (process and object model), MS-DOS 4.0 beta (segment-based memory, CSID job control), and the OSDev Wiki (x86\_64 tutorials).
