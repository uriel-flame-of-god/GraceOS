; ============================
; GraceOS IDT Handlers (FASM)
; ============================

format ELF64

section '.text' executable

; ----------------------------
; Exception Handlers (0-31)
; ----------------------------

; Macro to create exception handler without error code
macro make_isr num {
    public isr#num
    isr#num:
        cli
        push 0              ; Dummy error code
        push num            ; Exception number
        jmp isr_common
}

; Macro to create exception handler with error code
macro make_isr_err num {
    public isr#num
    isr#num:
        cli
        push num            ; Exception number
        jmp isr_common
}

; CPU Exceptions
make_isr 0      ; Divide by Zero
make_isr 1      ; Debug
make_isr 2      ; NMI
make_isr 3      ; Breakpoint
make_isr 4      ; Overflow
make_isr 5      ; Bound Range Exceeded
make_isr 6      ; Invalid Opcode
make_isr 7      ; Device Not Available
make_isr_err 8  ; Double Fault (has error code)
make_isr 9      ; Coprocessor Segment Overrun
make_isr_err 10 ; Invalid TSS (has error code)
make_isr_err 11 ; Segment Not Present (has error code)
make_isr_err 12 ; Stack-Segment Fault (has error code)
make_isr_err 13 ; General Protection Fault (has error code)
make_isr_err 14 ; Page Fault (has error code)
make_isr 15     ; Reserved
make_isr 16     ; x87 FPU Error
make_isr_err 17 ; Alignment Check (has error code)
make_isr 18     ; Machine Check
make_isr 19     ; SIMD Floating-Point Exception
make_isr 20     ; Virtualization Exception
make_isr_err 21 ; Control Protection Exception (has error code)
make_isr 22     ; Reserved
make_isr 23     ; Reserved
make_isr 24     ; Reserved
make_isr 25     ; Reserved
make_isr 26     ; Reserved
make_isr 27     ; Reserved
make_isr 28     ; Hypervisor Injection Exception
make_isr_err 29 ; VMM Communication Exception (has error code)
make_isr_err 30 ; Security Exception (has error code)
make_isr 31     ; Reserved


; ----------------------------
; IRQ Handlers (32-47)
; ----------------------------

macro make_irq num {
    public irq#num
    irq#num:
        cli
        push 0                      ; Dummy error code
        push num + 32               ; IRQ number + offset
        jmp irq_common
}

make_irq 0      ; Timer
make_irq 1      ; Keyboard
make_irq 2      ; Cascade
make_irq 3      ; COM2
make_irq 4      ; COM1
make_irq 5      ; LPT2
make_irq 6      ; Floppy
make_irq 7      ; LPT1
make_irq 8      ; RTC
make_irq 9      ; Free
make_irq 10     ; Free
make_irq 11     ; Free
make_irq 12     ; PS/2 Mouse
make_irq 13     ; FPU
make_irq 14     ; Primary ATA
make_irq 15     ; Secondary ATA


; ----------------------------
; Common ISR Handler
; ----------------------------

extrn isr_handler

isr_common:
    ; Save all registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    ; Call C handler
    mov rdi, rsp            ; Pass stack pointer as argument
    call isr_handler
    
    ; Restore all registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    ; Clean up error code and exception number
    add rsp, 16
    
    ; Return from interrupt
    iretq


; ----------------------------
; Common IRQ Handler
; ----------------------------

extrn irq_handler

irq_common:
    ; Save all registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    ; Call C handler
    mov rdi, rsp            ; Pass stack pointer as argument
    call irq_handler
    
    ; Restore all registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    ; Clean up error code and IRQ number
    add rsp, 16
    
    ; Return from interrupt
    iretq


; ----------------------------
; Load IDT
; ----------------------------

public idt_load

idt_load:
    lidt [rdi]      ; Load IDT pointer from first argument
    ret
