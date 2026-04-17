/* ============================
   GraceOS ARM64 Linker Script
   Raspberry Pi 3 / 4
   ============================

   The RPi firmware loads kernel8.img as a flat binary at 0x80000
   and jumps directly to the first byte.

   .text.boot MUST be the very first section so _start lands at 0x80000.
*/

OUTPUT_FORMAT(elf64-littleaarch64)
ENTRY(_start)

SECTIONS
{
    . = 0x80000;            /* RPi load address */

    /* Boot entry — must be first so _start is at 0x80000 */
    .text.boot : {
        KEEP(*(.text.boot))
    }

    /* Exception vector table — must be 2 KB aligned (VBAR_EL1 requirement) */
    .text.vectors ALIGN(2048) : {
        KEEP(*(.text.vectors))
    }

    /* Remaining kernel code */
    .text ALIGN(4096) : {
        *(.text*)
    }

    /* Read-only data */
    .rodata ALIGN(4096) : {
        *(.rodata*)
    }

    /* Initialized data */
    .data ALIGN(4096) : {
        *(.data*)
    }

    /* BSS — zeroed by boot.S before kmain() */
    .bss ALIGN(4096) : {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }

    /* Kernel stack: 32 KB, 16-byte aligned */
    .stack ALIGN(4096) : {
        . = ALIGN(16);
        stack_bottom = .;
        . = . + 32K;
        stack_top = .;
    }

    . = ALIGN(4096);
    __kernel_end = .;
    end = .;
}
