# GraceOS (Lumina Kernel) Implementation Status

Last Updated: 2026-04-17
Version: v0.6.1 (Kernel v0.3.5)

This document tracks which features are complete, in progress, or planned.

---

## Changelog

### v0.6.1 / Kernel v0.3.5 — 2026-04-17

**Fixes**
- **Logger (klog)**: Kernel logger subsystem now fully operational. Log levels
  (`INIT`, `INFO`, `WARN`, `ERROR`) route correctly to both the serial console
  and the in-memory ring buffer. Earlier builds had a silent init-ordering bug
  where `klog_init()` was called before the UART was ready.
- **minit**: Process orchestration layer fixed. Service tree construction, restart
  policy, and exhaustion handling now work correctly. Previously minit could
  deadlock during boot if a service exited before its dependents were registered.
- **sysinfo CPU decode**: ARM64 MIDR PartNum now decoded to exact core name
  (Cortex-A53, Cortex-A72, …) instead of hardcoded `"ARM Cortex (BCM2837/BCM2711)"`.
  Implementer byte now decoded to vendor string.
- **sysinfo memory**: ARM64 total RAM derived from `RPI_MODEL` compile-time
  constant (1 GiB for Pi 3, 4 GiB for Pi 4) instead of a hardcoded 1 GiB literal.
- **Console readline (ARM64)**: Removed blind pre-classification echo, fixed
  double-newline on Enter, fixed backspace double-erase, fixed ESC sequence
  leaving `[A` in the command buffer.
- **Keyboard driver (ARM64)**: Eliminated dead 256-byte circular buffer and
  `keyboard_service_run()` polling thread. ARM64 path is now thin UART wrappers —
  one input system, no architectural split.
- **ESC handling**: Replaced 50,000-NOP timing-based busy-wait with an ANSI
  CSI sequence drain (`while (uart_rx_ready()) { read; if final-byte break; }`).
  Works reliably on both QEMU and real hardware.

**ARM64 / Raspberry Pi 3B**
- Port is **currently in active development and broken** — boots to banner,
  UART I/O works in QEMU, but real-hardware input, framebuffer stability, and
  several subsystems remain unfinished. See [RPI3B.MD](RPI3B.MD) for full details.

---

## Code Size Snapshot

Measured with `cloc drivers kernel userland` on April 13, 2026.

| Language | Files | Blank | Comment | Code |
|----------|------:|------:|--------:|-----:|
| C | 95 | 4903 | 3605 | 20996 |
| C/C++ Header | 78 | 1093 | 1653 | 2810 |
| Assembly | 7 | 169 | 199 | 526 |
| Linker Script | 1 | 9 | 24 | 39 |
| **Total** | **181** | **6174** | **5481** | **24371** |

## Fully Implemented

### Core Kernel

Boot Process:
- GRUB2 Multiboot2 boot
- 32-bit to 64-bit long mode transition
- Identity-mapped paging with 256 x 2MB huge pages (512MB)
- GDT setup with kernel code and data segments
- Stack initialization

Interrupt and Exception Handling:
- IDT with 256 entries
- All CPU exception handlers (0x00-0x1F)
- 8259 PIC initialization and remapping
- IRQ handlers for hardware interrupts (IRQ0-15)
- PS/2 keyboard on IRQ1
- PIT timer on IRQ0 at 1000 Hz
- Timer IRQ invokes scheduler tick — preemptive multitasking active
- EOI sent to PIC before sched_tick() to prevent IRQ0 masking on context switch

Memory Management:
- Physical Memory Manager using bitmap allocation
- Virtual Memory Manager with 4-level paging
- Kernel heap for dynamic allocation
- SASY segment allocator with handle-based access
- Multiboot memory map parsing

Security Policy Manager (SPM):
- Initialised in kmain before the process manager via `spm_init()`
- Manages security policies that govern process behaviour
- Syscall interface exposed via `kernel/sys/sys_spm.c`
- Logged at boot with `klog_init_msg("Initializing spm security subsystem")`

Process Manager:
- Full NT-style Process Control Block
- Process table supporting up to 8192 processes
- PID and CSID allocation
- Process states: NEW, READY, RUNNING, WAIT, BLOCKED, ZOMBIE, DEAD
- Hybrid scheduler combining SJF, FCFS, and aging
- Preemptive scheduling: sched_tick() called on every PIT IRQ0
- Context switching implemented in assembly
- fork() for process cloning with copy-on-write
- exec() for ELF program loading
- wait() and waitpid() for child process synchronization
- exit() for process termination
- Zombie cleanup and CSID job control
- Capability-based security validation
- Timer-based sleep: sys_sleep blocks process until timer expiry
- pipe() syscall for anonymous IPC pipes (64 pipes, 4 KB ring buffers each)
- minit orchestration layer: named service tree, pre-allocated stack pool (8 stacks), kernel process spawn via minit_register/minit_spawn
- Keyboard service: runs as kernel process under minit ("keyboard"), polls PS/2 input with sti/hlt idle

Syscall Interface:
- INT 0x80 handler with register-based ABI
- Dispatcher handling 70+ syscalls
- Basic I/O: read, write, exit (1-11)
- read/write dispatch to pipe file descriptors via process fd table
- Time management: time, clock_gettime, gettimeofday, settimeofday (25-28)
- SASY segments: create, lock, unlock, free, resize, info (30-39)
- Process management: fork, wait, kill, getpid, getppid, yield, sleep (40-49)
- sleep blocks the process and yields; wakeup handled by proc_check_sleepers()
- IPC: pipe (70) — creates read/write fd pair routed through kernel pipe table
- Power management: shutdown, reboot (50-51)
- System info: sysinfo (20)
- Networking: net_init, net_status, http_fetch, http_stream, net_get_ip, icmp_ping, net_set_ip (120-126)
- SPM: security policy syscalls via sys_spm.c

Time System:
- RTC driver reading CMOS hardware clock at boot
- UTC-based system time using Unix epoch
- Monotonic clock for timers and measurements
- PIT configured at 1000 Hz for 1ms tick resolution
- Nanosecond precision with timespec structure
- Calendar conversion functions (gmtime, localtime, mktime)
- Time formatting engine with custom format tokens
- Time profile support for timezone offsets

### Drivers

VGA Driver:
- Text mode 80x25 with standard VGA
- Direct memory-mapped I/O at 0xB8000
- 16-color support
- Hardware cursor control

TTY Driver:
- Buffered terminal emulation
- Automatic scrolling
- Dynamic color management
- Newline, backspace, and carriage return handling

Keyboard Driver:
- PS/2 scancode translation (Scancode Set 1)
- US QWERTY layout
- Modifier keys: Shift, Ctrl, Alt, Right Ctrl, Right Alt
- Caps Lock toggle
- 256-byte circular buffer
- Blocking input with getkey()
- Non-blocking check with haskey()
- Ctrl+C, Ctrl+S, Ctrl+X control codes
- Extended key support: Up/Down/Left/Right arrows
- Extended key support: Home, End, PgUp, PgDn, Insert, Delete
- keyboard_service_run() registered as minit "keyboard" service

RTC Driver:
- CMOS clock reading at boot
- BCD to binary conversion
- Year, month, day, hour, minute, second extraction

BranchFS Filesystem:
- B+ tree structure with O(log n) operations
- Memory-backed storage as a RAM disk
- File operations: create, find, delete, list
- Filename hashing to uint64_t keys
- Full B+ tree node splitting: leaf splits and recursive internal node splits
- Unlimited file count (limited only by available RAM pages)
- Directory support: zero-size BFS entries used as directory placeholders
- `bfs_list_dir(bfs, prefix, out, out_len)` — lists direct children of a directory by BFS path prefix
- Directories distinguished from files by size=0 and `/` suffix in listing output

Power Management:
- Reboot via keyboard controller reset (port 0x64)
- Shutdown via QEMU ACPI port (0x604)
- CPU halt loop

### Libraries

libc:
- String functions: strlen, strcmp, strcpy, strncpy, strcat, strncmp
- Memory functions: memset, memcpy, memmove, memcmp
- Integer types: uint8_t through uint64_t, int8_t through int64_t
- Time functions: time, clock_gettime, gmtime, localtime, mktime, strftime
- Custom format_time() with DD, MM, YYYY, HH, hh, mm, ss, A tokens

libgrace:
- Syscall wrappers for all implemented syscalls
- Process API: fork, wait, exit, getpid
- Segment API: seg_alloc, seg_lock, seg_unlock, seg_free
- Power API: grace_shutdown, grace_reboot
- I/O API: getkey, haskey, print, println

### Shell

Command prompt running as a minit-managed kernel service ("shell").
`shell_init()` initialises history and prints the welcome banner.
`shell_run()` is the main REPL loop: prints prompt, calls `tty_readline()`, dispatches command.
`tty_readline()` uses non-blocking keyboard reads with `sti; hlt` when idle.
Input supports 256-byte input buffer and command history (up/down arrow navigation).

Built-in commands:
- help, clear, about, echo
- sysinfo, pmmstat, vmstat, segstat
- ls — lists direct children of the current directory; directories shown with trailing `/` in blue, files in grey; uses bfs_list_dir()
- cd / chdir — changes current directory; resolves `.` and `..`; accepts absolute and relative paths; verifies target exists in BFS
- mkdir — creates a directory placeholder (zero-size BFS entry) at the resolved path
- mkfile, find, rm
- now (with --date, --time, --format, --stream options)
- chivm, chivm build, run (.cmdt execution)
- netstat, ifconfig, ping, fetch
- shutdown, reboot, halt
- exit

### Userland Applications

Snake Game:
- Classic snake gameplay
- Framebuffer-based rendering
- Keyboard control (Enter/Arrows)
- Score tracking and game states (Menu, Playing, GameOver)

ChibiVM (Step 1):
- Interactive REPL (`chivm`)
- Source-to-CMDT compilation (`chivm build <src> <out.cmdt>`)
- CMDT execution path via shell `run <file.cmdt>`
- Integer variable/input/output runtime with interpolation-aware `cprintf`

### Networking Stack

Subsystem: `kernel/net/`, Driver: `drivers/net/virtio_net.c`

Virtio-Net Driver:
- Legacy virtio-net PCI device detection via PCI config space scan
- Virtqueue setup: descriptor table, available ring, used ring (VRING_SIZE = 256)
- TX path: prepends virtio-net header, writes descriptor chain, notifies device
- RX path: pre-populated RX queue with write buffers; non-blocking poll and descriptor recycle
- Static IP configuration for QEMU usermode networking (10.0.2.15 / 255.255.255.0, gateway 10.0.2.2)
- MAC address read from PCI device-specific config space
- Registers itself via `net_register_device()` at init

Ethernet Layer (`kernel/net/ethernet/`):
- IEEE 802.3 frame encapsulation and dispatch
- Ethertype dispatch: ARP (0x0806), IPv4 (0x0800)
- Broadcast MAC address support
- `ethernet_send()` / `ethernet_poll()` API

ARP Layer (`kernel/net/arp/`):
- ARP request/reply for Ethernet + IPv4 (RFC 826)
- ARP cache: 32 entries, timestamp-based
- `arp_lookup()` — cache hit returns MAC; miss sends ARP request
- `arp_send_request()` / `arp_receive()` 

IPv4 Layer (`kernel/net/ip/`):
- IPv4 header construction and checksum computation
- Next-hop routing via subnet check (destination or configured gateway)
- ARP resolution retry/poll in send path for robust first-packet transmit
- Dispatches ICMP (protocol 1) and TCP (protocol 6)
- `ipv4_send()` / `ipv4_receive()`

ICMP Layer (`kernel/net/icmp/`):
- ICMP echo request/reply support
- `icmp_ping_one()` with RTT timing and timeout handling

TCP Layer (`kernel/net/tcp/`):
- Client-side TCP state machine: CLOSED → SYN_SENT → ESTABLISHED → FIN_WAIT
- 64 KB receive ring buffer per connection
- `tcp_connect()` — 3-way handshake with polling wait
- `tcp_send()` / `tcp_recv()` / `tcp_recv_wait()` / `tcp_close()`
- Pseudo-header checksum for TCP segments
- Handshake diagnostics for SYN send, timeout, and unexpected SYN_SENT responses

HTTP Client (`kernel/net/http/`):
- HTTP/1.1 GET over TCP
- `http_fetch()` — buffered response into caller buffer
- `http_fetch_stream()` — streaming with write callback
- Host: header, Connection: close
- Numeric IPv4 host input (no DNS resolver yet)
- Chunked transfer decoding and content-length handling

Shell fetch command (`userland/shell`):
- `fetch <ip> [path] [outfile]`
- Prints response body to terminal, or saves response bytes into BranchFS when outfile is provided

### LLM Runtime

Subsystem: `kernel/llm/`  
Userland API: `lib/libgrace/llm.c`

- GGUF v2/v3 model loader via SASY segment allocator (up to 4 models resident)
- Transformer forward pass: LLaMA-style RoPE, Grouped Query Attention (GQA), SwiGLU
- Quantization dequantizers: Q4_0, Q4_1, Q5_0, Q8_0, Q4_K, Q5_K, Q6_K
- KV-cache allocations managed through SASY segment handles
- 6 sampling algorithms: greedy, temperature, top-k, top-p, min-p, mirostat v2
- Tokenizer: BPE / SentencePiece compatible; byte-fallback Unicode support
- Chat templates: ChatML, Llama-2, Llama-3, Mistral, Vicuna, Alpaca
- LoRA adapter loading with weight merging into active model
- 13 LLM syscalls: SYS_LLM_LOAD_MODEL (150) through SYS_LLM_GET_INFO (162)
- Userland C API via `lib/libgrace/llm.c` wrapping all 13 syscalls

### Audio (VoiceBox)

- VoiceBox core/server integrated in kernel (`kernel/audio/`)
- `mplayer` command integrated in shell for WAV/MP3/sample playback
- SB16 backend is the default playback path in QEMU when ISA low-memory DMA is available
- AC97 backend is integrated and can be selected at runtime via `mplayer --switch ac97`
- Dedicated background audio clock service (`audio_clock`) registered under minit for pacing
- Embedded sample playback path active via `mplayer --sample`
- Backend selection now degrades more safely when a preferred backend is unavailable (SB16 -> AC97 -> HDA -> ISA in AUTO mode)

### Ports

- libsodium: builds as a static library for userland ports (build/ports/libsodium/libsodium.a)
- 7zip: C-only decoder build (build/ports/7zip/7zdec.a) with BranchFS I/O
- 7zip uses libsodium for SHA-256 while keeping 7zip AES for encryption
- Crypto access gated by SPM permission `crypto/7zip` with `PERM_EXEC`

### Documentation
- KERNEL.MD coverage
- STATUS.MD updates
- API.MD (System Architecture & Syscalls)
- CONTRIBUTORS.MD
- LLM.MD — LLM Runtime reference
- SPM.MD — Security Policy Manager reference

### Logging System

Colored kernel log macros: klog_info, klog_error, klog_warn, klog_log
Module tags: [INIT], [PMM], [VMM], [SASY], [PROC], [SYS]

## Partially Implemented

Audio backend parity:
- Intel HDA path is not production-ready for stable audible playback.
- ISA speaker backend is fallback only (degraded quality).
- SB16 backend is usable but still has minor jitter/artifacts.
- AC97 backend is usable but can still exhibit jitter depending on host audio path.
- On boots with no free pages below 16MB, SB16 may disable and fall back to AC97/other backends.

For detailed backend notes and current recommendations, see `docs/AUDIO.MD`.

## Not Yet Implemented

Disk I/O:
- ATA/IDE driver (stubs present)
- AHCI/SATA driver (stubs present)
- Disk-backed filesystem persistence

IPC (additional):
- Shared memory segments
- Semaphores
- Signals (POSIX-style kill/signal delivery)

Session Manager:
- Virtual screens
- Screen switching with hotkeys

User Mode:
- Ring 3 execution
- User/kernel memory separation
- System call return to userspace

## Syscall Table

| Number | Name | Status |
|--------|------|--------|
| 1 | SYS_WRITE | Done |
| 2 | SYS_READ | Done |
| 3 | SYS_EXIT | Done |
| 4 | SYS_EXEC | Done |
| 5 | SYS_ALLOC | Done |
| 6 | SYS_FREE | Done |
| 7 | SYS_OPEN | Done |
| 8 | SYS_CLOSE | Done |
| 9 | SYS_LIST | Done |
| 10 | SYS_GETKEY | Done |
| 11 | SYS_HASKEY | Done |
| 20 | SYS_SYSINFO | Done |
| 25 | SYS_TIME | Done |
| 26 | SYS_CLOCK_GETTIME | Done |
| 27 | SYS_GETTIMEOFDAY | Done |
| 28 | SYS_SETTIMEOFDAY | Done |
| 30 | SYS_SEG_CREATE | Done |
| 31 | SYS_SEG_LOCK | Done |
| 32 | SYS_SEG_UNLOCK | Done |
| 33 | SYS_SEG_FREE | Done |
| 34 | SYS_SEG_RESIZE | Done |
| 35 | SYS_SEG_INFO | Done |
| 40 | SYS_FORK | Done |
| 41 | SYS_WAIT | Done |
| 42 | SYS_KILL | Done |
| 43 | SYS_GETPID | Done |
| 44 | SYS_GETPPID | Done |
| 45 | SYS_SETSID | Done |
| 46 | SYS_YIELD | Done |
| 47 | SYS_SLEEP | Done (timer-based blocking) |
| 48 | SYS_GETUID | Done |
| 49 | SYS_SETUID | Done |
| 50 | SYS_SHUTDOWN | Done |
| 51 | SYS_REBOOT | Done |
| 70 | SYS_PIPE | Done |
| 150 | SYS_LLM_LOAD_MODEL | Done |
| 151 | SYS_LLM_UNLOAD_MODEL | Done |
| 152 | SYS_LLM_CREATE_CONTEXT | Done |
| 153 | SYS_LLM_DESTROY_CONTEXT | Done |
| 154 | SYS_LLM_TOKENIZE | Done |
| 155 | SYS_LLM_DETOKENIZE | Done |
| 156 | SYS_LLM_EVAL | Done |
| 157 | SYS_LLM_SAMPLE | Done |
| 158 | SYS_LLM_CHAT_BEGIN | Done |
| 159 | SYS_LLM_CHAT_APPEND | Done |
| 160 | SYS_LLM_CHAT_GENERATE | Done |
| 161 | SYS_LLM_CHAT_RESET | Done |
| 162 | SYS_LLM_GET_INFO | Done |
| 120 | SYS_NET_INIT | Done |
| 121 | SYS_NET_STATUS | Done |
| 122 | SYS_HTTP_FETCH | Done |
| 123 | SYS_HTTP_STREAM | Done |
| 124 | SYS_NET_GET_IP | Done |
| 125 | SYS_ICMP_PING | Done |
| 126 | SYS_NET_SET_IP | Done |

## Version History

| Version | Date | Changes |
|---------|------|---------|
| v0.1.0 | 2025 | Initial kernel, boot, drivers, shell |
| v0.2.0 | Jan 2026 | PMM, VMM, SASY, Process Manager, Power Management |
| v0.3.0 | Jan 2026 | Time system, RTC driver, PIT timer, now command with streaming |
| v0.4.0 | Feb 2026 | Preemptive scheduling, timer-based sleep, extended keyboard keys, shell history, BranchFS node splitting, IPC pipes (SYS_PIPE) |
| v0.5.0 | Feb 2026 | LLM Runtime (kernel/llm/): GGUF loader, transformer inference, quantization, sampling, chat templates, LoRA adapters, 13 LLM syscalls, libgrace LLM API |
| v0.6.0 | Mar 2026 | Networking stack: virtio-net PCI driver, Ethernet/ARP/IPv4/TCP layers, HTTP/1.1 client, 5 networking syscalls (120-124) |
| v0.6.1 | Mar 2026 | Networking hardening: ICMP support, gateway ARP warmup, robust ARP retry in IPv4 send, virtio RX ring fixes, networking syscalls extended to 120-126, shell fetch filesystem output |
| v0.7.0 | Apr 2026 | SPM security manager, minit process orchestration layer, keyboard kernel service, shell cd/ls (directory-aware)/mkdir commands, BranchFS directory support (bfs_list_dir), scheduler EOI-before-tick fix, minit_kernel_trampoline preemption fix |
