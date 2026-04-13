# Lumina Kernel API Documentation

## System Architecture

Lumina Kernel is a 64-bit monolithic kernel.

### Memory Layout
- **0x00000000 - 0x00000FFF**: BIOS/IVT
- **0x00001000 - 0x00100000**: Boot/Page Tables
- **0x00100000 - 0x00200000**: Kernel Code
- **0x00200000 - 0x00400000**: Kernel Heap
- **0x00400000+**: Physical Memory Manager (Bitmap)

### Privilege Levels
- **Ring 0**: Kernel (All hardware access)
- **Ring 3**: Userland (Restricted, Syscall access only) [Planned]

---

## Syscall Interface (INT 0x80)

### Calling Convention
Registers handling for `syscall(num, a1, a2, a3, a4, a5)`:

| Register | Usage |
|----------|-------|
| `RAX`    | Syscall Number / Return Value |
| `RDI`    | Argument 1 |
| `RSI`    | Argument 2 |
| `RDX`    | Argument 3 |
| `R10`    | Argument 4 |
| `R8`     | Argument 5 |
| `R9`     | Argument 6 |

### Syscall List

#### File I/O
| Num | Name | Description |
|-----|------|-------------|
| 1 | `SYS_WRITE` | Write data to file descriptor |
| 2 | `SYS_READ` | Read data from file descriptor |
| 7 | `SYS_OPEN` | Open file |
| 8 | `SYS_CLOSE` | Close file descriptor |
| 9 | `SYS_LIST` | List directory contents |

#### Networking
| Num | Name | Description |
|-----|------|-------------|
| 120 | `SYS_NET_INIT` | Re-initialize network stack; returns 0 on success, -1 if no device |
| 121 | `SYS_NET_STATUS` | Returns 1 if network device is ready, 0 otherwise |
| 122 | `SYS_HTTP_FETCH` | HTTP/1.1 GET into buffer: `(host, path, buf, max_size)` → bytes written or -1 |
| 123 | `SYS_HTTP_STREAM` | Streaming HTTP GET: `(host, path, write_cb)` → total bytes or -1 |
| 124 | `SYS_NET_GET_IP` | Returns local IP address in host byte order, or 0 |
| 125 | `SYS_ICMP_PING` | ICMP echo request: `(dst_ip, id, seq, timeout_ms)` → RTT in ms or -1 |
| 126 | `SYS_NET_SET_IP` | Set static IPv4 config: `(ip, gateway, netmask)` → 0 or -1 |

HTTP notes:
- `SYS_HTTP_FETCH` and `SYS_HTTP_STREAM` currently accept numeric IPv4 hosts.
- HTTP transport is plain HTTP/1.1 over TCP port 80 (no TLS/HTTPS yet).

#### Process Management
| Num | Name | Description |
|-----|------|-------------|
| 3 | `SYS_EXIT` | Terminate current process |
| 4 | `SYS_EXEC` | Execute new program (current user-visible path supports `.cmdt` workloads) |
| 40 | `SYS_FORK` | Clone current process |
| 41 | `SYS_WAIT` | Wait for child process |
| 42 | `SYS_KILL` | Send signal to process |
| 43 | `SYS_GETPID` | Get Process ID |

#### Memory Management (SASY)
| Num | Name | Description |
|-----|------|-------------|
| 30 | `SYS_SEG_CREATE` | Create shared memory segment |
| 31 | `SYS_SEG_LOCK` | Map segment to address space |
| 32 | `SYS_SEG_UNLOCK` | Unmap segment |
| 33 | `SYS_SEG_FREE` | Destroy segment |

#### Graphics (Framebuffer)
| Num | Name | Description |
|-----|------|-------------|
| 60 | `SYS_FB_GETINFO` | Get resolution and bpp |
| 61 | `SYS_FB_MAP` | Map framebuffer memory |
| 63 | `SYS_FB_CLEAR` | Fill screen with color |
| 64 | `SYS_FB_PIXEL` | Draw single pixel |
| 65 | `SYS_FB_RECT` | Draw filled rectangle |
| 66 | `SYS_FB_PRESENT` | Swap buffers / Present |
| 67 | `SYS_FB_CIRCLE` | Draw filled circle |
| 68 | `SYS_FB_LINE` | Draw line |
| 69 | `SYS_FB_BLIT` | Blit image data |

---

## LibGrace API

Userland wrapper library for system calls.

### Core Functions
- `grace_get_pid()`
- `grace_fork()`
- `grace_exit(code)`
- `grace_shutdown()`, `grace_reboot()`

### Graphics (Raylib-inspired)
- `InitWindow(w, h, title)` - Initialize graphics
- `BeginDrawing()`, `EndDrawing()` - Frame control
- `DrawRectangle(x, y, w, h, color)`
- `DrawCircle(x, y, r, color)`
- `DrawLine(x1, y1, x2, y2, color)`
- `DrawText(text, x, y, size, color)`

---

## Device Drivers

- **VGA**: Text mode 80x25
- **TTY**: Terminal emulator with scrollback
- **Keyboard**: PS/2 with simple scancode mapping
- **Framebuffer**: VESA BIOS Extensions (VBE) linear framebuffer
