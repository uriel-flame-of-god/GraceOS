# Debugging GraceOS

## Overview
This guide covers debugging techniques, common issues, and troubleshooting steps for GraceOS kernel development.

## Debugging Tools

### QEMU Debug Options

#### Basic Debug Output
```cmd
qemu-system-x86_64 -cdrom graceos.iso -m 512M -d int,cpu_reset -D debug.log
```

Options:
- `-d int` - Log interrupts
- `-d cpu_reset` - Log CPU resets
- `-d int,cpu_reset` - Log both
- `-D debug.log` - Write to file instead of stderr

#### Full CPU State Dumps
```cmd
qemu-system-x86_64 -cdrom graceos.iso -m 512M -d int,cpu_reset,in_asm -D debug.log
```

Additional options:
- `-d in_asm` - Log executed instructions
- `-d exec` - Log every basic block
- `-d guest_errors` - Log guest errors

#### GDB Debugging
```cmd
# Terminal 1: Start QEMU with GDB server
qemu-system-x86_64 -cdrom graceos.iso -m 512M -s -S

# Terminal 2: Connect GDB
gdb build/kernel.elf
(gdb) target remote :1234
(gdb) break kmain
(gdb) continue
```

QEMU GDB options:
- `-s` - Start GDB server on port 1234
- `-S` - Freeze CPU at startup (wait for GDB)

### Disassembly Tools

#### Disassemble Kernel
```cmd
tools\clang\bin\llvm-objdump.exe -d build\kernel.elf > kernel.asm
```

#### View Specific Function
```cmd
tools\clang\bin\llvm-objdump.exe -d build\kernel.elf | Select-String -Pattern "kmain" -Context 20
```

#### Check Section Sizes
```cmd
tools\clang\bin\llvm-size.exe build\kernel.elf
```

#### List Symbols
```cmd
tools\clang\bin\llvm-nm.exe build\kernel.elf
```

### Memory Inspection

#### Dump Memory Region
```cmd
# In QEMU monitor (Ctrl+Alt+2)
(qemu) x /100x 0x200000    # Dump 100 bytes at heap base
(qemu) x /10i 0x100000     # Disassemble 10 instructions
```

#### Check Registers
```cmd
(qemu) info registers
(qemu) info registers -a   # All registers including segment
```

## Common Issues

### 1. Kernel Won't Boot

#### Symptom
QEMU shows blank screen or "Boot failed"

#### Diagnosis
```cmd
# Check Multiboot2 header
tools\clang\bin\llvm-objdump.exe -s build\kernel.elf | Select-String "E85250D6"
```

Should show: `E8 52 50 D6` (Multiboot2 magic)

#### Causes & Fixes

**Cause**: Multiboot header not in first 8KB
```ld
# In linker.ld, ensure .multiboot is first:
SECTIONS {
    . = 1M;
    .text : {
        *(.multiboot)    # <-- Must be first!
        *(.text)
    }
}
```

**Cause**: Wrong entry point
```
# Check entry point
tools\clang\bin\llvm-readelf.exe -h build\kernel.elf

Should show: Entry point address: 0x100000 (or similar)
```

### 2. Triple Fault / Reset Loop

#### Symptom
QEMU constantly resets, debug.log shows CPU reset

#### Diagnosis
```cmd
qemu-system-x86_64 -cdrom graceos.iso -m 512M -d int,cpu_reset -D debug.log

# Look for:
check_exception old: 0xffffffff new 0xe  # Page fault
check_exception old: 0xffffffff new 0xd  # General protection
```

#### Common Causes

**Cause**: Invalid IDT entry
```c
// In idt.c, ensure all entries are valid:
idt_set_gate(i, (uint64_t)isr_stub_table[i], 0x08, 0x8E);
//                                           ^^^^  ^^^^
//                                        Code seg  Type
```

**Cause**: Stack overflow
```ld
# In linker.ld, increase stack size:
.stack : {
    . = ALIGN(16);
    . += 0x8000;    # 32KB instead of 16KB
    stack_top = .;
}
```

**Cause**: Unmapped memory access
```c
// Check if address is within mapped range
if (addr >= 0x20000000) {
    tty_print("ERROR: Access beyond 512MB!\n");
    return;
}
```

### 3. INVALID OPCODE Exception (INT 0x06)

#### Symptom
```
Servicing hardware INT=0x0e
check_exception old: 0xffffffff new 0x6
```

#### Diagnosis
```cmd
# Find instruction at crash address (e.g., 0x104f04)
tools\clang\bin\llvm-objdump.exe -d build\kernel.elf | Select-String "104f04"

# If you see SSE instructions (xorps, movups):
  104f04: 0f 57 c0    xorps %xmm0, %xmm0
```

#### Cause & Fix
SSE not enabled in CPU

**Fix**: Already implemented in boot.asm:
```asm
; Enable SSE
mov rax, cr0
and ax, 0xFFFB          ; Clear CR0.EM
or ax, 0x2              ; Set CR0.MP
mov cr0, rax

mov rax, cr4
or ax, 3 shl 9          ; Set CR4.OSFXSR and CR4.OSXMMEXCPT
mov cr4, rax
```

### 4. Page Fault (INT 0x0E)

#### Symptom
```
check_exception old: 0xffffffff new 0xe
CR2=0x12345678  # Faulting address
```

#### Diagnosis
```cmd
# Check if address is mapped
# Mapped range: 0x00000000 - 0x1FFFFFFF (512MB)

# If CR2 shows address > 0x20000000:
# - Access beyond mapped memory
```

#### Causes & Fixes

**Cause**: Access beyond 512MB
```c
// Add bounds check
if (ptr >= (void*)0x20000000) {
    tty_print("ERROR: Pointer beyond limit\n");
    return;
}
```

**Cause**: Null pointer dereference
```c
// Check CR2 in debug.log
CR2=0x00000000  # Null pointer

// Fix: Add null checks
if (ptr == NULL) {
    return -1;
}
```

### 5. Heap Corruption

#### Symptom
```
kmalloc returns NULL
kfree crashes
Random data corruption
```

#### Diagnosis
```c
// Add heap validation
if (!kheap_validate()) {
    tty_print("HEAP CORRUPTED!\n");
    // Print all blocks
    kheap_dump();
}
```

#### Debug Functions
```c
void kheap_dump(void)
{
    struct kheap_block* block = (struct kheap_block*)heap_base;
    int i = 0;
    
    while ((uint64_t)block < heap_limit) {
        if (block->magic != KHEAP_MAGIC) {
            tty_print("Block ");
            tty_print_hex(i);
            tty_print(" CORRUPTED at ");
            tty_print_hex((uint64_t)block);
            tty_print("\n");
            break;
        }
        
        tty_print("Block ");
        tty_print_hex(i);
        tty_print(": size=");
        tty_print_hex(block->size);
        tty_print(" free=");
        tty_print_hex(block->free);
        tty_print("\n");
        
        block = (struct kheap_block*)((uint64_t)block + block->size);
        i++;
    }
}
```

#### Common Causes

**Buffer overflow**:
```c
// Wrong:
char* buf = kmalloc(10);
strcpy(buf, "This is too long!");  // Overflows into next block

// Right:
char* buf = kmalloc(20);
strncpy(buf, str, 19);
buf[19] = '\0';
```

**Double free**:
```c
// Wrong:
kfree(ptr);
kfree(ptr);  // Corrupts heap

// Right:
kfree(ptr);
ptr = NULL;
```

**Use after free**:
```c
// Wrong:
kfree(ptr);
ptr[0] = 'x';  // Use after free

// Right:
kfree(ptr);
ptr = NULL;
// Don't use ptr
```

### 6. Misaligned Access

#### Symptom
```
[BFS] WARNING: Misaligned page address 0x200001
```

#### Diagnosis
```c
void check_alignment(void* ptr, size_t align)
{
    if (((uint64_t)ptr & (align - 1)) != 0) {
        tty_print("ERROR: Misaligned pointer ");
        tty_print_hex((uint64_t)ptr);
        tty_print(" (expected ");
        tty_print_hex(align);
        tty_print("-byte alignment)\n");
    }
}
```

#### Causes & Fixes

**Cause**: Using `__attribute__((packed))`
```c
// Wrong:
struct foo {
    uint32_t a;
    uint64_t b;  // Misaligned!
} __attribute__((packed));

// Right:
struct foo {
    uint32_t a;
    uint64_t b;
} __attribute__((aligned(8)));
```

**Cause**: Manual pointer arithmetic
```c
// Wrong:
uint8_t* ptr = (uint8_t*)base + 1;
uint64_t* val = (uint64_t*)ptr;  // Misaligned!

// Right:
uint64_t* val = (uint64_t*)(base + 8);  // Aligned to 8
```

## Debugging Techniques

### 1. Strategic Print Debugging

#### Early Boot Messages
```c
void kmain(void)
{
    tty_initialize();
    tty_print("=== BOOT START ===\n");
    
    tty_print("Step 1: Multiboot\n");
    // ...
    
    tty_print("Step 2: IDT\n");
    // ...
    
    tty_print("Step 3: Heap\n");
    // ...
    
    tty_print("=== BOOT COMPLETE ===\n");
}
```

#### Hex Printing
```c
tty_print("Value: ");
tty_print_hex(value);
tty_print("\n");
```

#### Memory Dumps
```c
void hexdump(void* addr, size_t len)
{
    uint8_t* p = (uint8_t*)addr;
    
    for (size_t i = 0; i < len; i++) {
        if (i % 16 == 0) {
            tty_print("\n");
            tty_print_hex((uint64_t)(p + i));
            tty_print(": ");
        }
        
        tty_print_hex(p[i]);
        tty_print(" ");
    }
    
    tty_print("\n");
}
```

### 2. Assertions

```c
#define ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            tty_print("ASSERTION FAILED: " msg "\n"); \
            tty_print("  File: " __FILE__ "\n"); \
            tty_print("  Line: "); \
            tty_print_hex(__LINE__); \
            tty_print("\n"); \
            while(1) { __asm__ volatile("cli; hlt"); } \
        } \
    } while(0)

// Usage:
ASSERT(ptr != NULL, "Null pointer");
ASSERT(size > 0, "Invalid size");
```

### 3. Watchpoints

Using QEMU monitor:
```
(qemu) watch 0x200000 4   # Watch 4 bytes at heap base
```

### 4. Instruction Tracing

```cmd
qemu-system-x86_64 -cdrom graceos.iso -m 512M -d in_asm,int -D trace.log
```

Warning: Creates huge log files!

### 5. Memory Corruption Detection

```c
#define CANARY_VALUE 0xDEADBEEFCAFEBABE

struct guarded_allocation {
    uint64_t canary_before;
    uint8_t data[SIZE];
    uint64_t canary_after;
};

void check_canary(struct guarded_allocation* ga)
{
    if (ga->canary_before != CANARY_VALUE) {
        tty_print("UNDERFLOW detected!\n");
    }
    if (ga->canary_after != CANARY_VALUE) {
        tty_print("OVERFLOW detected!\n");
    }
}
```

## Performance Profiling

### Timing Measurements

Using RDTSC (Read Time-Stamp Counter):
```c
static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void profile_function(void)
{
    uint64_t start = rdtsc();
    
    // Function to profile
    my_function();
    
    uint64_t end = rdtsc();
    uint64_t cycles = end - start;
    
    tty_print("Cycles: ");
    tty_print_hex(cycles);
    tty_print("\n");
}
```

## Testing Strategies

### Unit Testing

```c
void test_heap(void)
{
    tty_print("=== HEAP TESTS ===\n");
    
    // Test 1: Small allocation
    void* p1 = kmalloc(16);
    ASSERT(p1 != NULL, "Small alloc failed");
    kfree(p1);
    
    // Test 2: Large allocation
    void* p2 = kmalloc(0x100000);  // 1MB
    ASSERT(p2 != NULL, "Large alloc failed");
    kfree(p2);
    
    // Test 3: Multiple allocations
    void* p3 = kmalloc(100);
    void* p4 = kmalloc(200);
    void* p5 = kmalloc(300);
    ASSERT(p3 != NULL && p4 != NULL && p5 != NULL, "Multi alloc failed");
    kfree(p4);
    kfree(p3);
    kfree(p5);
    
    tty_print("=== ALL TESTS PASSED ===\n");
}
```

### Integration Testing

```c
void test_bfs(void)
{
    tty_print("=== BFS TESTS ===\n");
    
    // Setup
    void* disk = kmalloc(0x100000);
    struct bfs_instance bfs;
    bfs_init(&bfs, (uint64_t)disk, 0x100000);
    bfs_format(&bfs);
    
    // Test insert
    bfs_insert(&bfs, 100, 200);
    
    // Test search
    struct bfs_search_result r = bfs_search(&bfs, 100);
    ASSERT(r.found == 1, "Search failed");
    ASSERT(r.value == 200, "Wrong value");
    
    // Cleanup
    kfree(disk);
    
    tty_print("=== BFS TESTS PASSED ===\n");
}
```

## Debugging Checklist

Before reporting a bug:
- [ ] Clean rebuild (`make clean && make`)
- [ ] Check compiler warnings
- [ ] Run with debug output (`-d int,cpu_reset`)
- [ ] Check memory bounds
- [ ] Validate all pointers
- [ ] Check for null dereferences
- [ ] Verify alignment
- [ ] Check heap integrity
- [ ] Disassemble crash address
- [ ] Try GDB debugging
- [ ] Add strategic print statements
- [ ] Check QEMU version compatibility

## Reference: x86_64 Exception Codes

```
0x00 - Divide Error
0x01 - Debug
0x02 - NMI
0x03 - Breakpoint
0x04 - Overflow
0x05 - Bound Range Exceeded
0x06 - Invalid Opcode
0x07 - Device Not Available
0x08 - Double Fault
0x09 - Coprocessor Segment Overrun
0x0A - Invalid TSS
0x0B - Segment Not Present
0x0C - Stack-Segment Fault
0x0D - General Protection
0x0E - Page Fault
0x10 - x87 FPU Error
0x11 - Alignment Check
0x12 - Machine Check
0x13 - SIMD Floating-Point
```

## Additional Resources

- [OSDev Wiki](https://wiki.osdev.org/)
- [Intel SDM](https://software.intel.com/content/www/us/en/develop/articles/intel-sdm.html)
- [AMD APM](https://developer.amd.com/resources/developer-guides-manuals/)
- [QEMU Documentation](https://www.qemu.org/docs/master/)
