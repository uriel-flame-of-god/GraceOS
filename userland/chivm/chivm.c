#include "chivm.h"

#include "../../drivers/video/tty.h"
#include "../../drivers/storage/bfs.h"
#include "../../lib/libc/string.h"

#define CHIVM_MAX_VARS      64
#define CHIVM_MAX_NAME_LEN  32
#define CHIVM_MAX_LINE      256
#define CHIVM_MAX_SOURCE    8192
#define CHIVM_MAX_CMDT      32768

extern struct bfs_instance g_bfs;

typedef struct {
    int used;
    char name[CHIVM_MAX_NAME_LEN];
    int value;
} chivm_var_t;

typedef struct {
    chivm_var_t vars[CHIVM_MAX_VARS];
} chivm_state_t;

static chivm_state_t g_vm;

static void vm_print(const char* s)
{
    tty_print(s);
}

static void vm_println(const char* s)
{
    tty_print(s);
    tty_print("\n");
}

static void vm_print_int(int value)
{
    char buf[20];
    int i = 0;

    if (value == 0) {
        tty_putchar('0');
        return;
    }

    if (value < 0) {
        tty_putchar('-');
        value = -value;
    }

    while (value > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i > 0)
        tty_putchar(buf[--i]);
}

static void vm_reset(void)
{
    memset(&g_vm, 0, sizeof(g_vm));
}

static int is_space(char c)
{
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

static const char* skip_ws(const char* p)
{
    while (*p && is_space(*p)) p++;
    return p;
}

static int is_ident_start(char c)
{
    return (c == '_') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int is_ident_char(char c)
{
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static int parse_ident(const char** pp, char* out, int out_len)
{
    const char* p = skip_ws(*pp);
    int n = 0;

    if (!is_ident_start(*p))
        return 0;

    while (*p && is_ident_char(*p)) {
        if (n >= out_len - 1)
            return 0;
        out[n++] = *p++;
    }

    out[n] = '\0';
    *pp = p;
    return 1;
}

static int parse_int_literal(const char** pp, int* out)
{
    const char* p = skip_ws(*pp);
    int sign = 1;
    int value = 0;
    int has_digit = 0;

    if (*p == '-') {
        sign = -1;
        p++;
    }

    while (*p >= '0' && *p <= '9') {
        has_digit = 1;
        value = value * 10 + (*p - '0');
        p++;
    }

    if (!has_digit)
        return 0;

    *out = sign * value;
    *pp = p;
    return 1;
}

static int parse_quoted_string(const char** pp, char* out, int out_len)
{
    const char* p = skip_ws(*pp);
    int n = 0;

    if (*p != '"')
        return 0;
    p++;

    while (*p && *p != '"') {
        char c = *p++;

        if (c == '\\') {
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

static chivm_var_t* find_var(const char* name)
{
    for (int i = 0; i < CHIVM_MAX_VARS; i++) {
        if (g_vm.vars[i].used && strcmp(g_vm.vars[i].name, name) == 0)
            return &g_vm.vars[i];
    }
    return (chivm_var_t*)0;
}

static chivm_var_t* create_var(const char* name)
{
    chivm_var_t* existing = find_var(name);
    if (existing)
        return existing;

    for (int i = 0; i < CHIVM_MAX_VARS; i++) {
        if (!g_vm.vars[i].used) {
            g_vm.vars[i].used = 1;
            g_vm.vars[i].value = 0;
            strncpy(g_vm.vars[i].name, name, CHIVM_MAX_NAME_LEN - 1);
            g_vm.vars[i].name[CHIVM_MAX_NAME_LEN - 1] = '\0';
            return &g_vm.vars[i];
        }
    }

    return (chivm_var_t*)0;
}

static int parse_value(const char** pp, int* out)
{
    const char* p = skip_ws(*pp);
    char name[CHIVM_MAX_NAME_LEN];

    if (parse_int_literal(&p, out)) {
        *pp = p;
        return 1;
    }

    if (parse_ident(&p, name, sizeof(name))) {
        chivm_var_t* v = find_var(name);
        if (!v)
            return 0;
        *out = v->value;
        *pp = p;
        return 1;
    }

    return 0;
}

static void vm_error(const char* msg)
{
    tty_set_color(TTY_LIGHT_RED, TTY_BLACK);
    vm_print("[chivm] error: ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    vm_println(msg);
}

static int ensure_end_stmt(const char* p)
{
    p = skip_ws(p);
    return (*p == '\0' || *p == ';');
}

static int exec_cprintf(const char* line)
{
    const char* p = line + 7;
    char text[CHIVM_MAX_LINE];

    p = skip_ws(p);
    if (*p != '(') {
        vm_error("expected '(' after cprintf");
        return -1;
    }
    p++;

    if (!parse_quoted_string(&p, text, sizeof(text))) {
        vm_error("invalid cprintf string");
        return -1;
    }

    p = skip_ws(p);
    if (*p != ')') {
        vm_error("expected ')' after cprintf string");
        return -1;
    }
    p++;

    if (!ensure_end_stmt(p)) {
        vm_error("unexpected tokens after cprintf");
        return -1;
    }

    for (int i = 0; text[i] != '\0'; ) {
        if (text[i] == '$' && text[i + 1] == '{') {
            char name[CHIVM_MAX_NAME_LEN];
            int n = 0;
            i += 2;
            while (text[i] && text[i] != '}') {
                if (n >= CHIVM_MAX_NAME_LEN - 1) {
                    vm_error("interpolation variable name too long");
                    return -1;
                }
                name[n++] = text[i++];
            }
            if (text[i] != '}') {
                vm_error("unterminated interpolation pattern");
                return -1;
            }
            name[n] = '\0';
            i++;

            chivm_var_t* v = find_var(name);
            if (!v) {
                vm_error("unknown variable in interpolation");
                return -1;
            }
            vm_print_int(v->value);
            continue;
        }

        tty_putchar(text[i]);
        i++;
    }

    return 0;
}

static int exec_dinput(const char* line)
{
    const char* p = line + 6;
    char prompt[CHIVM_MAX_LINE];
    char ident[CHIVM_MAX_NAME_LEN];
    char input[64];
    int value;

    p = skip_ws(p);
    if (*p != '(') {
        vm_error("expected '(' after dinput");
        return -1;
    }
    p++;

    if (!parse_quoted_string(&p, prompt, sizeof(prompt))) {
        vm_error("invalid dinput prompt string");
        return -1;
    }

    p = skip_ws(p);
    if (*p != ',') {
        vm_error("expected ',' after dinput prompt");
        return -1;
    }
    p++;

    if (!parse_ident(&p, ident, sizeof(ident))) {
        vm_error("expected variable name in dinput");
        return -1;
    }

    p = skip_ws(p);
    if (*p != ')') {
        vm_error("expected ')' after dinput arguments");
        return -1;
    }
    p++;

    if (!ensure_end_stmt(p)) {
        vm_error("unexpected tokens after dinput");
        return -1;
    }

    if (prompt[0] != '\0')
        vm_print(prompt);

    tty_readline(input, sizeof(input));

    p = input;
    if (!parse_int_literal(&p, &value)) {
        vm_error("dinput expects integer input");
        return -1;
    }

    chivm_var_t* v = create_var(ident);
    if (!v) {
        vm_error("variable table full");
        return -1;
    }
    v->value = value;
    return 0;
}

static int exec_var_decl(const char* line)
{
    const char* p = line + 3;
    char ident[CHIVM_MAX_NAME_LEN];
    int value = 0;

    if (!is_space(*p)) {
        vm_error("expected space after 'int'");
        return -1;
    }

    if (!parse_ident(&p, ident, sizeof(ident))) {
        vm_error("invalid variable name");
        return -1;
    }

    p = skip_ws(p);
    if (*p == '=') {
        p++;
        if (!parse_value(&p, &value)) {
            vm_error("invalid initializer value");
            return -1;
        }
    }

    if (!ensure_end_stmt(p)) {
        vm_error("unexpected tokens in declaration");
        return -1;
    }

    chivm_var_t* v = create_var(ident);
    if (!v) {
        vm_error("variable table full");
        return -1;
    }
    v->value = value;
    return 0;
}

static int exec_assignment(const char* line)
{
    const char* p = line;
    char ident[CHIVM_MAX_NAME_LEN];
    int value;

    if (!parse_ident(&p, ident, sizeof(ident)))
        return -1;

    p = skip_ws(p);
    if (*p != '=')
        return -1;
    p++;

    if (!parse_value(&p, &value)) {
        vm_error("invalid assignment value");
        return -1;
    }

    if (!ensure_end_stmt(p)) {
        vm_error("unexpected tokens in assignment");
        return -1;
    }

    chivm_var_t* v = find_var(ident);
    if (!v) {
        vm_error("assignment to undefined variable");
        return -1;
    }

    v->value = value;
    return 0;
}

static int exec_line(const char* line)
{
    const char* p = skip_ws(line);

    if (*p == '\0')
        return 0;

    if (strncmp(p, "//", 2) == 0)
        return 0;

    if (strncmp(p, "int", 3) == 0)
        return exec_var_decl(p);

    if (strncmp(p, "cprintf", 7) == 0)
        return exec_cprintf(p);

    if (strncmp(p, "dinput", 6) == 0)
        return exec_dinput(p);

    return exec_assignment(p);
}

static int exec_source(const char* source)
{
    char stmt[CHIVM_MAX_LINE];
    int n = 0;

    for (const char* p = source; ; p++) {
        char c = *p;
        int end_stmt = (c == ';' || c == '\n' || c == '\0');

        if (!end_stmt) {
            if (n < CHIVM_MAX_LINE - 1)
                stmt[n++] = c;
            else {
                vm_error("statement too long");
                return -1;
            }
            continue;
        }

        stmt[n] = '\0';
        if (exec_line(stmt) != 0)
            return -1;
        n = 0;

        if (c == '\0')
            break;
    }

    return 0;
}


int chivm_run_file(const char* filename)
{
    char source[CHIVM_MAX_SOURCE];
    uint64_t size = 0;
    uint64_t out_size = 0;

    if (!filename || *filename == '\0') {
        vm_error("missing filename");
        return -1;
    }

    vm_reset();

    if (bfs_file_size(&g_bfs, filename, &size) != 0) {
        vm_error("file not found");
        return -1;
    }

    if (size >= sizeof(source)) {
        vm_error("source file too large");
        return -1;
    }

    if (bfs_read_file(&g_bfs, filename, source, sizeof(source) - 1, &out_size) != 0) {
        vm_error("failed to read source file");
        return -1;
    }

    source[out_size] = '\0';
    return exec_source(source);
}

/* -----------------------------------------------------------------------
   Intel HEX decoder — used to execute compiled .cmdt files
   ----------------------------------------------------------------------- */

static int ihex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Parse two hex chars at p into *out.  Returns 1 on success. */
static int ihex_byte(const char* p, uint8_t* out)
{
    int hi = ihex_nibble(p[0]);
    int lo = ihex_nibble(p[1]);
    if (hi < 0 || lo < 0) return 0;
    *out = (uint8_t)((hi << 4) | lo);
    return 1;
}

/* Decode Intel HEX text in 'hex' into 'out'.
   Returns number of bytes written, or -1 on error. */
static int ihex_decode(const char* hex, char* out, int out_cap)
{
    int total = 0;
    const char* p = hex;

    while (*p) {
        /* skip blank lines / CR */
        while (*p == '\r' || *p == '\n' || *p == ' ') p++;
        if (*p == '\0') break;

        if (*p != ':') return -1;
        p++;

        uint8_t count, addrhi, addrlo, rectype;
        if (!ihex_byte(p, &count))  return -1; p += 2;
        if (!ihex_byte(p, &addrhi)) return -1; p += 2;
        if (!ihex_byte(p, &addrlo)) return -1; p += 2;
        if (!ihex_byte(p, &rectype)) return -1; p += 2;

        (void)addrhi; (void)addrlo; /* address unused — records are sequential */

        if (rectype == 0x01) break; /* EOF record */

        if (rectype == 0x00) {
            /* Data record: copy bytes into output */
            for (int i = 0; i < (int)count; i++) {
                uint8_t b;
                if (!ihex_byte(p, &b)) return -1;
                p += 2;
                if (total >= out_cap - 1) return -1;
                out[total++] = (char)b;
            }
        } else {
            p += (int)count * 2; /* skip unknown record data */
        }

        p += 2; /* skip checksum */
        while (*p && *p != '\n') p++; /* skip to end of line */
    }

    return total;
}

/* Read a compiled .cmdt (Intel HEX), decode it, and run through a fresh VM. */
int chivm_exec_cmdt(const char* filename)
{
    static char cmdt[CHIVM_MAX_CMDT];
    char source[CHIVM_MAX_SOURCE];
    uint64_t file_size = 0;
    uint64_t read_size = 0;

    if (!filename || !*filename) {
        vm_error("missing filename");
        return -1;
    }

    if (bfs_file_size(&g_bfs, filename, &file_size) != 0) {
        vm_error("file not found");
        return -1;
    }

    if (file_size >= sizeof(cmdt)) {
        vm_error("cmdt file too large");
        return -1;
    }

    if (bfs_read_file(&g_bfs, filename, cmdt, sizeof(cmdt) - 1, &read_size) != 0) {
        vm_error("failed to read cmdt file");
        return -1;
    }
    cmdt[read_size] = '\0';

    int decoded = ihex_decode(cmdt, source, sizeof(source));
    if (decoded < 0) {
        vm_error("invalid .cmdt (ihex decode failed)");
        return -1;
    }
    source[decoded] = '\0';

    vm_reset();
    return exec_source(source);
}

static char hex_digit(uint8_t v)
{
    v &= 0x0F;
    return (v < 10) ? (char)('0' + v) : (char)('A' + (v - 10));
}

static int out_ch(char* out, int out_cap, int* pos, char c)
{
    if (*pos >= out_cap - 1)
        return -1;
    out[*pos] = c;
    (*pos)++;
    return 0;
}

static int out_hex_byte(char* out, int out_cap, int* pos, uint8_t b)
{
    if (out_ch(out, out_cap, pos, hex_digit((uint8_t)(b >> 4))) < 0)
        return -1;
    if (out_ch(out, out_cap, pos, hex_digit((uint8_t)(b & 0x0F))) < 0)
        return -1;
    return 0;
}

/* Encode one Intel-HEX-compatible line:
   :LLAAAATT[DD...]CC\n */
static int emit_hex_record(char* out, int out_cap, int* pos,
                           uint8_t count, uint16_t addr,
                           uint8_t rectype, const uint8_t* data)
{
    uint8_t sum = 0;
    sum = (uint8_t)(sum + count);
    sum = (uint8_t)(sum + (uint8_t)(addr >> 8));
    sum = (uint8_t)(sum + (uint8_t)(addr & 0xFF));
    sum = (uint8_t)(sum + rectype);

    if (out_ch(out, out_cap, pos, ':') < 0) return -1;
    if (out_hex_byte(out, out_cap, pos, count) < 0) return -1;
    if (out_hex_byte(out, out_cap, pos, (uint8_t)(addr >> 8)) < 0) return -1;
    if (out_hex_byte(out, out_cap, pos, (uint8_t)(addr & 0xFF)) < 0) return -1;
    if (out_hex_byte(out, out_cap, pos, rectype) < 0) return -1;

    for (uint8_t i = 0; i < count; i++) {
        uint8_t b = data ? data[i] : 0;
        sum = (uint8_t)(sum + b);
        if (out_hex_byte(out, out_cap, pos, b) < 0) return -1;
    }

    /* Two's complement checksum */
    if (out_hex_byte(out, out_cap, pos, (uint8_t)(0 - sum)) < 0) return -1;
    if (out_ch(out, out_cap, pos, '\n') < 0) return -1;
    return 0;
}

int chivm_compile_file(const char* src_filename, const char* out_cmdt_filename)
{
    char source[CHIVM_MAX_SOURCE];
    char cmdt[CHIVM_MAX_CMDT];
    uint64_t src_size = 0;
    uint64_t out_size = 0;
    int pos = 0;

    if (!src_filename || !*src_filename || !out_cmdt_filename || !*out_cmdt_filename) {
        vm_error("usage: chivm_compile_file(<src>, <out.cmdt>)");
        return -1;
    }

    if (bfs_file_size(&g_bfs, src_filename, &src_size) != 0) {
        vm_error("source file not found");
        return -1;
    }

    if (src_size == 0 || src_size >= sizeof(source)) {
        vm_error("invalid source size");
        return -1;
    }

    if (bfs_read_file(&g_bfs, src_filename, source, sizeof(source) - 1, &out_size) != 0) {
        vm_error("failed to read source file");
        return -1;
    }

    source[out_size] = '\0';

    /* Emit data records (16-byte chunks). */
    uint16_t addr = 0;
    uint64_t off = 0;
    while (off < out_size) {
        uint8_t chunk = (uint8_t)((out_size - off) > 16 ? 16 : (out_size - off));
        if (emit_hex_record(cmdt, sizeof(cmdt), &pos, chunk, addr, 0x00,
                            (const uint8_t*)(source + off)) < 0) {
            vm_error("compiled .cmdt output too large");
            return -1;
        }
        off += chunk;
        addr = (uint16_t)(addr + chunk);
    }

    /* EOF record */
    if (emit_hex_record(cmdt, sizeof(cmdt), &pos, 0, 0, 0x01, (const uint8_t*)0) < 0) {
        vm_error("failed to emit .cmdt EOF record");
        return -1;
    }
    cmdt[pos] = '\0';

    /* Create file if missing; ignore already-exists error. */
    (void)bfs_create(&g_bfs, out_cmdt_filename, (uint64_t)pos);

    if (bfs_write_file(&g_bfs, out_cmdt_filename, cmdt, (uint64_t)pos) != 0) {
        vm_error("failed to write .cmdt file");
        return -1;
    }

    return 0;
}

void chivm_run(void)
{
    char line[CHIVM_MAX_LINE];

    vm_reset();
    tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
    vm_println("ChibiVM Step 1 (variables/input/output)");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    vm_println("Syntax: int x = 1;  x = 2;  dinput(\"n? \", x);  cprintf(\"x=${x}\\n\");");
    vm_println("Type 'help' for commands, 'exit' to return to shell.");

    while (1) {
        tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
        vm_print("chivm> ");
        tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
        tty_readline(line, sizeof(line));

        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0)
            break;

        if (strcmp(line, "help") == 0) {
            vm_println("Commands:");
            vm_println("  help              Show this help");
            vm_println("  reset             Clear VM variables");
            vm_println("  run <file>        Execute source file from BranchFS");
            vm_println("  exit              Return to shell");
            continue;
        }

        if (strcmp(line, "reset") == 0) {
            vm_reset();
            vm_println("state reset");
            continue;
        }

        if (strncmp(line, "run ", 4) == 0) {
            const char* file = skip_ws(line + 4);
            if (chivm_run_file(file) == 0)
                vm_println("[chivm] file executed");
            continue;
        }

        if (exec_line(line) != 0)
            vm_println("[chivm] statement failed");
    }

    vm_println("Leaving ChibiVM.");
}
