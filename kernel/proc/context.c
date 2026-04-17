// ============================
// GraceOS Context Helpers
// C wrapper functions
// ============================

#include "context.h"
#include "../../lib/libc/string.h"

/* ============================
   Initialize Kernel Context
   
   Sets up context for a new kernel thread.
   ============================ */

void ctx_init_kernel(struct cpu_context* ctx, void (*entry)(void), void* stack_top)
{
    if (!ctx || !entry || !stack_top)
        return;

    memset(ctx, 0, sizeof(struct cpu_context));

#ifdef ARCH_ARM64
    /* AArch64: set lr = entry, sp = stack_top (16-byte aligned).
       ctx_switch() saves/restores lr and does 'ret', jumping to entry
       the first time this context is switched into. */
    ctx->lr = (uint64_t)entry;
    ctx->sp = (uint64_t)stack_top & ~0xFULL;  /* 16-byte aligned */
#else
    /* x86_64 */
    ctx->rip    = (uint64_t)entry;
    uint64_t sp = (uint64_t)stack_top & ~0xFULL;
    sp -= 8;          /* Space for fake return address */
    ctx->rsp    = sp;
    ctx->rflags = 0x200;          /* IF set — interrupts enabled */
    ctx->cs     = 0x08;           /* Kernel code segment */
    ctx->ss     = 0x10;           /* Kernel data segment */
#endif
}

/* ============================
   Initialize User Context
   
   Sets up context for a new user process.
   ============================ */

#ifndef ARCH_ARM64
void ctx_init_user(struct cpu_context* ctx, uint64_t entry, uint64_t user_stack, uint64_t pml4)
{
    if (!ctx)
        return;

    memset(ctx, 0, sizeof(struct cpu_context));

    ctx->rip    = entry;
    ctx->rsp    = user_stack & ~0xFULL;
    ctx->rflags = 0x200;
    ctx->cs     = 0x1B;     /* User code segment | RPL 3 */
    ctx->ss     = 0x23;     /* User data segment | RPL 3 */
    ctx->cr3    = pml4;
}
#endif /* !ARCH_ARM64 */

/* ============================
   FPU Context Wrappers
   ============================ */

extern void ctx_init_fpu(void);

void ctx_fpu_init(struct fpu_context* fpu)
{
    if (!fpu)
        return;
    
    memset(fpu, 0, sizeof(struct fpu_context));
    fpu->used = 0;
}
