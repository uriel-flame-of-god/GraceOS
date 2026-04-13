; ============================
; GraceOS VBE BIOS Thunk (FASM)
; ============================

format ELF64

section '.text' executable

public vbe_bios_call

extrn gdt64_pointer

use64
vbe_bios_call:
    pushfq
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    cli

    mov rax, cr0
    mov [saved_cr0], rax
    mov rax, cr3
    mov [saved_cr3], rax
    mov rax, cr4
    mov [saved_cr4], rax

    mov ecx, 0xC0000080
    rdmsr
    mov [saved_efer], eax
    mov [saved_efer + 4], edx

    mov [saved_rsp], rsp

    lgdt [vbe_gdt_ptr64]
    push 0x08
    mov rax, pm32_entry
    push rax
    db 0x48, 0xCB               ; retfq — far return to pm32_entry:0x08

pm64_return:
    mov rsp, [saved_rsp]
    lgdt [gdt64_pointer]
    push 0x08
    mov rax, pm64_restore
    push rax
    db 0x48, 0xCB               ; retfq — far return to pm64_restore:0x08

pm64_restore:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    popfq
    ret

use32
pm32_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x9000

    mov eax, cr0
    and eax, 0x7FFFFFFF
    mov cr0, eax

    mov ecx, 0xC0000080
    rdmsr
    and eax, 0xFFFFFEFF
    wrmsr

    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax

    ; Store far pointer to pm32_return at fixed address 0x6FF0
    mov dword [0x6FF0], pm32_return
    mov word  [0x6FF4], 0x08

    jmp 0x0000:0x7000

pm32_return:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x9000

    mov eax, dword [saved_cr3]
    mov cr3, eax
    mov eax, dword [saved_cr4]
    mov cr4, eax

    mov ecx, 0xC0000080
    mov eax, dword [saved_efer]
    mov edx, dword [saved_efer + 4]
    wrmsr

    mov eax, dword [saved_cr0]
    mov cr0, eax

    jmp 0x18:pm64_return

section '.rmcode' executable
use16
org 0
vbe_rm_code_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x6F00

    mov ax, [0x6000]
    cmp ax, 1
    je vbe_op_info
    cmp ax, 2
    je vbe_op_modeinfo
    cmp ax, 3
    je vbe_op_setmode
    cmp ax, 4
    je vbe_op_vga
    mov word [0x6004], 0x014F
    jmp vbe_done

vbe_op_info:
    mov ax, 0x0620
    mov es, ax
    xor di, di
    mov ax, 0x4F00
    int 0x10
    mov [0x6004], ax

    mov si, [0x6200 + 0x0E]
    mov ds, [0x6200 + 0x10]
    mov ax, 0x0660
    mov es, ax
    xor di, di
    mov cx, 256

.mode_copy:
    mov ax, [ds:si]
    mov [es:di], ax
    add si, 2
    add di, 2
    cmp ax, 0xFFFF
    je .copy_done
    dec cx
    jnz .mode_copy

.copy_done:
    xor ax, ax
    mov ds, ax
    jmp vbe_done

vbe_op_modeinfo:
    mov cx, [0x6002]
    mov ax, 0x0640
    mov es, ax
    xor di, di
    mov ax, 0x4F01
    int 0x10
    mov [0x6004], ax
    jmp vbe_done

vbe_op_setmode:
    mov bx, [0x6002]
    or bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    mov [0x6004], ax
    jmp vbe_done

vbe_op_vga:
    mov ax, [0x6002]
    int 0x10
    mov [0x6004], ax
    jmp vbe_done

vbe_done:
    cli
    lgdt [0x67F0]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp far dword [0x6FF0]

vbe_rm_code_end:

section '.bss' writeable
align 8
saved_cr0:
    dq 0
saved_cr3:
    dq 0
saved_cr4:
    dq 0
saved_efer:
    dq 0
saved_rsp:
    dq 0

section '.data' writeable
align 8
vbe_gdt_ptr64:
    dw (5 * 8) - 1
    dq 0x6800
