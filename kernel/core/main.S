/* ============================
   GraceOS Low-Level Routines
   ============================ */

.section .text
.code64

/* --------------------------------
   Halt CPU
   -------------------------------- */

.global halt_cpu

halt_cpu:
    cli
1:
    hlt
    jmp 1b


/* --------------------------------
   Enable Interrupts
   -------------------------------- */

.global enable_interrupts

enable_interrupts:
    sti
    ret


/* --------------------------------
   Disable Interrupts
   -------------------------------- */

.global disable_interrupts

disable_interrupts:
    cli
    ret


/* --------------------------------
   Read CR3 (Page Table Base)
   -------------------------------- */

.global read_cr3

read_cr3:
    mov %cr3, %rax
    ret


/* --------------------------------
   Write CR3
   -------------------------------- */

.global write_cr3

write_cr3:
    mov %rdi, %cr3
    ret
