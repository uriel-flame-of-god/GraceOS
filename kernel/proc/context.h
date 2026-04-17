// ============================
// GraceOS Process Context
// CPU state for context switching
// ============================

#ifndef GRACEOS_PROC_CONTEXT_H
#define GRACEOS_PROC_CONTEXT_H

#include "../../lib/libc/int.h"

/* ============================
   CPU Context Structure

   Saved/restored during context switch.
   Layout differs per architecture.
   ============================ */

#ifdef ARCH_ARM64
/* AArch64: callee-saved registers + sp + lr (as instruction pointer).
   Offsets must match kernel/arch/arm64/context.S exactly:
     x19=+0x00, x20=+0x08, x21=+0x10, x22=+0x18, x23=+0x20,
     x24=+0x28, x25=+0x30, x26=+0x38, x27=+0x40, x28=+0x48,
     fp(x29)=+0x50, lr(x30)=+0x58, sp=+0x60
*/
struct cpu_context {
    uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
    uint64_t fp;    /* x29 frame pointer */
    uint64_t lr;    /* x30 link register — used as instruction pointer */
    uint64_t sp;    /* stack pointer */
};
#else
/* x86_64: callee-saved registers + rip + rsp + rflags */
struct cpu_context {
    /* Instruction pointer */
    uint64_t rip;

    /* Stack pointer */
    uint64_t rsp;

    /* Flags register */
    uint64_t rflags;

    /* Callee-saved registers (System V AMD64 ABI) */
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    /* Segment selectors (for user/kernel transitions) */
    uint64_t cs;
    uint64_t ss;

    /* CR3 - Page table base (stored for fast access) */
    uint64_t cr3;
};
#endif

/* ============================
   FPU/SSE Context (optional)
   ============================ */

struct fpu_context {
    uint8_t fxsave_area[512] __attribute__((aligned(16)));
    int used;  /* 1 if process has used FPU */
};

/* ============================
   Full Thread Context
   ============================ */

typedef struct thread_context {
    struct cpu_context cpu;
    struct fpu_context fpu;
} thread_context_t;

/* ============================
   Context Switch API
   ============================ */

/* Assembly routine: switch from old to new context */
extern void ctx_switch(struct cpu_context* old_ctx, struct cpu_context* new_ctx);

/* Initialize a new context for kernel thread */
void ctx_init_kernel(struct cpu_context* ctx, void (*entry)(void), void* stack_top);

#ifndef ARCH_ARM64
/* x86_64-only: drop to user mode / save/restore FPU */
extern void ctx_switch_to_user(uint64_t rip, uint64_t rsp, uint64_t rflags);
void ctx_init_user(struct cpu_context* ctx, uint64_t entry, uint64_t user_stack, uint64_t pml4);
void ctx_save_fpu(struct fpu_context* fpu);
void ctx_restore_fpu(struct fpu_context* fpu);
#endif

#endif /* GRACEOS_PROC_CONTEXT_H */
