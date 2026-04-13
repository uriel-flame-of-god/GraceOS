# Memory Management in GraceOS

## Overview
GraceOS uses a simple but effective memory management system consisting of:
1. **Identity-mapped paging** for virtual memory
2. **Block-based kernel heap allocator** for dynamic memory
3. **Fixed 512MB memory limit** for simplicity

## Memory Layout

### Physical Memory Map
```
┌──────────────────────────────────────────┬──────────────┐
│ Region                                   │ Size         │
├──────────────────────────────────────────┼──────────────┤
│ 0x00000000 - 0x00000FFF  BIOS/IVT       │ 4 KB         │
│ 0x00001000 - 0x00001FFF  PML4 Table     │ 4 KB         │
│ 0x00002000 - 0x00002FFF  PDP Table      │ 4 KB         │
│ 0x00003000 - 0x00003FFF  PD Table       │ 4 KB         │
│ 0x00100000 - 0x001FFFFF  Kernel Code    │ ~1 MB        │
│ 0x00200000 - 0x003FFFFF  Kernel Heap    │ 2 MB         │
│ 0x00400000 - 0x1FFFFFFF  Free RAM       │ ~508 MB      │
│ 0x20000000+              Unmapped       │ -            │
└──────────────────────────────────────────┴──────────────┘
```

## Paging System

### Page Table Hierarchy
GraceOS uses x86_64 4-level paging with 2MB huge pages:

```
PML4 (Page Map Level 4)
  └─ PDPT (Page Directory Pointer Table)
       └─ PD (Page Directory)
            ├─ 2MB Page 0 (0x00000000 - 0x001FFFFF)
            ├─ 2MB Page 1 (0x00200000 - 0x003FFFFF)
            ├─ 2MB Page 2 (0x00400000 - 0x005FFFFF)
            └─ ...
                └─ 2MB Page 255 (0x1FE00000 - 0x1FFFFFFF)
```

### Page Table Setup (boot.asm)
```asm
; Clear page table area
mov edi, 0x1000
mov cr3, edi              ; Set page table base
xor eax, eax
mov ecx, 4096
rep stosd                 ; Zero 16KB (4 pages)

; PML4[0] -> PDPT at 0x2000
mov dword [0x1000], 0x2003

; PDPT[0] -> PD at 0x3000  
mov dword [0x2000], 0x3003

; Map 256 × 2MB pages (512MB total)
mov edi, 0x3000
mov ebx, 0               ; Physical base
mov ecx, 256             ; Page count

.map_loop:
    mov eax, ebx
    or eax, 0x83         ; Present | Write | Page Size
    mov [edi], eax
    mov dword [edi+4], 0
    
    add ebx, 0x200000    ; Next 2MB
    add edi, 8           ; Next PDE
    loop .map_loop
```

### Page Entry Format (2MB)
```
Bits 63-52: Available for OS use
Bits 51-21: Physical address [51:21]
Bits 20-13: Available
Bit  12:    PAT
Bit  11-9:  Available
Bit  8:     Global
Bit  7:     Page Size (1 = 2MB)
Bit  6:     Dirty
Bit  5:     Accessed
Bit  4:     Cache Disable
Bit  3:     Write Through
Bit  2:     User/Supervisor (0 = kernel only)
Bit  1:     Read/Write (1 = writable)
Bit  0:     Present (1 = valid)
```

Our flags: `0x83 = 1000 0011b`
- Present: Yes
- Writable: Yes
- User: No (kernel only)
- Page Size: Yes (2MB)

### Identity Mapping
Virtual Address = Physical Address

Example:
```c
void* ptr = (void*)0x500000;  // Virtual address
// Points directly to physical 0x500000
// No translation needed
```

Benefits:
- Simple to understand
- No TLB misses for kernel code
- Direct hardware access
- Fast pointer arithmetic

Drawbacks:
- No memory protection
- No address space isolation
- Cannot relocate kernel
- Fixed 512MB limit

## Kernel Heap Allocator

### Design: Block Allocator
Location: `kernel/kheap/kheap.c`

The heap uses a first-fit explicit free list allocator with boundary tags.

### Block Structure
```c
struct kheap_block {
    uint64_t size;      // Block size (including header)
    uint64_t magic;     // Magic number (0xDEADBEEF)
    uint8_t free;       // 1 = free, 0 = allocated
    uint8_t padding[7]; // Align to 24 bytes
    // Payload follows here
};
```

### Memory Organization
```
Heap Start (0x200000)
│
├─ [Block Header 1] (24 bytes)
│  ├─ size = 1024
│  ├─ magic = 0xDEADBEEF  
│  ├─ free = 0
│  └─ padding
├─ [Payload 1] (1000 bytes)
│
├─ [Block Header 2] (24 bytes)
│  ├─ size = 2048
│  ├─ magic = 0xDEADBEEF
│  ├─ free = 1
│  └─ padding
├─ [Payload 2] (2024 bytes)
│
└─ ... more blocks ...
    │
    Heap End (0x400000)
```

### Initialization
```c
void kheap_init(uint64_t heap_start, uint64_t heap_size)
{
    heap_base = heap_start;
    heap_limit = heap_start + heap_size;
    
    // Create initial free block
    struct kheap_block* initial = (struct kheap_block*)heap_start;
    initial->size = heap_size;
    initial->magic = KHEAP_MAGIC;
    initial->free = 1;
    
    free_list = initial;
}
```

### Allocation Algorithm (kmalloc)

**Strategy**: First-fit

```c
void* kmalloc(uint64_t size)
{
    // 1. Align size to 8 bytes
    size = ALIGN(size, 8);
    
    // 2. Total needed = header + payload
    uint64_t needed = sizeof(struct kheap_block) + size;
    
    // 3. Search free list
    for (block in free_list) {
        if (block->size >= needed) {
            // Found suitable block
            
            // 4. Split if remainder > 32 bytes
            if (block->size - needed > 32) {
                split_block(block, needed);
            }
            
            // 5. Mark as allocated
            block->free = 0;
            remove_from_free_list(block);
            
            // 6. Return payload pointer
            return (void*)((uint64_t)block + sizeof(struct kheap_block));
        }
    }
    
    // 7. Out of memory
    return NULL;
}
```

### Block Splitting
```c
void split_block(struct kheap_block* block, uint64_t size)
{
    // Create new block after allocated region
    struct kheap_block* new_block = 
        (struct kheap_block*)((uint64_t)block + size);
    
    new_block->size = block->size - size;
    new_block->magic = KHEAP_MAGIC;
    new_block->free = 1;
    
    // Update original block
    block->size = size;
    
    // Add new block to free list
    add_to_free_list(new_block);
}
```

### Deallocation Algorithm (kfree)

**Strategy**: Immediate coalescing

```c
void kfree(void* ptr)
{
    // 1. Get block header
    struct kheap_block* block = 
        (struct kheap_block*)((uint64_t)ptr - sizeof(struct kheap_block));
    
    // 2. Validate magic number
    if (block->magic != KHEAP_MAGIC) {
        return; // Corrupted block
    }
    
    // 3. Mark as free
    block->free = 1;
    
    // 4. Coalesce with next block if free
    struct kheap_block* next = get_next_block(block);
    if (next && next->free) {
        block->size += next->size;
        remove_from_free_list(next);
    }
    
    // 5. Coalesce with previous block if free
    struct kheap_block* prev = get_prev_block(block);
    if (prev && prev->free) {
        prev->size += block->size;
        block = prev;
    }
    
    // 6. Add to free list
    add_to_free_list(block);
}
```

### Free List Management
```c
// Free list is a linked list embedded in blocks
struct kheap_block {
    // ... existing fields ...
    struct kheap_block* next_free;  // Only valid if free==1
    struct kheap_block* prev_free;
};

void add_to_free_list(struct kheap_block* block)
{
    // Insert at head of list
    block->next_free = free_list;
    block->prev_free = NULL;
    
    if (free_list) {
        free_list->prev_free = block;
    }
    
    free_list = block;
}
```

## Memory Allocation Examples

### Small Allocation (16 bytes)
```c
char* str = kmalloc(16);
strcpy(str, "Hello, World!");
kfree(str);
```

Heap state:
```
Before kmalloc(16):
  [Free: 2MB]

After kmalloc(16):
  [Allocated: 40 bytes (24 header + 16 payload)]
  [Free: 2MB - 40 bytes]

After kfree(str):
  [Free: 2MB] (coalesced back)
```

### Large Allocation (1MB)
```c
void* disk = kmalloc(0x100000);  // 1MB for BFS
// Use disk buffer
kfree(disk);
```

Heap state:
```
Before kmalloc(1MB):
  [Free: 2MB]

After kmalloc(1MB):
  [Allocated: 1MB + 24 bytes]
  [Free: ~1MB]

After kfree(disk):
  [Free: 2MB] (coalesced)
```

### Multiple Allocations
```c
void* a = kmalloc(100);  // Header at 0x200000
void* b = kmalloc(200);  // Header at 0x200078
void* c = kmalloc(300);  // Header at 0x200140
kfree(b);                // Create hole
void* d = kmalloc(150);  // Reuse hole
```

## Memory Debugging

### Heap Statistics
```c
void kheap_stats(void)
{
    uint64_t total_free = 0;
    uint64_t total_allocated = 0;
    uint64_t block_count = 0;
    
    struct kheap_block* block = (struct kheap_block*)heap_base;
    
    while ((uint64_t)block < heap_limit) {
        if (block->magic != KHEAP_MAGIC) break;
        
        if (block->free) {
            total_free += block->size;
        } else {
            total_allocated += block->size;
        }
        
        block_count++;
        block = get_next_block(block);
    }
    
    tty_print("Heap Statistics:\n");
    tty_print("  Total Free: ");
    tty_print_hex(total_free);
    tty_print("\n  Total Allocated: ");
    tty_print_hex(total_allocated);
    tty_print("\n  Blocks: ");
    tty_print_hex(block_count);
    tty_print("\n");
}
```

### Corruption Detection
```c
int kheap_validate(void)
{
    struct kheap_block* block = (struct kheap_block*)heap_base;
    
    while ((uint64_t)block < heap_limit) {
        // Check magic number
        if (block->magic != KHEAP_MAGIC) {
            tty_print("ERROR: Corrupted block at ");
            tty_print_hex((uint64_t)block);
            return 0;
        }
        
        // Check alignment
        if ((uint64_t)block & 7) {
            tty_print("ERROR: Misaligned block at ");
            tty_print_hex((uint64_t)block);
            return 0;
        }
        
        block = get_next_block(block);
    }
    
    return 1;
}
```

## Memory Limitations

### Hard Limits
- **Total Memory**: 512MB (hardware limit in page tables)
- **Heap Size**: 2MB (can be increased by changing init call)
- **Max Allocation**: 2MB (limited by heap size)

### Fragmentation
The allocator can suffer from external fragmentation over time:

```
After many allocations/deallocations:
  [Allocated: 100B] [Free: 50B] [Allocated: 200B] [Free: 30B] ...
  
Total free: 500B
But: Cannot allocate 500B contiguously
```

Mitigation:
- Immediate coalescing on free
- First-fit policy reduces fragmentation vs. best-fit
- 2MB heap is large enough for kernel needs

### No Guard Pages
The allocator doesn't use guard pages or canaries:
- Buffer overruns can corrupt heap
- No detection until next allocation/free
- Use careful bounds checking in code

## Future Enhancements

### Slab Allocator
For small, fixed-size allocations:
```c
struct kmem_cache* task_cache = 
    kmem_cache_create(sizeof(struct task), 16);
struct task* t = kmem_cache_alloc(task_cache);
```

### Buddy Allocator
For efficient power-of-2 allocations:
- Reduces fragmentation
- Fast allocation/deallocation
- Good for page frame allocation

### Virtual Memory Manager
When adding user processes:
- Per-process page tables
- Copy-on-write
- Demand paging
- Swap space

### Memory Protection
- Guard pages
- Red zones
- Canaries
- ASLR (Address Space Layout Randomization)
