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
    
    /* Entry point */
    ctx->rip = (uint64_t)entry;
    
    /* Stack (16-byte aligned, room for return address) */
    uint64_t sp = (uint64_t)stack_top;
    sp &= ~0xFULL;  /* Align to 16 bytes */
    sp -= 8;        /* Space for fake return address */
    ctx->rsp = sp;
    
    /* Flags: interrupts enabled */
    ctx->rflags = 0x200;
    
    /* Kernel segments */
    ctx->cs = 0x08;     /* Kernel code segment */
    ctx->ss = 0x10;     /* Kernel data segment */
}

/* ============================
   Initialize User Context
   
   Sets up context for a new user process.
   ============================ */

void ctx_init_user(struct cpu_context* ctx, uint64_t entry, uint64_t user_stack, uint64_t pml4)
{
    if (!ctx)
        return;
    
    memset(ctx, 0, sizeof(struct cpu_context));
    
    /* User entry point */
    ctx->rip = entry;
    
    /* User stack (16-byte aligned) */
    ctx->rsp = user_stack & ~0xFULL;
    
    /* Flags: interrupts enabled */
    ctx->rflags = 0x200;
    
    /* User segments (with RPL = 3) */
    ctx->cs = 0x1B;     /* User code segment | RPL 3 */
    ctx->ss = 0x23;     /* User data segment | RPL 3 */
    
    /* Page table */
    ctx->cr3 = pml4;
}

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
