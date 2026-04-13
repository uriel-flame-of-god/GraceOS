; ============================
; GraceOS Userland Syscall Stubs
; ============================

format ELF64

section '.text' executable

; Export syscall functions
public __syscall0
public __syscall1
public __syscall2
public __syscall3
public __syscall4
public __syscall5

; ============================
; __syscall0(num)
; No arguments
; ============================
__syscall0:
    mov rax, rdi        ; syscall number
    int 0x80
    ret

; ============================
; __syscall1(num, a1)
; 1 argument
; ============================
__syscall1:
    mov rax, rdi        ; syscall number
    mov rdi, rsi        ; arg1
    int 0x80
    ret

; ============================
; __syscall2(num, a1, a2)
; 2 arguments
; ============================
__syscall2:
    mov rax, rdi        ; syscall number
    mov rdi, rsi        ; arg1
    mov rsi, rdx        ; arg2
    int 0x80
    ret

; ============================
; __syscall3(num, a1, a2, a3)
; 3 arguments
; ============================
__syscall3:
    mov rax, rdi        ; syscall number
    mov rdi, rsi        ; arg1
    mov rsi, rdx        ; arg2
    mov rdx, rcx        ; arg3
    int 0x80
    ret

; ============================
; __syscall4(num, a1, a2, a3, a4)
; 4 arguments
; ============================
__syscall4:
    mov rax, rdi        ; syscall number
    mov rdi, rsi        ; arg1
    mov rsi, rdx        ; arg2
    mov rdx, rcx        ; arg3
    mov r10, r8         ; arg4
    int 0x80
    ret

; ============================
; __syscall5(num, a1, a2, a3, a4, a5)
; 5 arguments
; ============================
__syscall5:
    mov rax, rdi        ; syscall number
    mov rdi, rsi        ; arg1
    mov rsi, rdx        ; arg2
    mov rdx, rcx        ; arg3
    mov r10, r8         ; arg4
    mov r8, r9          ; arg5
    int 0x80
    ret
