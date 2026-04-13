# GraceOS Video Subsystem Documentation

## Overview

The video subsystem consists of four layers:

```
+---------------+
|    raygui     |  Widget rendering (GuiButton, etc.)
+---------------+
|    raylib     |  API translation (DrawRectangle, etc.)
+---------------+
|   Syscalls    |  SYS_FB_CIRCLE, SYS_FB_LINE, SYS_FB_BLIT
+---------------+
| Framebuffer   |  Pixel storage + presentation
+---------------+
|  VBE / BIOS   |  Hardware mode switching
+---------------+
```

---

## Layer 1: VBE (Hardware)

**Files:** `kernel/arch/x86_64/vbe.c`, `kernel/arch/x86_64/vbe.h`

### Initialization Flow

1. `vbe_init_auto()` → BIOS INT 10h thunk to enumerate modes
2. Finds best 32-bit mode (prefers 1024x768+, fallback 800x600+)
3. Sets mode via BIOS call
4. Returns `vbe_mode_info` with physical base, pitch, dimensions

### Critical Addresses

| Address | Purpose |
|---------|---------|
| `0x6000` | Real-mode data exchange area |
| `0x6200` | VBE info block |
| `0x6400` | VBE mode info block |
| `0x7000` | Real-mode code stub |

### Potential Black Screen Causes

1. **BIOS call fails silently** - VBE returns 0, fb_init uses fallback
2. **No valid 32-bit modes** - `vbe_init_auto()` returns 0
3. **Physical base = 0** - Mode info corrupted or unsupported

### Debug Check

```c
// In vbe_init_auto(), add:
klog_log("VBE mode: wxh @ phys");
```

---

## Layer 2: Framebuffer Driver

**Files:** `drivers/video/fb.c`, `drivers/video/fb.h`

### Initialization Flow (fb_init)

1. Check `multiboot_get_framebuffer()` - if GRUB provided framebuffer
2. If not, call `vbe_init_auto()` to set VBE mode
3. Validate: bpp must be 32, dimensions must be > 0
4. Map physical memory via VMM
5. Allocate backbuffer from kernel heap
6. Clear to black, present

### State Structure

```c
struct fb_state {
    uint64_t addr;          // Physical framebuffer address
    uint32_t width;         // Screen width in pixels
    uint32_t height;        // Screen height in pixels
    uint32_t pitch;         // Bytes per row
    uint8_t bpp;            // Bits per pixel (must be 32)
    uint8_t type;           // Framebuffer type
    uint8_t ready;          // 1 = usable
    uint8_t reserved;       // Padding
    uint32_t* backbuffer;   // Heap-allocated backbuffer
};
```

### Drawing Flow

```
DrawRectangle()
    → fb_matrix_fill_rect()
        → fb_fill32() [scalar loop]
            → writes to backbuffer

EndDrawing()
    → fb_present()
        → fb_copy8() [scalar loop]
            → copies backbuffer → physical fb
```

### Potential Black Screen Causes

1. **fb.ready = 0** - Init failed, all draws silently return
2. **backbuffer = NULL** - kmalloc failed
3. **VMM mapping failed** - Writes go to unmapped memory
4. **Physical addr wrong** - Writes go to wrong location

### Debug Checks

Add to `fb_init()`:

```c
klog_log("FB addr: "); klog_hex(fb.addr);
klog_log("FB size: "); klog_hex(fb.width); klog_log("x"); klog_hex(fb.height);
klog_log("FB pitch: "); klog_hex(fb.pitch);
klog_log("Backbuffer: "); klog_hex((uint64_t)fb.backbuffer);
```

---

## Layer 3: Raylib Shim

**Files:** `userland/raylib/raylib.c`, `userland/raylib/raylib.h`

### Module State

```c
static int rl_ready = 0;           // Set to fb_ready() during InitWindow
static int rl_width = 0;           // From fb_state
static int rl_height = 0;          // From fb_state
static struct fb_matrix rl_mat = {0};  // Points to backbuffer
```

### Initialization Flow (InitWindow)

```c
void InitWindow(int width, int height, const char* title)
{
    if (!fb_ready())
        fb_init();           // <-- May fail silently

    struct fb_state* state = fb_get_state();
    rl_width = state->width;
    rl_height = state->height;
    rl_ready = fb_ready();   // <-- If 0, nothing draws

    fb_matrix_get(&rl_mat);  // <-- rl_mat.data = backbuffer
}
```

### Potential Black Screen Causes

1. **rl_ready = 0** - fb_init failed, WindowShouldClose returns 1 immediately
2. **rl_mat.data = NULL** - Backbuffer not allocated
3. **rl_width/rl_height = 0** - No valid dimensions

### Debug Check

In `InitWindow()`:

```c
if (!rl_ready) klog_error("Raylib: fb not ready");
klog_log("Raylib: "); klog_hex(rl_width); klog_log("x"); klog_hex(rl_height);
```

---

## Layer 4: Raygui

**Files:** `userland/raygui/raygui.c`, `userland/raygui/raygui.h`

### GuiButton Implementation

```c
int GuiButton(Rectangle bounds, const char* text)
{
    Vector2 mouse = GetMousePosition();
    int hover = /* check if mouse inside bounds */;
    
    Color bg = hover ? LIGHT : DARK;
    DrawRectangle(bounds.x, bounds.y, bounds.width, bounds.height, bg);
    DrawRectangleLines(bounds.x, bounds.y, bounds.width, bounds.height, BLACK);
    DrawText(text, bounds.x + 8, bounds.y + 6, 16, BLACK);
    
    if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        return 1;
    return 0;
}
```

### Potential Black Screen Causes

Raygui itself is unlikely to cause black screen - if fb is working, raygui will draw.

---

## GUI Application

**Files:** `userland/gui/gui.c`, `userland/gui/gui.h`

### Main Loop

```c
void gui_run(void)
{
    InitWindow(0, 0, "GraceOS GUI");
    if (WindowShouldClose())   // <-- Exits immediately if fb fails
        return;

    while (!WindowShouldClose())
    {
        // Process keyboard for cursor movement
        BeginDrawing();
        ClearBackground(LIGHTGRAY);   // <-- Should make screen gray
        // Draw window, button, cursor
        EndDrawing();                 // <-- Presents to hardware
    }

    vbe_set_vga_text();  // Returns to text mode
}
```

### Potential Black Screen Causes

1. **WindowShouldClose() returns 1** - Exits before first draw
2. **ClearBackground not working** - fb operations fail
3. **EndDrawing() not called** - No present to hardware

---

## Debugging Checklist

### 1. Verify VBE Mode Set

```c
// In gui_run() before InitWindow:
struct vbe_mode_info vbe;
if (!vbe_init_auto(&vbe)) {
    klog_error("VBE: No valid mode");
    return;
}
klog_log("VBE phys: "); klog_hex(vbe.phys_base);
```

### 2. Verify Framebuffer Ready

```c
// In gui_run after InitWindow:
if (!fb_ready()) {
    klog_error("FB not ready");
    return;
}
struct fb_state* s = fb_get_state();
klog_log("FB: "); klog_hex(s->width); klog_log("x"); klog_hex(s->height);
klog_log("Backbuffer: "); klog_hex((uint64_t)s->backbuffer);
```

### 3. Verify Something Draws

```c
// Add solid color test before main loop:
fb_clear(0xFF0000FF);  // Blue
fb_present();
// Should show blue screen
```

### 4. Verify Present Works

```c
// Direct hardware write test:
uint32_t* fb = (uint32_t*)(uintptr_t)fb_get_state()->addr;
for (int i = 0; i < 1000; i++) fb[i] = 0xFFFF0000;  // Red line
```

---

## Common Issues

### Issue: Black Screen After GUI Starts

**Cause:** `fb_present()` not copying data correctly.

**Fix Check:**
```c
// In fb_present(), verify:
klog_log("Presenting "); klog_hex(bytes); klog_log(" bytes");
klog_log("From: "); klog_hex((uint64_t)src);
klog_log("To: "); klog_hex((uint64_t)dst);
```

### Issue: Black Screen, Text Mode Works

**Cause:** VBE mode switch failed or framebuffer mapping failed.

**Fix Check:** Add logging in `vbe_call()` to verify BIOS returns 0x004F.

### Issue: Partial Draws / Corruption

**Cause:** Wrong pitch or dimensions.

**Fix Check:** Verify `fb.pitch == fb.width * 4` (or at least >= width*4).

### Issue: GUI Exits Immediately

**Cause:** `WindowShouldClose()` returns 1.

**Fix Check:**
```c
if (WindowShouldClose()) {
    klog_error("Window closed immediately - rl_ready=");
    klog_hex(rl_ready);
    return;
}
```

---

## Color Format

All colors are 32-bit ARGB:

```
| A (8 bits) | R (8 bits) | G (8 bits) | B (8 bits) |
|  31-24     |  23-16     |  15-8      |  7-0       |
```

Raylib Color struct (RGBA order in struct, converted to ARGB):

```c
struct Color { uint8_t r, g, b, a; };

// Conversion in rl_color_to_u32():
uint32_t = (a << 24) | (b << 16) | (g << 8) | r;
```

**NOTE:** This may be incorrect! VBE typically expects:
- Direct color mode 6 = BGR (blue in low bits)
- Check `red_pos`, `green_pos`, `blue_pos` from VBE mode info

---

## Memory Layout

```
Kernel Heap
    ├── fb.backbuffer (width * height * 4 bytes)
    └── ...

Identity-Mapped Physical Memory
    └── Hardware Framebuffer (fb.addr)
        └── VMM maps with PWT|PCD flags (write-through, cache disable)
```

---

## Quick Diagnosis Table

| Symptom | Likely Cause | Check |
|---------|--------------|-------|
| Black screen, no boot | VBE mode set failed | Add klog in vbe_init_auto |
| Black screen, boots | fb_init failed | Check fb.ready |
| Gray screen, no widgets | Drawing works, widget code broken | Add klog in gui_run loop |
| Colored garbage | Wrong pitch or color format | Verify pitch, check BGR vs RGB |
| Flicker/tearing | No vsync (expected) | Normal for bare-metal |
| Cursor moves, nothing else | Draw calls fail | Check rl_mat.data |

---

## GRUB Configuration

Current `grub.cfg` does **not** request graphics mode:

```
menuentry "GraceOS" {
    multiboot2 /boot/kernel.elf
    boot
}
```

To let GRUB set up framebuffer (more reliable):

```
set gfxmode=1024x768x32
set gfxpayload=keep

menuentry "GraceOS" {
    multiboot2 /boot/kernel.elf
    boot
}
```

This provides framebuffer info via Multiboot2 tag, avoiding VBE BIOS thunk.

---

## Recommended Fix Order

1. Add `gfxmode` and `gfxpayload` to grub.cfg
2. Add logging to `multiboot_get_framebuffer()` 
3. Add logging to `fb_init()` for all failure paths
4. Test with solid color before GUI
5. Verify `fb_present()` copies correct amount
