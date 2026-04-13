// ============================
// GraceOS Userland Interface
// ============================

#ifndef GRACE_H
#define GRACE_H

/* ============================
   Type Definitions
   ============================ */

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

typedef uint64_t size_t;
typedef int64_t  ssize_t;

#define NULL ((void*)0)

/* ============================
   Syscall Numbers
   Keep in sync with kernel!
   ============================ */

#define SYS_WRITE   1
#define SYS_READ    2
#define SYS_EXIT    3
#define SYS_EXEC    4
#define SYS_ALLOC   5
#define SYS_FREE    6
#define SYS_OPEN    7
#define SYS_CLOSE   8
#define SYS_LIST    9
#define SYS_GETKEY  10
#define SYS_HASKEY  11
#define SYS_SYSINFO 20

/* SASY Segment Syscalls */
#define SYS_SEG_CREATE  30
#define SYS_SEG_LOCK    31
#define SYS_SEG_UNLOCK  32
#define SYS_SEG_FREE    33
#define SYS_SEG_RESIZE  34
#define SYS_SEG_INFO    35

/* Time Management Syscalls */
#define SYS_TIME           25
#define SYS_TIME_MS        26
#define SYS_CLOCK_GETTIME  27
#define SYS_GETTIMEOFDAY   28
#define SYS_SETTIMEOFDAY   29

/* Process Management Syscalls */
#define SYS_FORK    40
#define SYS_WAIT    41
#define SYS_KILL    42
#define SYS_GETPID  43
#define SYS_GETPPID 44
#define SYS_SETSID  45
#define SYS_YIELD   46
#define SYS_SLEEP   47
#define SYS_GETUID  48
#define SYS_SETUID  49

/* Power Management Syscalls */
#define SYS_SHUTDOWN 50
#define SYS_REBOOT   51

/* Framebuffer Syscalls */
#define SYS_FB_GETINFO 60
#define SYS_FB_MAP     61
#define SYS_FB_UNMAP   62
#define SYS_FB_CLEAR   63
#define SYS_FB_PIXEL   64
#define SYS_FB_RECT    65
#define SYS_FB_PRESENT 66
#define SYS_FB_CIRCLE  67
#define SYS_FB_LINE    68
#define SYS_FB_BLIT    69

/* SPM Syscalls */
#define SYS_SPM_USER_ADD    90
#define SYS_SPM_USER_ENUM   91
#define SYS_SPM_CAP_GRANT   92
#define SYS_SPM_CAP_ENUM    93
#define SYS_SPM_CHECK       94
#define SYS_SPM_USER_PASSWD 95
#define SYS_SPM_AUTH        96

/* ============================
   Segment Types
   ============================ */

typedef enum {
    SEG_FIXED,          /* Fixed segment - never moved or swapped */
    SEG_MANUAL,         /* Manually managed segment */
    SEG_CODE,           /* Shared code segment */
    SEG_DATA_AUTO,      /* Automatic data segment */
    SEG_DATA_INST,      /* Instance data segment */
    SEG_PHYSICAL        /* Physical memory mapping */
} seg_type_t;

/* Segment handle */
typedef uint32_t seg_handle_t;
#define INVALID_HANDLE ((seg_handle_t)-1)

/* Segment flags */
#define SEG_FLAG_NONE       0x00000000
#define SEG_FLAG_READ       0x00000001
#define SEG_FLAG_WRITE      0x00000002
#define SEG_FLAG_EXEC       0x00000004
#define SEG_FLAG_USER       0x00000008
#define SEG_FLAG_DISCARD    0x00000020
#define SEG_FLAG_ZEROED     0x00000080

/* ============================
   File Descriptors
   ============================ */

#define STDIN  0
#define STDOUT 1
#define STDERR 2

/* ============================
   Low-level Syscall Interface
   (from syscall.asm)
   ============================ */

extern long __syscall0(long num);
extern long __syscall1(long num, long a1);
extern long __syscall2(long num, long a1, long a2);
extern long __syscall3(long num, long a1, long a2, long a3);
extern long __syscall4(long num, long a1, long a2, long a3, long a4);
extern long __syscall5(long num, long a1, long a2, long a3, long a4, long a5);

/* ============================
   High-level Syscall Wrappers
   ============================ */

/* I/O */
ssize_t write(int fd, const char* buf, size_t len);
ssize_t read(int fd, char* buf, size_t len);
int getkey(void);
int haskey(void);

/* Process */
void exit(int code);
int exec(const char* path);

/* Process Management */
typedef uint16_t pid_t;
typedef uint16_t uid_t;
typedef uint16_t gid_t;

pid_t fork(void);
pid_t waitpid(pid_t pid, int* status, int options);
pid_t wait(int* status);
int kill(pid_t pid, int signal);
pid_t getpid(void);
pid_t getppid(void);
int setsid(void);
void yield(void);
int sleep_ms(int ms);
uid_t getuid(void);
int setuid(uid_t uid);

/* Wait options */
#define WNOHANG 0x01

/* Signals */
#define SIGKILL 9
#define SIGTERM 15
#define SIGSTOP 19
#define SIGCONT 18

/* Memory */
void* malloc(size_t size);
void free(void* ptr);

/* Files */
int open(const char* path, int flags);
int close(int fd);
ssize_t list(const char* path, char* buf, size_t buflen);

/* ============================
   Convenience Functions
   ============================ */

/* Print string to stdout */
int print(const char* str);

/* Print string with newline */
int println(const char* str);

/* Read line from stdin */
ssize_t readline(char* buf, size_t maxlen);

/* ============================
   Segment API
   ============================ */

/* Allocate a segment */
seg_handle_t seg_alloc(size_t size);

/* Allocate with type and flags */
seg_handle_t seg_alloc_ex(size_t size, seg_type_t type, uint32_t flags);

/* Lock segment (get address) */
void* seg_lock(seg_handle_t h);

/* Unlock segment */
void seg_unlock(seg_handle_t h);

/* Free segment */
void seg_free(seg_handle_t h);

/* Resize segment */
int seg_resize(seg_handle_t h, size_t new_size);

/* ============================
   Power Management
   ============================ */

/* Shutdown the system */
void grace_shutdown(int flags);

/* Reboot the system */
void grace_reboot(int flags);

/* ============================
   Time API
   ============================ */

/* Get current time in milliseconds (monotonic) */
uint64_t time_ms(void);

/* ============================
   Framebuffer API
   ============================ */

/* Framebuffer info structure */
struct fb_info {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
};

/* Get framebuffer info. Returns 0 on success. */
int fb_get_info(struct fb_info* info);

/* Map framebuffer into user memory. Returns physical address or 0 on failure. */
uint64_t fb_map(void);

/* Unmap framebuffer */
void fb_unmap(uint64_t addr);

/* Present framebuffer to screen */
void grace_fb_present(void);

/* Userland framebuffer functions (draw via syscalls) */
void grace_fb_clear(uint32_t color);
void grace_fb_put_pixel(int x, int y, uint32_t color);
void grace_fb_fill_rect(int x, int y, int w, int h, uint32_t color);
void grace_fb_circle(int x, int y, int radius, uint32_t color);
void grace_fb_line(int x1, int y1, int x2, int y2, uint32_t color);
void grace_fb_blit(uint32_t* src, int x, int y, int w, int h);

#endif /* GRACE_H */
