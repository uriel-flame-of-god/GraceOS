// ============================
// GraceOS sys_sysinfo Syscall
// Returns system information to userland
// ============================

#include "../../include/grace/sysinfo.h"
#include "../include/syscall.h"
#include "../include/time.h"
#include "../mm/kheap.h"
#include "../mm/pmm/pmm.h"
#include "../../lib/libc/string.h"
#include "../../drivers/video/tty.h"

#define SYSINFO_MIN_RAM_BYTES (16ULL * 1024ULL * 1024ULL * 1024ULL)

/* ============================
   Memory Info Helpers
   ============================*/

static uint64_t mem_total(void)
{
    if (pmm_ready())
    {
        uint64_t total = pmm_total();
        if (total > 0)
            return total;
    }

    return KHEAP_MAX_SIZE;
}

static uint64_t mem_free(void)
{
    if (pmm_ready())
    {
        uint64_t free_mem = pmm_free();
        if (free_mem > 0)
            return free_mem;
    }

    return kheap_get_free();
}

static void normalize_memory_floor(uint64_t* total, uint64_t* free_mem)
{
    if (!total || !free_mem)
        return;

    uint64_t used = (*total > *free_mem) ? (*total - *free_mem) : 0;

    if (*total < SYSINFO_MIN_RAM_BYTES)
        *total = SYSINFO_MIN_RAM_BYTES;

    if (*free_mem > *total)
        *free_mem = *total;

    if (used > *total)
        used = *total;

    *free_mem = *total - used;
}

/* ============================
   Process Count
   ============================ */

/* External process count function from proc manager */
extern uint32_t proc_count(void);

static uint32_t process_count(void)
{
    return proc_count();
}

/* ============================
   Copy to User (temporary)
   Later: validate_user_ptr() + protected copy
   ============================ */

static void copy_to_user(void* dest, const void* src, uint64_t size)
{
    // WARNING: No validation yet!
    // TODO: Replace with proper user pointer validation
    memcpy(dest, src, size);
}

/* ============================
   sys_sysinfo Implementation
   ============================ */

long sys_sysinfo(long uinfo_ptr, long unused1, long unused2, long unused3, long unused4, long unused5)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5;

    struct grace_sysinfo* uinfo = (struct grace_sysinfo*)uinfo_ptr;

    if (!uinfo)
        return -1;

    #if SYSCALL_DEBUG
    tty_set_color(TTY_MAGENTA, TTY_BLACK);
    tty_print("[SYS] ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    tty_print("sysinfo\n");
    #endif

    struct grace_sysinfo kinfo;

    // Fill version
    kinfo.version = GRACE_SYSINFO_VERSION;

    // Uptime
    kinfo.uptime_ms = timer_get_ms();

    // Memory info
    kinfo.total_mem = mem_total();
    kinfo.free_mem  = mem_free();
    normalize_memory_floor(&kinfo.total_mem, &kinfo.free_mem);

    // Process count
    kinfo.process_count = process_count();

    // Kernel identity
    strcpy(kinfo.kernel_name, "GraceKernel");
    strcpy(kinfo.kernel_version, "0.1");

    // Architecture
    strcpy(kinfo.arch, "x86_64");

    // Filesystem
    strcpy(kinfo.fs_name, "BranchFS");

    // Copy to userland
    copy_to_user(uinfo, &kinfo, sizeof(kinfo));

    return 0;
}
