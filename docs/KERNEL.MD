# Lumina Kernel Documentation

**Version**: v0.0.2-dev  
**Architecture**: x86_64  
**Last Updated**: March 9, 2026

---

## Overview

**Lumina Kernel** is a minimal 64-bit operating system kernel built from scratch for x86_64. It provides fundamental OS services including process management, segment-based memory allocation, device drivers, syscall interface, and a custom filesystem.

### Design Goals

- **Educational**: Learn low-level system programming
- **Minimalist**: Keep codebase small and understandable
- **NT-Inspired**: Draw design from Windows NT executive
- **MS-DOS 4 Heritage**: Segment-based memory, CSID job control

### Architecture

Grace-Kernel follows a **monolithic architecture** where all core services run in kernel mode (Ring 0):

```
┌─────────────────────────────────────────┐
│           Kernel Executive              │
├──────────┬──────────┬──────────────────┤
│ Process  │  Memory  │     Syscall      │
│ Manager  │  (SASY)  │   Dispatcher     │
├──────────┴──────────┴──────────────────┤
│           Device Drivers                │
│  VGA | Keyboard | BranchFS | Power     │
├─────────────────────────────────────────┤
│        Hardware Abstraction             │
│       PMM | VMM | IDT | I/O Ports      │
└─────────────────────────────────────────┘
```

---

## Kernel Subsystems

### 1. Boot Subsystem
**Location**: `boot/boot.asm`, `kernel/core/main.S`

- Multiboot2 protocol support
- 32-bit to 64-bit long mode transition
- Page table setup (256 × 2MB pages = 512MB)
- GDT with kernel code/data segments
- SSE/SSE2 initialization

### 2. Memory Management
**Location**: `kernel/mm/`

| Component | Location | Purpose |
|-----------|----------|---------|
| PMM | `pmm/` | Physical frame allocation (bitmap) |
| VMM | `vmm/` | Virtual memory, 4-level paging |
| SASY | `sasy/` | Segment allocator (handles) |
| kheap | `kheap.c` | Kernel dynamic allocation |
| fallback | `fallback/` | Bootstrap allocator |

### 3. Process Manager
**Location**: `kernel/proc/`

| File | Purpose |
|------|---------|
| `proc.c/h` | Process creation/destruction |
| `sched.c/h` | Hybrid scheduler (SJF+FCFS+Aging) |
| `context.asm` | Context switching (FASM) |
| `table.c` | Process table management |
| `exec.c` | ELF program loading |
| `fork.c` | Process cloning |
| `wait.c` | waitpid() implementation |
| `zombie.c` | Exit and cleanup |
| `job.c` | CSID job control |
| `security.c` | Capabilities and validation |

### 4. Syscall Interface
**Location**: `kernel/sys/`

| File | Purpose |
|------|---------|
| `syscall.asm` | INT 0x80 handler |
| `syscall.c` | Syscall dispatcher |
| `sys_sysinfo.c` | System info syscall |
| `sys_proc.c` | Process syscalls |
| `power.asm` | Power management routines |
| `power.c` | Shutdown/reboot syscalls |

### 5. Interrupt Handling
**Location**: `kernel/arch/x86_64/`

- 256-entry IDT
- CPU exception handlers (0x00-0x1F)
- PIC initialization and IRQ routing
- Keyboard IRQ handler (IRQ1)

### 6. Device Drivers
**Location**: `drivers/`

| Driver | Location | Features |
|--------|----------|----------|
| VGA | `video/vga.c` | 80×25 text, 16 colors |
| TTY | `video/tty.c` | Terminal emulation, scrolling |
| Keyboard | `input/keyboard.c` | PS/2, US QWERTY |
| BranchFS | `storage/bfs.c` | B+ tree filesystem |
| virtio-net | `drivers/net/virtio_net.c` | Legacy virtio PCI NIC |

Keyboard driver notes:

- Uses IRQ1 when available and falls back to a polling loop in `keyboard_service_run` (started by minit at boot).
- Maintains a 256-byte ring buffer with blocking and non-blocking reads (`keyboard_getchar`, `keyboard_getchar_nonblocking`).
- Emits special key codes for extended keys (arrows, home/end, pgup/pgdn, ins/del).
### 7. Network Stack
**Location**: `kernel/net/`, `drivers/net/`

| File | Purpose |
|------|---------|
| `drivers/net/virtio_net.c/h` | Legacy virtio-net PCI driver; PCI scan, virtqueue setup, TX/RX |
| `kernel/net/net.c/h` | Network device registry; `net_init()`, `net_register_device()`, `net_get_device()` |
| `kernel/net/ethernet/ethernet.c/h` | Ethernet frame send/dispatch; ethertype routing |
| `kernel/net/arp/arp.c/h` | ARP request/reply; 32-entry cache |
| `kernel/net/ip/ipv4.c/h` | IPv4 header construction, checksum, TCP dispatch |
| `kernel/net/tcp/tcp.c/h` | Client TCP state machine; 64 KB receive buffer |
| `kernel/net/http/http_client.c/h` | HTTP/1.1 GET: buffered (`http_fetch`) + streaming (`http_fetch_stream`) |
| `kernel/sys/sys_net.c` | 5 networking syscalls (120-124) |

#### Networking Syscall Range (120–124)
| Number | Name | Description |
|--------|------|-------------|
| 120 | SYS_NET_INIT | Re-initialize the network stack; returns 0 on success |
| 121 | SYS_NET_STATUS | Returns 1 if network device is ready, 0 otherwise |
| 122 | SYS_HTTP_FETCH | HTTP GET into caller buffer; returns bytes written or -1 |
| 123 | SYS_HTTP_STREAM | Streaming HTTP GET; calls write_cb per chunk |
| 124 | SYS_NET_GET_IP | Returns local IP address in host byte order |

### 8. LLM Runtime
**Location**: `kernel/llm/`

The LLM Runtime is an in-kernel large language model inference engine. Models in GGUF format are loaded directly from BranchFS into SASY-managed segments and executed by the transformer forward pass without any userland involvement.

| File | Purpose |
|------|---------|
| `runtime.c/h` | Subsystem entry point; dispatches all 13 LLM syscalls |
| `llama-arch.c/h` | Model architecture definitions (LLaMA, Mistral, Phi …) |
| `llama-io.c/h` | GGUF v2/v3 file reader |
| `llama-mmap.c/h` | Segment-backed model weight mapping via SASY |
| `llama-quant.c/h` | Dequantization: Q4_0, Q4_1, Q5_0, Q8_0, Q4_K, Q5_K, Q6_K |
| `llama-context.c/h` | Inference context: KV-cache, RoPE, GQA, SwiGLU forward pass |
| `llama-sampler.c/h` | 6 sampling algorithms: greedy, temp, top-k, top-p, min-p, mirostat v2 |
| `llama-chat.c/h` | Chat template engine: ChatML, Llama-2/3, Mistral, Vicuna, Alpaca |
| `llama-adapter.c/h` | LoRA adapter loading and weight merging |
| `llama-impl.c/h` | Internal math helpers: f16↔f32, SiLU, GELU, `llm_snprintf` |
| `unicode.c/h` | BPE / SentencePiece tokenizer with byte-fallback |
| `unicode-data.c/h` | Unicode codepoint category tables |

#### LLM Syscall Range (150–162)
| Number | Name | Description |
|--------|------|-------------|
| 150 | SYS_LLM_LOAD_MODEL | Load GGUF model from BranchFS path |
| 151 | SYS_LLM_UNLOAD_MODEL | Release model and free SASY segments |
| 152 | SYS_LLM_CREATE_CONTEXT | Allocate inference context with KV-cache |
| 153 | SYS_LLM_DESTROY_CONTEXT | Free inference context |
| 154 | SYS_LLM_TOKENIZE | Encode text to token array |
| 155 | SYS_LLM_DETOKENIZE | Decode token array to text |
| 156 | SYS_LLM_EVAL | Run transformer forward pass |
| 157 | SYS_LLM_SAMPLE | Sample next token from logits |
| 158 | SYS_LLM_CHAT_BEGIN | Start chat session with template + system prompt |
| 159 | SYS_LLM_CHAT_APPEND | Append a message to the chat history |
| 160 | SYS_LLM_CHAT_GENERATE | Generate assistant reply (full pipeline) |
| 161 | SYS_LLM_CHAT_RESET | Reset context while keeping model loaded |
| 162 | SYS_LLM_GET_INFO | Query model metadata as JSON-like summary |

---

## Memory Layout

```
0x00000000 - 0x00000FFF   BIOS/IVT (4KB)
0x00001000 - 0x00003FFF   Page Tables (12KB)
0x00100000 - 0x001FFFFF   Kernel Code (~1MB)
0x00200000 - 0x003FFFFF   Kernel Heap (2MB)
0x00400000 - 0x00FFFFFF   PMM Bitmap + SASY Pool
0x01000000 - 0x1FFFFFFF   User Memory (~496MB)
```

---

## Syscall ABI

### Register Convention (INT 0x80)
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

### Syscall Numbers
| Range | Category |
|-------|----------|
| 1-10 | Basic I/O (read, write, exit) |
| 20-29 | System info |
| 30-39 | SASY segments |
| 40-49 | Process management |
| 50-59 | Power management |
| 60-69 | Framebuffer graphics |
| 70 | IPC (pipe) |
| 120-124 | Networking |
| 150-162 | LLM Runtime |

---

## Process Model

### Process States
```
NEW → READY ←→ RUNNING → ZOMBIE → DEAD
        ↓         ↓
     BLOCKED ← WAIT
```

### Process Control Block
```c
typedef struct process {
    pid_t pid;              // Process ID
    csid_t csid;            // Command Session ID
    proc_state_t state;     // Current state
    uint64_t pml4;          // Page table root
    seg_handle_t segments[16];  // SASY handles
    uid_t uid, euid;        // Credentials
    uint32_t caps;          // Capabilities
    struct cpu_context ctx; // Saved registers
} process_t;
```

### Scheduler
Hybrid algorithm: SJF + FCFS + Aging
```
score = est_runtime - (wait_time / 100) + invalid_penalty
```

---

## Kernel API

### Process Management
```c
process_t* proc_create(process_t* parent);
void proc_destroy(process_t* p);
process_t* proc_find(pid_t pid);
void proc_exit(int code);
```

### Memory (SASY)
```c
seg_handle_t sasy_create(size_t size, seg_type_t type, uint32_t flags);
void* sasy_lock(seg_handle_t h);
void sasy_unlock(seg_handle_t h);
void sasy_free(seg_handle_t h);
```

### Logging
```c
klog_info(fmt, ...);
klog_error(fmt, ...);
klog_warn(fmt, ...);
klog_debug(fmt, ...);
```

---

## Configuration

### Constants
| Constant | Value | Location |
|----------|-------|----------|
| `MAX_PROC` | 8192 | proc.h |
| `PROC_NAME_MAX` | 32 | proc.h |
| `PROC_MAX_SEGMENTS` | 16 | proc.h |
| `PROC_MAX_FDS` | 64 | proc.h |
| `SCHED_QUANTUM_MS` | 10 | sched.h |
| `SCHED_AGING_FACTOR` | 100 | sched.h |

---

## Building

See [BUILD.MD](BUILD.MD) for detailed instructions.

```cmd
make clean
make run
```

---

## See Also

- [ARCHITECTURE.MD](ARCHITECTURE.MD) - System architecture
- [PROC.MD](PROC.MD) - Process manager details
- [SASY.MD](SASY.MD) - Segment allocator details
- [MEMORY.MD](MEMORY.MD) - Memory management
- [DEBUGGING.MD](DEBUGGING.MD) - Debugging guide
- [LLM.MD](LLM.MD) - LLM Runtime reference
