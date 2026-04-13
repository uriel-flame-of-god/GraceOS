#include "../../drivers/video/tty.h"
#include "../../drivers/input/keyboard.h"
#include "../../drivers/storage/bfs.h"
#include "../../drivers/storage/rtc.h"
#include "../arch/x86_64/idt.h"
#include "../include/syscall.h"
#include "../include/time.h"
#include "../../userland/shell/shell.h"
#include "sysinfo.h"
#include "multiboot.h"
#include "../mm/kheap.h"
#include "../mm/pmm/pmm.h"
#include "../mm/vmm/vmm.h"
#include "../mm/sasy/sasy.h"
#include "../mm/sasy/swap.h"
#include "../proc/proc.h"
#include "../proc/sched.h"
#include "../proc/pipe.h"
#include "../proc/minit/minit.h"
#include "../spm/spm.h"
#include "../log/klog.h"
#include "../../lib/libc/string.h"
#include "../../drivers/storage/pmem.h"
#include "../net/net.h"

/* Kernel end symbol from linker */
extern char end;

/* ============================
   Shell entry wrapper for minit
   minit_spawn calls entry_fn() — this bridges to the shell subsystem.
   ============================ */

static void shell_entry(void)
{
    shell_init();
    shell_run();
}

static void enable_sse(void)
{
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));

    if ((edx & (1U << 25)) == 0)
        return;

    uint64_t cr0;
    uint64_t cr4;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));

    cr0 &= ~(1ULL << 2); // Clear EM
    cr0 |= (1ULL << 1);  // Set MP
    cr4 |= (1ULL << 9);  // OSFXSR
    cr4 |= (1ULL << 10); // OSXMMEXCPT

    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4));
}

/* Fancy console logging macros */
#define log_init(msg) \
    do { \
        tty_set_color(TTY_CYAN, TTY_BLACK); \
        tty_print("[INIT]    "); \
        tty_set_color(TTY_LIGHT_GREY, TTY_BLACK); \
        tty_print(msg); \
        tty_print("\n"); \
    } while(0)

#define log_success(msg) \
    do { \
        tty_set_color(TTY_GREEN, TTY_BLACK); \
        tty_print("[SUCCESS] "); \
        tty_set_color(TTY_LIGHT_GREY, TTY_BLACK); \
        tty_print(msg); \
        tty_print("\n"); \
    } while(0)

#define log_error(msg) \
    do { \
        tty_set_color(TTY_RED, TTY_BLACK); \
        tty_print("[ERROR]   "); \
        tty_set_color(TTY_LIGHT_GREY, TTY_BLACK); \
        tty_print(msg); \
        tty_print("\n"); \
    } while(0)

#define log_warn(msg) \
    do { \
        tty_set_color(TTY_BROWN, TTY_BLACK); \
        tty_print("[WARN]    "); \
        tty_set_color(TTY_LIGHT_GREY, TTY_BLACK); \
        tty_print(msg); \
        tty_print("\n"); \
    } while(0)

#define log_debug(msg) \
    do { \
        tty_set_color(TTY_LIGHT_GREY, TTY_BLACK); \
        tty_print("[DEBUG]   "); \
        tty_print(msg); \
        tty_print("\n"); \
    } while(0)

#define log_get(msg) \
    do { \
        tty_set_color(TTY_MAGENTA, TTY_BLACK); \
        tty_print("[GET]     "); \
        tty_set_color(TTY_LIGHT_GREY, TTY_BLACK); \
        tty_print(msg); \
    } while(0)

#define log_set(msg) \
    do { \
        tty_set_color(TTY_MAGENTA, TTY_BLACK); \
        tty_print("[SET]     "); \
        tty_set_color(TTY_LIGHT_GREY, TTY_BLACK); \
        tty_print(msg); \
        tty_print("\n"); \
    } while(0)


/* Global BFS instance */
struct bfs_instance g_bfs;

/* Static fallback filesystem buffer (64KB - fits in BSS, no kmalloc needed)
 * Used when PMEM is not available. Sufficient for basic shell operations. */
#define FALLBACK_FS_SIZE (64 * 1024)
static uint8_t fallback_fs_buffer[FALLBACK_FS_SIZE] __attribute__((aligned(4096)));

void kmain(uint64_t multiboot_info)
{
    multiboot_init(multiboot_info);
    vmm_init();
    pmm_init((void*)multiboot_info);
    kheap_init();
    tty_init();
    klog_init();

    // Re-initialize VMM now that PMM is online (full page-table setup).
    vmm_init();
    klog_log("VMM ready");
    tty_clear();

    // Display boot banner
    tty_set_color(TTY_WHITE, TTY_BLUE);
    tty_print("                                                                                ");
    tty_print("                         Lumina Kernel v0.0.3-dev                                  ");
    tty_print("                                                                                ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    tty_print("\n\n");

    klog_init_msg("Boot sequence started");
    klog_init_msg("PMM already initialized");
    klog_init_msg("Heap already initialized");
    
    // Initialize keyboard
    klog_init_msg("Initializing keyboard");
    keyboard_init();
    klog_log("Keyboard initialized");

    enable_sse();

    // Initialize RTC and system time
    klog_init_msg("Initializing RTC and system time");
    rtc_init();
    klog_log("RTC and time initialized");

    // Initialize IDT (Interrupts)
    klog_init_msg("Setting up IDT");
    idt_init();
    klog_log("IDT configured");

    // Check PMM status
    if (pmm_ready())
        klog_log("Memory subsystem stable");
    else
        klog_warn("Memory subsystem degraded");

    // Initialize system info
    sysinfo_init();
    
    // Initialize persistent memory backend
    klog_init_msg("Initializing persistent memory");
    pmem_init();

    // Initialize BranchFS (persistent memory preferred)
    klog_init_msg("Initializing BranchFS");

    uint64_t fs_size = 0x100000;  // 1MB fallback
    void* fs_base = 0;
    uint64_t swap_size = 0;

    if (pmem_ready())
    {
        uint64_t pmem_bytes = pmem_size();
        uint64_t max_fs = 64ULL * 1024 * 1024 * 1024;

        fs_size = pmem_bytes / 2;
        if (fs_size > max_fs)
            fs_size = max_fs;
        fs_size &= ~(BFS_PAGE_SIZE - 1);

        if (fs_size < (2 * BFS_PAGE_SIZE))
            fs_size = 0;

        if (pmem_bytes > fs_size)
            swap_size = pmem_bytes - fs_size;

        swap_set_region(fs_size, swap_size);
    }
    else
    {
        swap_set_region(0, 0);
    }

    if (!pmem_ready() || fs_size == 0)
    {
        // Use static fallback buffer - no kmalloc for large buffers
        fs_size = FALLBACK_FS_SIZE;
        fs_base = fallback_fs_buffer;
        klog_warn("Using static 64KB fallback filesystem");
    }

    int bfs_result = bfs_init(&g_bfs, fs_size, fs_base);
    
    if (bfs_result != 0)
    {
        // Not formatted, format it
        bfs_format(&g_bfs);
        klog_log("BranchFS formatted");
    }
    else
    {
        klog_log("BranchFS mounted");
    }
    
    // Initialize SASY (Segment Allocator System)
    klog_init_msg("Initializing SASY segment allocator");
    sasy_init();
    klog_logn("SASY ready");
    
    // Initialize security subsystem (must precede proc)
    klog_init_msg("Initializing spm security subsystem");
    spm_init();
    klog_logn("spm ready");

    // Initialize Process Manager
    klog_init_msg("Initializing process manager");
    proc_init();
    exec_init();
    sched_init();
    pipe_init();
    klog_logn("Process manager ready");

    // Initialize process orchestration layer (after proc)
    klog_init_msg("Initializing minit orchestration layer");
    minit_init();
    klog_logn("minit ready");
    
    klog_logn("Boot complete");
    tty_print("\n");

    // Initialize syscall system
    klog_init_msg("Initializing syscall interface");
    syscall_init();
    klog_logn("Syscall interface ready (INT 0x80)");

    // Initialize network stack
    klog_init_msg("Initializing network stack");
    net_init();

    // Register and spawn the keyboard service through minit
    klog_init_msg("Registering keyboard service with minit");
    minit_node_t* kb_node = minit_register("keyboard", minit_root);
    if (kb_node)
    {
        kb_node->entry_fn = keyboard_service_run;
        kb_node->auto_restart = true;
        kb_node->base_priority = SCHED_DEFAULT_PRIORITY;
        minit_spawn(kb_node);
    }
    else
    {
        klog_warn("minit: failed to register keyboard node");
    }

    // Register and spawn the shell through minit (correct architecture)
    klog_init_msg("Registering shell service with minit");
    tty_print("\n");

    minit_node_t* shell_node = minit_register("shell", minit_root);
    if (shell_node)
    {
        shell_node->entry_fn    = shell_entry;
        shell_node->auto_restart = false;   /* No restart on exit for now */
        shell_node->base_priority = SCHED_DEFAULT_PRIORITY;
        minit_spawn(shell_node);
    }
    else
    {
        klog_warn("minit: failed to register shell node");
    }

    /* Enter scheduler/idle context; interrupts enable via idle rflags. */
    __asm__ volatile ("cli");
    current = sched_get_idle();
    ctx_switch(NULL, &current->ctx);

    // Should never reach here
    for (;;)
        __asm__ volatile ("hlt");
}
