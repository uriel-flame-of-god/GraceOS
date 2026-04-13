; ============================
; GraceOS Context Switch
; x86_64 Assembly Implementation
; ============================

format ELF64

section '.text' executable

; Export symbols
public ctx_switch
public ctx_switch_to_user
public ctx_save_fpu
public ctx_restore_fpu
public ctx_init_fpu

; ============================
; ctx_switch - Kernel context switch
;
; void ctx_switch(struct cpu_context* old, struct cpu_context* new);
;
; Calling convention: System V AMD64 ABI
; RDI = pointer to old context (struct cpu_context*)
; RSI = pointer to new context (struct cpu_context*)
;
; struct cpu_context layout:
;   0x00: rip
;   0x08: rsp
;   0x10: rflags
;   0x18: rbx
;   0x20: rbp
;   0x28: r12
;   0x30: r13
;   0x38: r14
;   0x40: r15
;   0x48: cs
;   0x50: ss
;   0x58: cr3
; ============================

ctx_switch:
    ; Save old context (if old != NULL)
    test    rdi, rdi
    jz      .restore
    
    ; Save return address as RIP
    mov     rax, [rsp]
    mov     [rdi + 0x00], rax
    
    ; Save stack pointer (after return addr)
    lea     rax, [rsp + 8]
    mov     [rdi + 0x08], rax
    
    ; Save flags
    pushfq
    pop     rax
    mov     [rdi + 0x10], rax
    
    ; Save callee-saved registers
    mov     [rdi + 0x18], rbx
    mov     [rdi + 0x20], rbp
    mov     [rdi + 0x28], r12
    mov     [rdi + 0x30], r13
    mov     [rdi + 0x38], r14
    mov     [rdi + 0x40], r15
    
.restore:
    ; Restore new context
    test    rsi, rsi
    jz      .done
    
    ; Restore callee-saved registers
    mov     rbx, [rsi + 0x18]
    mov     rbp, [rsi + 0x20]
    mov     r12, [rsi + 0x28]
    mov     r13, [rsi + 0x30]
    mov     r14, [rsi + 0x38]
    mov     r15, [rsi + 0x40]
    
    ; Restore flags
    mov     rax, [rsi + 0x10]
    push    rax
    popfq
    
    ; Load new stack
    mov     rsp, [rsi + 0x08]
    
    ; Jump to new RIP
    jmp     qword [rsi + 0x00]
    
.done:
    ret


; ============================
; ctx_switch_to_user - Switch to user mode
;
; void ctx_switch_to_user(uint64_t rip, uint64_t rsp, uint64_t rflags);
;
; RDI = user RIP
; RSI = user RSP
; RDX = user RFLAGS
;
; Uses IRETQ to switch to ring 3.
; ============================

ctx_switch_to_user:
    ; Build IRETQ frame on stack:
    ; [SS]
    ; [RSP]
    ; [RFLAGS]
    ; [CS]
    ; [RIP]
    
    ; User data segment selector (ring 3)
    ; With RPL=3: 0x23 = user data, 0x1B = user code
    
    mov     rax, 0x23
    push    rax                 ; SS
    
    push    rsi                 ; RSP (user stack)
    
    ; Ensure interrupts enabled in user mode
    mov     rax, rdx
    or      rax, 0x200
    push    rax                 ; RFLAGS
    
    mov     rax, 0x1B
    push    rax                 ; CS
    
    push    rdi                 ; RIP (user entry point)
    
    ; Clear registers for security
    xor     rax, rax
    xor     rbx, rbx
    xor     rcx, rcx
    xor     rdx, rdx
    xor     rdi, rdi
    xor     rsi, rsi
    xor     rbp, rbp
    xor     r8, r8
    xor     r9, r9
    xor     r10, r10
    xor     r11, r11
    xor     r12, r12
    xor     r13, r13
    xor     r14, r14
    xor     r15, r15
    
    ; Switch to user mode
    iretq


; ============================
; ctx_save_fpu - Save FPU/SSE state
;
; void ctx_save_fpu(struct fpu_context* fpu);
; RDI = pointer to fpu_context
; ============================

ctx_save_fpu:
    test    rdi, rdi
    jz      .save_done
    
    fxsave  [rdi]
    
    ; Mark as used
    mov     dword [rdi + 512], 1
    
.save_done:
    ret


; ============================
; ctx_restore_fpu - Restore FPU/SSE state
;
; void ctx_restore_fpu(struct fpu_context* fpu);
; RDI = pointer to fpu_context
; ============================

ctx_restore_fpu:
    test    rdi, rdi
    jz      .restore_done
    
    ; Check if used
    cmp     dword [rdi + 512], 0
    je      .restore_done
    
    fxrstor [rdi]
    
.restore_done:
    ret


; ============================
; ctx_init_fpu - Initialize FPU state
; ============================

ctx_init_fpu:
    fninit
    ret
