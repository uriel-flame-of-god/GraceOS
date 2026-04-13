; ============================
; GraceOS Power Management
; Shutdown and Reboot routines
; ============================

format ELF64

section '.text' executable

; ============================
; power_reboot - Reboot the system
; Uses keyboard controller reset
; ============================
public power_reboot
power_reboot:
    cli                     ; Disable interrupts
    
    ; Try keyboard controller reset (port 0x64)
    ; Wait for keyboard controller ready
.wait_kbd:
    in      al, 0x64
    test    al, 0x02        ; Check if input buffer is full
    jnz     .wait_kbd
    
    ; Send reset command (0xFE)
    mov     al, 0xFE
    out     0x64, al
    
    ; If that didn't work, try triple fault
    ; Load a null IDT and trigger interrupt
    lidt    [.null_idt]
    int     3               ; Triple fault -> reboot
    
    ; Should never reach here
    hlt
    jmp     $
    
.null_idt:
    dw 0                    ; Limit = 0
    dq 0                    ; Base = 0


; ============================
; power_shutdown - Shutdown the system
; For QEMU/Bochs: uses ACPI or debug port
; ============================
public power_shutdown
power_shutdown:
    cli                     ; Disable interrupts
    
    ; Method 1: QEMU debug exit (if configured with -device isa-debug-exit)
    mov     ax, 0x2000
    mov     dx, 0x604       ; QEMU ACPI power off port
    out     dx, ax
    
    ; Method 2: Bochs shutdown port
    mov     dx, 0xB004
    mov     ax, 0x2000
    out     dx, ax
    
    ; Method 3: Old QEMU versions
    mov     dx, 0x4004
    mov     ax, 0x3400
    out     dx, ax
    
    ; Method 4: VirtualBox ACPI
    mov     dx, 0x4004
    mov     ax, 0x3400
    out     dx, ax
    
    ; If nothing worked, just halt with message
    ; Loop forever in halt state
.halt_loop:
    hlt
    jmp     .halt_loop


; ============================
; power_halt - Halt the CPU
; Enters low-power halt state
; ============================
public power_halt
power_halt:
    cli
.halt:
    hlt
    jmp     .halt

