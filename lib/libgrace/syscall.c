// ============================
// GraceOS Userland Syscall Wrappers
// ============================

#include "grace.h"
#include "../../include/grace/spm_syscalls.h"

/* ============================
   I/O Syscalls
   ============================ */

ssize_t write(int fd, const char* buf, size_t len)
{
    return __syscall3(SYS_WRITE, fd, (long)buf, (long)len);
}

ssize_t read(int fd, char* buf, size_t len)
{
    return __syscall3(SYS_READ, fd, (long)buf, (long)len);
}

int getkey(void)
{
    return (int)__syscall0(SYS_GETKEY);
}

int haskey(void)
{
    return (int)__syscall0(SYS_HASKEY);
}

/* ============================
   Process Syscalls
   ============================ */

void exit(int code)
{
    __syscall1(SYS_EXIT, code);
    
    // Should never return, but loop just in case
    for (;;)
        ;
}

int exec(const char* path)
{
    return (int)__syscall1(SYS_EXEC, (long)path);
}

/* ============================
   Process Management Syscalls
   ============================ */

pid_t fork(void)
{
    return (pid_t)__syscall0(SYS_FORK);
}

pid_t waitpid(pid_t pid, int* status, int options)
{
    return (pid_t)__syscall3(SYS_WAIT, (long)pid, (long)status, (long)options);
}

pid_t wait(int* status)
{
    return waitpid((pid_t)-1, status, 0);
}

int kill(pid_t pid, int signal)
{
    return (int)__syscall2(SYS_KILL, (long)pid, (long)signal);
}

pid_t getpid(void)
{
    return (pid_t)__syscall0(SYS_GETPID);
}

pid_t getppid(void)
{
    return (pid_t)__syscall0(SYS_GETPPID);
}

int setsid(void)
{
    return (int)__syscall0(SYS_SETSID);
}

void yield(void)
{
    __syscall0(SYS_YIELD);
}

int sleep_ms(int ms)
{
    return (int)__syscall1(SYS_SLEEP, (long)ms);
}

uid_t getuid(void)
{
    return (uid_t)__syscall0(SYS_GETUID);
}

int setuid(uid_t uid)
{
    return (int)__syscall1(SYS_SETUID, (long)uid);
}

/* ============================
   SPM Syscalls
   ============================ */

int spm_user_add(uint32_t uid, const char* name)
{
    return (int)__syscall2(SYS_SPM_USER_ADD, (long)uid, (long)name);
}

int spm_user_enum(int index, spm_user_info_t* out)
{
    return (int)__syscall2(SYS_SPM_USER_ENUM, (long)index, (long)out);
}

int spm_cap_grant(uint32_t uid, uint32_t perm, const char* target)
{
    return (int)__syscall3(SYS_SPM_CAP_GRANT, (long)uid, (long)perm, (long)target);
}

int spm_cap_enum(int index, spm_cap_info_t* out)
{
    return (int)__syscall2(SYS_SPM_CAP_ENUM, (long)index, (long)out);
}

int spm_check_user(uint32_t uid, uint32_t perm, const char* target)
{
    return (int)__syscall3(SYS_SPM_CHECK, (long)uid, (long)perm, (long)target);
}

int spm_user_passwd(uint32_t uid, const char* password)
{
    return (int)__syscall2(SYS_SPM_USER_PASSWD, (long)uid, (long)password);
}

int spm_user_auth(uint32_t uid, const char* password)
{
    return (int)__syscall2(SYS_SPM_AUTH, (long)uid, (long)password);
}

/* ============================
   Memory Syscalls
   ============================ */

void* malloc(size_t size)
{
    return (void*)__syscall1(SYS_ALLOC, (long)size);
}

void free(void* ptr)
{
    __syscall1(SYS_FREE, (long)ptr);
}

/* ============================
   File Syscalls
   ============================ */

int open(const char* path, int flags)
{
    return (int)__syscall2(SYS_OPEN, (long)path, flags);
}

int close(int fd)
{
    return (int)__syscall1(SYS_CLOSE, fd);
}

ssize_t list(const char* path, char* buf, size_t buflen)
{
    return __syscall3(SYS_LIST, (long)path, (long)buf, (long)buflen);
}

/* ============================
   Convenience Functions
   ============================ */

/* Get string length (local helper) */
static size_t _strlen(const char* str)
{
    size_t len = 0;
    while (str[len])
        len++;
    return len;
}

int print(const char* str)
{
    return (int)write(STDOUT, str, _strlen(str));
}

int println(const char* str)
{
    print(str);
    return (int)write(STDOUT, "\n", 1);
}

ssize_t readline(char* buf, size_t maxlen)
{
    return read(STDIN, buf, maxlen);
}

/* ============================
   Segment API
   ============================ */

seg_handle_t seg_alloc(size_t size)
{
    return (seg_handle_t)__syscall3(
        SYS_SEG_CREATE,
        (long)size,
        SEG_DATA_AUTO,
        SEG_FLAG_READ | SEG_FLAG_WRITE | SEG_FLAG_USER | SEG_FLAG_ZEROED
    );
}

seg_handle_t seg_alloc_ex(size_t size, seg_type_t type, uint32_t flags)
{
    return (seg_handle_t)__syscall3(
        SYS_SEG_CREATE,
        (long)size,
        (long)type,
        (long)flags
    );
}

void* seg_lock(seg_handle_t h)
{
    return (void*)__syscall1(SYS_SEG_LOCK, (long)h);
}

void seg_unlock(seg_handle_t h)
{
    __syscall1(SYS_SEG_UNLOCK, (long)h);
}

void seg_free(seg_handle_t h)
{
    __syscall1(SYS_SEG_FREE, (long)h);
}

int seg_resize(seg_handle_t h, size_t new_size)
{
    return (int)__syscall2(SYS_SEG_RESIZE, (long)h, (long)new_size);
}

/* ============================
   Power Management
   ============================ */

void grace_shutdown(int flags)
{
    __syscall1(SYS_SHUTDOWN, flags);
}

void grace_reboot(int flags)
{
    __syscall1(SYS_REBOOT, flags);
}

/* ============================
   Framebuffer API
   ============================ */

int fb_get_info(struct fb_info* info)
{
    return (int)__syscall1(SYS_FB_GETINFO, (long)info);
}

uint64_t fb_map(void)
{
    return (uint64_t)__syscall0(SYS_FB_MAP);
}

void fb_unmap(uint64_t addr)
{
    __syscall1(SYS_FB_UNMAP, (long)addr);
}

void grace_fb_present(void)
{
    __syscall0(SYS_FB_PRESENT);
}

void grace_fb_clear(uint32_t color)
{
    __syscall1(SYS_FB_CLEAR, (long)color);
}

void grace_fb_put_pixel(int x, int y, uint32_t color)
{
    __syscall3(SYS_FB_PIXEL, (long)x, (long)y, (long)color);
}

void grace_fb_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    __syscall5(SYS_FB_RECT, (long)x, (long)y, (long)w, (long)h, (long)color);
}

void grace_fb_circle(int x, int y, int radius, uint32_t color)
{
    __syscall4(SYS_FB_CIRCLE, (long)x, (long)y, (long)radius, (long)color);
}

void grace_fb_line(int x1, int y1, int x2, int y2, uint32_t color)
{
    __syscall5(SYS_FB_LINE, (long)x1, (long)y1, (long)x2, (long)y2, (long)color);
}

void grace_fb_blit(uint32_t* src, int x, int y, int w, int h)
{
    __syscall5(SYS_FB_BLIT, (long)src, (long)x, (long)y, (long)w, (long)h);
}

/* ============================
   Time API
   ============================ */

uint64_t time_ms(void)
{
    return (uint64_t)__syscall0(SYS_TIME_MS);
}
