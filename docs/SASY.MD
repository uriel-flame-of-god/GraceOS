# SASY - Segment Allocator System

**NT/MS-DOS 4 Inspired Segment Manager for GraceOS**

## Overview

SASY (Segment Allocator System) is GraceOS's virtual memory layer that provides:

- **Movable segments** - Memory can be relocated without application knowledge
- **Handle-based access** - Applications use handles, not raw pointers
- **Demand paging** - Memory is loaded only when needed
- **Swapping** - Segments can be swapped to disk under memory pressure
- **Code sharing** - Multiple processes can share the same code segment
- **Discardable memory** - Clean read-only segments can be freed without swap

## Architecture Stack

```
Applications
     ↓
SASY (Segments + Handles)
     ↓
VMM (Paging)
     ↓
PMM (Frames)
     ↓
Hardware
```

SASY never touches hardware directly - it works through VMM and PMM.

## Directory Structure

```
kernel/mm/sasy/
├── sasy.c       # Core segment manager
├── sasy.h       # Public API
├── handle.c     # Handle table management
├── handle.h     # Handle types and API
├── swap.c       # Swap subsystem
├── swap.h       # Swap types and API
├── loader.c     # Demand paging and code loading
├── loader.h     # Loader API
└── types.h      # Common types and definitions
```

## Segment Types

| Type | Description |
|------|-------------|
| `SEG_FIXED` | Fixed segment - never moved or swapped |
| `SEG_MANUAL` | Manually managed segment |
| `SEG_CODE` | Shared code segment (executable) |
| `SEG_DATA_AUTO` | Automatic data segment (per-process) |
| `SEG_DATA_INST` | Instance data segment (copy-on-write) |
| `SEG_PHYSICAL` | Direct physical memory mapping |

## Segment Flags

| Flag | Description |
|------|-------------|
| `SEG_FLAG_READ` | Segment is readable |
| `SEG_FLAG_WRITE` | Segment is writable |
| `SEG_FLAG_EXEC` | Segment is executable |
| `SEG_FLAG_USER` | User-accessible segment |
| `SEG_FLAG_DISCARD` | Discardable when under pressure |
| `SEG_FLAG_NOSWAP` | Never swap to disk |
| `SEG_FLAG_ZEROED` | Zero-fill on allocation |
| `SEG_FLAG_SHARED` | Shared between processes |

## Kernel API

### Creating Segments

```c
// Simple creation
seg_handle_t h = sasy_create(size, SEG_DATA_AUTO, SEG_FLAGS_USER_RW);

// Extended creation with owner
seg_handle_t h = sasy_create_ex(size, type, flags, owner_pid);
```

### Using Segments (Lock/Unlock Pattern)

```c
// Lock segment to get address
void* ptr = sasy_lock(h);
if (ptr) {
    // Use the segment
    memcpy(ptr, data, len);
    
    // Mark dirty if modified
    sasy_mark_dirty(h);
}
// Unlock when done
sasy_unlock(h);
```

**Important:** Always lock before use, unlock after use. Never store the pointer!

### Freeing Segments

```c
sasy_free(h);
```

### Resizing Segments

```c
int result = sasy_resize(h, new_size);
```

## Syscall Interface

| Syscall | Number | Description |
|---------|--------|-------------|
| `SYS_SEG_CREATE` | 30 | Create a new segment |
| `SYS_SEG_LOCK` | 31 | Lock segment and get address |
| `SYS_SEG_UNLOCK` | 32 | Unlock segment |
| `SYS_SEG_FREE` | 33 | Free segment |
| `SYS_SEG_RESIZE` | 34 | Resize segment |
| `SYS_SEG_INFO` | 35 | Get segment information |

## Userland API (libgrace)

```c
// Allocate a segment
seg_handle_t h = seg_alloc(size);

// Lock to get address
void* ptr = seg_lock(h);

// Use memory...

// Unlock
seg_unlock(h);

// Free when done
seg_free(h);
```

## Memory Pressure

When PMM reports low memory, SASY's pressure handler:

1. First frees discardable segments (!locked && !dirty && refcnt == 0)
2. Then swaps out unlocked segments to disk

## Shell Commands

### `segstat`

Display SASY statistics:

```
SASY Segment Allocator Statistics
----------------------------------
[SEG  ] Total Segments  : 5
[SEG  ] Resident        : 4
[SEG  ] Swapped         : 1
[SEG  ] Locked          : 2
[SEG  ] Virtual Memory  : 256 KB
[SEG  ] Physical Memory : 192 KB
[SEG  ] Swapped Memory  : 64 KB
[HNDL ] Handles Used    : 5
[HNDL ] Handles Free    : 8187
```

## Implementation Phases

| Phase | Feature | Status |
|-------|---------|--------|
| 1 | Fixed segments | ✅ |
| 2 | Handles | ✅ |
| 3 | Demand paging | ✅ |
| 4 | Swap | ✅ |
| 5 | Sharing | ✅ |
| 6 | COW | Planned |

## Configuration

| Constant | Value | Description |
|----------|-------|-------------|
| `MAX_SEGMENTS` | 4096 | Maximum number of segments |
| `MAX_HANDLES` | 8192 | Maximum number of handles |
| `SASY_BASE_ADDR` | 0x0000100000000000 | Segment virtual base |
| `SASY_MAX_SIZE` | 1TB | Maximum segment space |

## Example: Process Memory Layout

Each process has:

```c
struct proc {
    seg_handle_t heap;   // Heap segment (DATA_AUTO)
    seg_handle_t stack;  // Stack segment (DATA_INST, NOSWAP)
    seg_handle_t code;   // Code segment (CODE, shared)
};
```

Created via:

```c
proc->heap = sasy_create_heap(pid, 64 * 1024);    // 64K initial heap
proc->stack = sasy_create_stack(pid, 16 * 1024);  // 16K stack
proc->code = sasy_load_code("app.bin", pid);      // Shared code
```

## Notes

- Handles are opaque 32-bit values
- Applications never see raw virtual addresses
- Segments are always page-aligned
- Locked segments cannot be swapped or moved
- Code segments are automatically shared via hash matching
