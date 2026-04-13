# GraceOS (Lumina Kernel)

**Version**: v0.6.0 (Kernel v0.0.3-dev)

A minimal 64-bit operating system built on the **Lumina Kernel**, inspired by Windows NT and MS-DOS 4.0.

## Overview

GraceOS (formerly Grace-Kernel) is a hobby operating system built from scratch for x86_64. It features true multitasking, segment-based memory management, a B+ tree filesystem, and a syscall-based userland interface.

### Features

- **Preemptive Multitasking**: Hybrid SJF/FCFS scheduler with aging
- **Lumina Kernel**: NT-style executive with full PCB and job trees
- **Memory Management**: SASY segment allocator with handle-based access
- **Security**: SPM policy manager with user/permission tables and userland `spm` tool
- **Filesystem**: BranchFS using in-memory B+ trees
- **Graphics**: VBE Framebuffer with Raylib compatibility layer
- **Service Orchestration**: minit service tree with restart policy for core services
- **Input**: PS/2 keyboard driver with IRQ + polling service
- **Networking**: virtio-net PCI driver, Ethernet/ARP/IPv4/ICMP/TCP stack, HTTP/1.1 client
- **LLM Runtime**: In-kernel GGUF inference engine — transformer forward pass, quantization, chat templates, LoRA
- **Userland**: Interactive shell, Snake game, custom syscall interface (INT 0x80)
- **libtranslate**: POSIX-style libc shim for non-POSIX userland apps
- **Time**: Real-time clock, PIT-based timekeeping, sub-millisecond precision
- **Power**: ACPI shutdown and reboot support

## Quick Start

### Requirements

- Windows 10/11 with WSL (for GRUB)
- QEMU (for emulation)
- FASM and Clang/LLVM (see `docs/BUILD.MD`)

### Build and Run

```cmd
cd E:\Projects\GraceOS
make clean
make run
```

This builds the kernel, creates a bootable ISO, and launches QEMU.

### Shell Commands

| Command | Description |
|---------|-------------|
| `help` | Show available commands |
| `sysinfo` | Display system and kernel information |
| `ls`, `mkfile`, `find`, `rm`, `cat` | BranchFS file operations |
| `cd [path]`, `chdir [path]` | Change current directory |
| `mkdir <name>` | Create directory at current path |
| `now` | Show current date and time |
| `netstat` | Show active network device status |
| `ifconfig [ip gw mask]` | Show or set static IP config |
| `ping <ip> [count]` | ICMP echo test |
| `fetch <ip> [path] [outfile]` | HTTP GET, optionally save response body to BranchFS |
| `quill <file>` | Text editor |
| `chivm` / `chivm build` / `run` | ChibiVM REPL, compiler, and program launcher |
| `7c encode` / `7c decode` | ZIP store-only tool (encode/decode to BranchFS) |
| `snake` | Play Snake game |
| `spm ...` | Security Policy Manager user/permission commands |
| `shutdown`, `reboot`, `halt` | Power controls |

## Architecture

```
User Applications (Shell, Snake, GraceBASIC)
            |
    Syscall Interface (INT 0x80, 80+ calls)
            |
    Lumina Kernel Executive
    - Process Manager (PCB, CSID, fork/exec/wait)
    - Memory Manager (SASY segments, PMM, VMM)
    - Scheduler (Hybrid SJF/FCFS)
    - Graphics (Framebuffer, Blitting)
    - LLM Runtime (GGUF inference, 13 syscalls)
    - Network Stack (Ethernet/ARP/IPv4/ICMP/TCP/HTTP)
            |
    Device Drivers
    - VGA/TTY & VBE Framebuffer
    - PS/2 Keyboard
    - RTC & PIT
    - BranchFS
    - virtio-net (QEMU usermode networking)
            |
    Hardware Abstraction
    - PMM Bitmap
    - 4-Level Paging
    - IDT/ISR
```

## Project Structure

```
GraceOS/
  boot/                   Bootloader (Multiboot2, FASM)
  kernel/                 Lumina Kernel Source
    arch/x86_64/          IDT, VBE, I/O ports, linker script
    core/                 kmain, sysinfo, multiboot, time
    font/                 PSF2 font renderer
    include/              Internal kernel headers (syscall.h, time.h)
    llm/                  In-kernel LLM inference engine (GGUF)
    log/                  Kernel logging (klog)
    mm/                   Memory management
      pmm/                Physical Memory Manager (bitmap)
      vmm/                Virtual Memory Manager (4-level paging)
      sasy/               Segment Allocator (handle-based)
      fallback/           Bootstrap allocator
    net/                  Network stack
      arp/                ARP layer (32-entry cache)
      ethernet/           Ethernet frame dispatch
      http/               HTTP/1.1 client
      icmp/               ICMP echo/echo-reply support
      ip/                 IPv4 layer
      tcp/                TCP client state machine
    proc/                 Process Manager
      context.asm         Context switch (assembly)
      proc/sched/fork/    PCB, scheduler, fork, exec, wait, pipe, job
    spm/                  Security Policy Manager
    sys/                  Syscall dispatcher + handlers
      sys_net.c           Networking syscalls (120-126)
      sys_proc.c          Process syscalls
      sys_time.c          Time syscalls
      sys_sysinfo.c       Sysinfo syscall
  drivers/                Device drivers
    input/                PS/2 keyboard, mouse
    net/                  virtio-net PCI driver
    storage/              BranchFS, RTC, IDE, NVMe, PMEM
    video/                VGA, TTY, Framebuffer, serial, text
  lib/
    libc/                 C library subset (string, float, array, time)
    libtranslate/          POSIX-style translation layer (stdlib/stdio/ctype/math)
    libgrace/             Syscall wrappers + LLM userland API
  include/grace/          Shared userland headers
    llm_syscalls.h        LLM syscall numbers (150-162)
    net_syscalls.h        Net syscall numbers (120-126)
    sysinfo.h             Sysinfo structure
  userland/
    basic/                GraceBASIC interpreter
    chivm/                ChibiVM runtime + CMDT compiler
    coreutils/            now, timestat utilities
    doas/                 Privilege escalation (stub)
    gui/                  GUI application
    quill/                Quill text editor
    raylib/               Raylib compatibility shim
    raygui/               RayGUI compatibility shim
    shell/                Interactive shell + history
    snake/                Snake game
    spm/                  Security Policy Manager CLI
  ports/                  BSD-style ports tree (third-party software)
  grub/                   GRUB2 config and themes
  iso/                    Bootable ISO staging area
  docs/                   Documentation (17 files)
```

## Ports

GraceOS includes a BSD-style ports tree for third-party software. See [docs/PORTS.MD](docs/PORTS.MD) and [ports/README.MD](ports/README.MD) for details.

- Build all ports: `make PORTS=1`
- Build a single port: `make port-7zip` or `make port-libsodium`

Current ports:
- 7zip (C-only decoder, BranchFS I/O) + `7c` ZIP store-only tool
- libsodium (static crypto library)

## ChibiVM (Step 1)

ChibiVM is a lightweight VM toolchain in userland with three shell entry points:

- `chivm` opens the interactive REPL runtime.
- `chivm build <src> <out.cmdt>` compiles source into CMDT (Intel-HEX-like) format.
- `run <file.cmdt>` executes a CMDT program through the process manager exec path.

Current Step 1 language features:

- `int` variable declarations and assignments
- `dinput("prompt", var)` integer input
- `cprintf("...")` output with `${var}` interpolation

Example workflow:

```text
chivm build hello.chi hello.cmdt
run hello.cmdt
```

ChibiVM files are stored in BranchFS, so compiled CMDT artifacts can be listed, read, and removed with normal shell file commands (`ls`, `cat`, `rm`).

## Documentation

- **[API.MD](docs/API.MD)** - System API and Syscall Reference
- **[CHIVM.MD](docs/CHIVM.MD)** - ChibiVM language, REPL, and CMDT workflow
- **[CONTRIBUTORS.MD](docs/CONTRIBUTORS.MD)** - Project Credits
- **[STATUS.MD](docs/STATUS.MD)** - Feature Matrix
- **[KERNEL.MD](docs/KERNEL.MD)** - Kernel Internals
- **[LLM.MD](docs/LLM.MD)** - LLM Runtime Reference
- **[ARCHITECTURE.MD](docs/ARCHITECTURE.MD)** - System Architecture
- **[VIDEO.MD](docs/VIDEO.MD)** - Graphics Subsystem

## Code Size Snapshot

Source: `cloc drivers kernel userland` (April 13, 2026)

| Language | Files | Blank | Comment | Code |
|----------|------:|------:|--------:|-----:|
| C | 95 | 4903 | 3605 | 20996 |
| C/C++ Header | 78 | 1093 | 1653 | 2810 |
| Assembly | 7 | 169 | 199 | 526 |
| Linker Script | 1 | 9 | 24 | 39 |
| **Total** | **181** | **6174** | **5481** | **24371** |

## License

MIT

## Acknowledgments

Design inspiration from Windows NT for the process and object model, from the MS-DOS 4.0 beta for segment-based memory and CSID job control, and from the OSDev Wiki for x86_64 tutorials.
