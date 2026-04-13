// ============================
// GraceOS Program Loader (exec)
// ELF loading and process setup
// ============================

#include "proc.h"
#include "sched.h"
#include "context.h"
#include "../mm/kheap.h"
#include "../mm/vmm/vmm.h"
#include "../mm/sasy/sasy.h"
#include "../log/klog.h"
#include "../../drivers/video/tty.h"
#include "../../lib/libc/string.h"
#include "../../drivers/storage/bfs.h"

/* ============================
   ELF Header Structures
   ============================ */

#define ELF_MAGIC       0x464C457F  /* "\x7FELF" */

#define ET_EXEC         2           /* Executable file */
#define ET_DYN          3           /* Shared object (PIE) */

#define PT_NULL         0
#define PT_LOAD         1
#define PT_DYNAMIC      2
#define PT_INTERP       3
#define PT_NOTE         4
#define PT_PHDR         6

#define PF_X            0x1         /* Execute */
#define PF_W            0x2         /* Write */
#define PF_R            0x4         /* Read */

/* ELF64 Header */
typedef struct {
    uint32_t e_magic;
    uint8_t  e_class;       /* 1 = 32-bit, 2 = 64-bit */
    uint8_t  e_data;        /* 1 = LE, 2 = BE */
    uint8_t  e_version;
    uint8_t  e_osabi;
    uint8_t  e_abiversion;
    uint8_t  e_pad[7];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version2;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf64_hdr_t;

/* ELF64 Program Header */
typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr_t;

/* ============================
   ELF Image Info
   ============================ */

typedef struct {
    uint64_t entry;         /* Entry point */
    uint64_t load_base;     /* Lowest load address */
    uint64_t load_end;      /* Highest load address */
    uint64_t bss_start;     /* BSS start */
    uint64_t bss_end;       /* BSS end */
} elf_image_t;

/* ============================
   User Memory Layout
   ============================ */

#define USER_STACK_TOP      0x00007FFFFFFFF000ULL
#define USER_STACK_SIZE     0x100000            /* 1MB stack */
#define USER_HEAP_START     0x0000100000000000ULL
#define USER_CODE_BASE      0x0000000000400000ULL

/* ============================
   ELF Validation
   ============================ */

static int elf_validate(elf64_hdr_t* hdr)
{
    if (hdr->e_magic != ELF_MAGIC)
    {
        klog_error("exec: Invalid ELF magic");
        return -1;
    }
    
    if (hdr->e_class != 2)  /* 64-bit */
    {
        klog_error("exec: Not 64-bit ELF");
        return -1;
    }
    
    if (hdr->e_data != 1)   /* Little endian */
    {
        klog_error("exec: Not little-endian ELF");
        return -1;
    }
    
    if (hdr->e_type != ET_EXEC && hdr->e_type != ET_DYN)
    {
        klog_error("exec: Not executable ELF");
        return -1;
    }
    
    if (hdr->e_machine != 0x3E)  /* x86_64 */
    {
        klog_error("exec: Not x86_64 ELF");
        return -1;
    }
    
    return 0;
}

/* ============================
   Load ELF from File
   ============================ */

static int elf_load(const char* path, uint8_t* file_data, uint64_t file_size, elf_image_t* img)
{
    if (!file_data || !img || file_size < sizeof(elf64_hdr_t))
        return -1;
    
    elf64_hdr_t* hdr = (elf64_hdr_t*)file_data;
    
    /* Validate ELF */
    if (elf_validate(hdr) < 0)
        return -1;
    
    /* Initialize image info */
    img->entry = hdr->e_entry;
    img->load_base = UINT64_MAX;
    img->load_end = 0;
    img->bss_start = 0;
    img->bss_end = 0;
    
    /* Process program headers */
    elf64_phdr_t* phdrs = (elf64_phdr_t*)(file_data + hdr->e_phoff);
    
    for (uint16_t i = 0; i < hdr->e_phnum; i++)
    {
        elf64_phdr_t* ph = &phdrs[i];
        
        if (ph->p_type != PT_LOAD)
            continue;
        
        /* Track memory range */
        if (ph->p_vaddr < img->load_base)
            img->load_base = ph->p_vaddr;
        
        uint64_t seg_end = ph->p_vaddr + ph->p_memsz;
        if (seg_end > img->load_end)
            img->load_end = seg_end;
        
        /* Track BSS (memsz > filesz) */
        if (ph->p_memsz > ph->p_filesz)
        {
            uint64_t bss = ph->p_vaddr + ph->p_filesz;
            if (img->bss_start == 0 || bss < img->bss_start)
                img->bss_start = bss;
            if (seg_end > img->bss_end)
                img->bss_end = seg_end;
        }
    }
    
    (void)path;
    return 0;
}

/* ============================
   Setup User Stack
   
   Stack layout (top to bottom):
   - envp strings
   - argv strings
   - NULL (envp terminator)
   - envp pointers
   - NULL (argv terminator)
   - argv pointers
   - argc
   ============================ */

static uint64_t setup_user_stack(process_t* p, char** argv, char** envp)
{
    (void)envp;
    
    /* Calculate stack size needed */
    int argc = 0;
    uint64_t strings_size = 0;
    
    if (argv)
    {
        while (argv[argc])
        {
            strings_size += strlen(argv[argc]) + 1;
            argc++;
        }
    }
    (void)strings_size;
    
    /* Allocate stack segment via SASY */
    seg_handle_t stack_seg = sasy_create_stack(p->pid, USER_STACK_SIZE);
    if (stack_seg == INVALID_HANDLE)
    {
        klog_error("exec: Failed to create stack segment");
        return 0;
    }
    
    /* Store segment handle */
    p->segments[0] = stack_seg;
    
    /* Get stack memory */
    void* stack_mem = sasy_lock(stack_seg);
    if (!stack_mem)
    {
        klog_error("exec: Failed to lock stack segment");
        return 0;
    }
    
    /* Setup stack (simplified) */
    uint64_t sp = USER_STACK_TOP - 8;
    
    /* Write argc at top of stack */
    sp -= 8;
    *((uint64_t*)((uint8_t*)stack_mem + USER_STACK_SIZE - 8)) = (uint64_t)argc;
    
    sasy_unlock(stack_seg);
    
    p->stack_base = USER_STACK_TOP - USER_STACK_SIZE;
    p->stack_size = USER_STACK_SIZE;
    
    return sp;
}

/* ============================
   Setup SASY Segments
   ============================ */

static int sasy_setup_segments(process_t* p, elf_image_t* img, uint8_t* file_data)
{
    (void)file_data;
    
    /* Create code segment */
    uint64_t code_size = img->load_end - img->load_base;
    seg_handle_t code_seg = sasy_create_ex(
        code_size,
        SEG_CODE,
        SEG_FLAGS_USER_RX,
        p->pid
    );
    
    if (code_seg == INVALID_HANDLE)
    {
        klog_error("exec: Failed to create code segment");
        return -1;
    }
    
    p->segments[1] = code_seg;
    
    /* Create data segment (heap) */
    seg_handle_t heap_seg = sasy_create_heap(p->pid, 0x10000);  /* 64KB initial heap */
    if (heap_seg != INVALID_HANDLE)
    {
        p->segments[2] = heap_seg;
    }
    
    return 0;
}

/* ============================
   CMDT Runtime (Intel-HEX-like)
   ============================ */

#define CMDT_MAX_SOURCE    8192
#define CMDT_MAX_VARS      64
#define CMDT_MAX_NAME_LEN  32
#define CMDT_MAX_LINE      256

typedef struct {
    int used;
    char name[CMDT_MAX_NAME_LEN];
    int value;
} cmdt_var_t;

typedef struct {
    cmdt_var_t vars[CMDT_MAX_VARS];
} cmdt_vm_state_t;

static int str_ends_with(const char* s, const char* suffix)
{
    size_t sl = strlen(s);
    size_t tl = strlen(suffix);
    if (sl < tl)
        return 0;
    return strcmp(s + sl - tl, suffix) == 0;
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_hex_byte(const char* p, uint8_t* out)
{
    int hi = hex_val(p[0]);
    int lo = hex_val(p[1]);
    if (hi < 0 || lo < 0)
        return -1;
    *out = (uint8_t)((hi << 4) | lo);
    return 0;
}

static int cmdt_decode_source(const char* text, uint64_t size, char* out_src, uint64_t out_cap)
{
    uint32_t upper = 0;
    uint64_t max_written = 0;
    uint64_t i = 0;

    memset(out_src, 0, (size_t)out_cap);

    while (i < size)
    {
        while (i < size && (text[i] == '\r' || text[i] == '\n'))
            i++;
        if (i >= size)
            break;

        if (text[i] != ':')
        {
            klog_error("cmdt: invalid record prefix");
            return -1;
        }

        uint64_t line_start = i;
        while (i < size && text[i] != '\n')
            i++;
        uint64_t line_len = i - line_start;
        const char* line = text + line_start;

        /* Minimum record length: :LLAAAATTCC */
        if (line_len < 11)
        {
            klog_error("cmdt: short record");
            return -1;
        }

        uint8_t count = 0, addr_hi = 0, addr_lo = 0, rectype = 0;
        if (parse_hex_byte(line + 1, &count) < 0 ||
            parse_hex_byte(line + 3, &addr_hi) < 0 ||
            parse_hex_byte(line + 5, &addr_lo) < 0 ||
            parse_hex_byte(line + 7, &rectype) < 0)
        {
            klog_error("cmdt: invalid header hex");
            return -1;
        }

        uint64_t expected = (uint64_t)(11 + (uint64_t)count * 2);
        if (line_len < expected)
        {
            klog_error("cmdt: truncated record");
            return -1;
        }

        uint8_t sum = 0;
        sum = (uint8_t)(sum + count + addr_hi + addr_lo + rectype);

        uint8_t data[255];
        for (uint8_t d = 0; d < count; d++)
        {
            if (parse_hex_byte(line + 9 + d * 2, &data[d]) < 0)
            {
                klog_error("cmdt: invalid data hex");
                return -1;
            }
            sum = (uint8_t)(sum + data[d]);
        }

        uint8_t chk = 0;
        if (parse_hex_byte(line + 9 + count * 2, &chk) < 0)
        {
            klog_error("cmdt: invalid checksum field");
            return -1;
        }
        sum = (uint8_t)(sum + chk);
        if (sum != 0)
        {
            klog_error("cmdt: checksum mismatch");
            return -1;
        }

        uint32_t addr = (uint32_t)(((uint32_t)addr_hi << 8) | (uint32_t)addr_lo);
        uint32_t abs_addr = upper + addr;

        if (rectype == 0x00)
        {
            if ((uint64_t)abs_addr + count >= out_cap)
            {
                klog_error("cmdt: decoded image too large");
                return -1;
            }

            memcpy(out_src + abs_addr, data, count);
            if ((uint64_t)abs_addr + count > max_written)
                max_written = (uint64_t)abs_addr + count;
        }
        else if (rectype == 0x01)
        {
            break;
        }
        else if (rectype == 0x04)
        {
            if (count != 2)
            {
                klog_error("cmdt: bad extended linear record");
                return -1;
            }
            upper = (uint32_t)((((uint32_t)data[0] << 8) | (uint32_t)data[1]) << 16);
        }
        else
        {
            /* Unsupported record types are ignored to stay Intel-HEX friendly. */
        }
    }

    if (max_written >= out_cap)
        return -1;
    out_src[max_written] = '\0';
    return 0;
}

static int vm_is_space(char c)
{
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

static const char* vm_skip_ws(const char* p)
{
    while (*p && vm_is_space(*p)) p++;
    return p;
}

static int vm_ident_start(char c)
{
    return (c == '_') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int vm_ident_char(char c)
{
    return vm_ident_start(c) || (c >= '0' && c <= '9');
}

static int vm_parse_ident(const char** pp, char* out, int out_len)
{
    const char* p = vm_skip_ws(*pp);
    int n = 0;
    if (!vm_ident_start(*p))
        return 0;
    while (*p && vm_ident_char(*p))
    {
        if (n >= out_len - 1)
            return 0;
        out[n++] = *p++;
    }
    out[n] = '\0';
    *pp = p;
    return 1;
}

static int vm_parse_int(const char** pp, int* out)
{
    const char* p = vm_skip_ws(*pp);
    int sign = 1;
    int v = 0;
    int has_digit = 0;

    if (*p == '-') { sign = -1; p++; }
    while (*p >= '0' && *p <= '9')
    {
        has_digit = 1;
        v = v * 10 + (*p - '0');
        p++;
    }
    if (!has_digit)
        return 0;
    *out = sign * v;
    *pp = p;
    return 1;
}

static int vm_parse_string(const char** pp, char* out, int out_len)
{
    const char* p = vm_skip_ws(*pp);
    int n = 0;

    if (*p != '"')
        return 0;
    p++;

    while (*p && *p != '"')
    {
        char c = *p++;
        if (c == '\\')
        {
            if (*p == 'n') { c = '\n'; p++; }
            else if (*p == 't') { c = '\t'; p++; }
            else if (*p == 'r') { c = '\r'; p++; }
            else if (*p == '\\') { c = '\\'; p++; }
            else if (*p == '"') { c = '"'; p++; }
        }

        if (n >= out_len - 1)
            return 0;
        out[n++] = c;
    }

    if (*p != '"')
        return 0;

    p++;
    out[n] = '\0';
    *pp = p;
    return 1;
}

static cmdt_var_t* vm_find_var(cmdt_vm_state_t* vm, const char* name)
{
    for (int i = 0; i < CMDT_MAX_VARS; i++)
        if (vm->vars[i].used && strcmp(vm->vars[i].name, name) == 0)
            return &vm->vars[i];
    return (cmdt_var_t*)0;
}

static cmdt_var_t* vm_create_var(cmdt_vm_state_t* vm, const char* name)
{
    cmdt_var_t* existing = vm_find_var(vm, name);
    if (existing)
        return existing;

    for (int i = 0; i < CMDT_MAX_VARS; i++)
    {
        if (!vm->vars[i].used)
        {
            vm->vars[i].used = 1;
            vm->vars[i].value = 0;
            strncpy(vm->vars[i].name, name, CMDT_MAX_NAME_LEN - 1);
            vm->vars[i].name[CMDT_MAX_NAME_LEN - 1] = '\0';
            return &vm->vars[i];
        }
    }
    return (cmdt_var_t*)0;
}

static void vm_print_int(int value)
{
    char buf[20];
    int i = 0;

    if (value == 0)
    {
        tty_putchar('0');
        return;
    }

    if (value < 0)
    {
        tty_putchar('-');
        value = -value;
    }

    while (value > 0 && i < (int)sizeof(buf))
    {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i > 0)
        tty_putchar(buf[--i]);
}

static int vm_parse_value(cmdt_vm_state_t* vm, const char** pp, int* out)
{
    const char* p = vm_skip_ws(*pp);
    char name[CMDT_MAX_NAME_LEN];

    if (vm_parse_int(&p, out))
    {
        *pp = p;
        return 1;
    }

    if (vm_parse_ident(&p, name, sizeof(name)))
    {
        cmdt_var_t* v = vm_find_var(vm, name);
        if (!v)
            return 0;
        *out = v->value;
        *pp = p;
        return 1;
    }

    return 0;
}

static int vm_end_stmt(const char* p)
{
    p = vm_skip_ws(p);
    return (*p == '\0' || *p == ';');
}

static int vm_exec_line(cmdt_vm_state_t* vm, const char* line)
{
    const char* p = vm_skip_ws(line);
    char ident[CMDT_MAX_NAME_LEN];
    int value = 0;

    if (*p == '\0' || strncmp(p, "//", 2) == 0)
        return 0;

    if (strncmp(p, "int", 3) == 0)
    {
        p += 3;
        if (!vm_is_space(*p)) return -1;
        if (!vm_parse_ident(&p, ident, sizeof(ident))) return -1;
        p = vm_skip_ws(p);
        if (*p == '=')
        {
            p++;
            if (!vm_parse_value(vm, &p, &value)) return -1;
        }
        if (!vm_end_stmt(p)) return -1;
        cmdt_var_t* v = vm_create_var(vm, ident);
        if (!v) return -1;
        v->value = value;
        return 0;
    }

    if (strncmp(p, "dinput", 6) == 0)
    {
        char prompt[CMDT_MAX_LINE];
        char input[64];

        p += 6;
        p = vm_skip_ws(p);
        if (*p != '(') return -1;
        p++;

        if (!vm_parse_string(&p, prompt, sizeof(prompt))) return -1;
        p = vm_skip_ws(p);
        if (*p != ',') return -1;
        p++;

        if (!vm_parse_ident(&p, ident, sizeof(ident))) return -1;
        p = vm_skip_ws(p);
        if (*p != ')') return -1;
        p++;
        if (!vm_end_stmt(p)) return -1;

        tty_print(prompt);
        tty_readline(input, sizeof(input));
        p = input;
        if (!vm_parse_int(&p, &value)) return -1;

        cmdt_var_t* v = vm_create_var(vm, ident);
        if (!v) return -1;
        v->value = value;
        return 0;
    }

    if (strncmp(p, "cprintf", 7) == 0)
    {
        char text[CMDT_MAX_LINE];
        p += 7;
        p = vm_skip_ws(p);
        if (*p != '(') return -1;
        p++;
        if (!vm_parse_string(&p, text, sizeof(text))) return -1;
        p = vm_skip_ws(p);
        if (*p != ')') return -1;
        p++;
        if (!vm_end_stmt(p)) return -1;

        for (int i = 0; text[i] != '\0'; )
        {
            if (text[i] == '$' && text[i + 1] == '{')
            {
                char name[CMDT_MAX_NAME_LEN];
                int n = 0;
                i += 2;
                while (text[i] && text[i] != '}')
                {
                    if (n >= CMDT_MAX_NAME_LEN - 1) return -1;
                    name[n++] = text[i++];
                }
                if (text[i] != '}') return -1;
                name[n] = '\0';
                i++;

                cmdt_var_t* v = vm_find_var(vm, name);
                if (!v) return -1;
                vm_print_int(v->value);
                continue;
            }

            tty_putchar(text[i++]);
        }
        return 0;
    }

    /* Assignment */
    if (!vm_parse_ident(&p, ident, sizeof(ident)))
        return -1;
    p = vm_skip_ws(p);
    if (*p != '=')
        return -1;
    p++;
    if (!vm_parse_value(vm, &p, &value))
        return -1;
    if (!vm_end_stmt(p))
        return -1;

    cmdt_var_t* v = vm_find_var(vm, ident);
    if (!v)
        return -1;
    v->value = value;
    return 0;
}

static int vm_exec_source(const char* source)
{
    cmdt_vm_state_t vm;
    char stmt[CMDT_MAX_LINE];
    int n = 0;

    memset(&vm, 0, sizeof(vm));

    for (const char* p = source; ; p++)
    {
        char c = *p;
        int end_stmt = (c == ';' || c == '\n' || c == '\0');

        if (!end_stmt)
        {
            if (n >= CMDT_MAX_LINE - 1)
                return -1;
            stmt[n++] = c;
            continue;
        }

        stmt[n] = '\0';
        if (vm_exec_line(&vm, stmt) != 0)
            return -1;
        n = 0;

        if (c == '\0')
            break;
    }

    return 0;
}

static int cmdt_exec_vm(process_t* p, const char* path, const char* file_data, uint64_t file_size)
{
    (void)p;
    char source[CMDT_MAX_SOURCE];

    if (cmdt_decode_source(file_data, file_size, source, sizeof(source)) != 0)
    {
        klog_error("exec: CMDT decode failed");
        return -1;
    }

    klog_log("exec: CMDT decoded; entering VM runtime");
    if (vm_exec_source(source) != 0)
    {
        klog_error("exec: CMDT VM runtime failure");
        return -1;
    }

    klog_log(path);
    return 0;
}

/* ============================
   proc_exec - Execute program
   ============================ */

int proc_exec(process_t* p, const char* path, char** argv)
{
    if (!p || !path)
        return -1;

    extern struct bfs_instance g_bfs;
    uint64_t file_size = 0;
    uint64_t read_size = 0;

    if (bfs_file_size(&g_bfs, path, &file_size) != 0 || file_size == 0)
    {
        klog_error("exec: file not found or empty");
        return -1;
    }

    uint8_t* file_data = (uint8_t*)kmalloc((size_t)file_size + 1);
    if (!file_data)
    {
        klog_error("exec: out of memory reading executable");
        return -1;
    }

    if (bfs_read_file(&g_bfs, path, file_data, file_size, &read_size) != 0 || read_size != file_size)
    {
        klog_error("exec: failed to read executable file");
        kfree(file_data);
        return -1;
    }
    file_data[file_size] = '\0';

    /* Set process name from path */
    const char* name = path;
    const char* slash = path;
    while (*slash)
    {
        if (*slash == '/')
            name = slash + 1;
        slash++;
    }
    strncpy(p->name, name, PROC_NAME_MAX - 1);
    p->name[PROC_NAME_MAX - 1] = '\0';

    p->state = PROC_RUNNING;

    /* Current supported executable path: .cmdt (Intel-HEX-like VM image). */
    if (str_ends_with(path, ".cmdt"))
    {
        int rc = cmdt_exec_vm(p, path, (const char*)file_data, file_size);
        p->exit_code = (rc == 0) ? 0 : 1;
        p->state = PROC_ZOMBIE;
        kfree(file_data);
        (void)argv;
        return rc;
    }

    /* Keep ELF flow available for future completion, but fail cleanly today. */
    klog_error("exec: only .cmdt is currently executable in this path");
    p->exit_code = 1;
    p->state = PROC_ZOMBIE;
    kfree(file_data);
    (void)argv;
    return -1;
}

/* ============================
   exec_init - Initialize exec subsystem
   ============================ */

void exec_init(void)
{
    klog_init_msg("Exec subsystem initialized");
}
