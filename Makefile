# ============================
# GraceOS Build System
# ============================

NAME := graceos


# ============================
# Directories
# ============================

BUILD   := build
BOOT    := boot
KERNEL  := kernel
LIB     := lib
DRIVERS := drivers
TOOLS   := tools
ISO_DIR := iso

# ============================
# Ports (optional)
# ============================

PORTS_DIR := ports
PORTS_LIST := 7zip sqlite libsodium
PORTS_ENABLED := 0
ifneq (,$(filter ports port -port,$(MAKECMDGOALS)))
PORTS_ENABLED := 1
endif
ifneq ($(PORTS),)
PORTS_ENABLED := 1
endif
ifeq ($(PORTS_ENABLED),1)
ALL_TARGETS := ports iso
else
ALL_TARGETS := iso
endif


# ============================
# Tools
# ============================

FASM := fasm

CLANG := clang
LLD   := ld.lld

OBJCOPY := llvm-objcopy
GRUB_MKRESCUE := wsl grub2-mkrescue

CC := $(CLANG) --target=x86_64-elf
QEMU ?= qemu-system-x86_64
QEMU_LOG := qemu.log
DISK_IMG := disk.img
DISK_SIZE := 64M
QEMU_NET   := -netdev user,id=net0 -device virtio-net-pci,netdev=net0,mac=52:54:00:12:34:56
QEMU_AUDIODEV ?= -audiodev dsound,id=snd0
QEMU_AUDIO := $(QEMU_AUDIODEV) -machine pcspk-audiodev=snd0 -device intel-hda -device hda-duplex,audiodev=snd0


# ============================
# Flags
# ============================

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic \
		  -m64 -O2 -Wall -Wextra \
		  -nostdlib -nostdinc -Ilib/libc -Ilib/libtranslate -Iinclude

ASFLAGS := -ffreestanding -m64

LDFLAGS := -nostdlib -z max-page-size=0x1000


# ============================
# Files
# ============================

FONT_FILE := kernel/font/default.psf2
FONT_OBJ  := build/kernel/font/default_font.o

# Audio samples (embedded via llvm-objcopy, same technique as font)
SAMPLE_MP3_SRC := kernel/samples/sample.mp3
SAMPLE_MP3_OBJ := build/kernel/samples/sample_mp3.o


# Boot
BOOT_OBJ := build/boot.o


# Kernel
KERNEL_ASM_OBJ := build/kernel/core/main.o
KERNEL_IDT_OBJ := build/kernel/arch/x86_64/idt_asm.o
KERNEL_C_OBJ   := build/kernel/core/kmain.o
KERNEL_IDT_C   := build/kernel/arch/x86_64/idt.o
KERNEL_SYSINFO := build/kernel/core/sysinfo.o
KERNEL_MULTIBOOT := build/kernel/core/multiboot.o
KERNEL_KHEAP := build/kernel/mm/kheap.o
KERNEL_PMM := build/kernel/mm/pmm/pmm.o
KERNEL_BITMAP := build/kernel/mm/pmm/bitmap.o
KERNEL_FALLBACK := build/kernel/mm/fallback/simple_fallback.o
KERNEL_VMM := build/kernel/mm/vmm/vmm.o
KERNEL_PAGING := build/kernel/mm/vmm/paging.o
KERNEL_PORT := build/kernel/arch/x86_64/io/port.o
KERNEL_KLOG := build/kernel/log/klog.o
KERNEL_VBE_C := build/kernel/arch/x86_64/vbe.o
KERNEL_VBE_ASM := build/kernel/arch/x86_64/vbe_asm.o
KERNEL_FONT := build/kernel/font/font.o
KERNEL_TEXT := build/drivers/video/text.o

# SASY (Segment Allocator System)
KERNEL_SASY := build/kernel/mm/sasy/sasy.o
KERNEL_SASY_HANDLE := build/kernel/mm/sasy/handle.o
KERNEL_SASY_SWAP := build/kernel/mm/sasy/swap.o
KERNEL_SASY_LOADER := build/kernel/mm/sasy/loader.o

# Syscall
SYSCALL_ASM_OBJ := build/kernel/sys/syscall_asm.o
SYSCALL_C_OBJ   := build/kernel/sys/syscall.o
SYSINFO_SYS_OBJ := build/kernel/sys/sys_sysinfo.o
SYSPROC_OBJ     := build/kernel/sys/sys_proc.o
SYSTIME_OBJ     := build/kernel/sys/sys_time.o
POWER_ASM_OBJ   := build/kernel/sys/power_asm.o
POWER_C_OBJ     := build/kernel/sys/power.o

# Time
KERNEL_TIME_OBJ := build/kernel/core/time.o

# Process Manager
PROC_OBJ        := build/kernel/proc/proc.o
PROC_TABLE_OBJ  := build/kernel/proc/table.o
PROC_SCHED_OBJ  := build/kernel/proc/sched.o
PROC_PIPE_OBJ   := build/kernel/proc/pipe.o
PROC_CONTEXT_OBJ := build/kernel/proc/context.o
PROC_CONTEXT_ASM := build/kernel/proc/context_asm.o
PROC_EXEC_OBJ   := build/kernel/proc/exec.o
PROC_FORK_OBJ   := build/kernel/proc/fork.o
PROC_WAIT_OBJ   := build/kernel/proc/wait.o
PROC_ZOMBIE_OBJ := build/kernel/proc/zombie.o
PROC_JOB_OBJ    := build/kernel/proc/job.o
PROC_SECURITY_OBJ := build/kernel/proc/security.o

# minit — Process Orchestration Layer
MINIT_TREE_OBJ    := build/kernel/proc/minit/tree.o
MINIT_EXHAUST_OBJ := build/kernel/proc/minit/exhaustion.o
MINIT_ORCH_OBJ    := build/kernel/proc/minit/orchestration.o

# spm — Security Policy Manager
SPM_OBJ := build/kernel/spm/spm.o

# Disk syscalls
SYS_DISK_OBJ := build/kernel/sys/sys_disk.o

# spm syscalls
SYS_SPM_OBJ := build/kernel/sys/sys_spm.o

# cfdisk — Userland Partition Manager
CFDISK_OBJ := build/userland/coreutils/cfdisk/cfdisk.o \
              build/userland/coreutils/cfdisk/disk.o \
              build/userland/coreutils/cfdisk/mbr.o \
              build/userland/coreutils/cfdisk/gpt.o \
              build/userland/coreutils/cfdisk/ui.o \
              build/userland/coreutils/cfdisk/input.o \
              build/userland/coreutils/cfdisk/operations.o

# Shell (linked into kernel for now)
SHELL_OBJ := build/userland/shell/shell.o
SHELL_SYSINFO_OBJ := build/userland/shell/cmd_sysinfo.o
SHELL_HISTORY_OBJ := build/userland/shell/history.o

# Quill editor
QUILL_OBJ := build/userland/quill/quill.o

# ChibiVM (step 1)
CHIVM_OBJ := build/userland/chivm/chivm.o

# 7c (ZIP utility)
SEVENZIP_OBJ := build/userland/7z/7c.o

# GUI
RAYLIB_OBJ := build/userland/raylib/raylib.o
RAYGUI_OBJ := build/userland/raygui/raygui.o
GUI_OBJ    := build/userland/gui/gui.o

# Snake Game - NEW
SNAKE_OBJ  := build/userland/snake/snake.o

# Coreutils
NOW_OBJ := build/userland/coreutils/now.o
# SPM CLI + doas
SPMCLI_OBJ := build/userland/spm/spm.o
DOAS_OBJ := build/userland/doas/doas.o
# TIMESTAT_OBJ := build/userland/coreutils/timestat.o  # Separate executable

# Lib
LIB_STRING_OBJ := build/lib/libc/string.o
LIB_FLOAT_OBJ  := build/lib/libc/float.o
LIB_ARRAY_OBJ  := build/lib/libc/array.o
LIB_TIME_OBJ   := build/lib/libc/time.o
LIB_TIMEPROFILE_OBJ := build/lib/libc/timeprofile.o
LIB_TIMESYSCALL_OBJ := build/lib/libc/time_syscall.o

LIB_TRANSLATE_STDLIB_OBJ := build/lib/libtranslate/stdlib.o
LIB_TRANSLATE_STDIO_OBJ := build/lib/libtranslate/stdio.o
LIB_TRANSLATE_CTYPE_OBJ := build/lib/libtranslate/ctype.o
LIB_TRANSLATE_MATH_OBJ := build/lib/libtranslate/math.o

LIB_GRACE_SYSINFO_OBJ := build/lib/libgrace/sysinfo.o
LIB_GRACE_SYSCALL_ASM_OBJ := build/lib/libgrace/syscall_asm.o
LIB_GRACE_SYSCALL_C_OBJ := build/lib/libgrace/syscall.o
LIB_GRACE_LLM_OBJ := build/lib/libgrace/llm.o

LIB_OBJ := $(LIB_STRING_OBJ) $(LIB_FLOAT_OBJ) $(LIB_ARRAY_OBJ) $(LIB_TIME_OBJ) $(LIB_TIMEPROFILE_OBJ) $(LIB_TIMESYSCALL_OBJ) \
		   $(LIB_TRANSLATE_STDLIB_OBJ) $(LIB_TRANSLATE_STDIO_OBJ) $(LIB_TRANSLATE_CTYPE_OBJ) $(LIB_TRANSLATE_MATH_OBJ) \
		   $(LIB_GRACE_SYSINFO_OBJ) $(LIB_GRACE_SYSCALL_ASM_OBJ) $(LIB_GRACE_SYSCALL_C_OBJ)

# LLM Runtime kernel objects
LLM_UNICODE_OBJ      := build/kernel/llm/unicode.o
LLM_UNICODE_DATA_OBJ := build/kernel/llm/unicode-data.o
LLM_ARCH_OBJ         := build/kernel/llm/llama-arch.o
LLM_IMPL_OBJ         := build/kernel/llm/llama-impl.o
LLM_IO_OBJ           := build/kernel/llm/llama-io.o
LLM_MMAP_OBJ         := build/kernel/llm/llama-mmap.o
LLM_QUANT_OBJ        := build/kernel/llm/llama-quant.o
LLM_CONTEXT_OBJ      := build/kernel/llm/llama-context.o
LLM_SAMPLER_OBJ      := build/kernel/llm/llama-sampler.o
LLM_CHAT_OBJ         := build/kernel/llm/llama-chat.o
LLM_ADAPTER_OBJ      := build/kernel/llm/llama-adapter.o
LLM_RUNTIME_OBJ      := build/kernel/llm/runtime.o
LLM_OBJ := $(LLM_UNICODE_OBJ) $(LLM_UNICODE_DATA_OBJ) $(LLM_ARCH_OBJ) \
           $(LLM_IMPL_OBJ) $(LLM_IO_OBJ) $(LLM_MMAP_OBJ) $(LLM_QUANT_OBJ) \
           $(LLM_CONTEXT_OBJ) $(LLM_SAMPLER_OBJ) $(LLM_CHAT_OBJ) \
           $(LLM_ADAPTER_OBJ) $(LLM_RUNTIME_OBJ)

# Drivers
VGA_OBJ      := build/drivers/video/vga.o
TTY_OBJ      := build/drivers/video/tty.o
FB_OBJ       := build/drivers/video/fb.o
KEYBOARD_OBJ := build/drivers/input/keyboard.o
BFS_OBJ      := build/drivers/storage/bfs.o
RTC_OBJ      := build/drivers/storage/rtc.o
IDE_OBJ      := build/drivers/storage/ide.o
NVME_OBJ     := build/drivers/storage/nvme.o
PMEM_OBJ     := build/drivers/storage/pmem.o
SERIAL_OBJ   := build/drivers/video/serial.o
MOUSE_OBJ := build/drivers/input/mouse.o

# Audio driver (Intel HDA)
HDA_OBJ := build/drivers/audio/hda.o
ISA_SPK_OBJ := build/drivers/audio/isa_speaker.o
SB16_OBJ := build/drivers/audio/sb16.o
AC97_OBJ := build/drivers/audio/ac97.o

# VoiceBox kernel audio subsystem
VBOX_CORE_OBJ   := build/kernel/audio/voicebox.o
VBOX_SERVER_OBJ := build/kernel/audio/voicebox_server.o
AUDIO_CLOCK_OBJ := build/kernel/audio/audio_clock.o
SYS_AUDIO_OBJ   := build/kernel/sys/sys_audio.o

# mplayer CLI
MPLAYER_OBJ := build/userland/mplayer/mplayer.o

# Network driver
VIRTIO_NET_OBJ := build/drivers/net/virtio_net.o

# Kernel networking stack
NET_OBJ      := build/kernel/net/net.o
NET_ETH_OBJ  := build/kernel/net/ethernet/ethernet.o
NET_ARP_OBJ  := build/kernel/net/arp/arp.o
NET_IP_OBJ   := build/kernel/net/ip/ipv4.o
NET_TCP_OBJ  := build/kernel/net/tcp/tcp.o
NET_HTTP_OBJ := build/kernel/net/http/http_client.o
NET_ICMP_OBJ := build/kernel/net/icmp/icmp.o
SYS_NET_OBJ  := build/kernel/sys/sys_net.o

# All objects (including Snake)
OBJ := \
	$(KERNEL_ASM_OBJ) \
	$(KERNEL_IDT_OBJ) \
	$(KERNEL_C_OBJ) \
	$(KERNEL_IDT_C) \
	$(KERNEL_SYSINFO) \
	$(KERNEL_MULTIBOOT) \
	$(KERNEL_KHEAP) \
	$(KERNEL_PMM) \
	$(KERNEL_BITMAP) \
	$(KERNEL_FALLBACK) \
	$(KERNEL_VMM) \
	$(KERNEL_PAGING) \
	$(KERNEL_PORT) \
	$(KERNEL_KLOG) \
	$(KERNEL_VBE_C) \
	$(KERNEL_VBE_ASM) \
	$(KERNEL_SASY) \
	$(KERNEL_SASY_HANDLE) \
	$(KERNEL_SASY_SWAP) \
	$(KERNEL_SASY_LOADER) \
	$(KERNEL_FONT) \
	$(KERNEL_TEXT) \
	$(FONT_OBJ) \
	$(SYSCALL_ASM_OBJ) \
	$(SYSCALL_C_OBJ) \
	$(SYSINFO_SYS_OBJ) \
	$(SYSPROC_OBJ) \
	$(SYSTIME_OBJ) \
	$(POWER_ASM_OBJ) \
	$(POWER_C_OBJ) \
	$(KERNEL_TIME_OBJ) \
	$(PROC_OBJ) \
	$(PROC_TABLE_OBJ) \
	$(PROC_SCHED_OBJ) \
	$(PROC_PIPE_OBJ) \
	$(PROC_CONTEXT_OBJ) \
	$(PROC_CONTEXT_ASM) \
	$(PROC_EXEC_OBJ) \
	$(PROC_FORK_OBJ) \
	$(PROC_WAIT_OBJ) \
	$(PROC_ZOMBIE_OBJ) \
	$(PROC_JOB_OBJ) \
	$(PROC_SECURITY_OBJ) \
	$(MINIT_TREE_OBJ) \
	$(MINIT_EXHAUST_OBJ) \
	$(MINIT_ORCH_OBJ) \
	$(SPM_OBJ) \
	$(SYS_DISK_OBJ) \
	$(SYS_SPM_OBJ) \
	$(HDA_OBJ) \
	$(ISA_SPK_OBJ) \
	$(SB16_OBJ) \
	$(AC97_OBJ) \
	$(VBOX_CORE_OBJ) \
	$(VBOX_SERVER_OBJ) \
	$(AUDIO_CLOCK_OBJ) \
	$(SYS_AUDIO_OBJ) \
	$(MPLAYER_OBJ) \
	$(SAMPLE_MP3_OBJ) \
	$(CFDISK_OBJ) \
	$(SHELL_OBJ) \
	$(SHELL_SYSINFO_OBJ) \
	$(SHELL_HISTORY_OBJ) \
	$(QUILL_OBJ) \
	$(CHIVM_OBJ) \
	$(SEVENZIP_OBJ) \
	$(RAYLIB_OBJ) \
	$(RAYGUI_OBJ) \
	$(GUI_OBJ) \
	$(SNAKE_OBJ) \
	$(NOW_OBJ) \
	$(SPMCLI_OBJ) \
	$(DOAS_OBJ) \
	$(LLM_OBJ) \
	$(LIB_OBJ) \
	$(VGA_OBJ) \
	$(TTY_OBJ) \
	$(FB_OBJ) \
	$(KEYBOARD_OBJ) \
	$(MOUSE_OBJ) \
	$(BFS_OBJ) \
	$(RTC_OBJ) \
	$(IDE_OBJ) \
	$(NVME_OBJ) \
	$(PMEM_OBJ) \
	$(SERIAL_OBJ) \
	$(VIRTIO_NET_OBJ) \
	$(NET_OBJ) \
	$(NET_ETH_OBJ) \
	$(NET_ARP_OBJ) \
	$(NET_IP_OBJ) \
	$(NET_TCP_OBJ) \
	$(NET_HTTP_OBJ) \
	$(NET_ICMP_OBJ) \
	$(SYS_NET_OBJ)


KERNEL_ELF := build\kernel.elf
ISO_FILE   := $(NAME).iso



# ============================
# Targets
# ============================

all: $(ALL_TARGETS)

ports: $(PORTS_LIST:%=port-%)
port: ports
-port: ports

port-%:
	@if exist $(PORTS_DIR)\$*\Makefile (echo Building port $* && $(MAKE) -C $(PORTS_DIR)\$*) else if exist $(PORTS_DIR)\$*\makefile (echo Building port $* && $(MAKE) -C $(PORTS_DIR)\$* -f makefile) else (echo Skipping port $*: no Makefile)

port-7zip: port-libsodium


# ----------------------------
# Directories
# ----------------------------

dirs:
	if not exist build mkdir build
	if not exist build\kernel mkdir build\kernel
	if not exist build\kernel\core mkdir build\kernel\core
	if not exist build\kernel\mm mkdir build\kernel\mm
	if not exist build\kernel\mm\pmm mkdir build\kernel\mm\pmm
	if not exist build\kernel\mm\vmm mkdir build\kernel\mm\vmm
	if not exist build\kernel\mm\fallback mkdir build\kernel\mm\fallback
	if not exist build\kernel\mm\sasy mkdir build\kernel\mm\sasy
	if not exist build\kernel\proc mkdir build\kernel\proc
	if not exist build\kernel\log mkdir build\kernel\log
	if not exist build\kernel\sys mkdir build\kernel\sys
	if not exist build\kernel\arch mkdir build\kernel\arch
	if not exist build\kernel\arch\x86_64 mkdir build\kernel\arch\x86_64
	if not exist build\kernel\arch\x86_64\io mkdir build\kernel\arch\x86_64\io
	if not exist build\kernel\llm mkdir build\kernel\llm
	if not exist build\lib mkdir build\lib
	if not exist build\lib\libc mkdir build\lib\libc
	if not exist build\lib\libtranslate mkdir build\lib\libtranslate
	if not exist build\drivers mkdir build\drivers
	if not exist build\drivers\video mkdir build\drivers\video
	if not exist build\drivers\input mkdir build\drivers\input
	if not exist build\drivers\storage mkdir build\drivers\storage
	if not exist build\userland mkdir build\userland
	if not exist build\userland\shell mkdir build\userland\shell
	if not exist build\userland\chivm mkdir build\userland\chivm
	if not exist build\userland\7z mkdir build\userland\7z
	if not exist build\userland\quill mkdir build\userland\quill
	if not exist build\userland\raylib mkdir build\userland\raylib
	if not exist build\userland\raygui mkdir build\userland\raygui
	if not exist build\userland\gui mkdir build\userland\gui
	if not exist build\userland\snake mkdir build\userland\snake
	if not exist build\userland\coreutils mkdir build\userland\coreutils
	if not exist build\userland\coreutils\cfdisk mkdir build\userland\coreutils\cfdisk
	if not exist build\userland\spm mkdir build\userland\spm
	if not exist build\userland\doas mkdir build\userland\doas
	if not exist build\kernel\spm mkdir build\kernel\spm
	if not exist build\kernel\proc\minit mkdir build\kernel\proc\minit
	if not exist build\kernel\font mkdir build\kernel\font
	if not exist build\drivers\net mkdir build\drivers\net
	if not exist build\kernel\net mkdir build\kernel\net
	if not exist build\kernel\net\ethernet mkdir build\kernel\net\ethernet
	if not exist build\kernel\net\arp mkdir build\kernel\net\arp
	if not exist build\kernel\net\ip mkdir build\kernel\net\ip
	if not exist build\kernel\net\tcp mkdir build\kernel\net\tcp
	if not exist build\kernel\net\http mkdir build\kernel\net\http
	if not exist build\kernel\audio mkdir build\kernel\audio
	if not exist build\kernel\samples mkdir build\kernel\samples
	if not exist build\drivers\audio mkdir build\drivers\audio
	if not exist build\userland\mplayer mkdir build\userland\mplayer

# Ensure all object outputs wait for directory creation in parallel builds.
$(OBJ) $(BOOT_OBJ) $(FONT_OBJ): dirs

$(FONT_OBJ): $(FONT_FILE)
	$(OBJCOPY) \
	  -I binary \
	  -O elf64-x86-64 \
	  --rename-section .data=.font,alloc,load,readonly,data,contents \
	  $< $@


# ----------------------------
# Boot
# ----------------------------

boot: dirs
	$(FASM) boot/boot.asm $(BOOT_OBJ)


# ----------------------------
# Kernel
# ----------------------------

# ChibiVM
build/userland/chivm/chivm.o: userland/chivm/chivm.c
	$(CC) $(CFLAGS) -c $< -o $@

# 7c (ZIP utility)
build/userland/7z/7c.o: userland/7z/7c.c
	$(CC) $(CFLAGS) -c $< -o $@

# Kernel ASM
build/kernel/core/main.o: kernel/core/main.S
	$(CC) $(ASFLAGS) -c $< -o $@

# Kernel IDT ASM
build/kernel/arch/x86_64/idt_asm.o: kernel/arch/x86_64/idt.asm
	$(FASM) $< $@

# Kernel C (kmain)
build/kernel/core/kmain.o: kernel/core/kmain.c
	$(CC) $(CFLAGS) -c $< -o $@

# Kernel IDT C
build/kernel/arch/x86_64/idt.o: kernel/arch/x86_64/idt.c
	$(CC) $(CFLAGS) -c $< -o $@

# Kernel Sysinfo
build/kernel/core/sysinfo.o: kernel/core/sysinfo.c
	$(CC) $(CFLAGS) -c $< -o $@

# Kernel Multiboot
build/kernel/core/multiboot.o: kernel/core/multiboot.c
	$(CC) $(CFLAGS) -c $< -o $@

#Font
# Kernel Font
build/kernel/font/font.o: kernel/font/font.c
	$(CC) $(CFLAGS) -c $< -o $@


# Memory Management

# Kernel Heap
build/kernel/mm/kheap.o: kernel/mm/kheap.c
	$(CC) $(CFLAGS) -c $< -o $@

# Simple Allocator
build/kernel/mm/simple_alloc.o: kernel/mm/simple_alloc.c
	$(CC) $(CFLAGS) -c $< -o $@

# PMM (Physical Memory Manager)
build/kernel/mm/pmm/pmm.o: kernel/mm/pmm/pmm.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/mm/pmm/bitmap.o: kernel/mm/pmm/bitmap.c
	$(CC) $(CFLAGS) -c $< -o $@

# Fallback Allocator
build/kernel/mm/fallback/simple_fallback.o: kernel/mm/fallback/simple_fallback.c
	$(CC) $(CFLAGS) -c $< -o $@

# Kernel Logger
build/kernel/log/klog.o: kernel/log/klog.c
	$(CC) $(CFLAGS) -c $< -o $@

# Kernel Time
build/kernel/core/time.o: kernel/core/time.c
	$(CC) $(CFLAGS) -c $< -o $@

# VMM (Virtual Memory Manager)
build/kernel/mm/vmm/vmm.o: kernel/mm/vmm/vmm.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/mm/vmm/paging.o: kernel/mm/vmm/paging.asm
	$(FASM) $< $@

# SASY (Segment Allocator System)
build/kernel/mm/sasy/sasy.o: kernel/mm/sasy/sasy.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/mm/sasy/handle.o: kernel/mm/sasy/handle.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/mm/sasy/swap.o: kernel/mm/sasy/swap.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/mm/sasy/loader.o: kernel/mm/sasy/loader.c
	$(CC) $(CFLAGS) -c $< -o $@

# I/O Port Layer
build/kernel/arch/x86_64/io/port.o: kernel/arch/x86_64/io/port.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/arch/x86_64/vbe.o: kernel/arch/x86_64/vbe.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/arch/x86_64/vbe_asm.o: kernel/arch/x86_64/vbe.asm
	$(FASM) $< $@


# Mouse driver
build/drivers/input/mouse.o: drivers/input/mouse.c
	if not exist build\drivers\input mkdir build\drivers\input
	$(CC) $(CFLAGS) -c $< -o $@

# ----------------------------
# Syscall
# ----------------------------

# Syscall ASM entry point
build/kernel/sys/syscall_asm.o: kernel/sys/syscall.asm
	$(FASM) $< $@

# Syscall C dispatcher
build/kernel/sys/syscall.o: kernel/sys/syscall.c
	$(CC) $(CFLAGS) -c $< -o $@

# Sys_sysinfo syscall
build/kernel/sys/sys_sysinfo.o: kernel/sys/sys_sysinfo.c
	$(CC) $(CFLAGS) -c $< -o $@

# Sys_proc syscall
build/kernel/sys/sys_proc.o: kernel/sys/sys_proc.c
	$(CC) $(CFLAGS) -c $< -o $@

# Sys_spm syscall
build/kernel/sys/sys_spm.o: kernel/sys/sys_spm.c
	$(CC) $(CFLAGS) -c $< -o $@

# Networking syscalls
build/kernel/sys/sys_net.o: kernel/sys/sys_net.c
	$(CC) $(CFLAGS) -c $< -o $@

# Serial driver - NEW
build/drivers/video/serial.o: drivers/video/serial.c
	if not exist build\drivers\video mkdir build\drivers\video
	$(CC) $(CFLAGS) -c $< -o $@

# ----------------------------
# Network Driver
# ----------------------------

build/drivers/net/virtio_net.o: drivers/net/virtio_net.c
	if not exist build\drivers\net mkdir build\drivers\net
	$(CC) $(CFLAGS) -c $< -o $@

# ----------------------------
# Kernel Network Stack
# ----------------------------

build/kernel/net/net.o: kernel/net/net.c
	if not exist build\kernel\net mkdir build\kernel\net
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/net/ethernet/ethernet.o: kernel/net/ethernet/ethernet.c
	if not exist build\kernel\net\ethernet mkdir build\kernel\net\ethernet
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/net/arp/arp.o: kernel/net/arp/arp.c
	if not exist build\kernel\net\arp mkdir build\kernel\net\arp
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/net/ip/ipv4.o: kernel/net/ip/ipv4.c
	if not exist build\kernel\net\ip mkdir build\kernel\net\ip
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/net/tcp/tcp.o: kernel/net/tcp/tcp.c
	if not exist build\kernel\net\tcp mkdir build\kernel\net\tcp
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/net/http/http_client.o: kernel/net/http/http_client.c
	if not exist build\kernel\net\http mkdir build\kernel\net\http
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/net/icmp/icmp.o: kernel/net/icmp/icmp.c
	if not exist build\kernel\net\icmp mkdir build\kernel\net\icmp
	$(CC) $(CFLAGS) -c $< -o $@

# ----------------------------
# LLM Runtime
# ----------------------------

build/kernel/llm/unicode.o: kernel/llm/unicode.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/llm/unicode-data.o: kernel/llm/unicode-data.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/llm/llama-arch.o: kernel/llm/llama-arch.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/llm/llama-impl.o: kernel/llm/llama-impl.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/llm/llama-io.o: kernel/llm/llama-io.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/llm/llama-mmap.o: kernel/llm/llama-mmap.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/llm/llama-quant.o: kernel/llm/llama-quant.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/llm/llama-context.o: kernel/llm/llama-context.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/llm/llama-sampler.o: kernel/llm/llama-sampler.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/llm/llama-chat.o: kernel/llm/llama-chat.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/llm/llama-adapter.o: kernel/llm/llama-adapter.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/llm/runtime.o: kernel/llm/runtime.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/sys/sys_time.o: kernel/sys/sys_time.c
	$(CC) $(CFLAGS) -c $< -o $@

# Power management (ASM)
build/kernel/sys/power_asm.o: kernel/sys/power.asm
	$(FASM) $< $@

# Power management (C)
build/kernel/sys/power.o: kernel/sys/power.c
	$(CC) $(CFLAGS) -c $< -o $@


# ----------------------------
# Process Manager
# ----------------------------

build/kernel/proc/proc.o: kernel/proc/proc.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/proc/table.o: kernel/proc/table.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/proc/sched.o: kernel/proc/sched.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/proc/pipe.o: kernel/proc/pipe.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/proc/context.o: kernel/proc/context.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/proc/context_asm.o: kernel/proc/context.asm
	$(FASM) $< $@

build/kernel/proc/exec.o: kernel/proc/exec.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/proc/fork.o: kernel/proc/fork.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/proc/wait.o: kernel/proc/wait.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/proc/zombie.o: kernel/proc/zombie.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/proc/job.o: kernel/proc/job.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/proc/security.o: kernel/proc/security.c
	$(CC) $(CFLAGS) -c $< -o $@

# ----------------------------
# minit — Process Orchestration Layer
# ----------------------------

build/kernel/proc/minit/tree.o: kernel/proc/minit/tree.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/proc/minit/exhaustion.o: kernel/proc/minit/exhaustion.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/proc/minit/orchestration.o: kernel/proc/minit/orchestration.c
	$(CC) $(CFLAGS) -c $< -o $@

# ----------------------------
# spm — Security Policy Manager
# ----------------------------

build/kernel/spm/spm.o: kernel/spm/spm.c
	$(CC) $(CFLAGS) -c $< -o $@

# ----------------------------
# Audio Sample Embedding
# Uses llvm-objcopy (same technique as font embedding)
# Section renamed to .sample_mp3 for the linker script
# ----------------------------

build/kernel/samples/sample_mp3.o: $(SAMPLE_MP3_SRC)
	if not exist build\kernel\samples mkdir build\kernel\samples
	$(OBJCOPY) \
	  -I binary \
	  -O elf64-x86-64 \
	  --rename-section .data=.sample_mp3,alloc,load,readonly,data,contents \
	  $< $@

# ----------------------------
# Intel HDA Audio Driver
# ----------------------------

build/drivers/audio/hda.o: drivers/audio/hda.c
	if not exist build\drivers\audio mkdir build\drivers\audio
	$(CC) $(CFLAGS) -c $< -o $@

build/drivers/audio/isa_speaker.o: drivers/audio/isa_speaker.c
	if not exist build\drivers\audio mkdir build\drivers\audio
	$(CC) $(CFLAGS) -c $< -o $@

build/drivers/audio/sb16.o: drivers/audio/sb16.c
	if not exist build\drivers\audio mkdir build\drivers\audio
	$(CC) $(CFLAGS) -c $< -o $@

build/drivers/audio/ac97.o: drivers/audio/ac97.c
	if not exist build\drivers\audio mkdir build\drivers\audio
	$(CC) $(CFLAGS) -c $< -o $@

# ----------------------------
# VoiceBox Kernel Audio Subsystem
# ----------------------------

build/kernel/audio/voicebox.o: kernel/audio/voicebox.c
	if not exist build\kernel\audio mkdir build\kernel\audio
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/audio/voicebox_server.o: kernel/audio/voicebox_server.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/audio/audio_clock.o: kernel/audio/audio_clock.c
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/sys/sys_audio.o: kernel/sys/sys_audio.c
	$(CC) $(CFLAGS) -c $< -o $@

# ----------------------------
# mplayer CLI
# ----------------------------

build/userland/mplayer/mplayer.o: userland/mplayer/mplayer.c
	if not exist build\userland\mplayer mkdir build\userland\mplayer
	$(CC) $(CFLAGS) -c $< -o $@

# ----------------------------
# Disk syscalls
# ----------------------------

build/kernel/sys/sys_disk.o: kernel/sys/sys_disk.c
	$(CC) $(CFLAGS) -c $< -o $@

# ----------------------------
# cfdisk — Userland Partition Manager
# ----------------------------

build/userland/coreutils/cfdisk/cfdisk.o: userland/coreutils/cfdisk/cfdisk.c
	$(CC) $(CFLAGS) -c $< -o $@

build/userland/coreutils/cfdisk/disk.o: userland/coreutils/cfdisk/disk.c
	$(CC) $(CFLAGS) -c $< -o $@

build/userland/coreutils/cfdisk/mbr.o: userland/coreutils/cfdisk/mbr.c
	$(CC) $(CFLAGS) -c $< -o $@

build/userland/coreutils/cfdisk/gpt.o: userland/coreutils/cfdisk/gpt.c
	$(CC) $(CFLAGS) -c $< -o $@

build/userland/coreutils/cfdisk/ui.o: userland/coreutils/cfdisk/ui.c
	$(CC) $(CFLAGS) -c $< -o $@

build/userland/coreutils/cfdisk/input.o: userland/coreutils/cfdisk/input.c
	$(CC) $(CFLAGS) -c $< -o $@

build/userland/coreutils/cfdisk/operations.o: userland/coreutils/cfdisk/operations.c
	$(CC) $(CFLAGS) -c $< -o $@

# ----------------------------
# Shell (temporary kernel-mode)
# ----------------------------

build/userland/shell/shell.o: userland/shell/shell.c
	$(CC) $(CFLAGS) -c $< -o $@

build/userland/shell/cmd_sysinfo.o: userland/shell/cmd_sysinfo.c
	$(CC) $(CFLAGS) -c $< -o $@

build/userland/shell/history.o: userland/shell/history.c
	$(CC) $(CFLAGS) -c $< -o $@

build/userland/quill/quill.o: userland/quill/quill.c
	$(CC) $(CFLAGS) -c $< -o $@

build/userland/raylib/raylib.o: userland/raylib/raylib.c
	$(CC) $(CFLAGS) -c $< -o $@

build/userland/raygui/raygui.o: userland/raygui/raygui.c
	$(CC) $(CFLAGS) -c $< -o $@

build/userland/gui/gui.o: userland/gui/gui.c
	$(CC) $(CFLAGS) -c $< -o $@

# ----------------------------
# Snake Game
# ----------------------------
build/userland/snake/snake.o: userland/snake/snake.c
	if not exist build\userland\snake mkdir build\userland\snake
	$(CC) $(CFLAGS) -c $< -o $@

build/drivers/video/text.o: drivers/video/text.c
	if not exist build\drivers\video mkdir build\drivers\video
	$(CC) $(CFLAGS) -c $< -o $@

# ----------------------------
# Coreutils
# ----------------------------

build/userland/coreutils/now.o: userland/coreutils/now.c
	$(CC) $(CFLAGS) -c $< -o $@

build/userland/coreutils/timestat.o: userland/coreutils/timestat.c
	$(CC) $(CFLAGS) -c $< -o $@

# ----------------------------
# SPM + doas
# ----------------------------

build/userland/spm/spm.o: userland/spm/spm.c
	$(CC) $(CFLAGS) -c $< -o $@

build/userland/doas/doas.o: userland/doas/doas.c
	$(CC) $(CFLAGS) -c $< -o $@


# ----------------------------
# Lib
# ----------------------------

build/lib/libc/string.o: lib/libc/string.c
	$(CC) $(CFLAGS) -c $< -o $@

build/lib/libc/float.o: lib/libc/float.c
	$(CC) $(CFLAGS) -c $< -o $@

build/lib/libc/array.o: lib/libc/array.c
	$(CC) $(CFLAGS) -c $< -o $@

build/lib/libc/time.o: lib/libc/time.c
	$(CC) $(CFLAGS) -c $< -o $@

build/lib/libc/timeprofile.o: lib/libc/timeprofile.c
	$(CC) $(CFLAGS) -c $< -o $@

build/lib/libc/time_syscall.o: lib/libc/time_syscall.c
	$(CC) $(CFLAGS) -c $< -o $@

build/lib/libtranslate/stdlib.o: lib/libtranslate/stdlib.c
	$(CC) $(CFLAGS) -c $< -o $@

build/lib/libtranslate/stdio.o: lib/libtranslate/stdio.c
	$(CC) $(CFLAGS) -c $< -o $@

build/lib/libtranslate/ctype.o: lib/libtranslate/ctype.c
	$(CC) $(CFLAGS) -c $< -o $@

build/lib/libtranslate/math.o: lib/libtranslate/math.c
	$(CC) $(CFLAGS) -c $< -o $@

build/lib/libgrace/sysinfo.o: lib/libgrace/sysinfo.c
	if not exist build\lib\libgrace mkdir build\lib\libgrace
	$(CC) $(CFLAGS) -c $< -o $@

build/lib/libgrace/syscall_asm.o: lib/libgrace/syscall.asm
	if not exist build\lib\libgrace mkdir build\lib\libgrace
	$(FASM) $< $@

build/lib/libgrace/syscall.o: lib/libgrace/syscall.c
	if not exist build\lib\libgrace mkdir build\lib\libgrace
	$(CC) $(CFLAGS) -c $< -o $@

build/lib/libgrace/llm.o: lib/libgrace/llm.c
	if not exist build\lib\libgrace mkdir build\lib\libgrace
	$(CC) $(CFLAGS) -c $< -o $@


# ----------------------------
# Drivers
# ----------------------------

build/drivers/video/vga.o: drivers/video/vga.c
	$(CC) $(CFLAGS) -c $< -o $@

build/drivers/video/tty.o: drivers/video/tty.c
	$(CC) $(CFLAGS) -c $< -o $@

build/drivers/video/fb.o: drivers/video/fb.c
	$(CC) $(CFLAGS) -c $< -o $@

build/drivers/input/keyboard.o: drivers/input/keyboard.c
	$(CC) $(CFLAGS) -c $< -o $@

build/drivers/storage/bfs.o: drivers/storage/bfs.c
	$(CC) $(CFLAGS) -c $< -o $@

build/drivers/storage/rtc.o: drivers/storage/rtc.c
	$(CC) $(CFLAGS) -c $< -o $@

build/drivers/storage/ide.o: drivers/storage/ide.c
	$(CC) $(CFLAGS) -c $< -o $@

build/drivers/storage/nvme.o: drivers/storage/nvme.c
	$(CC) $(CFLAGS) -c $< -o $@

build/drivers/storage/pmem.o: drivers/storage/pmem.c
	$(CC) $(CFLAGS) -c $< -o $@



# ----------------------------
# Link
# ----------------------------

kernel: boot $(OBJ)
	$(LLD) $(LDFLAGS) \
		-T kernel/arch/x86_64/linker.ld \
		$(BOOT_OBJ) $(OBJ) \
		-o $(KERNEL_ELF)


# ----------------------------
# ISO
# ----------------------------

iso: kernel
	if not exist $(ISO_DIR) mkdir $(ISO_DIR)
	if not exist $(ISO_DIR)\boot mkdir $(ISO_DIR)\boot
	if not exist $(ISO_DIR)\boot\grub mkdir $(ISO_DIR)\boot\grub

	@echo Copying GRUB themes...

	@wsl sh -lc 'if [ -d grub/themes ]; then cp -r grub/themes "$(ISO_DIR)/boot/grub/"; else echo "No themes directory found"; fi'

	copy "$(KERNEL_ELF)" "$(ISO_DIR)\boot\kernel.elf"
	copy grub\grub.cfg "$(ISO_DIR)\boot\grub\grub.cfg"

	@wsl sh -lc 'if [ -f "$(ISO_DIR)/boot/grub/themes/teleport-abyss-1280x720/theme.txt" ]; then echo "Theme file found."; else echo "Warning: theme.txt missing."; fi'

	wsl grub2-mkrescue -o $(ISO_FILE) $(ISO_DIR)


# ----------------------------
# Run Targets
# ----------------------------

# Run with serial output to console (most useful for debugging)
run: iso disk
	$(QEMU) \
		-cdrom $(ISO_FILE) \
		-drive file=$(DISK_IMG),format=raw,if=ide \
		$(QEMU_NET) \
		$(QEMU_AUDIO) \
		-serial stdio \
		-m 1024M

# Run with debug output to console
run-debug: iso disk
	$(QEMU) \
		-cdrom $(ISO_FILE) \
		-drive file=$(DISK_IMG),format=raw,if=ide \
		$(QEMU_NET) \
		$(QEMU_AUDIO) \
		-serial stdio \
		-m 1024M \
		-s -S \
		-no-reboot -no-shutdown \
		-d int,cpu_reset,guest_errors \
		-D $(QEMU_LOG)

# Run for GDB debugging
run-gdb: iso disk
	@echo "Starting QEMU with GDB server on port 1234..."
	@echo "Connect with: gdb -ex 'target remote localhost:1234' iso/boot/kernel.elf"
	$(QEMU) \
		-cdrom $(ISO_FILE) \
		-drive file=$(DISK_IMG),format=raw,if=ide \
		$(QEMU_NET) \
		$(QEMU_AUDIO) \
		-serial file:serial.log \
		-m 1024M \
		-s -S \
		-no-reboot -no-shutdown

# Run with explicit Windows audio wiring (matches manual command style)
run-audio: iso
	$(QEMU) \
		-cdrom $(ISO_FILE) \
		-serial file:log.txt \
		$(QEMU_AUDIO) \
		-m 1024M

# Run with serial output to file
run-serial: iso disk
	@echo "Running with serial output to serial.log..."
	$(QEMU) \
		-cdrom $(ISO_FILE) \
		-drive file=$(DISK_IMG),format=raw,if=ide \
		$(QEMU_NET) \
		-serial file:serial.log \
		-m 1024M
	@echo "=== Serial Log Content ==="
	@type serial.log

# Run with BOTH serial console AND video (dual output)
run-both: iso disk
	$(QEMU) \
		-cdrom $(ISO_FILE) \
		-drive file=$(DISK_IMG),format=raw,if=ide \
		$(QEMU_NET) \
		-serial stdio \
		-vga std \
		-m 1024M

# Run with multiple serial ports (for advanced debugging)
run-multiserial: iso disk
	$(QEMU) \
		-cdrom $(ISO_FILE) \
		-drive file=$(DISK_IMG),format=raw,if=ide \
		$(QEMU_NET) \
		-serial stdio \
		-serial file:debug.log \
		-m 1024M

# Quick test without disk
run-quick: iso
	$(QEMU) \
		-cdrom $(ISO_FILE) \
		$(QEMU_NET) \
		-serial stdio \
		-m 1024M

# Run with specific video mode for testing
run-vga: iso disk
	$(QEMU) \
		-cdrom $(ISO_FILE) \
		-drive file=$(DISK_IMG),format=raw,if=ide \
		$(QEMU_NET) \
		-serial stdio \
		-vga std \
		-m 1024M

disk:
	if not exist $(DISK_IMG) ( fsutil file createnew $(DISK_IMG) 67108864 )

# Quick test target that builds and runs
quick: iso
	$(QEMU) \
		-cdrom $(ISO_FILE) \
		$(QEMU_NET) \
		-serial stdio \
		-m 1024M


# ----------------------------
# Clean
# ----------------------------

clean:
	if exist build rmdir /s /q build
	if exist $(ISO_FILE) del $(ISO_FILE)
	if exist serial.log del serial.log
	if exist debug.log del debug.log
	if exist $(QEMU_LOG) del $(QEMU_LOG)

# Clean just serial logs
clean-logs:
	if exist serial.log del serial.log
	if exist debug.log del debug.log
	if exist $(QEMU_LOG) del $(QEMU_LOG)

# View serial log without running
view-serial:
	@if exist serial.log ( \
		echo === Serial Log === && \
		type serial.log \
	) else ( \
		echo No serial.log file found \
	)


.PHONY: all ports port -port port-% dirs boot kernel iso run run-debug run-gdb run-serial run-both run-multiserial run-quick run-vga quick clean clean-logs view-serial disk