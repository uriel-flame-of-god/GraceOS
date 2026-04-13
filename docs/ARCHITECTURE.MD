# GraceOS Architecture (Lumina Kernel)

**Last Updated**: March 9, 2026

## Overview

GraceOS is a minimal x86_64 OS built on top of the **Lumina Kernel**, featuring NT/MS-DOS 4 inspired design. It features a monolithic architecture with preemptive multitasking, segment-based memory management, and a syscall-based userland interface.

## System Architecture

### Platform
- **Architecture**: x86_64 (64-bit)
- **CPU Mode**: Long Mode (64-bit protected mode)
- **Boot Protocol**: Multiboot2
- **Bootloader**: GRUB2
- **Assembler**: FASM 1.73.34
- **Compiler**: Clang/LLVM (freestanding target)
- **Linker**: LLD

### Component Hierarchy

```
┌─────────────────────────────────────────────────────────┐
│                    User Applications                     │
│            (Shell, Quill Editor, GraceBASIC)            │
├─────────────────────────────────────────────────────────┤
│                    Syscall Interface                     │
│              INT 0x80 (50+ system calls)                │
├─────────────────────────────────────────────────────────┤
│                   Kernel Executive                       │
│  ┌──────────┬──────────┬──────────┬──────────────────┐ │
│  │ Process  │  Memory  │ Scheduler│     Security     │ │
│  │ Manager  │  (SASY)  │ (Hybrid) │  (Capabilities)  │ │
│  └──────────┴──────────┴──────────┴──────────────────┘ │
├─────────────────────────────────────────────────────────┤
│                   Device Drivers                         │
│  ┌────────┬──────────┬──────────┬────────┬─────────┐  │
│  │  VGA   │ Keyboard │ BranchFS │ Power  │virtio-  │  │
│  │ (TTY)  │  (PS/2)  │  (BFS)   │(ACPI)  │  net    │  │
│  └────────┴──────────┴──────────┴────────┴─────────┘  │
├─────────────────────────────────────────────────────────┤
│                Hardware Abstraction                      │
│         PMM (Bitmap) → VMM (Paging) → IDT (x86_64)     │
└─────────────────────────────────────────────────────────┘
```

## Boot Process

### Stage 1: Bootloader (boot.asm)
```
[GRUB Loads Kernel]
    ↓
[Multiboot2 Header Check]
    ↓
[32-bit Entry Point (_start)]
    ↓
[Save Multiboot Info in EDI]
    ↓
[Setup Initial Stack]
    ↓
[Create Page Tables]
    ├─ PML4T[0] → PDPT at 0x2000
    ├─ PDPT[0]  → PDT at 0x3000
    └─ PDT[0..255] → 256 × 2MB pages
    ↓
[Enable PAE + Long Mode + Paging]
    ↓
[Load 64-bit GDT]
    ↓
[Enable SSE/SSE2]
    ↓
[Call kmain()]
```

### Stage 2: Kernel Initialization (kmain.c)
```
kmain()
    ↓
[Initialize VGA + TTY]
    ↓
[Parse Multiboot2 Info]
    ↓
[Initialize IDT]
    ↓
[Initialize PMM (Bitmap)]
    ↓
[Initialize VMM (Paging)]
    ↓
[Initialize SASY (Segments)]
    ↓
[Initialize Process Manager]
    ↓
[Initialize Scheduler]
    ↓
[Initialize Syscalls]
    ↓
[Initialize BranchFS]
    ↓
[Initialize Network Stack]
    ↓
[Enter Shell]
```

## Memory Layout

### Physical Memory Map
```
0x00000000 - 0x00000FFF   BIOS/IVT (4KB)
0x00001000 - 0x00003FFF   Page Tables (12KB)
0x00100000 - 0x001FFFFF   Kernel Code (~1MB)
0x00200000 - 0x003FFFFF   Kernel Heap (2MB)
0x00400000 - 0x00FFFFFF   PMM Bitmap + SASY Pool
0x01000000 - 0x1FFFFFFF   User Memory (~496MB)
```

### Memory Subsystems

| Layer | Location | Purpose |
|-------|----------|---------|
| PMM | kernel/mm/pmm/ | Physical frame allocation (bitmap) |
| VMM | kernel/mm/vmm/ | Virtual memory and paging |
| SASY | kernel/mm/sasy/ | Segment allocation (handles) |
| kheap | kernel/mm/kheap.c | Kernel dynamic allocation |

## Process Manager

### Process Control Block (PCB)
```c
typedef struct process {
    /* Identity */
    pid_t pid;              // Process ID
    csid_t csid;            // Command Session ID
    pid_t parent;           // Parent PID
    
    /* State */
    proc_state_t state;     // READY, RUNNING, BLOCKED, etc.
    int valid;              // Security validation flag
    
    /* Timing */
    uint64_t cpu_time;      // Total CPU time
    uint64_t est_runtime;   // Estimated burst (SJF)
    uint64_t wait_time;     // Wait time (aging)
    
    /* Memory */
    uint64_t pml4;          // Page table root
    seg_handle_t segments[16];  // SASY segments
    
    /* Credentials */
    uid_t uid, euid;        // User IDs
    uint32_t caps;          // Capabilities
    
    /* Context */
    struct cpu_context ctx; // Saved registers
} process_t;
```

### Scheduler Algorithm
Hybrid SJF + FCFS + Aging:
```
score = est_runtime - (wait_time / 100) + invalid_penalty
```
- Lower score = higher priority
- Wait time reduces score (anti-starvation)
- Invalid processes get +10000 penalty

## Syscall Interface

### Register ABI (INT 0x80)
| Register | Purpose |
|----------|---------|
| RAX | Syscall number |
| RDI | Argument 1 |
| RSI | Argument 2 |
| RDX | Argument 3 |
| R10 | Argument 4 |
| R8 | Argument 5 |
| R9 | Argument 6 |
| RAX | Return value |

### Syscall Categories
| Range | Category |
|-------|----------|
| 1-10 | Basic I/O |
| 20-29 | System info |
| 30-39 | SASY segments |
| 40-49 | Process management |
| 50-59 | Power management |
| 60-69 | Framebuffer graphics |
| 70 | IPC (pipe) |
| 120-124 | Networking |
| 150-162 | LLM Runtime |

## Device Drivers

### VGA Driver
- Text mode 80×25
- 16-color support
- Memory-mapped at 0xB8000

### Keyboard Driver
- PS/2 scancode translation
- US QWERTY layout
- 256-byte circular buffer

### Network Stack
- virtio-net PCI driver (legacy virtio)
- Ethernet, ARP, IPv4, TCP layers
- HTTP/1.1 client (buffered + streaming)
- Static IP: 10.0.0.2 (QEMU usermode networking)

### BranchFS
- B+ tree structure
- O(log n) operations
- In-memory storage

### Power Management
- Reboot: Keyboard controller (0xFE to port 0x64)
- Shutdown: QEMU ACPI (0x2000 to port 0x604)
- Halt: CLI + HLT loop

## Build System

### Toolchain
| Tool | Purpose |
|------|---------|
| FASM | Assembly (boot, context switch) |
| Clang | C compilation (--target=x86_64-elf) |
| LLD | Linking |
| GRUB2 | ISO creation |

### Build Artifacts
```
build/
├── boot.o                  # Bootloader
├── kernel/
│   ├── core/*.o           # Kernel core
│   ├── mm/**/*.o          # Memory managers
│   ├── proc/*.o           # Process manager
│   └── sys/*.o            # Syscalls
├── drivers/**/*.o         # Device drivers
├── lib/**/*.o             # Libraries
└── kernel.elf             # Final kernel
```

## Design Inspiration

- **Windows NT**: Process/object model, executive architecture
- **MS-DOS 4.0 Beta**: Segment-based memory, CSID job control
- **Unix**: fork/exec/wait process model
