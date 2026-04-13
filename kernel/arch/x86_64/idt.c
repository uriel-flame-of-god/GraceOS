// ============================
// GraceOS IDT Implementation
// ============================

#include "idt.h"
#include "../../../drivers/video/tty.h"
#include "../../../drivers/input/keyboard.h"
#include "../../include/time.h"
#include "../../proc/sched.h"
#include "io/port.h"

/* IDT Table */
static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idtp;

/* Exception Messages */
static const char* exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 FPU Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved"
};


/* Set IDT Gate */
void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector, uint8_t flags)
{
    idt[num].offset_low  = handler & 0xFFFF;
    idt[num].offset_mid  = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    
    idt[num].selector   = selector;
    idt[num].ist        = 0;
    idt[num].type_attr  = flags;
    idt[num].zero       = 0;
}


/* PIC I/O Ports */
#define PIC1_CMD    0x20
#define PIC1_DATA   0x21
#define PIC2_CMD    0xA0
#define PIC2_DATA   0xA1

/* PIT (Programmable Interval Timer) Ports */
#define PIT_CHANNEL0    0x40
#define PIT_COMMAND     0x43

/* PIT frequency: base clock is 1193182 Hz */
#define PIT_BASE_FREQ   1193182
#define PIT_TARGET_FREQ 1000    /* 1000 Hz = 1ms per tick */

/* Initialize PIT for 1000 Hz (1ms) timer interrupts */
static void pit_init(void)
{
    // Calculate divisor for desired frequency
    uint16_t divisor = PIT_BASE_FREQ / PIT_TARGET_FREQ;
    
    // Command: Channel 0, lobyte/hibyte, rate generator, binary
    outb(PIT_COMMAND, 0x36);
    
    // Send divisor (low byte first, then high byte)
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}

/* Remap PIC to avoid conflicts with CPU exceptions */
static void pic_remap(void)
{
    uint8_t a1, a2;
    
    // Save masks
    a1 = inb(PIC1_DATA);
    a2 = inb(PIC2_DATA);
    
    // Start initialization
    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);
    
    // Set vector offsets
    outb(PIC1_DATA, 32);    // Master PIC offset to 32
    outb(PIC2_DATA, 40);    // Slave PIC offset to 40
    
    // Tell Master PIC about Slave
    outb(PIC1_DATA, 4);
    outb(PIC2_DATA, 2);
    
    // Set 8086 mode
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);
    
    // Restore masks
    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);
}


/* Initialize IDT */
void idt_init(void)
{
    // Setup IDT pointer
    idtp.limit = (sizeof(struct idt_entry) * IDT_ENTRIES) - 1;
    idtp.base = (uint64_t)&idt;
    
    // Clear IDT
    for (int i = 0; i < IDT_ENTRIES; i++)
    {
        idt[i].offset_low  = 0;
        idt[i].offset_mid  = 0;
        idt[i].offset_high = 0;
        idt[i].selector    = 0;
        idt[i].ist         = 0;
        idt[i].type_attr   = 0;
        idt[i].zero        = 0;
    }
    
    // Remap PIC
    pic_remap();
    
    // Install ISRs (CPU exceptions 0-31)
    idt_set_gate(0,  (uint64_t)isr0,  0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(1,  (uint64_t)isr1,  0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(2,  (uint64_t)isr2,  0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(3,  (uint64_t)isr3,  0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(4,  (uint64_t)isr4,  0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(5,  (uint64_t)isr5,  0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(6,  (uint64_t)isr6,  0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(7,  (uint64_t)isr7,  0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(8,  (uint64_t)isr8,  0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(9,  (uint64_t)isr9,  0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(10, (uint64_t)isr10, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(11, (uint64_t)isr11, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(12, (uint64_t)isr12, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(13, (uint64_t)isr13, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(14, (uint64_t)isr14, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(15, (uint64_t)isr15, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(16, (uint64_t)isr16, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(17, (uint64_t)isr17, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(18, (uint64_t)isr18, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(19, (uint64_t)isr19, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(20, (uint64_t)isr20, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(21, (uint64_t)isr21, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(22, (uint64_t)isr22, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(23, (uint64_t)isr23, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(24, (uint64_t)isr24, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(25, (uint64_t)isr25, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(26, (uint64_t)isr26, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(27, (uint64_t)isr27, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(28, (uint64_t)isr28, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(29, (uint64_t)isr29, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(30, (uint64_t)isr30, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(31, (uint64_t)isr31, 0x08, IDT_TYPE_INTERRUPT);
    
    // Install IRQs (32-47)
    idt_set_gate(32, (uint64_t)irq0,  0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(33, (uint64_t)irq1,  0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(34, (uint64_t)irq2,  0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(35, (uint64_t)irq3,  0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(36, (uint64_t)irq4,  0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(37, (uint64_t)irq5,  0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(38, (uint64_t)irq6,  0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(39, (uint64_t)irq7,  0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(40, (uint64_t)irq8,  0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(41, (uint64_t)irq9,  0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(42, (uint64_t)irq10, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(43, (uint64_t)irq11, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(44, (uint64_t)irq12, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(45, (uint64_t)irq13, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(46, (uint64_t)irq14, 0x08, IDT_TYPE_INTERRUPT);
    idt_set_gate(47, (uint64_t)irq15, 0x08, IDT_TYPE_INTERRUPT);
    
    // Install syscall handler (INT 0x80)
    // Ring 3 accessible for userland syscalls
    idt_set_gate(SYSCALL_VECTOR, (uint64_t)syscall_entry, 0x08, IDT_TYPE_INT_RING3);
    
    // Load IDT
    idt_load(&idtp);
    
    // Initialize PIT for timer interrupts (1000 Hz)
    pit_init();
    
    // Enable IRQ0 (timer) and IRQ1 (keyboard)
    // Unmask bits 0 and 1 on PIC1
    outb(PIC1_DATA, 0xFC);  // 11111100 = all masked except IRQ0 and IRQ1
    outb(PIC2_DATA, 0xFF);  // Mask all on slave PIC
}


/* ISR Handler - Called from assembly */
void isr_handler(struct interrupt_frame* frame)
{
    // Special handling for page fault
    if (frame->int_no == 14)
    {
        uint64_t fault_addr;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_addr));
        
        tty_set_color(TTY_WHITE, TTY_RED);
        tty_print("\n\n*** PAGE FAULT ***\n");
        tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
        
        tty_print("Address: 0x");
        // Print fault address (simplified)
        tty_print("\nError: ");
        
        if (!(frame->error_code & 0x1))
            tty_print("Page not present ");
        if (frame->error_code & 0x2)
            tty_print("Write ");
        else
            tty_print("Read ");
        if (frame->error_code & 0x4)
            tty_print("User-mode ");
        else
            tty_print("Kernel-mode ");
        
        tty_print("\n");
        
        // Halt the system
        for (;;)
            __asm__ volatile ("cli; hlt");
    }
    
    tty_set_color(TTY_WHITE, TTY_RED);
    tty_print("\n\n*** EXCEPTION: ");
    
    if (frame->int_no < 32)
    {
        tty_print(exception_messages[frame->int_no]);
    }
    else
    {
        tty_print("Unknown");
    }
    
    tty_print(" ***\n");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    
    // Halt the system
    for (;;)
        __asm__ volatile ("cli; hlt");
}


/* IRQ Handler - Called from assembly */
void irq_handler(struct interrupt_frame* frame)
{
    /* Send EOI to PIC before processing.
       sched_tick() may call schedule() which context-switches away from this
       handler entirely.  If EOI were sent at the end (after the switch), the
       return path would never execute and the PIC would keep IRQ0 masked
       permanently — no more timer interrupts, no more preemption. */
    if (frame->int_no >= 40)
        outb(PIC2_CMD, 0x20);   /* EOI to slave */
    outb(PIC1_CMD, 0x20);       /* EOI to master */

    switch (frame->int_no)
    {
        case 32:  // IRQ0 - Timer
            timer_tick();
            sched_tick();
            break;

        case 33:  // IRQ1 - Keyboard
            keyboard_handler();
            break;
    }
}
