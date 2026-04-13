; ============================
; GraceOS Boot Entry (FASM)
; Multiboot2 + x86_64
; ============================

format ELF64


; ----------------------------
; Multiboot2 Header
; ----------------------------

MB2_MAGIC        = 0xE85250D6
MB2_ARCH_I386    = 0
MB2_HEADER_LEN   = header_end - header_start
MB2_CHECKSUM     = -(MB2_MAGIC + MB2_ARCH_I386 + MB2_HEADER_LEN)

section '.multiboot' align 8

header_start:

dd MB2_MAGIC
dd MB2_ARCH_I386
dd MB2_HEADER_LEN
dd MB2_CHECKSUM

; Framebuffer tag request (type 5)
; flags = 1 means optional - GRUB can still boot if framebuffer isn't available
; When gfxpayload=text, GRUB ignores this and boots in text mode
; When gfxpayload=keep, GRUB provides a linear framebuffer
align 8
dw 5              ; type = framebuffer
dw 1              ; flags = optional (bit 0 set)
dd 20             ; size = 20 bytes
dd 1280            ; width (preferred)
dd 720            ; height (preferred)
dd 32             ; depth (bits per pixel)

; End tag (must be 8-byte aligned)
align 8
dw 0              ; type
dw 0              ; flags
dd 8              ; size

header_end:


; ----------------------------
; Code
; ----------------------------

section '.text' executable

public _start
extrn kmain
extrn stack_top

use32
_start:
    cli
    
    ; Save multiboot info pointer (EBX contains multiboot2 info address)
    ; Use EBP which is callee-saved and not used in page table setup
    mov ebp, ebx
    
    ; Setup stack (32-bit) - use linker-provided stack
    mov esp, stack_top
    
    ; Setup page tables for long mode
    ; Clear page table area (need space for 16GB mapping)
    ; 18 pages × 4KB = 72KB → 18432 dwords
    mov edi, 0x1000
    mov cr3, edi
    xor eax, eax
    mov ecx, 20480          ; Clear 80KB for page tables
    rep stosd
    mov edi, cr3
    
    ; Setup identity mapping for first 16GB using 2MB pages
    ; This supports large RAM and MMIO regions like framebuffer at 0xFD000000
    ;
    ; Structure:
    ;   PML4T at 0x1000: PML4T[0] -> PDPT
    ;   PDPT  at 0x2000: PDPT[0-15] -> PDT0-PDT15
    ;   PDT0  at 0x3000:  maps 0GB-1GB
    ;   PDT1  at 0x4000:  maps 1GB-2GB
    ;   ...
    ;   PDT15 at 0x12000: maps 15GB-16GB
    
    ; PML4T[0] -> PDPT at 0x2000
    mov dword [edi], 0x2003
    add edi, 0x1000         ; edi = 0x2000 (PDPT)
    
    ; Setup 16 PDPT entries pointing to 16 PDTs
    ; PDT0 at 0x3000, PDT1 at 0x4000, ..., PDT15 at 0x12000
    mov eax, 0x3003         ; First PDT address with flags
    mov ecx, 16             ; 16 GB
.pdpt_loop:
    mov [edi], eax
    mov dword [edi + 4], 0  ; Upper 32 bits = 0
    add eax, 0x1000         ; Next PDT
    add edi, 8              ; Next PDPT entry
    loop .pdpt_loop
    
    ; Now fill all 16 PDTs (8192 entries total = 16GB)
    ; Each 2MB page entry needs proper upper bits for addresses > 4GB
    mov edi, 0x3000         ; Start of PDT0
    xor ebx, ebx            ; Physical base address (lower 32 bits)
    xor edx, edx            ; Physical base address (upper 32 bits)
    mov ecx, 8192           ; Number of 2MB pages (16GB / 2MB)
    
.map_loop:
    mov eax, ebx
    or eax, 0x83            ; Present | Write | Page Size (2MB)
    mov [edi], eax          ; Write PDE (lower 32 bits)
    mov [edi + 4], edx      ; Write PDE (upper 32 bits)
    
    add ebx, 0x200000       ; Next 2MB (physical address)
    adc edx, 0              ; Carry to upper 32 bits if overflow
    add edi, 8              ; Next PDE (8 bytes per entry)
    
    loop .map_loop
    
    ; Enable PAE
    mov eax, cr4
    or eax, 1 shl 5
    mov cr4, eax
    
    ; Set long mode bit in EFER MSR
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 shl 8
    wrmsr
    
    ; Enable paging (activates long mode)
    mov eax, cr0
    or eax, 1 shl 31
    mov cr0, eax
    
    ; Load 64-bit GDT
    lgdt [gdt64_pointer]
    
    ; Jump to 64-bit code
    jmp 0x08:long_mode_start

use64
long_mode_start:
    ; Clear segment registers
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Setup 64-bit stack - use linker-provided stack
    mov rsp, stack_top
    
    ; Enable SSE/SSE2 (required for XMM registers)
    mov rax, cr0
    and ax, 0xFFFB          ; Clear CR0.EM (bit 2) - No emulation
    or ax, 0x2              ; Set CR0.MP (bit 1) - Monitor coprocessor
    mov cr0, rax
    
    mov rax, cr4
    or ax, 3 shl 9          ; Set CR4.OSFXSR (bit 9) and CR4.OSXMMEXCPT (bit 10) - Enable SSE
    mov cr4, rax
    
    ; Pass multiboot info to kernel
    ; RBP contains the saved multiboot2 info pointer from EBP in 32-bit mode
    ; RDI is first argument in System V AMD64 ABI
    mov rdi, rbp
    
    ; Call C kernel
    call kmain

.hang:
    cli
    hlt
    jmp .hang


; ----------------------------
; GDT for 64-bit mode
; ----------------------------

section '.rodata' align 16

align 16
gdt64:
    ; Null Descriptor (0x00)
    dq 0x0000000000000000
    
    ; Kernel Code Segment (0x08)
    ; Base: 0, Limit: 0xFFFFF
    ; Flags: Present, Ring 0, Code, Executable, Readable
    ; Long mode, 4KB granularity
    dq 0x00209A0000000000
    
    ; Kernel Data Segment (0x10)
    ; Base: 0, Limit: 0xFFFFF
    ; Flags: Present, Ring 0, Data, Writable
    dq 0x0000920000000000
    
    ; User Code Segment (0x18)
    ; Base: 0, Limit: 0xFFFFF
    ; Flags: Present, Ring 3, Code, Executable, Readable
    ; Long mode, 4KB granularity
    dq 0x0020FA0000000000
    
    ; User Data Segment (0x20)
    ; Base: 0, Limit: 0xFFFFF
    ; Flags: Present, Ring 3, Data, Writable
    dq 0x0000F20000000000
    
gdt64_end:

gdt64_pointer:
    dw gdt64_end - gdt64 - 1    ; Limit
    dq gdt64                    ; Base (64-bit address)

; Export GDT pointer for reloading from C
public gdt64_pointer
