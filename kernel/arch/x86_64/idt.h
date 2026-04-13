#ifndef GRACEOS_IDT_H
#define GRACEOS_IDT_H

#include "../../../lib/libc/int.h"

/* ============================
   Interrupt Descriptor Table
   ============================ */

/* IDT Entry Structure */
struct idt_entry
{
    uint16_t offset_low;    // Offset bits 0-15
    uint16_t selector;      // Code segment selector
    uint8_t  ist;           // Interrupt Stack Table offset (0-7)
    uint8_t  type_attr;     // Type and attributes
    uint16_t offset_mid;    // Offset bits 16-31
    uint32_t offset_high;   // Offset bits 32-63
    uint32_t zero;          // Reserved (must be zero)
} __attribute__((packed));

/* IDT Pointer Structure */
struct idt_ptr
{
    uint16_t limit;         // Size of IDT - 1
    uint64_t base;          // Base address of IDT
} __attribute__((packed));

/* Interrupt Stack Frame */
struct interrupt_frame
{
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, error_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed));

/* IDT Constants */
#define IDT_ENTRIES 256

#define IDT_TYPE_INTERRUPT  0x8E    // 64-bit interrupt gate, present, ring 0
#define IDT_TYPE_TRAP       0x8F    // 64-bit trap gate, present, ring 0
#define IDT_TYPE_INT_RING3  0xEE    // 64-bit interrupt gate, present, ring 3

/* Syscall interrupt vector */
#define SYSCALL_VECTOR      0x80

/* External ASM functions */
extern void idt_load(struct idt_ptr* ptr);

/* ISR Handlers (0-31) */
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

/* IRQ Handlers (32-47) */
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

/* Syscall entry point (from kernel/sys/syscall.asm) */
extern void syscall_entry(void);

/* API Functions */
void idt_init(void);
void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector, uint8_t flags);

/* Handler functions */
void isr_handler(struct interrupt_frame* frame);
void irq_handler(struct interrupt_frame* frame);

#endif /* GRACEOS_IDT_H */
