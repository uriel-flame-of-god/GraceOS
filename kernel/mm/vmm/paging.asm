; ============================
; GraceOS VMM Assembly Routines
; x86_64 paging support
; ============================

format ELF64


section '.text' executable


; Write CR3 (page table base)
; void vmm_write_cr3(uint64_t pml4_phys)
public vmm_write_cr3
vmm_write_cr3:
    mov cr3, rdi
    ret


; Read CR3
; uint64_t vmm_read_cr3(void)
public vmm_read_cr3
vmm_read_cr3:
    mov rax, cr3
    ret


; Invalidate TLB entry
; void vmm_invlpg(uint64_t virt)
public vmm_invlpg
vmm_invlpg:
    invlpg [rdi]
    ret


; Flush entire TLB (reload CR3)
public vmm_flush_tlb
vmm_flush_tlb:
    mov rax, cr3
    mov cr3, rax
    ret
