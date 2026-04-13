/* ============================
   GraceOS Linker Script
   ============================ */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    /* Kernel loads at 1MB (Multiboot standard) */
    . = 1M;

    /* ----------------------------
       Text / Code
       ---------------------------- */
    .text ALIGN(4K) : {
        KEEP(*(.multiboot))     /* Multiboot header */
      __rmcode_start = .;
      KEEP(*(.rmcode))        /* VBE real-mode stub */
      __rmcode_end = .;
        *(.text*)
    }

    .font ALIGN(4K): {
      _font_start=.;
      KEEP(*(.font))
      _font_end=.;
    }: rodata

    /* ----------------------------
       Read-only data
       ---------------------------- */
    .rodata ALIGN(4K) : {
        *(.rodata*)
    }

    /* ----------------------------
       Data
       ---------------------------- */
    .data ALIGN(4K) : {
        *(.data*)
    }

    /* ----------------------------
       BSS (uninitialized data)
       Includes 4MB framebuffer backbuffer
       ---------------------------- */
    .bss ALIGN(4K) : {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }

    /* ----------------------------
       Kernel Stack (32 KB, 16-byte aligned)
       Placed after BSS to avoid overlap
       ---------------------------- */
    .stack ALIGN(4K) : {
        . = ALIGN(16);
        stack_bottom = .;
        . = . + 32K;
        stack_top = .;
    }

    /* ----------------------------
       End marker
       ---------------------------- */
    . = ALIGN(4K);
    end = .;
    __kernel_end = .;
}
