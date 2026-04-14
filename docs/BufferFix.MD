# GraceOS Framebuffer Backbuffer Fix Guide
## (BufferFix.MD)

This document describes all known causes and fixes related to
framebuffer backbuffer allocation failures and black screen issues
in GraceOS.

This file must be consulted whenever graphics initialization fails.

---

# 1. Problem Summary

Symptoms:

- System boots normally
- Graphics mode enabled
- No GUI output
- Screen remains black
- Log shows:

```
[ERROR] FB: Backbuffer allocation failed
```

Cause:

The kernel heap cannot allocate a contiguous buffer large enough
to hold the framebuffer backbuffer.

---

# 2. Backbuffer Memory Requirements

Backbuffer size is determined by:

```
size = width × height × (bpp / 8)
```

Typical values:

| Resolution | Size |
|------------|------|
| 800×600×32 | ~1.8 MB |
| 1024×768×32 | ~3.1 MB |
| 1280×1024×32 | ~5.2 MB |
| 1920×1080×32 | ~8.3 MB |

Minimum supported target:

```
≥ 1024×768×32  → 3 MB
```

---

# 3. Root Causes

### 3.1 Heap Too Small

Default heap size:

```
16 MB
```

After kernel initialization:

```
Available contiguous space < 3 MB
```

Result:

```
kmalloc(3MB) → NULL
```

---

### 3.2 Heap Fragmentation

Early allocations fragment memory.

Even if total free space exists,
large contiguous blocks may not.

---

### 3.3 Incorrect Initialization Order

If framebuffer initializes before heap:

```
kmalloc() always fails
```

---

### 3.4 Wrong Allocation Source

Large buffers should not be allocated
from general-purpose heaps.

---

# 4. Mandatory Fixes

All systems must implement these.

---

## 4.1 Increase Kernel Heap Size

Minimum:

```c
32 * 1024 * 1024   // 32 MB
```

Recommended:

```c
64 * 1024 * 1024   // Future-proof
```

Example:

```c
kheap_init(heap_start, 32*1024*1024);
```

---

## 4.2 Enforce Initialization Order

Required boot order:

```
pmm_init()
kheap_init()
fb_init()
tty_init()
gui_init()
```

Framebuffer must never initialize
before heap.

---

## 4.3 Add Allocation Assertions

Backbuffer allocation must never fail silently.

```c
fb.backbuffer = kmalloc(size);

if (!fb.backbuffer) {
    klog_panic("FB: backbuffer alloc failed");
}
```

---

## 4.4 Log Memory State

Add diagnostics:

```c
klog_log("Heap free: ");
klog_hex(kheap_free_bytes());
```

Before fb_init().

---

# 5. Emergency Fallback Mode

If allocation fails in debug builds,
fallback to direct rendering:

```c
if (!fb.backbuffer) {
    fb.backbuffer = (uint32_t*)(uintptr_t)fb.addr;
    klog_warn("FB: Direct render fallback");
}
```

This enables display without double buffering.

Not for production use.

---

# 6. Recommended Architecture (Long-Term)

Backbuffers should be allocated using PMM + VMM,
not kmalloc.

### Correct Method:

```c
size_t pages =
    ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE;

phys = pmm_alloc_pages(pages);

virt = vmm_map_mmio(phys, size);

fb.backbuffer = virt;
```

Benefits:

* Guaranteed contiguous memory
* No heap fragmentation
* Predictable allocation
* Better performance

---

# 7. Large Buffer Allocation Policy

The following must NOT use kmalloc:

| Component              | Allocation Method |
| ---------------------- | ----------------- |
| Framebuffer backbuffer | PMM               |
| Video buffers          | PMM               |
| Audio buffers          | Backend-specific (SB16/AC97 DMA + VoiceBox ring/bounce) |
| DMA buffers            | PMM               |
| Disk cache             | PMM               |
| Page cache             | PMM               |

kmalloc is reserved for:

* Small objects
* Metadata
* Structures
* Tables

---

# 8. Validation Checklist

Before enabling GUI:

* [x] Heap ≥ 32 MB
* [x] Heap initialized before FB
* [x] Backbuffer uses static BSS buffer (4MB)
* [x] kmalloc max 64KB enforced
* [x] Magic numbers detect corruption
* [x] Boundary tags for safe coalescing
* [x] Page-aligned heap region
* [x] kheap_validate() available
* [x] fb_present() called after tty_print()

---

# 9. Debug Procedure

When buffer allocation fails:

1. Check heap size
2. Check heap free space
3. Check fragmentation
4. Enable fallback
5. Increase heap
6. Switch to PMM allocation

Do not proceed until resolved.

---

# 10. Performance Considerations

Direct rendering fallback:

* Causes tearing
* No vsync
* Visible flicker

Backbuffer:

* Stable display
* Smooth GUI
* Predictable timing

Always prefer backbuffer in release builds.

---

# 11. Future Improvements

Planned:

* Dedicated video memory pool
* Slab allocator for GUI objects
* Huge-page framebuffer buffers
* NUMA-aware allocation

---

# 12. Final Principle

Large memory buffers require
dedicated allocation strategies.

General-purpose heaps are unsuitable
for high-bandwidth graphics memory.

Respect this separation.

---
