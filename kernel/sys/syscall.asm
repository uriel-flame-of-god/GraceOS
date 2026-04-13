; ============================
; GraceOS Syscall Entry Point
; INT 0x80 Handler
; ============================

format ELF64

section '.text' executable

; Export symbols
public syscall_entry

; Import kernel dispatcher
extrn syscall_dispatch

; ============================
; syscall_entry
; Called via INT 0x80 from userland
;
; Input:
;   RAX = syscall number
;   RDI = arg1
;   RSI = arg2
;   RDX = arg3
;   R10 = arg4
;   R8  = arg5
;   R9  = arg6
;
; Output:
;   RAX = return value
; ============================

syscall_entry:
    ; Save registers that might be clobbered
    push rcx
    push r11
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    
    ; Setup arguments for syscall_dispatch
    ; C calling convention: RDI, RSI, RDX, RCX, R8, R9
    ; We need: num(RAX), a1(RDI), a2(RSI), a3(RDX), a4(R10), a5(R8)
    
    mov r9, r8          ; arg5 -> r9 (6th param)
    mov r8, r10         ; arg4 -> r8 (5th param)
    mov rcx, rdx        ; arg3 -> rcx (4th param)
    mov rdx, rsi        ; arg2 -> rdx (3rd param)
    mov rsi, rdi        ; arg1 -> rsi (2nd param)
    mov rdi, rax        ; syscall num -> rdi (1st param)
    
    ; Call kernel dispatcher
    call syscall_dispatch
    
    ; Return value is in RAX
    
    ; Restore registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    pop r11
    pop rcx
    
    ; Return from interrupt
    iretq
