// ============================
// GraceOS Syscall Interface
// ============================

#ifndef GRACEOS_SYSCALL_H
#define GRACEOS_SYSCALL_H

#include "../../lib/libc/int.h"

/* ============================
   Syscall Numbers
   Keep stable - ABI contract!
   ============================ */

enum syscall_num {
    SYS_WRITE   = 1,    // Write to fd
    SYS_READ    = 2,    // Read from fd
    SYS_EXIT    = 3,    // Exit process
    SYS_EXEC    = 4,    // Execute program
    SYS_ALLOC   = 5,    // Allocate memory
    SYS_FREE    = 6,    // Free memory
    SYS_OPEN    = 7,    // Open file
    SYS_CLOSE   = 8,    // Close file
    SYS_LIST    = 9,    // List directory
    SYS_GETKEY  = 10,   // Get keyboard input (blocking)
    SYS_HASKEY  = 11,   // Check if key available (non-blocking)
    SYS_SYSINFO = 20,   // Get system information
    
    /* Time Management Syscalls */
    SYS_TIME           = 25,   // Get current time in seconds
    SYS_TIME_MS        = 26,   // Get current time in milliseconds
    SYS_CLOCK_GETTIME  = 27,   // Get time with nanosecond precision
    SYS_GETTIMEOFDAY   = 28,   // Legacy time function
    SYS_SETTIMEOFDAY   = 29,   // Set system time (requires root)
    
    /* SASY Segment Syscalls */
    SYS_SEG_CREATE  = 30,   // Create segment
    SYS_SEG_LOCK    = 31,   // Lock segment (get address)
    SYS_SEG_UNLOCK  = 32,   // Unlock segment
    SYS_SEG_FREE    = 33,   // Free segment
    SYS_SEG_RESIZE  = 34,   // Resize segment
    SYS_SEG_INFO    = 35,   // Get segment info
    
    /* Process Management Syscalls */
    SYS_FORK    = 40,   // Fork process
    SYS_WAIT    = 41,   // Wait for child
    SYS_KILL    = 42,   // Send signal
    SYS_GETPID  = 43,   // Get process ID
    SYS_GETPPID = 44,   // Get parent PID
    SYS_SETSID  = 45,   // Create new session
    SYS_YIELD   = 46,   // Yield CPU
    SYS_SLEEP   = 47,   // Sleep for milliseconds
    SYS_GETUID  = 48,   // Get user ID
    SYS_SETUID  = 49,   // Set user ID
    
    /* Power Management Syscalls */
    SYS_SHUTDOWN = 50,  // Shutdown system
    SYS_REBOOT   = 51,  // Reboot system
    
    /* IPC Syscalls */
    SYS_PIPE     = 70,  // Create a pipe (pipefd[2]: [0]=read, [1]=write)
    
    /* Disk I/O Syscalls */
    SYS_DISK_INFO  = 80,  // Get disk info (size, sector count)
    SYS_DISK_READ  = 81,  // Read sectors from disk
    SYS_DISK_WRITE = 82,  // Write sectors to disk

   /* SPM Syscalls */
   SYS_SPM_USER_ADD    = 90,  // Add user
   SYS_SPM_USER_ENUM   = 91,  // Enumerate users
   SYS_SPM_CAP_GRANT   = 92,  // Grant capability
   SYS_SPM_CAP_ENUM    = 93,  // Enumerate capabilities
   SYS_SPM_CHECK       = 94,  // Check permission
   SYS_SPM_USER_PASSWD = 95,  // Set user password
   SYS_SPM_AUTH        = 96,  // Authenticate user

    /* Framebuffer Syscalls */
    SYS_FB_GETINFO = 60,  // Get framebuffer info (width, height, bpp)
    SYS_FB_MAP     = 61,  // Map framebuffer into process memory
    SYS_FB_UNMAP   = 62,  // Unmap framebuffer
    SYS_FB_CLEAR   = 63,  // Clear framebuffer
    SYS_FB_PIXEL   = 64,  // Draw pixel
    SYS_FB_RECT    = 65,  // Draw rectangle
    SYS_FB_PRESENT = 66,  // Present backbuffer to screen
    SYS_FB_CIRCLE  = 67,  // Draw filled circle
    SYS_FB_LINE    = 68,  // Draw line
    SYS_FB_BLIT    = 69,  // Blit image buffer
};

/* ============================
   Register ABI (int 0x80)
   
   RAX = syscall number
   RDI = arg1
   RSI = arg2
   RDX = arg3
   R10 = arg4
   R8  = arg5
   R9  = arg6
   RAX = return value
   ============================ */

/* Syscall function type */
typedef long (*syscall_fn_t)(long, long, long, long, long, long);

/* ============================
   Kernel Syscall API
   ============================ */

/* Initialize syscall subsystem */
void syscall_init(void);

/* Syscall dispatcher (called from ASM) */
long syscall_dispatch(long num, long a1, long a2, long a3, long a4, long a5);

/* ============================
   Syscall Implementations
   ============================ */

long sys_write(long fd, long buf, long len, long unused1, long unused2, long unused3);
long sys_read(long fd, long buf, long len, long unused1, long unused2, long unused3);
long sys_exit(long code, long unused1, long unused2, long unused3, long unused4, long unused5);
long sys_exec(long path, long unused1, long unused2, long unused3, long unused4, long unused5);
long sys_alloc(long size, long unused1, long unused2, long unused3, long unused4, long unused5);
long sys_free(long ptr, long unused1, long unused2, long unused3, long unused4, long unused5);
long sys_open(long path, long flags, long unused1, long unused2, long unused3, long unused4);
long sys_close(long fd, long unused1, long unused2, long unused3, long unused4, long unused5);
long sys_list(long path, long buf, long buflen, long unused1, long unused2, long unused3);
long sys_getkey(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6);
long sys_haskey(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6);
long sys_sysinfo(long uinfo_ptr, long unused1, long unused2, long unused3, long unused4, long unused5);

/* Time Management Syscall Implementations */
long sys_time(long t_ptr, long unused1, long unused2, long unused3, long unused4, long unused5);
long sys_time_ms(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6);
long sys_clock_gettime(long clk_id, long ts_ptr, long unused1, long unused2, long unused3, long unused4);
long sys_gettimeofday(long tv_ptr, long tz_ptr, long unused1, long unused2, long unused3, long unused4);
long sys_settimeofday(long ts_ptr, long unused1, long unused2, long unused3, long unused4, long unused5);

/* SASY Segment Syscall Implementations */
long sys_seg_create(long size, long type, long flags, long unused1, long unused2, long unused3);
long sys_seg_lock(long handle, long unused1, long unused2, long unused3, long unused4, long unused5);
long sys_seg_unlock(long handle, long unused1, long unused2, long unused3, long unused4, long unused5);
long sys_seg_free(long handle, long unused1, long unused2, long unused3, long unused4, long unused5);
long sys_seg_resize(long handle, long new_size, long unused1, long unused2, long unused3, long unused4);
long sys_seg_info(long handle, long info_ptr, long unused1, long unused2, long unused3, long unused4);

/* Process Management Syscall Implementations */
long sys_fork(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6);
long sys_wait(long pid, long status_ptr, long options, long unused1, long unused2, long unused3);
long sys_kill(long pid, long signal, long unused1, long unused2, long unused3, long unused4);
long sys_getpid(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6);
long sys_getppid(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6);
long sys_setsid(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6);
long sys_yield(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6);
long sys_sleep(long ms, long unused1, long unused2, long unused3, long unused4, long unused5);
long sys_getuid(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6);
long sys_setuid(long uid, long unused1, long unused2, long unused3, long unused4, long unused5);

/* Framebuffer Syscall Implementations */
long sys_fb_getinfo(long info_ptr, long unused1, long unused2, long unused3, long unused4, long unused5);
long sys_fb_map(long addr_hint, long unused1, long unused2, long unused3, long unused4, long unused5);
long sys_fb_unmap(long addr, long unused1, long unused2, long unused3, long unused4, long unused5);
long sys_fb_clear(long color, long unused1, long unused2, long unused3, long unused4, long unused5);
long sys_fb_pixel(long x, long y, long color, long unused1, long unused2, long unused3);
long sys_fb_rect(long x, long y, long w, long h, long color, long unused1);
long sys_fb_present(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6);
long sys_fb_circle(long x, long y, long radius, long color, long unused1, long unused2);
long sys_fb_line(long x1, long y1, long x2, long y2, long color, long unused1);
long sys_fb_blit(long src_buf, long x, long y, long w, long h, long unused1);

/* IPC Syscall Implementations */
long sys_pipe(long pipefd_ptr, long unused1, long unused2, long unused3, long unused4, long unused5);

/* Disk I/O Syscall Implementations */
long sys_disk_info(long dev, long out_ptr, long unused1, long unused2, long unused3, long unused4);
long sys_disk_read(long dev, long lba, long count, long buf, long unused1, long unused2);
long sys_disk_write(long dev, long lba, long count, long buf, long unused1, long unused2);

/* SPM Syscall Implementations */
long sys_spm_user_add(long uid, long name_ptr, long unused1, long unused2, long unused3, long unused4);
long sys_spm_user_enum(long index, long out_ptr, long unused1, long unused2, long unused3, long unused4);
long sys_spm_cap_grant(long uid, long perm, long target_ptr, long unused1, long unused2, long unused3);
long sys_spm_cap_enum(long index, long out_ptr, long unused1, long unused2, long unused3, long unused4);
long sys_spm_check(long uid, long perm, long target_ptr, long unused1, long unused2, long unused3);
long sys_spm_user_passwd(long uid, long password_ptr, long unused1, long unused2, long unused3, long unused4);
long sys_spm_auth(long uid, long password_ptr, long unused1, long unused2, long unused3, long unused4);

#endif /* GRACEOS_SYSCALL_H */
