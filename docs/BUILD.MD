# GraceOS Build Guide

**Last Updated**: January 26, 2026

## Prerequisites

### Required Tools

The `tools/` folder is **not included** in the repository (too large for git). You must download and set up the following:

#### 1. FASM (Flat Assembler) 1.73.34
- **Download**: https://flatassembler.net/fasmw17334.zip
- **Extract to**: `tools/fasmw17334/`
- **Verify**: `tools/fasmw17334/fasm.exe` exists

#### 2. LLVM/Clang Toolchain
- **Download**: https://github.com/llvm/llvm-project/releases
- **Get**: `LLVM-<version>-win64.exe` (installer) or `clang+llvm-<version>-x86_64-pc-windows-msvc.tar.xz`
- **Extract to**: `tools/clang/`
- **Required binaries**:
  - `tools/clang/bin/clang.exe` - C compiler
  - `tools/clang/bin/ld.lld.exe` - Linker
  - `tools/clang/bin/llvm-objdump.exe` - Disassembler (optional)

#### 3. GRUB2 (via WSL)
- `grub2-mkrescue` - Creates bootable ISO

#### 4. QEMU (for testing)
- `qemu-system-x86_64` - x86_64 emulator
- **Download**: https://www.qemu.org/download/#windows
- Add to system PATH

### Expected tools/ Structure
```
tools/
├── fasmw17334/
│   ├── fasm.exe
│   ├── INCLUDE/
│   └── ...
└── clang/
    ├── bin/
    │   ├── clang.exe
    │   ├── ld.lld.exe
    │   └── llvm-objdump.exe
    ├── include/
    └── lib/
```

### WSL Setup (Windows)
```bash
# Install WSL if not already installed
wsl --install

# In WSL, install GRUB tools
sudo apt update
sudo apt install grub2-common grub-pc-bin xorriso
```

## Quick Build

```cmd
cd E:\Projects\GraceOS
make clean
make run
```

This will:
1. Clean previous build artifacts
2. Compile all source files
3. Link the kernel
4. Create bootable ISO
5. Launch QEMU

## Build Process Details

### 1. Assemble Boot Code (FASM)
```cmd
tools/fasmw17334/fasm.exe boot/boot.asm build/boot.o
tools/fasmw17334/fasm.exe kernel/arch/x86_64/idt.asm build/kernel/arch/x86_64/idt_asm.o
tools/fasmw17334/fasm.exe kernel/sys/syscall.asm build/kernel/sys/syscall_asm.o
tools/fasmw17334/fasm.exe kernel/sys/power.asm build/kernel/sys/power_asm.o
tools/fasmw17334/fasm.exe kernel/proc/context.asm build/kernel/proc/context_asm.o
```

### 2. Compile C Sources (Clang)
```cmd
tools/clang/bin/clang.exe --target=x86_64-elf -ffreestanding \
    -fno-stack-protector -fno-pic -m64 -O2 -Wall -Wextra \
    -nostdlib -nostdinc -c <source.c> -o <output.o>
```

**Compiler Flags**:
| Flag | Purpose |
|------|---------|
| `--target=x86_64-elf` | Cross-compile for x86_64 |
| `-ffreestanding` | Freestanding environment |
| `-fno-stack-protector` | Disable stack canaries |
| `-fno-pic` | No position-independent code |
| `-m64` | 64-bit code generation |
| `-O2` | Optimization level 2 |
| `-nostdlib -nostdinc` | No host libraries/headers |

### 3. Link Kernel (LLD)
```cmd
tools/clang/bin/ld.lld.exe -nostdlib -z max-page-size=0x1000 \
    -T kernel/arch/x86_64/linker.ld \
    <all object files> \
    -o build/kernel.elf
```

### 4. Create ISO (GRUB2)
```cmd
wsl grub2-mkrescue -o graceos.iso iso
```

## Directory Structure

```
build/
├── boot.o                      # Bootloader
├── kernel/
│   ├── core/                   # kmain, sysinfo, multiboot
│   ├── arch/x86_64/            # IDT, I/O
│   ├── mm/                     # Memory managers
│   │   ├── pmm/               # Physical memory
│   │   ├── vmm/               # Virtual memory
│   │   ├── sasy/              # Segment allocator
│   │   └── fallback/          # Bootstrap allocator
│   ├── proc/                   # Process manager
│   ├── sys/                    # Syscalls, power
│   └── log/                    # Kernel logging
├── drivers/
│   ├── video/                  # VGA, TTY
│   ├── input/                  # Keyboard
│   └── storage/                # BranchFS
├── lib/
│   ├── libc/                   # String, int, float
│   ├── libtranslate/            # POSIX-style libc shim for userland apps
│   └── libgrace/               # Syscall wrappers
└── userland/
    └── shell/                  # Shell commands
```

## Make Targets

| Target | Description |
|--------|-------------|
| `make` | Build kernel.elf and graceos.iso |
| `make run` | Build and launch QEMU |
| `make clean` | Remove build/ and graceos.iso |
| `make kernel` | Build kernel.elf only |
| `make iso` | Create ISO from existing kernel |

## Ports (Optional)

Ports are built separately from the kernel and live under [ports/README.MD](../ports/README.MD).

Common port targets:

```cmd
make PORTS=1
make port-libsodium
make port-7zip
```

Notes:
- `port-7zip` depends on `port-libsodium`, so it builds libsodium first.
- The 7zip port uses libsodium for SHA-256 while keeping 7zip's AES implementation for encryption.
- Crypto access is gated by SPM permission `crypto/7zip` with `PERM_EXEC`.

## Testing

### Run in QEMU
```cmd
make run
# or
qemu-system-x86_64 -cdrom graceos.iso -m 1024M -serial stdio
```

### Debug with GDB
```cmd
# Terminal 1: Start QEMU with GDB server
qemu-system-x86_64 -cdrom graceos.iso -m 1024M -s -S

# Terminal 2: Connect GDB
gdb build/kernel.elf
(gdb) target remote :1234
(gdb) break kmain
(gdb) continue
```

### Disassemble Kernel
```cmd
tools/clang/bin/llvm-objdump.exe -d build/kernel.elf > disasm.txt
```

## Troubleshooting

### "grub2-mkrescue not found"
Make sure WSL is installed and GRUB tools are available:
```bash
wsl which grub2-mkrescue
```

### "Cannot find -lc"
The compiler is trying to link standard library. Ensure `-nostdlib` flag is used.

### "Undefined reference to __stack_chk_fail"
Stack protector is enabled. Ensure `-fno-stack-protector` flag is used.

### QEMU shows blank screen
Check that the Multiboot2 header is at the start of the kernel:
```cmd
tools/clang/bin/llvm-objdump.exe -s build/kernel.elf | findstr "E85250D6"
```

### Kernel crashes immediately
Run with debug output:
```cmd
qemu-system-x86_64 -cdrom graceos.iso -m 1024M -d int,cpu_reset -D debug.log
```
