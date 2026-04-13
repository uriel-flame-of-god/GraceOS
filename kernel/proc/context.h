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
   Callee-saved registers + stack/instruction pointers.
   ============================ */

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

/* Assembly routine: switch to user mode */
extern void ctx_switch_to_user(uint64_t rip, uint64_t rsp, uint64_t rflags);

/* Initialize a new context for kernel thread */
void ctx_init_kernel(struct cpu_context* ctx, void (*entry)(void), void* stack_top);

/* Initialize a new context for user thread */
void ctx_init_user(struct cpu_context* ctx, uint64_t entry, uint64_t user_stack, uint64_t pml4);

/* Save FPU state */
void ctx_save_fpu(struct fpu_context* fpu);

/* Restore FPU state */
void ctx_restore_fpu(struct fpu_context* fpu);

#endif /* GRACEOS_PROC_CONTEXT_H */
