// ============================
// GraceOS System Information
// ============================

#include "sysinfo.h"
#include "../mm/kheap.h"
#include "multiboot.h"
#include "../../drivers/video/tty.h"
#include "../../lib/libc/string.h"
#include "../include/time.h"

/* Boot time in milliseconds (captured at init) — x86_64 only */
#ifndef ARCH_ARM64
static uint64_t boot_time_ms = 0;
#endif


#ifndef ARCH_ARM64
/* CPUID helper (x86_64 only) */
static inline void cpuid(uint32_t code, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d)
{
    __asm__ volatile ("cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(code)
    );
}

static void get_cpu_vendor(char* vendor)
{
    uint32_t eax, ebx, ecx, edx;
    cpuid(0, &eax, &ebx, &ecx, &edx);
    memcpy(vendor + 0, &ebx, 4);
    memcpy(vendor + 4, &edx, 4);
    memcpy(vendor + 8, &ecx, 4);
    vendor[12] = '\0';
}

static void get_cpu_brand(char* brand)
{
    uint32_t eax, ebx, ecx, edx;
    cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x80000004)
    {
        cpuid(0x80000002, &eax, &ebx, &ecx, &edx);
        memcpy(brand +  0, &eax, 4); memcpy(brand +  4, &ebx, 4);
        memcpy(brand +  8, &ecx, 4); memcpy(brand + 12, &edx, 4);
        cpuid(0x80000003, &eax, &ebx, &ecx, &edx);
        memcpy(brand + 16, &eax, 4); memcpy(brand + 20, &ebx, 4);
        memcpy(brand + 24, &ecx, 4); memcpy(brand + 28, &edx, 4);
        cpuid(0x80000004, &eax, &ebx, &ecx, &edx);
        memcpy(brand + 32, &eax, 4); memcpy(brand + 36, &ebx, 4);
        memcpy(brand + 40, &ecx, 4); memcpy(brand + 44, &edx, 4);
        brand[48] = '\0';
    }
    else
    {
        strcpy(brand, "Unknown CPU");
    }
}
#endif /* !ARCH_ARM64 */


/* Initialize system info */
void sysinfo_init(void)
{
#ifndef ARCH_ARM64
    boot_time_ms = timer_get_ms();
#endif
}


/* Get CPU information */
void sysinfo_get_cpu(struct cpu_info* info)
{
#ifdef ARCH_ARM64
    /* ARM64: read MIDR_EL1 (valid at EL1 — our runtime level).
       Bits [31:24] Implementer | [23:20] Variant | [19:16] Architecture
            [15:4]  PartNum     | [3:0]   Revision                        */
    uint64_t midr = 0;
    __asm__ volatile ("mrs %0, midr_el1" : "=r"(midr));

    /* Implementer [31:24] → vendor string */
    uint32_t implementer = (uint32_t)(midr >> 24) & 0xFF;
    switch (implementer) {
        case 0x41: strcpy(info->vendor, "ARM Ltd");   break;
        case 0x42: strcpy(info->vendor, "Broadcom");  break;
        case 0x43: strcpy(info->vendor, "Cavium");    break;
        case 0x44: strcpy(info->vendor, "DEC");       break;
        case 0x4E: strcpy(info->vendor, "NVIDIA");    break;
        case 0x51: strcpy(info->vendor, "Qualcomm");  break;
        case 0x56: strcpy(info->vendor, "Marvell");   break;
        default:   strcpy(info->vendor, "Unknown");   break;
    }

    /* PartNum [15:4] → exact CPU core name */
    uint32_t part = (uint32_t)(midr >> 4) & 0xFFF;
    switch (part) {
        case 0xD03: strcpy(info->brand, "Cortex-A53"); break; /* BCM2837 — Pi 3 */
        case 0xD07: strcpy(info->brand, "Cortex-A57"); break;
        case 0xD08: strcpy(info->brand, "Cortex-A72"); break; /* BCM2711 — Pi 4 */
        case 0xD09: strcpy(info->brand, "Cortex-A73"); break;
        case 0xD0A: strcpy(info->brand, "Cortex-A75"); break;
        case 0xD0B: strcpy(info->brand, "Cortex-A76"); break;
        case 0xD0C: strcpy(info->brand, "Neoverse-N1"); break;
        default:    strcpy(info->brand, "Unknown ARM CPU"); break;
    }

    info->family   = (uint32_t)(midr >> 16) & 0xF;  /* Architecture field */
    info->model    = part;
    info->stepping = (uint32_t)(midr      ) & 0xF;   /* Revision */

    /* No x86-specific feature flags on AArch64 */
    info->has_sse  = 0;
    info->has_sse2 = 0;
    info->has_avx  = 0;
    info->has_apic = 0;
    info->has_msr  = 0;
#else
    uint32_t eax, ebx, ecx, edx;

    get_cpu_vendor(info->vendor);
    get_cpu_brand(info->brand);

    cpuid(1, &eax, &ebx, &ecx, &edx);

    info->stepping = eax & 0xF;
    info->model    = (eax >> 4) & 0xF;
    info->family   = (eax >> 8) & 0xF;

    if (info->family == 0xF)
        info->family += (eax >> 20) & 0xFF;
    if (info->family == 0x6 || info->family == 0xF)
        info->model += ((eax >> 16) & 0xF) << 4;

    info->has_apic = (edx >> 9)  & 1;
    info->has_msr  = (edx >> 5)  & 1;
    info->has_sse  = (edx >> 25) & 1;
    info->has_sse2 = (edx >> 26) & 1;
    info->has_avx  = (ecx >> 28) & 1;
#endif
}


/* Get memory information (simplified) */
void sysinfo_get_memory(struct memory_info* info)
{
#ifdef ARCH_ARM64
    /* Multiboot is not present on ARM64.  Derive total from RPI_MODEL.
       Pi 3 ships with 1 GiB; Pi 4 ships with 1/2/4/8 GiB — 4 GiB is
       the most common configuration.  A mailbox query would be exact. */
#if RPI_MODEL == 4
    uint64_t total = 4ULL * 1024 * 1024 * 1024;
#else
    uint64_t total = 1ULL * 1024 * 1024 * 1024;
#endif
    uint64_t used = kheap_get_used();
    uint64_t free = kheap_get_free();

    info->total_kb = total / 1024;
    info->used_kb = used / 1024;
    info->free_kb = free / 1024;
    return;
#else
    // Get memory info from kernel heap
    uint64_t total = multiboot_get_total_memory();
    uint64_t used = kheap_get_used();
    uint64_t free = kheap_get_free();
    
    info->total_kb = total / 1024;
    info->used_kb = used / 1024;
    info->free_kb = free / 1024;
#endif
}


/* Get system information */
void sysinfo_get_system(struct system_info* info)
{
    info->os_name = "GraceOS";
    info->os_version = "0.6.1";
#ifdef ARCH_ARM64
    info->kernel_version = "Lumina Kernel 0.3.5 (ARM64/RPi — WIP)";
    info->architecture   = "aarch64";
    info->uptime_seconds = 0;    /* No timer in initial ARM64 port */
#else
    info->kernel_version = "Lumina Kernel 0.3.5";
    info->architecture   = "x86_64";
    uint64_t current_ms  = timer_get_ms();
    uint64_t uptime_ms   = (current_ms >= boot_time_ms) ? (current_ms - boot_time_ms) : 0;
    info->uptime_seconds = uptime_ms / 1000;
#endif
}


/* Print number with padding */
static void print_number(uint64_t num)
{
    char buffer[32];
    int i = 0;
    
    if (num == 0)
    {
        tty_putchar('0');
        return;
    }
    
    while (num > 0)
    {
        buffer[i++] = '0' + (num % 10);
        num /= 10;
    }
    
    while (i > 0)
    {
        tty_putchar(buffer[--i]);
    }
}


/* Trim leading whitespace from string */
static const char* trim_left(const char* str)
{
    while (*str == ' ' || *str == '\t')
        str++;
    return str;
}


/* Display system information (neofetch style) */
void sysinfo_display(void)
{
#ifdef ARCH_ARM64
    struct cpu_info cpu;
    struct memory_info mem;
    struct system_info sys;

    sysinfo_get_cpu(&cpu);
    sysinfo_get_memory(&mem);
    sysinfo_get_system(&sys);

    tty_print("System Information\n");
    tty_print("------------------\n");
    tty_print("OS:        ");
    tty_print(sys.os_name);
    tty_print(" ");
    tty_print(sys.os_version);
    tty_print("\n");
    tty_print("Kernel:    ");
    tty_print(sys.kernel_version);
    tty_print("\n");
    tty_print("Arch:      ");
    tty_print(sys.architecture);
    tty_print("\n");
    tty_print("CPU:       ");
    tty_print(trim_left(cpu.brand));
    tty_print("\n");
    tty_print("Vendor:    ");
    tty_print(cpu.vendor);
    tty_print("\n");
    tty_print("Memory:    ");
    print_number(mem.used_kb / 1024);
    tty_print(" MB / ");
    print_number(mem.total_kb / 1024);
    tty_print(" MB\n");
    return;
#else
    struct cpu_info cpu;
    struct memory_info mem;
    struct system_info sys;
    
    sysinfo_get_cpu(&cpu);
    sysinfo_get_memory(&mem);
    sysinfo_get_system(&sys);
    
    // ASCII art logo
    tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
    tty_print("\n");
    tty_print("    _____                     ____  _____ \n");
    tty_print("   / ____|                   / __ \\/ ____|\n");
    tty_print("  | |  __ _ __ __ _  ___ ___| |  | | (___\n");
    tty_print("  | | |_ | '__/ _` |/ __/ _ \\ |  | |\\___ \\\n");
    tty_print("  | |__| | | | (_| | (_|  __/ |__| |____) |\n");
    tty_print("   \\_____|_|  \\__,_|\\___\\___|\\____/|_____/\n");
    tty_print("\n");
    
    // System information
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    
    // OS
    tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
    tty_print("  OS:          ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    tty_print(sys.os_name);
    tty_print(" ");
    tty_print(sys.os_version);
    tty_newline();
    
    // Kernel
    tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
    tty_print("  Kernel:      ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    tty_print(sys.kernel_version);
    tty_newline();
    
    // Architecture
    tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
    tty_print("  Arch:        ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    tty_print(sys.architecture);
    tty_newline();
    
    // CPU Vendor
    tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
    tty_print("  CPU Vendor:  ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    tty_print(cpu.vendor);
    tty_newline();
    
    // CPU Brand
    tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
    tty_print("  CPU:         ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    tty_print(trim_left(cpu.brand));
    tty_newline();
    
    // CPU Features
    tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
    tty_print("  CPU Flags:   ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    if (cpu.has_sse) tty_print("SSE ");
    if (cpu.has_sse2) tty_print("SSE2 ");
    if (cpu.has_avx) tty_print("AVX ");
    if (cpu.has_apic) tty_print("APIC ");
    if (cpu.has_msr) tty_print("MSR");
    tty_newline();
    
    // Memory
    tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
    tty_print("  Memory:      ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    print_number(mem.used_kb / 1024);
    tty_print(" MB / ");
    print_number(mem.total_kb / 1024);
    tty_print(" MB");
    tty_newline();
    
    // Color palette (like neofetch)
    tty_print("\n  ");
    tty_set_color(TTY_BLACK, TTY_BLACK);
    tty_print("   ");
    tty_set_color(TTY_RED, TTY_BLACK);
    tty_print("   ");
    tty_set_color(TTY_GREEN, TTY_BLACK);
    tty_print("   ");
    tty_set_color(TTY_BROWN, TTY_BLACK);
    tty_print("   ");
    tty_set_color(TTY_BLUE, TTY_BLACK);
    tty_print("   ");
    tty_set_color(TTY_MAGENTA, TTY_BLACK);
    tty_print("   ");
    tty_set_color(TTY_CYAN, TTY_BLACK);
    tty_print("   ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    tty_print("   ");
    tty_newline();
    
    tty_print("  ");
    tty_set_color(TTY_DARK_GREY, TTY_BLACK);
    tty_print("   ");
    tty_set_color(TTY_LIGHT_RED, TTY_BLACK);
    tty_print("   ");
    tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
    tty_print("   ");
    tty_set_color(TTY_YELLOW, TTY_BLACK);
    tty_print("   ");
    tty_set_color(TTY_LIGHT_BLUE, TTY_BLACK);
    tty_print("   ");
    tty_set_color(TTY_LIGHT_MAGENTA, TTY_BLACK);
    tty_print("   ");
    tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
    tty_print("   ");
    tty_set_color(TTY_WHITE, TTY_BLACK);
    tty_print("   ");
    
    // Reset colors
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    tty_print("\n\n");
#endif
}
