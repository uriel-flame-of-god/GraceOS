// ============================
// GraceOS ARM64 Context Switch
// ============================
//
// void ctx_switch(struct cpu_context* old, struct cpu_context* new);
//
// AArch64 cpu_context layout (must match ARCH_ARM64 block in context.h):
//
//   Offset  Register
//   0x00    x19
//   0x08    x20
//   0x10    x21
//   0x18    x22
//   0x20    x23
//   0x28    x24
//   0x30    x25
//   0x38    x26
//   0x40    x27
//   0x48    x28
//   0x50    x29  (fp — frame pointer, callee-saved)
//   0x58    x30  (lr — link register, used as instruction pointer)
//   0x60    sp   (stack pointer)
//
// Saving only callee-saved registers + sp + lr follows the AArch64 PCS.
// The caller (ctx_switch itself) is a function call so x0-x18 are caller-saved
// and do not need to be preserved across the switch boundary.
//
// First-time switch into a new thread:
//   ctx_init_kernel() sets lr = entry, sp = stack_top.
//   ctx_switch() restores these and executes 'ret', which jumps to entry.
//
// Resumed thread:
//   lr was saved as the return address inside a previous ctx_switch call.
//   'ret' returns to the instruction after 'bl ctx_switch' in the resumed code.

.section ".text", "ax"
.global ctx_switch

ctx_switch:
    // x0 = old ctx pointer (may be NULL — skip save if so)
    // x1 = new ctx pointer (must not be NULL)

    cbz     x0, .restore_ctx    // NULL old → jump straight to restore

    // ----- Save current context into *x0 -----
    stp     x19, x20, [x0, #0x00]
    stp     x21, x22, [x0, #0x10]
    stp     x23, x24, [x0, #0x20]
    stp     x25, x26, [x0, #0x30]
    stp     x27, x28, [x0, #0x40]
    stp     x29, x30, [x0, #0x50]   // fp, lr
    mov     x2,  sp
    str     x2,       [x0, #0x60]   // sp

.restore_ctx:
    // ----- Restore context from *x1 -----
    ldp     x19, x20, [x1, #0x00]
    ldp     x21, x22, [x1, #0x10]
    ldp     x23, x24, [x1, #0x20]
    ldp     x25, x26, [x1, #0x30]
    ldp     x27, x28, [x1, #0x40]
    ldp     x29, x30, [x1, #0x50]   // fp, lr
    ldr     x2,       [x1, #0x60]   // sp
    mov     sp,  x2

    // Return to the restored lr — either a new thread's entry point
    // (first switch) or the instruction after 'bl ctx_switch' (resume).
    ret
