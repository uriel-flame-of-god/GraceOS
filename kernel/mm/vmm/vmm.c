// ============================
// GraceOS Virtual Memory Manager
// x86_64 4-level paging
// ============================

#include "vmm.h"
#include "../pmm/pmm.h"
#include "../../log/klog.h"
#include "../../../drivers/video/fb.h"
#include "../../../lib/libc/string.h"

/* Page table structure (4 levels) */
/* PML4 -> PDPT -> PD -> PT -> Page */

/* Current kernel PML4 */
static uint64_t* kernel_pml4 = 0;
static uint64_t kernel_pml4_phys = 0;
static int vmm_initialized = 0;
static uint64_t mapped_page_count = 0;

/* External assembly functions */
extern void vmm_write_cr3(uint64_t pml4_phys);
extern uint64_t vmm_read_cr3(void);
extern void vmm_invlpg(uint64_t virt);

/* Index extraction macros */
#define PML4_INDEX(v) (((v) >> 39) & 0x1FF)
#define PDPT_INDEX(v) (((v) >> 30) & 0x1FF)
#define PD_INDEX(v)   (((v) >> 21) & 0x1FF)
#define PT_INDEX(v)   (((v) >> 12) & 0x1FF)

#define ALIGN_DOWN_4K(x) ((x) & ~(PAGE_SIZE_4K - 1))
#define ALIGN_UP_4K(x)   (((x) + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1))
#define IS_ALIGNED(x, a) (((x) & ((a) - 1)) == 0)

#define VMM_BOOT_MAP_MIN (64ULL * 1024 * 1024)
#define VMM_BOOT_MAP_MAX (256ULL * 1024 * 1024)

/* Physical to virtual conversion (identity mapped for now) */
static inline uint64_t phys_to_virt(uint64_t phys)
{
    /* Keep page table management addresses identity-mapped for stability. */
    return phys;
}

/* Virtual to physical conversion */
static inline uint64_t virt_to_phys(uint64_t virt)
{
    if (virt >= KERNEL_BASE)
        return virt - KERNEL_BASE;
    return virt;
}

/* Allocate a zeroed page table */
static uint64_t* alloc_table(void)
{
    uint64_t phys = pmm_alloc_page();
    
    if (!phys)
    {
        klog_error("VMM: Failed to allocate page table");
        return 0;
    }
    
    /* Use physical address directly (identity mapped) */
    uint64_t* table = (uint64_t*)phys_to_virt(phys);
    
    /* Zero the table */
    memset(table, 0, PAGE_SIZE_4K);
    
    return table;
}

/* Get or create a page table entry */
static uint64_t* get_or_create_entry(uint64_t* table, uint64_t index, int create)
{
    if (!table)
        return 0;

    if ((table[index] & VMM_PRESENT) && (table[index] & VMM_HUGE))
        return 0;
    
    if (!(table[index] & VMM_PRESENT))
    {
        if (!create)
            return 0;
        
        uint64_t* new_table = alloc_table();
        if (!new_table)
            return 0;
        
        /* Store physical address with present + write flags */
        table[index] = virt_to_phys((uint64_t)new_table) | VMM_PRESENT | VMM_WRITE;
    }
    
    /* Return pointer to next level table */
    return (uint64_t*)phys_to_virt(table[index] & VMM_ADDR_MASK);
}

static int vmm_map_1g(uint64_t virt, uint64_t phys, uint64_t flags)
{
    if (!kernel_pml4)
        return 0;
    if (!IS_ALIGNED(virt, PAGE_SIZE_1G) || !IS_ALIGNED(phys, PAGE_SIZE_1G))
        return 0;

    uint64_t* pdpt = get_or_create_entry(kernel_pml4, PML4_INDEX(virt), 1);
    if (!pdpt)
        return 0;

    uint64_t idx = PDPT_INDEX(virt);
    if (!(pdpt[idx] & VMM_PRESENT))
        mapped_page_count += (PAGE_SIZE_1G / PAGE_SIZE_4K);

    pdpt[idx] = (phys & VMM_ADDR_MASK) | (flags & ~VMM_HUGE) | VMM_PRESENT | VMM_HUGE;
    if (vmm_initialized)
        vmm_invlpg(virt);
    return 1;
}

static int vmm_map_2m(uint64_t virt, uint64_t phys, uint64_t flags)
{
    if (!kernel_pml4)
        return 0;
    if (!IS_ALIGNED(virt, PAGE_SIZE_2M) || !IS_ALIGNED(phys, PAGE_SIZE_2M))
        return 0;

    uint64_t* pdpt = get_or_create_entry(kernel_pml4, PML4_INDEX(virt), 1);
    if (!pdpt)
        return 0;
    uint64_t* pd = get_or_create_entry(pdpt, PDPT_INDEX(virt), 1);
    if (!pd)
        return 0;

    uint64_t idx = PD_INDEX(virt);
    if (!(pd[idx] & VMM_PRESENT))
        mapped_page_count += (PAGE_SIZE_2M / PAGE_SIZE_4K);

    pd[idx] = (phys & VMM_ADDR_MASK) | (flags & ~VMM_HUGE) | VMM_PRESENT | VMM_HUGE;
    if (vmm_initialized)
        vmm_invlpg(virt);
    return 1;
}

/* Initialize VMM */
void vmm_init(void)
{
    /* Check if PMM is ready */
    if (!pmm_ready())
    {
        kernel_pml4_phys = vmm_read_cr3() & VMM_ADDR_MASK;
        kernel_pml4 = (uint64_t*)phys_to_virt(kernel_pml4_phys);
        vmm_initialized = 1;
        return;
    }

    klog_init_msg("Initializing VMM");
    
    /* Allocate PML4 */
    kernel_pml4 = alloc_table();
    if (!kernel_pml4)
    {
        klog_fail("VMM: Failed to allocate PML4");
        return;
    }
    
    kernel_pml4_phys = virt_to_phys((uint64_t)kernel_pml4);
    
    /* Map a stable low-memory identity window for bootstrap/runtime. */
    uint64_t total_phys = pmm_total();
    uint64_t map_size = total_phys;
    if (map_size < VMM_BOOT_MAP_MIN)
        map_size = VMM_BOOT_MAP_MIN;
    if (map_size > VMM_BOOT_MAP_MAX)
        map_size = VMM_BOOT_MAP_MAX;
    map_size = ALIGN_UP_4K(map_size);

    vmm_map_range(0, 0, map_size, VMM_KERNEL_RW);

    /* Preserve active framebuffer mapping (MMIO) across CR3 switch. */
    if (fb_ready())
    {
        struct fb_state *fb = fb_get_state();
        if (fb)
        {
            uint64_t fb_phys = ALIGN_DOWN_4K(fb->addr);
            uint64_t fb_size = (uint64_t)fb->pitch * (uint64_t)fb->height;
            uint64_t fb_off = fb->addr - fb_phys;
            uint64_t fb_total = ALIGN_UP_4K(fb_off + fb_size);
            uint64_t fb_flags = VMM_KERNEL_RW | VMM_PWT | VMM_PCD;
            vmm_map_range(fb_phys, fb_phys, fb_total, fb_flags);
        }
    }

    /* VGA text range is already covered by bootstrap identity mapping. */
    
    /* Load new page tables */
    vmm_write_cr3(kernel_pml4_phys);
    
    vmm_initialized = 1;
    
    klog_init_msg("VMM online");
}

/* Map a virtual address to a physical address */
void vmm_map(uint64_t virt, uint64_t phys, uint64_t flags)
{
    if (!kernel_pml4)
        return;

    virt = ALIGN_DOWN_4K(virt);
    phys = ALIGN_DOWN_4K(phys);
    
    /* Get/create each level of the page table */
    uint64_t* pdpt = get_or_create_entry(kernel_pml4, PML4_INDEX(virt), 1);
    if (!pdpt) return;
    
    uint64_t* pd = get_or_create_entry(pdpt, PDPT_INDEX(virt), 1);
    if (!pd) return;
    
    uint64_t* pt = get_or_create_entry(pd, PD_INDEX(virt), 1);
    if (!pt) return;
    
    /* Set the page table entry */
    uint64_t pt_index = PT_INDEX(virt);
    
    if (!(pt[pt_index] & VMM_PRESENT))
        mapped_page_count++;
    
    pt[pt_index] = (phys & VMM_ADDR_MASK) | flags | VMM_PRESENT;
    
    /* Invalidate TLB entry only after VMM is live on current CR3. */
    if (vmm_initialized)
        vmm_invlpg(virt);
}

/* Map a range of addresses */
void vmm_map_range(uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags)
{
    if (size == 0)
        return;

    uint64_t vstart = ALIGN_DOWN_4K(virt);
    uint64_t pstart = ALIGN_DOWN_4K(phys);
    uint64_t vend = ALIGN_UP_4K(virt + size);

    for (uint64_t v = vstart, p = pstart; v < vend;)
    {
        uint64_t remaining = vend - v;

        if (remaining >= PAGE_SIZE_1G && IS_ALIGNED(v, PAGE_SIZE_1G) && IS_ALIGNED(p, PAGE_SIZE_1G))
        {
            if (vmm_map_1g(v, p, flags))
            {
                v += PAGE_SIZE_1G;
                p += PAGE_SIZE_1G;
                continue;
            }
        }

        if (remaining >= PAGE_SIZE_2M && IS_ALIGNED(v, PAGE_SIZE_2M) && IS_ALIGNED(p, PAGE_SIZE_2M))
        {
            if (vmm_map_2m(v, p, flags))
            {
                v += PAGE_SIZE_2M;
                p += PAGE_SIZE_2M;
                continue;
            }
        }

        vmm_map(v, p, flags);
        v += PAGE_SIZE_4K;
        p += PAGE_SIZE_4K;
    }
}

/* Unmap a virtual address */
void vmm_unmap(uint64_t virt)
{
    if (!kernel_pml4)
        return;

    virt = ALIGN_DOWN_4K(virt);
    
    uint64_t* pdpt = get_or_create_entry(kernel_pml4, PML4_INDEX(virt), 0);
    if (!pdpt) return;
    
    uint64_t* pd = get_or_create_entry(pdpt, PDPT_INDEX(virt), 0);
    if (!pd) return;
    
    uint64_t* pt = get_or_create_entry(pd, PD_INDEX(virt), 0);
    if (!pt) return;
    
    uint64_t pt_index = PT_INDEX(virt);
    
    if (pt[pt_index] & VMM_PRESENT)
    {
        pt[pt_index] = 0;
        mapped_page_count--;
        vmm_invlpg(virt);
    }
}

/* Translate virtual address to physical */
uint64_t vmm_translate(uint64_t virt)
{
    if (!kernel_pml4)
    {
        if (virt < VMM_BOOT_MAP_MIN)
            return virt;
        return 0;
    }
    
    uint64_t* pdpt = get_or_create_entry(kernel_pml4, PML4_INDEX(virt), 0);
    if (!pdpt) return 0;
    
    if (pdpt[PDPT_INDEX(virt)] & VMM_HUGE)
    {
        uint64_t base = pdpt[PDPT_INDEX(virt)] & VMM_ADDR_MASK;
        return base | (virt & (PAGE_SIZE_1G - 1));
    }

    uint64_t* pd = get_or_create_entry(pdpt, PDPT_INDEX(virt), 0);
    if (!pd) return 0;

    if (pd[PD_INDEX(virt)] & VMM_HUGE)
    {
        uint64_t base = pd[PD_INDEX(virt)] & VMM_ADDR_MASK;
        return base | (virt & (PAGE_SIZE_2M - 1));
    }
    
    uint64_t* pt = get_or_create_entry(pd, PD_INDEX(virt), 0);
    if (!pt) return 0;
    
    uint64_t pt_index = PT_INDEX(virt);
    
    if (!(pt[pt_index] & VMM_PRESENT))
        return 0;
    
    return (pt[pt_index] & VMM_ADDR_MASK) | (virt & 0xFFF);
}

/* Switch to a different page table */
void vmm_switch(uint64_t pml4_phys)
{
    vmm_write_cr3(pml4_phys);
}

/* Get current CR3 */
uint64_t vmm_get_cr3(void)
{
    return vmm_read_cr3();
}

/* Check if VMM is initialized */
int vmm_ready(void)
{
    return vmm_initialized;
}

/* Get mapped page count */
uint64_t vmm_mapped_pages(void)
{
    return mapped_page_count;
}
