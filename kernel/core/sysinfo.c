// ============================
// GraceOS System Information
// ============================

#include "sysinfo.h"
#include "../mm/kheap.h"
#include "multiboot.h"
#include "../../drivers/video/tty.h"
#include "../../lib/libc/string.h"

/* Global uptime counter (updated by timer) */
static uint64_t system_uptime = 0;


/* CPUID helper function */
static inline void cpuid(uint32_t code, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d)
{
    __asm__ volatile ("cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(code)
    );
}


/* Get CPU vendor string */
static void get_cpu_vendor(char* vendor)
{
    uint32_t eax, ebx, ecx, edx;
    cpuid(0, &eax, &ebx, &ecx, &edx);
    
    // Vendor string is stored in EBX, EDX, ECX (in that order)
    memcpy(vendor + 0, &ebx, 4);
    memcpy(vendor + 4, &edx, 4);
    memcpy(vendor + 8, &ecx, 4);
    vendor[12] = '\0';
}


/* Get CPU brand string */
static void get_cpu_brand(char* brand)
{
    uint32_t eax, ebx, ecx, edx;
    
    // Check if extended CPUID is available
    cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
    
    if (eax >= 0x80000004)
    {
        // Brand string is in 0x80000002-0x80000004
        cpuid(0x80000002, &eax, &ebx, &ecx, &edx);
        memcpy(brand + 0, &eax, 4);
        memcpy(brand + 4, &ebx, 4);
        memcpy(brand + 8, &ecx, 4);
        memcpy(brand + 12, &edx, 4);
        
        cpuid(0x80000003, &eax, &ebx, &ecx, &edx);
        memcpy(brand + 16, &eax, 4);
        memcpy(brand + 20, &ebx, 4);
        memcpy(brand + 24, &ecx, 4);
        memcpy(brand + 28, &edx, 4);
        
        cpuid(0x80000004, &eax, &ebx, &ecx, &edx);
        memcpy(brand + 32, &eax, 4);
        memcpy(brand + 36, &ebx, 4);
        memcpy(brand + 40, &ecx, 4);
        memcpy(brand + 44, &edx, 4);
        
        brand[48] = '\0';
    }
    else
    {
        strcpy(brand, "Unknown CPU");
    }
}


/* Initialize system info */
void sysinfo_init(void)
{
    system_uptime = 0;
}


/* Get CPU information */
void sysinfo_get_cpu(struct cpu_info* info)
{
    uint32_t eax, ebx, ecx, edx;
    
    // Get vendor
    get_cpu_vendor(info->vendor);
    
    // Get brand
    get_cpu_brand(info->brand);
    
    // Get family, model, stepping
    cpuid(1, &eax, &ebx, &ecx, &edx);
    
    info->stepping = eax & 0xF;
    info->model = (eax >> 4) & 0xF;
    info->family = (eax >> 8) & 0xF;
    
    // Extended model and family
    if (info->family == 0xF)
    {
        info->family += (eax >> 20) & 0xFF;
    }
    if (info->family == 0x6 || info->family == 0xF)
    {
        info->model += ((eax >> 16) & 0xF) << 4;
    }
    
    // Feature flags
    info->has_apic = (edx >> 9) & 1;
    info->has_msr = (edx >> 5) & 1;
    info->has_sse = (edx >> 25) & 1;
    info->has_sse2 = (edx >> 26) & 1;
    info->has_avx = (ecx >> 28) & 1;
}


/* Get memory information (simplified) */
void sysinfo_get_memory(struct memory_info* info)
{
    // Get memory info from kernel heap
    uint64_t total = multiboot_get_total_memory();
    uint64_t used = kheap_get_used();
    uint64_t free = kheap_get_free();
    
    info->total_kb = total / 1024;
    info->used_kb = used / 1024;
    info->free_kb = free / 1024;
}


/* Get system information */
void sysinfo_get_system(struct system_info* info)
{
    info->os_name = "GraceOS";
    info->os_version = "0.0.1";
    info->kernel_version = "Lumina Kernel 0.0.2-dev";
    info->architecture = "x86_64";
    info->uptime_seconds = system_uptime;
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


/* Trim leading spaces from string */
static const char* trim_left(const char* str)
{
    while (*str == ' ')
        str++;
    return str;
}


/* Display system information (neofetch style) */
void sysinfo_display(void)
{
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
}
