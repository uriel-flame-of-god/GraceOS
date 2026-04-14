// ============================
// GraceOS cfdisk — Disk I/O via syscalls
// ============================

#include "disk.h"

/* ============================
   Syscall stubs
   Must match kernel/include/syscall.h numbers.
   ============================ */

#define SYS_DISK_INFO  80
#define SYS_DISK_READ  81
#define SYS_DISK_WRITE 82

static long syscall3(long num, long a1, long a2, long a3)
{
    long ret;
    __asm__ volatile (
        "mov %1, %%rax\n"
        "mov %2, %%rdi\n"
        "mov %3, %%rsi\n"
        "mov %4, %%rdx\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(num), "r"(a1), "r"(a2), "r"(a3)
        : "rax", "rdi", "rsi", "rdx"
    );
    return ret;
}

static long syscall4(long num, long a1, long a2, long a3, long a4)
{
    long ret;
    __asm__ volatile (
        "mov %1, %%rax\n"
        "mov %2, %%rdi\n"
        "mov %3, %%rsi\n"
        "mov %4, %%rdx\n"
        "mov %5, %%r10\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(num), "r"(a1), "r"(a2), "r"(a3), "r"(a4)
        : "rax", "rdi", "rsi", "rdx", "r10"
    );
    return ret;
}

/* ============================
   API Implementation
   ============================ */

int disk_get_info(int dev, disk_info_t* info)
{
    long ret = syscall3(SYS_DISK_INFO, (long)dev, (long)info, 0);
    return (int)ret;
}

int disk_read(int dev, uint64_t lba, uint32_t count, void* buf)
{
    long ret = syscall4(SYS_DISK_READ, (long)dev, (long)lba, (long)count, (long)buf);
    return (int)ret;
}

int disk_write(int dev, uint64_t lba, uint32_t count, const void* buf)
{
    long ret = syscall4(SYS_DISK_WRITE, (long)dev, (long)lba, (long)count, (long)buf);
    return (int)ret;
}

uint64_t disk_align_lba(uint64_t lba)
{
    return (lba + ALIGNMENT_LBA - 1) & ~((uint64_t)(ALIGNMENT_LBA - 1));
}
