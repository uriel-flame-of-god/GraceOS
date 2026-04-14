// ============================
// GraceOS cfdisk — Input via syscalls
// ============================

#include "input.h"

#define SYS_GETKEY 10
#define SYS_HASKEY 11

static long syscall0(long num)
{
    long ret;
    __asm__ volatile (
        "mov %1, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(num)
        : "rax"
    );
    return ret;
}

int input_getkey(void)
{
    return (int)syscall0(SYS_GETKEY);
}

int input_haskey(void)
{
    return (int)syscall0(SYS_HASKEY);
}
