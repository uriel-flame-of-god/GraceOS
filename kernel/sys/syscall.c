// ============================
// GraceOS Syscall Dispatcher
// ============================

#include "../include/syscall.h"
#include "../../drivers/video/tty.h"
#include "../../drivers/video/fb.h"
#include "../../drivers/input/keyboard.h"
#include "../../drivers/storage/bfs.h"
#include "../mm/kheap.h"
#include "../mm/sasy/sasy.h"
#include "../../lib/libc/string.h"
#include "../log/klog.h"
#include "../proc/proc.h"
#include "../proc/pipe.h"
#include "../spm/spm.h"
#include "power.h"
#include "../llm/runtime.h"
#include "../../include/grace/llm_syscalls.h"
#include "../../include/grace/net_syscalls.h"

/* Forward declaration for network syscall registration */
extern void net_syscalls_init(syscall_fn_t *table);

/* ============================
   Debug Logging
   ============================ */

#define SYSCALL_DEBUG 1

#if SYSCALL_DEBUG
#define sys_log(msg) \
    do { \
        tty_set_color(TTY_MAGENTA, TTY_BLACK); \
        tty_print("[SYS] "); \
        tty_set_color(TTY_LIGHT_GREY, TTY_BLACK); \
        tty_print(msg); \
        tty_print("\n"); \
    } while(0)
#else
#define sys_log(msg)
#endif

/* ============================
   Syscall Table
   ============================ */

#define MAX_SYSCALLS 256

static syscall_fn_t syscall_table[MAX_SYSCALLS];

/* ============================
   External BFS instance
   ============================ */

extern struct bfs_instance g_bfs;

/* ============================
   File Descriptor Helpers
   ============================ */

/* Allocate the lowest available fd >= PIPE_FD_BASE in the current process.
   Returns the fd index or -1 if the fd table is full. */
static int alloc_fd(file_t** fd_table)
{
    for (int i = PIPE_FD_BASE; i < PROC_MAX_FDS; i++)
    {
        if (fd_table[i] == NULL)
            return i;
    }
    return -1;
}

/* ============================
   Syscall Implementations
   ============================ */

/* SYS_WRITE - Write to file descriptor */
long sys_write(long fd, long buf, long len, long unused1, long unused2, long unused3)
{
    (void)unused1; (void)unused2; (void)unused3;
    
    if (fd == 1 || fd == 2)  // stdout or stderr
    {
        char* str = (char*)buf;
        for (long i = 0; i < len; i++)
            tty_putchar(str[i]);
        return len;
    }

    /* Pipe write via process fd table */
    if (current && fd >= PIPE_FD_BASE && fd < PROC_MAX_FDS)
    {
        file_t* f = current->fds[fd];
        if (f && f->type == FILE_TYPE_PIPE_WRITE)
        {
            int n = pipe_write(f->pipe_id, (const void*)buf, (int)len);
            return (long)n;
        }
    }
    
    return -1;  // Invalid fd
}

/* SYS_READ - Read from file descriptor */
long sys_read(long fd, long buf, long len, long unused1, long unused2, long unused3)
{
    (void)unused1; (void)unused2; (void)unused3;
    
    if (fd == 0)  // stdin
    {
        char* str = (char*)buf;
        long bytes_read = 0;
        
        // Special case: single character reads (like shell)
        if (len == 1) {
            char c = keyboard_getchar_nonblocking();
            if (c != 0) {
                str[0] = c;
                return 1;
            }
            return 0;
        }
        
        // Multi-character reads
        for (long i = 0; i < len; i++)
        {
            char c = keyboard_getchar_nonblocking();
            if (c == 0)
                break;
            str[i] = c;
            bytes_read++;
            if (c == '\n')
                break;
        }
        
        return bytes_read;
    }

    /* Pipe read via process fd table */
    if (current && fd >= PIPE_FD_BASE && fd < PROC_MAX_FDS)
    {
        file_t* f = current->fds[fd];
        if (f && f->type == FILE_TYPE_PIPE_READ)
        {
            int n = pipe_read(f->pipe_id, (void*)buf, (int)len);
            /* -2 means no data yet (non-blocking); report as 0 */
            return (long)(n < 0 ? 0 : n);
        }
    }
    
    return -1;  // Invalid fd
}

/* SYS_EXIT - Exit current process */
long sys_exit(long code, long unused1, long unused2, long unused3, long unused4, long unused5)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    
    sys_log("exit called");
    
    // For now, just print exit code and halt
    tty_print("\nProcess exited with code: ");
    char buf[16];
    // Simple integer to string
    int val = (int)code;
    int i = 0;
    if (val == 0)
    {
        buf[i++] = '0';
    }
    else
    {
        int neg = 0;
        if (val < 0) { neg = 1; val = -val; }
        char tmp[16];
        int j = 0;
        while (val > 0) { tmp[j++] = '0' + (val % 10); val /= 10; }
        if (neg) buf[i++] = '-';
        while (j > 0) buf[i++] = tmp[--j];
    }
    buf[i] = '\0';
    tty_print(buf);
    tty_print("\n");
    
    // Halt (no process management yet)
    for (;;)
        __asm__ volatile("hlt");
    
    return 0;
}

/* SYS_EXEC - Execute program (stub) */
long sys_exec(long path, long unused1, long unused2, long unused3, long unused4, long unused5)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5;

    const char* exe_path = (const char*)path;
    if (!exe_path || *exe_path == '\0')
        return -1;

    /* Shell currently runs in kernel/idle context. In that case execute
       through proc_exec with a temporary process object to avoid allocator
       dependency and return cleanly to terminal. */
    if (!current || current->pid == PID_KERNEL)
    {
        process_t temp;
        memset(&temp, 0, sizeof(temp));
        int rc_kernel_ctx = proc_exec(&temp, exe_path, NULL);
        if (rc_kernel_ctx < 0)
            return -1;
        return (long)temp.exit_code;
    }

    process_t* child = proc_create(current);
    if (!child)
    {
        /* GraceOS shell often runs without a fully provisioned user process.
           Fallback to a temporary execution context so exec failures return
           cleanly to terminal instead of failing at process allocation. */
        process_t temp;
        memset(&temp, 0, sizeof(temp));
        int rc_fallback = proc_exec(&temp, exe_path, NULL);
        if (rc_fallback < 0)
            return -1;
        return (long)temp.exit_code;
    }

    int rc = proc_exec(child, exe_path, NULL);
    int exit_code = child->exit_code;

    proc_destroy(child);

    if (rc < 0)
        return -1;
    return (long)exit_code;
}

/* SYS_ALLOC - Allocate memory */
long sys_alloc(long size, long unused1, long unused2, long unused3, long unused4, long unused5)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    
    void* ptr = kmalloc((size_t)size);
    return (long)ptr;
}

/* SYS_FREE - Free memory */
long sys_free(long ptr, long unused1, long unused2, long unused3, long unused4, long unused5)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    
    // kfree not implemented yet, just ignore
    (void)ptr;
    return 0;
}

/* SYS_OPEN - Open file */
long sys_open(long path, long flags, long unused1, long unused2, long unused3, long unused4)
{
    (void)flags; (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    
    char* p = (char*)path;
    struct bfs_file_entry entry;
    
    if (bfs_find(&g_bfs, p, &entry) != 0)
        return -1;  // File not found

    /* No process context — return a legacy sentinel */
    if (!current)
        return 100;

    /* Allocate a file descriptor slot */
    int fd_idx = alloc_fd(current->fds);
    if (fd_idx < 0)
        return -1;  // fd table full

    file_t* f = (file_t*)kmalloc(sizeof(file_t));
    if (!f)
        return -1;

    f->type    = FILE_TYPE_NONE;
    f->pipe_id = -1;
    current->fds[fd_idx] = f;
    return (long)fd_idx;
}

/* SYS_CLOSE - Close file */
long sys_close(long fd, long unused1, long unused2, long unused3, long unused4, long unused5)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5;

    if (!current || fd < PIPE_FD_BASE || fd >= PROC_MAX_FDS)
        return -1;

    file_t* f = current->fds[fd];
    if (!f)
        return -1;

    if (f->type == FILE_TYPE_PIPE_READ)
        pipe_close_read(f->pipe_id);
    else if (f->type == FILE_TYPE_PIPE_WRITE)
        pipe_close_write(f->pipe_id);

    kfree(f);
    current->fds[fd] = NULL;
    return 0;
}

/* SYS_PIPE - Create a pipe (pipefd[0]=read end, pipefd[1]=write end) */
long sys_pipe(long pipefd_ptr, long unused1, long unused2, long unused3, long unused4, long unused5)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5;

    if (!current || !pipefd_ptr)
        return -1;

    int* pipefd = (int*)pipefd_ptr;

    int pipe_id = pipe_alloc();
    if (pipe_id < 0)
        return -1;

    /* Allocate read end */
    int rfd = alloc_fd(current->fds);
    if (rfd < 0)
    {
        pipe_close_read(pipe_id);
        pipe_close_write(pipe_id);
        return -1;
    }

    file_t* rf = (file_t*)kmalloc(sizeof(file_t));
    if (!rf)
    {
        pipe_close_read(pipe_id);
        pipe_close_write(pipe_id);
        return -1;
    }
    rf->type    = FILE_TYPE_PIPE_READ;
    rf->pipe_id = pipe_id;
    current->fds[rfd] = rf;

    /* Allocate write end */
    int wfd = alloc_fd(current->fds);
    if (wfd < 0)
    {
        kfree(rf);
        current->fds[rfd] = NULL;
        pipe_close_read(pipe_id);
        pipe_close_write(pipe_id);
        return -1;
    }

    file_t* wf = (file_t*)kmalloc(sizeof(file_t));
    if (!wf)
    {
        kfree(rf);
        current->fds[rfd] = NULL;
        pipe_close_read(pipe_id);
        pipe_close_write(pipe_id);
        return -1;
    }
    wf->type    = FILE_TYPE_PIPE_WRITE;
    wf->pipe_id = pipe_id;
    current->fds[wfd] = wf;

    pipefd[0] = rfd;
    pipefd[1] = wfd;
    return 0;
}

/* SYS_LIST - List directory contents */
long sys_list(long path, long buf, long buflen, long unused1, long unused2, long unused3)
{
    (void)path; (void)unused1; (void)unused2; (void)unused3;
    
    char* outbuf = (char*)buf;
    size_t maxlen = (size_t)buflen;
    
    if (!outbuf || maxlen == 0)
        return -1;
    
    // List files using bfs_list (which prints to tty)
    // For now, return empty - proper implementation needs BFS iterator
    outbuf[0] = '\0';
    
    // Use bfs_list to print to terminal (temporary)
    bfs_list(&g_bfs);
    
    return 0;
}

/* SYS_GETKEY - Get single key from keyboard */
long sys_getkey(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5; (void)unused6;
    
    return (long)keyboard_getchar();
}

/* SYS_HASKEY - Check if key available (non-blocking) */
long sys_haskey(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5; (void)unused6;
    
    return (long)keyboard_haschar();
}

/* ============================
   SASY Segment Syscalls
   ============================ */

/* SYS_SEG_CREATE - Create a new segment */
long sys_seg_create(long size, long type, long flags, long unused1, long unused2, long unused3)
{
    (void)unused1; (void)unused2; (void)unused3;
    
    seg_handle_t h = sasy_create_ex(
        (uint64_t)size,
        (seg_type_t)type,
        (uint32_t)flags,
        0  /* TODO: Get current process PID */
    );
    
    return (long)h;
}

/* SYS_SEG_LOCK - Lock segment and get address */
long sys_seg_lock(long handle, long unused1, long unused2, long unused3, long unused4, long unused5)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    
    void* addr = sasy_lock((seg_handle_t)handle);
    
    return (long)addr;
}

/* SYS_SEG_UNLOCK - Unlock segment */
long sys_seg_unlock(long handle, long unused1, long unused2, long unused3, long unused4, long unused5)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    
    sasy_unlock((seg_handle_t)handle);
    
    return 0;
}

/* SYS_SEG_FREE - Free segment */
long sys_seg_free(long handle, long unused1, long unused2, long unused3, long unused4, long unused5)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    
    sasy_free((seg_handle_t)handle);
    
    return 0;
}

/* SYS_SEG_RESIZE - Resize segment */
long sys_seg_resize(long handle, long new_size, long unused1, long unused2, long unused3, long unused4)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    
    return (long)sasy_resize((seg_handle_t)handle, (uint64_t)new_size);
}

/* SYS_SEG_INFO - Get segment information */
long sys_seg_info(long handle, long info_ptr, long unused1, long unused2, long unused3, long unused4)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    
    if (!info_ptr)
        return -1;
    
    segment_t* info = (segment_t*)info_ptr;
    
    return (long)sasy_get_info((seg_handle_t)handle, info);
}

/* ============================
   Framebuffer Syscalls
   ============================ */

/* SYS_FB_GETINFO - Get framebuffer information */
long sys_fb_getinfo(long info_ptr, long unused1, long unused2, long unused3, long unused4, long unused5)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    
    if (!info_ptr)
        return -1;
    
    struct fb_state* fb = fb_get_state();
    if (!fb || !fb->ready)
        return -1;
    
    // Return width, height, bpp
    uint32_t* info = (uint32_t*)info_ptr;
    info[0] = fb->width;
    info[1] = fb->height;
    info[2] = fb->bpp;
    
    return 0;
}

/* SYS_FB_MAP - Map framebuffer into user process memory */
long sys_fb_map(long addr_hint, long unused1, long unused2, long unused3, long unused4, long unused5)
{
    (void)addr_hint; (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    
    struct fb_state* fb = fb_get_state();
    if (!fb || !fb->ready)
        return 0;
    
    // Return physical address of framebuffer (simplified)
    // In a full implementation, this would map via page tables
    return (long)fb->addr;
}

/* SYS_FB_UNMAP - Unmap framebuffer from user process */
long sys_fb_unmap(long addr, long unused1, long unused2, long unused3, long unused4, long unused5)
{
    (void)addr; (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    
    // Simplified: nothing to unmap in this implementation
    return 0;
}

/* SYS_FB_PRESENT - Present backbuffer to screen */
long sys_fb_present(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5; (void)unused6;
    
    fb_present();
    return 0;
}

/* Alpha blending helper: out = src * alpha + dst * (1 - alpha) */
static inline uint32_t alpha_blend(uint32_t dst, uint32_t src)
{
    uint8_t a = (src >> 24) & 0xFF;
    
    // Fully opaque - just return source
    if (a == 255) return src;
    
    // Fully transparent - return destination
    if (a == 0) return dst;
    
    // Extract components
    uint8_t src_r = (src >> 16) & 0xFF;
    uint8_t src_g = (src >> 8) & 0xFF;
    uint8_t src_b = src & 0xFF;
    
    uint8_t dst_r = (dst >> 16) & 0xFF;
    uint8_t dst_g = (dst >> 8) & 0xFF;
    uint8_t dst_b = dst & 0xFF;
    
    // Blend: out = src * (a/255) + dst * (1 - a/255)
    uint8_t out_r = ((src_r * a) + (dst_r * (255 - a))) / 255;
    uint8_t out_g = ((src_g * a) + (dst_g * (255 - a))) / 255;
    uint8_t out_b = ((src_b * a) + (dst_b * (255 - a))) / 255;
    
    return (0xFF << 24) | (out_r << 16) | (out_g << 8) | out_b;
}

/* Get backbuffer pixel value for blending */
static inline uint32_t get_backbuffer_pixel(int x, int y)
{
    struct fb_state* fb = fb_get_state();
    if (!fb || !fb->ready) return 0;
    
    if (x < 0 || (uint32_t)x >= fb->width || y < 0 || (uint32_t)y >= fb->height)
        return 0;
    
    return fb->backbuffer[y * fb->width + x];
}

/* SYS_FB_CLEAR - Clear framebuffer */
long sys_fb_clear(long color, long unused1, long unused2, long unused3, long unused4, long unused5)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    
    fb_clear((uint32_t)color);
    return 0;
}

/* SYS_FB_PIXEL - Draw pixel to framebuffer with alpha blending */
long sys_fb_pixel(long x, long y, long color, long unused1, long unused2, long unused3)
{
    (void)unused1; (void)unused2; (void)unused3;
    
    uint32_t src = (uint32_t)color;
    uint8_t alpha = (src >> 24) & 0xFF;
    
    if (alpha == 255) {
        // Opaque - just write the pixel
        fb_put_pixel((int)x, (int)y, src);
    } else if (alpha > 0) {
        // Semi-transparent - blend with existing pixel
        uint32_t dst = get_backbuffer_pixel((int)x, (int)y);
        uint32_t blended = alpha_blend(dst, src);
        fb_put_pixel((int)x, (int)y, blended);
    }
    // alpha == 0: fully transparent, do nothing
    
    return 0;
}

/* SYS_FB_RECT - Draw filled rectangle to framebuffer with alpha blending */
long sys_fb_rect(long x, long y, long w, long h, long color, long unused1)
{
    (void)unused1;
    
    uint32_t src = (uint32_t)color;
    uint8_t alpha = (src >> 24) & 0xFF;
    
    if (alpha == 255) {
        // Opaque - use fast path
        fb_fill_rect((int)x, (int)y, (int)w, (int)h, src);
    } else if (alpha > 0) {
        // Semi-transparent - blend each pixel
        int x1 = (int)x;
        int y1 = (int)y;
        int x2 = x1 + (int)w;
        int y2 = y1 + (int)h;
        
        struct fb_state* fb = fb_get_state();
        if (!fb || !fb->ready) return -1;
        
        // Clip to screen bounds
        if (x1 < 0) x1 = 0;
        if (y1 < 0) y1 = 0;
        if ((uint32_t)x2 > fb->width) x2 = fb->width;
        if ((uint32_t)y2 > fb->height) y2 = fb->height;
        
        for (int row = y1; row < y2; row++) {
            for (int col = x1; col < x2; col++) {
                uint32_t dst = fb->backbuffer[row * fb->width + col];
                uint32_t blended = alpha_blend(dst, src);
                fb->backbuffer[row * fb->width + col] = blended;
            }
        }
    }
    // alpha == 0: fully transparent, do nothing
    
    return 0;
}

/* SYS_FB_CIRCLE - Draw filled circle with alpha blending */
long sys_fb_circle(long cx, long cy, long radius, long color, long unused1, long unused2)
{
    (void)unused1; (void)unused2;
    
    struct fb_state* fb = fb_get_state();
    if (!fb || !fb->ready) return -1;
    
    int x = (int)cx;
    int y = (int)cy;
    int r = (int)radius;
    uint32_t src = (uint32_t)color;
    uint8_t alpha = (src >> 24) & 0xFF;
    
    if (r < 0) return -1;
    if (r == 0) {
        // Draw single pixel
        if (alpha == 255) {
            fb_put_pixel(x, y, src);
        } else if (alpha > 0) {
            uint32_t dst = get_backbuffer_pixel(x, y);
            fb_put_pixel(x, y, alpha_blend(dst, src));
        }
        return 0;
    }
    
    // Midpoint circle algorithm - fill horizontal spans
    int dx = r;
    int dy = 0;
    int err = 0;
    
    while (dx >= dy) {
        // Fill horizontal spans at each y level
        for (int i = -dx; i <= dx; i++) {
            int px1 = x + i;
            int py1 = y + dy;
            int py2 = y - dy;
            
            if (alpha == 255) {
                fb_put_pixel(px1, py1, src);
                if (dy != 0) fb_put_pixel(px1, py2, src);
            } else if (alpha > 0) {
                uint32_t dst1 = get_backbuffer_pixel(px1, py1);
                fb_put_pixel(px1, py1, alpha_blend(dst1, src));
                if (dy != 0) {
                    uint32_t dst2 = get_backbuffer_pixel(px1, py2);
                    fb_put_pixel(px1, py2, alpha_blend(dst2, src));
                }
            }
        }
        
        for (int i = -dy; i <= dy; i++) {
            int px1 = x + i;
            int py1 = y + dx;
            int py2 = y - dx;
            
            if (dx != dy) {  // Avoid drawing same span twice
                if (alpha == 255) {
                    fb_put_pixel(px1, py1, src);
                    fb_put_pixel(px1, py2, src);
                } else if (alpha > 0) {
                    uint32_t dst1 = get_backbuffer_pixel(px1, py1);
                    fb_put_pixel(px1, py1, alpha_blend(dst1, src));
                    uint32_t dst2 = get_backbuffer_pixel(px1, py2);
                    fb_put_pixel(px1, py2, alpha_blend(dst2, src));
                }
            }
        }
        
        if (err <= 0) {
            dy++;
            err += 2*dy + 1;
        }
        if (err > 0) {
            dx--;
            err -= 2*dx + 1;
        }
    }
    
    return 0;
}

/* SYS_FB_LINE - Draw line using Bresenham's algorithm */
long sys_fb_line(long x1, long y1, long x2, long y2, long color, long unused1)
{
    (void)unused1;
    
    int ix1 = (int)x1;
    int iy1 = (int)y1;
    int ix2 = (int)x2;
    int iy2 = (int)y2;
    uint32_t src = (uint32_t)color;
    uint8_t alpha = (src >> 24) & 0xFF;
    
    // Bresenham's line algorithm
    int dx = ix2 - ix1;
    int dy = iy2 - iy1;
    
    // Make dx and dy positive
    int sx = dx < 0 ? -1 : 1;
    int sy = dy < 0 ? -1 : 1;
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    
    int err = dx - dy;
    int x = ix1;
    int y = iy1;
    
    while (1) {
        // Draw pixel at (x, y)
        if (alpha == 255) {
            fb_put_pixel(x, y, src);
        } else if (alpha > 0) {
            uint32_t dst = get_backbuffer_pixel(x, y);
            fb_put_pixel(x, y, alpha_blend(dst, src));
        }
        
        if (x == ix2 && y == iy2) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }
    
    return 0;
}

/* SYS_FB_BLIT - Blit image buffer to framebuffer */
long sys_fb_blit(long src_buf, long x, long y, long w, long h, long unused1)
{
    (void)unused1;
    
    struct fb_state* fb = fb_get_state();
    if (!fb || !fb->ready) return -1;
    
    uint32_t* src = (uint32_t*)src_buf;
    if (!src) return -1;
    
    int ix = (int)x;
    int iy = (int)y;
    int iw = (int)w;
    int ih = (int)h;
    
    // Bounds check
    if (iw <= 0 || ih <= 0) return -1;
    
    // Clip to screen
    int src_x_offset = 0;
    int src_y_offset = 0;
    
    if (ix < 0) {
        src_x_offset = -ix;
        iw += ix;
        ix = 0;
    }
    if (iy < 0) {
        src_y_offset = -iy;
        ih += iy;
        iy = 0;
    }
    if (ix + iw > (int)fb->width) {
        iw = fb->width - ix;
    }
    if (iy + ih > (int)fb->height) {
        ih = fb->height - iy;
    }
    
    if (iw <= 0 || ih <= 0) return 0;  // Completely off-screen
    
    // Blit with alpha blending
    for (int row = 0; row < ih; row++) {
        for (int col = 0; col < iw; col++) {
            uint32_t src_pixel = src[(src_y_offset + row) * (int)w + (src_x_offset + col)];
            uint8_t alpha = (src_pixel >> 24) & 0xFF;
            
            int dst_x = ix + col;
            int dst_y = iy + row;
            
            if (alpha == 255) {
                fb->backbuffer[dst_y * fb->width + dst_x] = src_pixel;
            } else if (alpha > 0) {
                uint32_t dst = fb->backbuffer[dst_y * fb->width + dst_x];
                fb->backbuffer[dst_y * fb->width + dst_x] = alpha_blend(dst, src_pixel);
            }
        }
    }
    
    return 0;
}

/* ============================
   Syscall Dispatcher
   ============================ */

long syscall_dispatch(long num, long a1, long a2, long a3, long a4, long a5)
{
    if (num <= 0 || num >= MAX_SYSCALLS || !syscall_table[num])
    {
        sys_log("invalid syscall");
        return -1;
    }
    
    return syscall_table[num](a1, a2, a3, a4, a5, 0);
}

/* ============================
   Syscall Initialization
   ============================ */

void syscall_init(void)
{
    sys_log("Initializing syscall table");
    
    // Clear table
    for (int i = 0; i < MAX_SYSCALLS; i++)
    {
        syscall_table[i] = 0;
    }
    
    // Register syscalls
    syscall_table[SYS_WRITE]   = sys_write;
    syscall_table[SYS_READ]    = sys_read;
    syscall_table[SYS_EXIT]    = sys_exit;
    syscall_table[SYS_EXEC]    = sys_exec;
    syscall_table[SYS_ALLOC]   = sys_alloc;
    syscall_table[SYS_FREE]    = sys_free;
    syscall_table[SYS_OPEN]    = sys_open;
    syscall_table[SYS_CLOSE]   = sys_close;
    syscall_table[SYS_LIST]    = sys_list;
    syscall_table[SYS_GETKEY]  = sys_getkey;
    syscall_table[SYS_HASKEY]  = sys_haskey;
    syscall_table[SYS_SYSINFO] = (syscall_fn_t)sys_sysinfo;
    
    // Time management syscalls
    syscall_table[SYS_TIME]          = sys_time;
    syscall_table[SYS_TIME_MS]       = sys_time_ms;
    syscall_table[SYS_CLOCK_GETTIME] = sys_clock_gettime;
    syscall_table[SYS_GETTIMEOFDAY]  = sys_gettimeofday;
    syscall_table[SYS_SETTIMEOFDAY]  = sys_settimeofday;
    
    // SASY Segment syscalls
    syscall_table[SYS_SEG_CREATE] = sys_seg_create;
    syscall_table[SYS_SEG_LOCK]   = sys_seg_lock;
    syscall_table[SYS_SEG_UNLOCK] = sys_seg_unlock;
    syscall_table[SYS_SEG_FREE]   = sys_seg_free;
    syscall_table[SYS_SEG_RESIZE] = sys_seg_resize;
    syscall_table[SYS_SEG_INFO]   = sys_seg_info;
    
    // Process management syscalls
    syscall_table[SYS_FORK]    = sys_fork;
    syscall_table[SYS_WAIT]    = sys_wait;
    syscall_table[SYS_KILL]    = sys_kill;
    syscall_table[SYS_GETPID]  = sys_getpid;
    syscall_table[SYS_GETPPID] = sys_getppid;
    syscall_table[SYS_SETSID]  = sys_setsid;
    syscall_table[SYS_YIELD]   = sys_yield;
    syscall_table[SYS_SLEEP]   = sys_sleep;
    syscall_table[SYS_GETUID]  = sys_getuid;
    syscall_table[SYS_SETUID]  = sys_setuid;
    
    // IPC syscalls
    syscall_table[SYS_PIPE] = sys_pipe;

    // Power management syscalls
    syscall_table[SYS_SHUTDOWN] = sys_shutdown;
    syscall_table[SYS_REBOOT]   = sys_reboot;

    // LLM runtime syscalls
    llm_syscalls_init();
    syscall_table[SYS_LLM_LOAD_MODEL]      = sys_llm_load_model;
    syscall_table[SYS_LLM_UNLOAD_MODEL]    = sys_llm_unload_model;
    syscall_table[SYS_LLM_CREATE_CONTEXT]  = sys_llm_create_context;
    syscall_table[SYS_LLM_DESTROY_CONTEXT] = sys_llm_destroy_context;
    syscall_table[SYS_LLM_TOKENIZE]        = sys_llm_tokenize;
    syscall_table[SYS_LLM_DETOKENIZE]      = sys_llm_detokenize;
    syscall_table[SYS_LLM_EVAL]            = sys_llm_eval;
    syscall_table[SYS_LLM_SAMPLE]          = sys_llm_sample;
    syscall_table[SYS_LLM_CHAT_BEGIN]      = sys_llm_chat_begin;
    syscall_table[SYS_LLM_CHAT_APPEND]     = sys_llm_chat_append;
    syscall_table[SYS_LLM_CHAT_GENERATE]   = sys_llm_chat_generate;
    syscall_table[SYS_LLM_CHAT_RESET]      = sys_llm_chat_reset;
    syscall_table[SYS_LLM_GET_INFO]        = sys_llm_get_info;
    
    // Networking syscalls
    net_syscalls_init(syscall_table);

    // Framebuffer syscalls
    syscall_table[SYS_FB_GETINFO] = sys_fb_getinfo;
    syscall_table[SYS_FB_MAP]     = sys_fb_map;
    syscall_table[SYS_FB_UNMAP]   = sys_fb_unmap;
    syscall_table[SYS_FB_CLEAR]   = sys_fb_clear;
    syscall_table[SYS_FB_PIXEL]   = sys_fb_pixel;
    syscall_table[SYS_FB_RECT]    = sys_fb_rect;
    syscall_table[SYS_FB_PRESENT] = sys_fb_present;
    syscall_table[SYS_FB_CIRCLE]  = sys_fb_circle;
    syscall_table[SYS_FB_LINE]    = sys_fb_line;
    syscall_table[SYS_FB_BLIT]    = sys_fb_blit;

    // Disk I/O syscalls
    syscall_table[SYS_DISK_INFO]  = sys_disk_info;
    syscall_table[SYS_DISK_READ]  = sys_disk_read;
    syscall_table[SYS_DISK_WRITE] = sys_disk_write;

    // SPM syscalls
    syscall_table[SYS_SPM_USER_ADD]    = sys_spm_user_add;
    syscall_table[SYS_SPM_USER_ENUM]   = sys_spm_user_enum;
    syscall_table[SYS_SPM_CAP_GRANT]   = sys_spm_cap_grant;
    syscall_table[SYS_SPM_CAP_ENUM]    = sys_spm_cap_enum;
    syscall_table[SYS_SPM_CHECK]       = sys_spm_check;
    syscall_table[SYS_SPM_USER_PASSWD] = sys_spm_user_passwd;
    syscall_table[SYS_SPM_AUTH]        = sys_spm_auth;

    // Audio syscalls (VoiceBoxSystem)
    audio_syscalls_init();
    syscall_table[SYS_AUDIO_OPEN]  = sys_audio_open;
    syscall_table[SYS_AUDIO_WRITE] = sys_audio_write;
    syscall_table[SYS_AUDIO_START] = sys_audio_start;
    syscall_table[SYS_AUDIO_STOP]  = sys_audio_stop;
    syscall_table[SYS_AUDIO_CLOSE] = sys_audio_close;
    syscall_table[SYS_AUDIO_CTL]   = sys_audio_ctl;

    sys_log("Syscall table ready");
}
