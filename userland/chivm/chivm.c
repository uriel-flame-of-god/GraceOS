#include "chivm.h"

#include "../../drivers/video/tty.h"
#include "../../drivers/storage/bfs.h"
#include "../../lib/libc/string.h"

/* ================================================================
   ChibiVM — types, conditionals, arrays
   Supports: int, bool, string, char, array (hetero), T[] (homo)
             if / else if / else  with { } blocks
   ================================================================ */

#define CHIVM_MAX_VARS        64
#define CHIVM_MAX_NAME_LEN    32
#define CHIVM_MAX_LINE        512
#define CHIVM_MAX_SOURCE      8192
#define CHIVM_MAX_CMDT        32768
#define CHIVM_MAX_STR_LEN     128
#define CHIVM_ARRAY_POOL_CAP  256

/* ========================= Value types ========================= */

typedef enum {
    VAL_NONE = 0,   /* uninitialized / heterogeneous marker */
    VAL_INT,
    VAL_BOOL,
    VAL_STRING,
    VAL_CHAR,
    VAL_ARRAY
} chivm_vkind_t;

typedef struct {
    chivm_vkind_t kind;
    union {
        int  i;                     /* VAL_INT  */
        int  b;                     /* VAL_BOOL: 0 or 1 */
        char s[CHIVM_MAX_STR_LEN];  /* VAL_STRING */
        char c;                     /* VAL_CHAR  */
        struct {
            int           pool_start;
            int           count;
            chivm_vkind_t elem_type; /* VAL_NONE = heterogeneous */
        } arr;                      /* VAL_ARRAY */
    } u;
} chivm_val_t;

/* ========================= Variable table ========================= */

typedef struct {
    int           used;
    char          name[CHIVM_MAX_NAME_LEN];
    chivm_vkind_t decl_kind;  /* declared type; VAL_NONE = untyped */
    chivm_val_t   val;
} chivm_var_t;

/* ========================= VM state ========================= */

typedef struct {
    chivm_var_t vars[CHIVM_MAX_VARS];
} chivm_state_t;

static chivm_state_t g_vm;
static chivm_val_t   g_array_pool[CHIVM_ARRAY_POOL_CAP];
static int           g_array_pool_next = 0;

extern struct bfs_instance g_bfs;

/* ========================= Output helpers ========================= */

static void vm_print(const char* s)   { tty_print(s); }
static void vm_println(const char* s) { tty_print(s); tty_print("\n"); }

static void vm_print_int(int v)
{
    char buf[20];
    int i = 0;
    if (v == 0) { tty_putchar('0'); return; }
    if (v < 0)  { tty_putchar('-'); v = -v; }
    while (v > 0 && i < (int)sizeof(buf)) { buf[i++] = (char)('0' + v % 10); v /= 10; }
    while (i > 0) tty_putchar(buf[--i]);
}

static void vm_error(const char* msg)
{
    tty_set_color(TTY_LIGHT_RED, TTY_BLACK);
    vm_print("[chivm] error: ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    vm_println(msg);
}

static void vm_print_val(const chivm_val_t* v);

static void vm_print_val(const chivm_val_t* v)
{
    switch (v->kind) {
    case VAL_INT:    vm_print_int(v->u.i); break;
    case VAL_BOOL:   vm_print(v->u.b ? "true" : "false"); break;
    case VAL_STRING: vm_print(v->u.s); break;
    case VAL_CHAR:   tty_putchar(v->u.c); break;
    case VAL_ARRAY:
        tty_putchar('[');
        for (int i = 0; i < v->u.arr.count; i++) {
            if (i > 0) { tty_putchar(','); tty_putchar(' '); }
            chivm_val_t* e = &g_array_pool[v->u.arr.pool_start + i];
            if (e->kind == VAL_STRING) {
                tty_putchar('"'); vm_print(e->u.s); tty_putchar('"');
            } else if (e->kind == VAL_CHAR) {
                tty_putchar('\''); tty_putchar(e->u.c); tty_putchar('\'');
            } else {
                vm_print_val(e);
            }
        }
        tty_putchar(']');
        break;
    default: break;
    }
}

/* ========================= VM reset ========================= */

static void vm_reset(void)
{
    memset(&g_vm, 0, sizeof(g_vm));
    memset(g_array_pool, 0, sizeof(g_array_pool));
    g_array_pool_next = 0;
}

/* ========================= String helpers ========================= */

static int vm_strlen(const char* s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

/* ========================= Character classifiers ========================= */

static int is_space(char c)       { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static int is_ident_start(char c) { return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_ident_char(char c)  { return is_ident_start(c) || (c >= '0' && c <= '9'); }

static const char* skip_ws(const char* p)
{
    while (*p && is_space(*p)) p++;
    return p;
}

/* ========================= Lexer helpers ========================= */

static int parse_ident(const char** pp, char* out, int out_len)
{
    const char* p = skip_ws(*pp);
    int n = 0;
    if (!is_ident_start(*p)) return 0;
    while (*p && is_ident_char(*p)) {
        if (n >= out_len - 1) return 0;
        out[n++] = *p++;
    }
    out[n] = '\0';
    *pp = p;
    return 1;
}

static int parse_int_lit(const char** pp, int* out)
{
    const char* p = skip_ws(*pp);
    int sign = 1, value = 0, has = 0;
    if (*p == '-') { sign = -1; p++; }
    while (*p >= '0' && *p <= '9') { has = 1; value = value * 10 + (*p - '0'); p++; }
    if (!has) return 0;
    *out = sign * value;
    *pp = p;
    return 1;
}

static int parse_quoted_string(const char** pp, char* out, int out_len)
{
    const char* p = skip_ws(*pp);
    int n = 0;
    if (*p != '"') return 0;
    p++;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            if      (*p == 'n')  { c = '\n'; p++; }
            else if (*p == 't')  { c = '\t'; p++; }
            else if (*p == 'r')  { c = '\r'; p++; }
            else if (*p == '\\') { c = '\\'; p++; }
            else if (*p == '"')  { c = '"';  p++; }
        }
        if (n >= out_len - 1) return 0;
        out[n++] = c;
    }
    if (*p != '"') return 0;
    p++;
    out[n] = '\0';
    *pp = p;
    return 1;
}

static int parse_char_lit(const char** pp, char* out)
{
    const char* p = skip_ws(*pp);
    if (*p != '\'') return 0;
    p++;
    char c = *p++;
    if (c == '\\') {
        if      (*p == 'n')  { c = '\n'; p++; }
        else if (*p == 't')  { c = '\t'; p++; }
        else if (*p == '\'') { c = '\''; p++; }
        else if (*p == '\\') { c = '\\'; p++; }
    }
    if (*p != '\'') return 0;
    p++;
    *out = c;
    *pp = p;
    return 1;
}

/* ========================= Variable table ========================= */

static chivm_var_t* find_var(const char* name)
{
    for (int i = 0; i < CHIVM_MAX_VARS; i++)
        if (g_vm.vars[i].used && strcmp(g_vm.vars[i].name, name) == 0)
            return &g_vm.vars[i];
    return (chivm_var_t*)0;
}

static chivm_var_t* alloc_var(const char* name, chivm_vkind_t decl_kind)
{
    chivm_var_t* existing = find_var(name);
    if (existing) return existing;
    for (int i = 0; i < CHIVM_MAX_VARS; i++) {
        if (!g_vm.vars[i].used) {
            g_vm.vars[i].used      = 1;
            g_vm.vars[i].decl_kind = decl_kind;
            strncpy(g_vm.vars[i].name, name, CHIVM_MAX_NAME_LEN - 1);
            g_vm.vars[i].name[CHIVM_MAX_NAME_LEN - 1] = '\0';
            memset(&g_vm.vars[i].val, 0, sizeof(chivm_val_t));
            g_vm.vars[i].val.kind = (decl_kind == VAL_NONE) ? VAL_INT : decl_kind;
            return &g_vm.vars[i];
        }
    }
    return (chivm_var_t*)0;
}

/* ========================= Array pool ========================= */

static int alloc_array_pool(int count)
{
    if (count < 0 || g_array_pool_next + count > CHIVM_ARRAY_POOL_CAP) return -1;
    int start = g_array_pool_next;
    g_array_pool_next += count;
    return start;
}

/* ========================= Value expression parsing ========================= */

/* Forward declaration — needed for array literal parsing (recursive). */
static int parse_val_expr(const char** pp, chivm_val_t* out);

static int parse_array_literal(const char** pp, chivm_val_t* out, chivm_vkind_t elem_type)
{
    const char* p = skip_ws(*pp);
    if (*p != '[') return 0;
    p++;

    chivm_val_t elems[64];
    int count = 0;

    p = skip_ws(p);
    while (*p && *p != ']') {
        if (count >= 64) { vm_error("array literal: too many elements"); return 0; }
        *pp = p;
        chivm_val_t elem;
        if (!parse_val_expr(pp, &elem)) { vm_error("invalid array element"); return 0; }
        p = *pp;

        if (elem_type != VAL_NONE && elem.kind != elem_type) {
            vm_error("array element type mismatch");
            return 0;
        }
        elems[count++] = elem;

        p = skip_ws(p);
        if (*p == ',') p++;
        p = skip_ws(p);
    }

    if (*p != ']') { vm_error("expected ']' to close array literal"); return 0; }
    p++;
    *pp = p;

    int start = alloc_array_pool(count);
    if (start < 0) { vm_error("array pool full"); return 0; }
    for (int i = 0; i < count; i++)
        g_array_pool[start + i] = elems[i];

    out->kind              = VAL_ARRAY;
    out->u.arr.pool_start  = start;
    out->u.arr.count       = count;
    out->u.arr.elem_type   = elem_type;
    return 1;
}

/*
 * parse_val_expr — parse one value (literal, keyword, or variable reference).
 * Handles: int literal, bool literal, string literal, char literal, array literal,
 *          bare variable, array[index] element.
 */
static int parse_val_expr(const char** pp, chivm_val_t* out)
{
    const char* p = skip_ws(*pp);

    /* String literal */
    if (*p == '"') {
        out->kind = VAL_STRING;
        if (!parse_quoted_string(pp, out->u.s, CHIVM_MAX_STR_LEN)) return 0;
        return 1;
    }

    /* Char literal */
    if (*p == '\'') {
        out->kind = VAL_CHAR;
        if (!parse_char_lit(pp, &out->u.c)) return 0;
        return 1;
    }

    /* Array literal */
    if (*p == '[') {
        return parse_array_literal(pp, out, VAL_NONE);
    }

    /* Negative or positive integer literal */
    if (*p == '-' || (*p >= '0' && *p <= '9')) {
        out->kind = VAL_INT;
        if (!parse_int_lit(pp, &out->u.i)) return 0;
        return 1;
    }

    /* Keyword or identifier */
    if (!is_ident_start(*p)) return 0;

    char name[CHIVM_MAX_NAME_LEN];
    int n = 0;
    while (*p && is_ident_char(*p)) {
        if (n >= CHIVM_MAX_NAME_LEN - 1) return 0;
        name[n++] = *p++;
    }
    name[n] = '\0';
    *pp = p;

    if (strcmp(name, "true")  == 0) { out->kind = VAL_BOOL; out->u.b = 1; return 1; }
    if (strcmp(name, "false") == 0) { out->kind = VAL_BOOL; out->u.b = 0; return 1; }

    chivm_var_t* v = find_var(name);
    if (!v) return 0;

    /* Array element: name[index] */
    p = skip_ws(p);
    if (*p == '[' && v->val.kind == VAL_ARRAY) {
        p++;
        *pp = p;

        int idx;
        if (!parse_int_lit(pp, &idx)) {
            char iname[CHIVM_MAX_NAME_LEN];
            if (!parse_ident(pp, iname, sizeof(iname))) {
                vm_error("bad array index");
                return 0;
            }
            chivm_var_t* iv = find_var(iname);
            if (!iv || iv->val.kind != VAL_INT) {
                vm_error("array index must be an int variable");
                return 0;
            }
            idx = iv->val.u.i;
        }

        p = skip_ws(*pp);
        if (*p != ']') { vm_error("expected ']' after array index"); return 0; }
        p++;
        *pp = p;

        if (idx < 0 || idx >= v->val.u.arr.count) {
            vm_error("array index out of bounds");
            return 0;
        }
        *out = g_array_pool[v->val.u.arr.pool_start + idx];
        return 1;
    }

    *out = v->val;
    return 1;
}

/* ========================= Value comparison / truthiness ========================= */

static int val_truthy(const chivm_val_t* v)
{
    switch (v->kind) {
    case VAL_INT:    return v->u.i != 0;
    case VAL_BOOL:   return v->u.b != 0;
    case VAL_STRING: return v->u.s[0] != '\0';
    case VAL_CHAR:   return v->u.c != '\0';
    case VAL_ARRAY:  return v->u.arr.count > 0;
    default:         return 0;
    }
}

/* Compare a op b.  Returns 0 or 1. */
static int val_compare(const chivm_val_t* a, const chivm_val_t* b, const char* op)
{
    if (a->kind != b->kind) {
        if (strcmp(op, "!=") == 0) return 1;
        if (strcmp(op, "==") == 0) return 0;
        vm_error("cannot compare values of different types");
        return 0;
    }

    int diff = 0;
    switch (a->kind) {
    case VAL_INT:
        if (strcmp(op, "==") == 0) return a->u.i == b->u.i;
        if (strcmp(op, "!=") == 0) return a->u.i != b->u.i;
        if (strcmp(op, "<")  == 0) return a->u.i <  b->u.i;
        if (strcmp(op, ">")  == 0) return a->u.i >  b->u.i;
        if (strcmp(op, "<=") == 0) return a->u.i <= b->u.i;
        if (strcmp(op, ">=") == 0) return a->u.i >= b->u.i;
        break;
    case VAL_BOOL:
        if (strcmp(op, "==") == 0) return a->u.b == b->u.b;
        if (strcmp(op, "!=") == 0) return a->u.b != b->u.b;
        break;
    case VAL_STRING:
        diff = strcmp(a->u.s, b->u.s);
        if (strcmp(op, "==") == 0) return diff == 0;
        if (strcmp(op, "!=") == 0) return diff != 0;
        if (strcmp(op, "<")  == 0) return diff <  0;
        if (strcmp(op, ">")  == 0) return diff >  0;
        if (strcmp(op, "<=") == 0) return diff <= 0;
        if (strcmp(op, ">=") == 0) return diff >= 0;
        break;
    case VAL_CHAR:
        if (strcmp(op, "==") == 0) return a->u.c == b->u.c;
        if (strcmp(op, "!=") == 0) return a->u.c != b->u.c;
        if (strcmp(op, "<")  == 0) return a->u.c <  b->u.c;
        if (strcmp(op, ">")  == 0) return a->u.c >  b->u.c;
        if (strcmp(op, "<=") == 0) return a->u.c <= b->u.c;
        if (strcmp(op, ">=") == 0) return a->u.c >= b->u.c;
        break;
    default: break;
    }
    return 0;
}

/* ========================= Condition evaluation ========================= */

/*
 * eval_condition — parse and evaluate a boolean condition.
 * Syntax: [!] value [op value]
 * where op is one of: == != < > <= >=
 * A single value without op is evaluated as truthy.
 * Sets *result to 0 or 1.  Returns 0 on success, -1 on parse error.
 */
static int eval_condition(const char** pp, int* result)
{
    const char* p = skip_ws(*pp);
    int negate = 0;

    if (*p == '!') { negate = 1; p++; *pp = p; }

    chivm_val_t lhs;
    if (!parse_val_expr(pp, &lhs)) { vm_error("bad condition left-hand side"); return -1; }

    p = skip_ws(*pp);

    /* Detect comparison operator */
    char op[3] = {'\0', '\0', '\0'};
    if      (p[0] == '=' && p[1] == '=') { op[0]='='; op[1]='='; p += 2; }
    else if (p[0] == '!' && p[1] == '=') { op[0]='!'; op[1]='='; p += 2; }
    else if (p[0] == '<' && p[1] == '=') { op[0]='<'; op[1]='='; p += 2; }
    else if (p[0] == '>' && p[1] == '=') { op[0]='>'; op[1]='='; p += 2; }
    else if (p[0] == '<')                { op[0]='<'; p++; }
    else if (p[0] == '>')                { op[0]='>'; p++; }

    if (op[0] != '\0') {
        *pp = p;
        chivm_val_t rhs;
        if (!parse_val_expr(pp, &rhs)) { vm_error("bad condition right-hand side"); return -1; }
        *result = val_compare(&lhs, &rhs, op);
    } else {
        *result = val_truthy(&lhs);
    }

    if (negate) *result = !(*result);
    return 0;
}

/* ========================= Block skipping ========================= */

/*
 * skip_to_close_paren — advance *pp to the matching ')'.
 * Called AFTER the opening '(' has been consumed.
 * Does NOT consume the ')'.
 */
static int skip_to_close_paren(const char** pp)
{
    const char* p = *pp;
    int depth = 0;
    while (*p) {
        if (*p == '"') {
            p++;
            while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; p++; }
            if (*p) p++;
        } else if (*p == '\'') {
            p++;
            if (*p == '\\' && p[1]) p++;
            if (*p) p++;
            if (*p == '\'') p++;
        } else if (*p == '(') {
            depth++; p++;
        } else if (*p == ')') {
            if (depth == 0) { *pp = p; return 0; }
            depth--; p++;
        } else {
            p++;
        }
    }
    return -1;
}

/*
 * skip_block_body — advance *pp to the matching '}'.
 * Called AFTER the opening '{' has been consumed.
 * Does NOT consume the '}'.
 */
static int skip_block_body(const char** pp)
{
    const char* p = *pp;
    int depth = 0;
    while (*p) {
        if (*p == '"') {
            p++;
            while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; p++; }
            if (*p) p++;
        } else if (*p == '\'') {
            p++;
            if (*p == '\\' && p[1]) p++;
            if (*p) p++;
            if (*p == '\'') p++;
        } else if (*p == '/' && p[1] == '/') {
            while (*p && *p != '\n') p++;
        } else if (*p == '{') {
            depth++; p++;
        } else if (*p == '}') {
            if (depth == 0) { *pp = p; return 0; }
            depth--; p++;
        } else {
            p++;
        }
    }
    return -1; /* unterminated block */
}

/* ========================= Statement execution ========================= */

/* Forward declarations for mutual recursion. */
static int exec_stmt(const char** pp);
static int exec_block_body(const char** pp);

/* exec_block_body — execute statements until '}' or end-of-string. */
static int exec_block_body(const char** pp)
{
    while (1) {
        const char* p = skip_ws(*pp);
        if (*p == '\0' || *p == '}') { *pp = p; return 0; }
        if (exec_stmt(pp) != 0) return -1;
    }
}

/* ------------------------------------------------------------------ */
/* if / else if / else                                                  */
/* ------------------------------------------------------------------ */
static int exec_if(const char** pp)
{
    const char* p = skip_ws(*pp);
    p += 2; /* consume 'if' */

    p = skip_ws(p);
    if (*p != '(') { vm_error("expected '(' after if"); return -1; }
    p++;
    *pp = p;

    int cond;
    if (eval_condition(pp, &cond) != 0) return -1;

    p = skip_ws(*pp);
    if (*p != ')') { vm_error("expected ')' after if condition"); return -1; }
    p++;
    p = skip_ws(p);
    if (*p != '{') { vm_error("expected '{' to open if block"); return -1; }
    p++;
    *pp = p;

    int taken = 0;
    if (cond) {
        taken = 1;
        if (exec_block_body(pp) != 0) return -1;
    } else {
        if (skip_block_body(pp) != 0) { vm_error("unterminated if block"); return -1; }
    }

    p = skip_ws(*pp);
    if (*p != '}') { vm_error("missing '}' to close if block"); return -1; }
    p++;
    *pp = p;

    /* Process any number of else-if / else branches. */
    while (1) {
        p = skip_ws(*pp);
        if (strncmp(p, "else", 4) != 0 || is_ident_char(p[4])) break;
        p += 4;
        p = skip_ws(p);

        if (strncmp(p, "if", 2) == 0 && !is_ident_char(p[2])) {
            /* else if branch */
            p += 2;
            p = skip_ws(p);
            if (*p != '(') { vm_error("expected '(' after else if"); return -1; }
            p++;
            *pp = p;

            int this_cond = 0;
            if (!taken) {
                if (eval_condition(pp, &this_cond) != 0) return -1;
            } else {
                /* Skip condition without evaluating */
                if (skip_to_close_paren(pp) != 0) {
                    vm_error("unterminated else-if condition");
                    return -1;
                }
            }

            p = skip_ws(*pp);
            if (*p != ')') { vm_error("expected ')' after else if condition"); return -1; }
            p++;
            p = skip_ws(p);
            if (*p != '{') { vm_error("expected '{' to open else if block"); return -1; }
            p++;
            *pp = p;

            if (!taken && this_cond) {
                taken = 1;
                if (exec_block_body(pp) != 0) return -1;
            } else {
                if (skip_block_body(pp) != 0) { vm_error("unterminated else if block"); return -1; }
            }

            p = skip_ws(*pp);
            if (*p != '}') { vm_error("missing '}' to close else if block"); return -1; }
            p++;
            *pp = p;

        } else {
            /* else branch — always the last in the chain */
            p = skip_ws(p);
            if (*p != '{') { vm_error("expected '{' after else"); return -1; }
            p++;
            *pp = p;

            if (!taken) {
                if (exec_block_body(pp) != 0) return -1;
            } else {
                if (skip_block_body(pp) != 0) { vm_error("unterminated else block"); return -1; }
            }

            p = skip_ws(*pp);
            if (*p != '}') { vm_error("missing '}' to close else block"); return -1; }
            p++;
            *pp = p;
            break; /* else terminates the chain */
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Variable declaration                                                 */
/* Handles: int, bool, string, char, array, int[], bool[], string[], char[] */
/* ------------------------------------------------------------------ */
static int exec_decl(const char** pp)
{
    const char* p = skip_ws(*pp);
    chivm_vkind_t decl_kind;
    chivm_vkind_t arr_elem_kind = VAL_NONE;

    /* Identify type (longest-match first to get int[] before int) */
    if      (strncmp(p, "int[]",    5) == 0 && !is_ident_char(p[5]))
        { decl_kind = VAL_ARRAY; arr_elem_kind = VAL_INT;    p += 5; }
    else if (strncmp(p, "bool[]",   6) == 0 && !is_ident_char(p[6]))
        { decl_kind = VAL_ARRAY; arr_elem_kind = VAL_BOOL;   p += 6; }
    else if (strncmp(p, "string[]", 8) == 0 && !is_ident_char(p[8]))
        { decl_kind = VAL_ARRAY; arr_elem_kind = VAL_STRING; p += 8; }
    else if (strncmp(p, "char[]",   6) == 0 && !is_ident_char(p[6]))
        { decl_kind = VAL_ARRAY; arr_elem_kind = VAL_CHAR;   p += 6; }
    else if (strncmp(p, "array",    5) == 0 && !is_ident_char(p[5]))
        { decl_kind = VAL_ARRAY; arr_elem_kind = VAL_NONE;   p += 5; }
    else if (strncmp(p, "string",   6) == 0 && !is_ident_char(p[6]))
        { decl_kind = VAL_STRING; p += 6; }
    else if (strncmp(p, "bool",     4) == 0 && !is_ident_char(p[4]))
        { decl_kind = VAL_BOOL;   p += 4; }
    else if (strncmp(p, "char",     4) == 0 && !is_ident_char(p[4]))
        { decl_kind = VAL_CHAR;   p += 4; }
    else if (strncmp(p, "int",      3) == 0 && !is_ident_char(p[3]))
        { decl_kind = VAL_INT;    p += 3; }
    else { vm_error("unknown type keyword"); return -1; }

    char name[CHIVM_MAX_NAME_LEN];
    *pp = p;
    if (!parse_ident(pp, name, sizeof(name))) {
        vm_error("expected variable name after type");
        return -1;
    }
    p = *pp;

    chivm_var_t* v = alloc_var(name, decl_kind);
    if (!v) { vm_error("variable table full"); return -1; }
    v->decl_kind = decl_kind;

    p = skip_ws(p);
    if (*p == '=') {
        p++;
        *pp = p;

        chivm_val_t init;
        if (decl_kind == VAL_ARRAY) {
            if (!parse_array_literal(pp, &init, arr_elem_kind)) {
                vm_error("invalid array initializer");
                return -1;
            }
        } else {
            if (!parse_val_expr(pp, &init)) {
                vm_error("invalid initializer");
                return -1;
            }
            if (init.kind != decl_kind) {
                vm_error("initializer type does not match declared type");
                return -1;
            }
        }
        v->val = init;
        p = *pp;
    } else {
        /* Default-initialize */
        memset(&v->val, 0, sizeof(chivm_val_t));
        v->val.kind = decl_kind;
        if (decl_kind == VAL_ARRAY) {
            v->val.u.arr.elem_type = arr_elem_kind;
        }
    }

    p = skip_ws(p);
    if (*p == ';') p++;
    *pp = p;
    return 0;
}

/* ------------------------------------------------------------------ */
/* cprintf("text with ${var} interpolation\n");                        */
/* ------------------------------------------------------------------ */
static int exec_cprintf(const char** pp)
{
    const char* p = *pp;
    p += 7; /* skip 'cprintf' */
    p = skip_ws(p);
    if (*p != '(') { vm_error("expected '(' after cprintf"); return -1; }
    p++;

    char text[CHIVM_MAX_LINE];
    if (!parse_quoted_string(&p, text, sizeof(text))) {
        vm_error("invalid cprintf string");
        return -1;
    }

    p = skip_ws(p);
    if (*p != ')') { vm_error("expected ')' after cprintf string"); return -1; }
    p++;
    p = skip_ws(p);
    if (*p == ';') p++;
    *pp = p;

    for (int i = 0; text[i] != '\0'; ) {
        if (text[i] == '$' && text[i + 1] == '{') {
            char vname[CHIVM_MAX_NAME_LEN];
            int n = 0;
            i += 2;
            while (text[i] && text[i] != '}') {
                if (n >= CHIVM_MAX_NAME_LEN - 1) { vm_error("interpolation name too long"); return -1; }
                vname[n++] = text[i++];
            }
            if (text[i] != '}') { vm_error("unterminated interpolation '}'"); return -1; }
            vname[n] = '\0';
            i++;

            chivm_var_t* v = find_var(vname);
            if (!v) { vm_error("unknown variable in interpolation"); return -1; }
            vm_print_val(&v->val);
            continue;
        }
        tty_putchar(text[i]);
        i++;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* dinput("prompt", varname);                                           */
/* Reads input according to the target variable's type.               */
/* ------------------------------------------------------------------ */
static int exec_dinput(const char** pp)
{
    const char* p = *pp;
    p += 6; /* skip 'dinput' */
    p = skip_ws(p);
    if (*p != '(') { vm_error("expected '(' after dinput"); return -1; }
    p++;

    char prompt[CHIVM_MAX_LINE];
    if (!parse_quoted_string(&p, prompt, sizeof(prompt))) {
        vm_error("invalid dinput prompt string");
        return -1;
    }

    p = skip_ws(p);
    if (*p != ',') { vm_error("expected ',' after dinput prompt"); return -1; }
    p++;

    char ident[CHIVM_MAX_NAME_LEN];
    if (!parse_ident(&p, ident, sizeof(ident))) {
        vm_error("expected variable name in dinput");
        return -1;
    }

    p = skip_ws(p);
    if (*p != ')') { vm_error("expected ')' after dinput arguments"); return -1; }
    p++;
    p = skip_ws(p);
    if (*p == ';') p++;
    *pp = p;

    if (prompt[0]) vm_print(prompt);

    char input[CHIVM_MAX_STR_LEN];
    tty_readline(input, sizeof(input));

    chivm_var_t* v = find_var(ident);
    chivm_vkind_t target = v ? v->val.kind : VAL_INT;

    if (!v) {
        /* Auto-create as int (legacy compat) */
        v = alloc_var(ident, VAL_INT);
        if (!v) { vm_error("variable table full"); return -1; }
        v->val.kind = VAL_INT;
    }

    switch (target) {
    case VAL_INT: {
        const char* ip = input;
        int val = 0;
        if (!parse_int_lit(&ip, &val)) { vm_error("dinput: expected integer"); return -1; }
        v->val.kind = VAL_INT;
        v->val.u.i  = val;
        break;
    }
    case VAL_BOOL:
        if (strcmp(input, "true") == 0 || strcmp(input, "1") == 0) {
            v->val.kind = VAL_BOOL; v->val.u.b = 1;
        } else if (strcmp(input, "false") == 0 || strcmp(input, "0") == 0) {
            v->val.kind = VAL_BOOL; v->val.u.b = 0;
        } else {
            vm_error("dinput: expected 'true' or 'false' for bool variable");
            return -1;
        }
        break;
    case VAL_STRING:
        v->val.kind = VAL_STRING;
        strncpy(v->val.u.s, input, CHIVM_MAX_STR_LEN - 1);
        v->val.u.s[CHIVM_MAX_STR_LEN - 1] = '\0';
        break;
    case VAL_CHAR:
        if (input[0] == '\0') { vm_error("dinput: expected a character for char variable"); return -1; }
        v->val.kind = VAL_CHAR;
        v->val.u.c  = input[0];
        break;
    default:
        vm_error("dinput: unsupported variable type");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Assignment: name = value;  or  name[idx] = value;                  */
/* ------------------------------------------------------------------ */
static int exec_assignment(const char** pp)
{
    const char* p = skip_ws(*pp);
    char name[CHIVM_MAX_NAME_LEN];

    if (!parse_ident(&p, name, sizeof(name))) {
        vm_error("expected identifier");
        return -1;
    }
    p = skip_ws(p);

    /* Array element assignment: name[idx] = value; */
    if (*p == '[') {
        p++;
        *pp = p;

        int idx;
        if (!parse_int_lit(pp, &idx)) {
            char iname[CHIVM_MAX_NAME_LEN];
            if (!parse_ident(pp, iname, sizeof(iname))) { vm_error("bad array index"); return -1; }
            chivm_var_t* iv = find_var(iname);
            if (!iv || iv->val.kind != VAL_INT) { vm_error("array index must be an int"); return -1; }
            idx = iv->val.u.i;
        }

        p = skip_ws(*pp);
        if (*p != ']') { vm_error("expected ']' after index"); return -1; }
        p++;
        p = skip_ws(p);
        if (*p != '=') { vm_error("expected '=' in array element assignment"); return -1; }
        p++;
        *pp = p;

        chivm_var_t* v = find_var(name);
        if (!v || v->val.kind != VAL_ARRAY) { vm_error("variable is not an array"); return -1; }
        if (idx < 0 || idx >= v->val.u.arr.count) { vm_error("array index out of bounds"); return -1; }

        chivm_val_t new_val;
        if (!parse_val_expr(pp, &new_val)) { vm_error("bad assignment value"); return -1; }

        if (v->val.u.arr.elem_type != VAL_NONE && new_val.kind != v->val.u.arr.elem_type) {
            vm_error("array element type mismatch (typed array)");
            return -1;
        }

        g_array_pool[v->val.u.arr.pool_start + idx] = new_val;
        p = skip_ws(*pp);
        if (*p == ';') p++;
        *pp = p;
        return 0;
    }

    /* Scalar assignment: name = value; */
    if (*p != '=') { vm_error("expected '=' in assignment"); return -1; }
    p++;
    *pp = p;

    chivm_var_t* v = find_var(name);
    if (!v) { vm_error("assignment to undefined variable"); return -1; }

    chivm_val_t new_val;
    if (!parse_val_expr(pp, &new_val)) { vm_error("bad assignment value"); return -1; }

    if (v->decl_kind != VAL_NONE && new_val.kind != v->decl_kind) {
        vm_error("assignment type does not match declared type");
        return -1;
    }

    v->val = new_val;
    p = skip_ws(*pp);
    if (*p == ';') p++;
    *pp = p;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Statement dispatcher                                                 */
/* ------------------------------------------------------------------ */
static int exec_stmt(const char** pp)
{
    const char* p = skip_ws(*pp);

    if (*p == '\0') return 0;

    /* Empty statement */
    if (*p == ';') { (*pp)++; return 0; }

    /* Line comment */
    if (*p == '/' && p[1] == '/') {
        while (*p && *p != '\n') p++;
        *pp = p;
        return 0;
    }

    /* if statement */
    if (strncmp(p, "if", 2) == 0 && !is_ident_char(p[2])) {
        *pp = p;
        return exec_if(pp);
    }

    /* Typed array declarations — check before plain types */
    if ((strncmp(p, "int[]",    5) == 0 && !is_ident_char(p[5])) ||
        (strncmp(p, "bool[]",   6) == 0 && !is_ident_char(p[6])) ||
        (strncmp(p, "string[]", 8) == 0 && !is_ident_char(p[8])) ||
        (strncmp(p, "char[]",   6) == 0 && !is_ident_char(p[6])) ||
        (strncmp(p, "array",    5) == 0 && !is_ident_char(p[5]))) {
        *pp = p;
        return exec_decl(pp);
    }

    /* Scalar type declarations */
    if ((strncmp(p, "string", 6) == 0 && !is_ident_char(p[6])) ||
        (strncmp(p, "bool",   4) == 0 && !is_ident_char(p[4])) ||
        (strncmp(p, "char",   4) == 0 && !is_ident_char(p[4])) ||
        (strncmp(p, "int",    3) == 0 && !is_ident_char(p[3]))) {
        *pp = p;
        return exec_decl(pp);
    }

    /* cprintf */
    if (strncmp(p, "cprintf", 7) == 0 && !is_ident_char(p[7])) {
        *pp = p;
        return exec_cprintf(pp);
    }

    /* dinput */
    if (strncmp(p, "dinput", 6) == 0 && !is_ident_char(p[6])) {
        *pp = p;
        return exec_dinput(pp);
    }

    /* Assignment (ident = ... or ident[idx] = ...) */
    if (is_ident_start(*p)) {
        *pp = p;
        return exec_assignment(pp);
    }

    vm_error("unrecognized statement");
    return -1;
}

/* ========================= Source entry point ========================= */

static int exec_source(const char* source)
{
    const char* p = source;
    return exec_block_body(&p);
}

/* ========================= File execution ========================= */

int chivm_run_file(const char* filename)
{
    char source[CHIVM_MAX_SOURCE];
    uint64_t size = 0, out_size = 0;

    if (!filename || !*filename) { vm_error("missing filename"); return -1; }

    vm_reset();

    if (bfs_file_size(&g_bfs, filename, &size) != 0) { vm_error("file not found"); return -1; }
    if (size >= sizeof(source)) { vm_error("source file too large"); return -1; }
    if (bfs_read_file(&g_bfs, filename, source, sizeof(source) - 1, &out_size) != 0) {
        vm_error("failed to read source file");
        return -1;
    }
    source[out_size] = '\0';
    return exec_source(source);
}

/* ========================= Intel HEX codec ========================= */

static int ihex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int ihex_byte(const char* p, uint8_t* out)
{
    int hi = ihex_nibble(p[0]), lo = ihex_nibble(p[1]);
    if (hi < 0 || lo < 0) return 0;
    *out = (uint8_t)((hi << 4) | lo);
    return 1;
}

static int ihex_decode(const char* hex, char* out, int out_cap)
{
    int total = 0;
    const char* p = hex;

    while (*p) {
        while (*p == '\r' || *p == '\n' || *p == ' ') p++;
        if (!*p) break;
        if (*p != ':') return -1;
        p++;

        uint8_t count, addrhi, addrlo, rectype;
        if (!ihex_byte(p, &count))   return -1; p += 2;
        if (!ihex_byte(p, &addrhi))  return -1; p += 2;
        if (!ihex_byte(p, &addrlo))  return -1; p += 2;
        if (!ihex_byte(p, &rectype)) return -1; p += 2;
        (void)addrhi; (void)addrlo;

        if (rectype == 0x01) break;

        if (rectype == 0x00) {
            for (int i = 0; i < (int)count; i++) {
                uint8_t b;
                if (!ihex_byte(p, &b)) return -1;
                p += 2;
                if (total >= out_cap - 1) return -1;
                out[total++] = (char)b;
            }
        } else {
            p += (int)count * 2;
        }
        p += 2;
        while (*p && *p != '\n') p++;
    }
    return total;
}

int chivm_exec_cmdt(const char* filename)
{
    static char cmdt[CHIVM_MAX_CMDT];
    char source[CHIVM_MAX_SOURCE];
    uint64_t file_size = 0, read_size = 0;

    if (!filename || !*filename) { vm_error("missing filename"); return -1; }
    if (bfs_file_size(&g_bfs, filename, &file_size) != 0) { vm_error("file not found"); return -1; }
    if (file_size >= sizeof(cmdt)) { vm_error("cmdt file too large"); return -1; }
    if (bfs_read_file(&g_bfs, filename, cmdt, sizeof(cmdt) - 1, &read_size) != 0) {
        vm_error("failed to read cmdt file");
        return -1;
    }
    cmdt[read_size] = '\0';

    int decoded = ihex_decode(cmdt, source, sizeof(source));
    if (decoded < 0) { vm_error("invalid .cmdt (ihex decode failed)"); return -1; }
    source[decoded] = '\0';

    vm_reset();
    return exec_source(source);
}

static char hex_digit(uint8_t v)
{
    v &= 0x0F;
    return (v < 10) ? (char)('0' + v) : (char)('A' + (v - 10));
}

static int out_ch(char* out, int cap, int* pos, char c)
{
    if (*pos >= cap - 1) return -1;
    out[(*pos)++] = c;
    return 0;
}

static int out_hex_byte(char* out, int cap, int* pos, uint8_t b)
{
    if (out_ch(out, cap, pos, hex_digit((uint8_t)(b >> 4))) < 0) return -1;
    if (out_ch(out, cap, pos, hex_digit((uint8_t)(b & 0x0F))) < 0) return -1;
    return 0;
}

static int emit_hex_record(char* out, int cap, int* pos,
                            uint8_t count, uint16_t addr,
                            uint8_t rectype, const uint8_t* data)
{
    uint8_t sum = (uint8_t)(count + (uint8_t)(addr >> 8) + (uint8_t)(addr & 0xFF) + rectype);
    if (out_ch(out, cap, pos, ':') < 0)            return -1;
    if (out_hex_byte(out, cap, pos, count) < 0)    return -1;
    if (out_hex_byte(out, cap, pos, (uint8_t)(addr >> 8)) < 0)  return -1;
    if (out_hex_byte(out, cap, pos, (uint8_t)(addr & 0xFF)) < 0) return -1;
    if (out_hex_byte(out, cap, pos, rectype) < 0)  return -1;
    for (uint8_t i = 0; i < count; i++) {
        uint8_t b = data ? data[i] : 0;
        sum = (uint8_t)(sum + b);
        if (out_hex_byte(out, cap, pos, b) < 0) return -1;
    }
    if (out_hex_byte(out, cap, pos, (uint8_t)(0 - sum)) < 0) return -1;
    if (out_ch(out, cap, pos, '\n') < 0) return -1;
    return 0;
}

int chivm_compile_file(const char* src_filename, const char* out_cmdt_filename)
{
    char source[CHIVM_MAX_SOURCE];
    char cmdt[CHIVM_MAX_CMDT];
    uint64_t src_size = 0, out_size = 0;
    int pos = 0;

    if (!src_filename || !*src_filename || !out_cmdt_filename || !*out_cmdt_filename) {
        vm_error("usage: chivm_compile_file(<src>, <out.cmdt>)");
        return -1;
    }
    if (bfs_file_size(&g_bfs, src_filename, &src_size) != 0) { vm_error("source file not found"); return -1; }
    if (src_size == 0 || src_size >= sizeof(source)) { vm_error("invalid source size"); return -1; }
    if (bfs_read_file(&g_bfs, src_filename, source, sizeof(source) - 1, &out_size) != 0) {
        vm_error("failed to read source file");
        return -1;
    }
    source[out_size] = '\0';

    uint16_t addr = 0;
    uint64_t off  = 0;
    while (off < out_size) {
        uint8_t chunk = (uint8_t)((out_size - off) > 16 ? 16 : (out_size - off));
        if (emit_hex_record(cmdt, sizeof(cmdt), &pos, chunk, addr, 0x00,
                            (const uint8_t*)(source + off)) < 0) {
            vm_error("compiled .cmdt output too large");
            return -1;
        }
        off  += chunk;
        addr  = (uint16_t)(addr + chunk);
    }
    if (emit_hex_record(cmdt, sizeof(cmdt), &pos, 0, 0, 0x01, (const uint8_t*)0) < 0) {
        vm_error("failed to emit .cmdt EOF record");
        return -1;
    }
    cmdt[pos] = '\0';

    (void)bfs_create(&g_bfs, out_cmdt_filename, (uint64_t)pos);
    if (bfs_write_file(&g_bfs, out_cmdt_filename, cmdt, (uint64_t)pos) != 0) {
        vm_error("failed to write .cmdt file");
        return -1;
    }
    return 0;
}

/* ========================= Interactive REPL ========================= */

/*
 * The REPL accumulates input lines until curly braces are balanced,
 * then executes the complete buffer.  This allows multi-line if blocks.
 *
 * Brace counting is simple (does not ignore braces inside string literals).
 * Sufficient for practical use; edge cases can be worked around by the user.
 */
void chivm_run(void)
{
    static char repl_buf[CHIVM_MAX_SOURCE / 2];
    char line[CHIVM_MAX_LINE];
    int  buf_pos    = 0;
    int  brace_depth = 0;

    vm_reset();
    tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
    vm_println("ChibiVM (int / bool / string / char / array / if-else)");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    vm_println("Type 'help' for commands, 'exit' to return to shell.");

    while (1) {
        if (brace_depth == 0) {
            tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
            vm_print("chivm> ");
        } else {
            tty_set_color(TTY_YELLOW, TTY_BLACK);
            vm_print("  ...  ");
        }
        tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
        tty_readline(line, sizeof(line));

        /* Top-level REPL commands */
        if (brace_depth == 0) {
            if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) break;

            if (strcmp(line, "help") == 0) {
                vm_println("Commands:");
                vm_println("  help              Show this help");
                vm_println("  reset             Clear all variables");
                vm_println("  run <file>        Execute source file from BranchFS");
                vm_println("  exit              Return to GraceOS shell");
                vm_println("");
                vm_println("Types:  int  bool  string  char  array  int[]  bool[]  string[]  char[]");
                vm_println("  int x = 42;");
                vm_println("  bool flag = true;");
                vm_println("  string s = \"hello\";");
                vm_println("  char c = 'A';");
                vm_println("  array mixed = [1, \"two\", true];");
                vm_println("  int[] nums = [10, 20, 30];");
                vm_println("");
                vm_println("Conditionals:");
                vm_println("  if (x > 10) { cprintf(\"big\\n\"); }");
                vm_println("  else if (x == 10) { cprintf(\"ten\\n\"); }");
                vm_println("  else { cprintf(\"small\\n\"); }");
                vm_println("");
                vm_println("Output:   cprintf(\"val=${x}\\n\");");
                vm_println("Input:    dinput(\"prompt: \", x);");
                continue;
            }

            if (strcmp(line, "reset") == 0) {
                vm_reset();
                buf_pos = 0; brace_depth = 0;
                repl_buf[0] = '\0';
                vm_println("state reset");
                continue;
            }

            if (strncmp(line, "run ", 4) == 0) {
                const char* file = skip_ws(line + 4);
                if (chivm_run_file(file) == 0)
                    vm_println("[chivm] file executed");
                continue;
            }
        }

        /* Accumulate line into buffer */
        int len = vm_strlen(line);
        if (buf_pos + len + 2 >= (int)sizeof(repl_buf)) {
            vm_error("REPL input buffer full — resetting");
            buf_pos = 0; brace_depth = 0;
            repl_buf[0] = '\0';
            continue;
        }
        for (int i = 0; i < len; i++) repl_buf[buf_pos++] = line[i];
        repl_buf[buf_pos++] = '\n';
        repl_buf[buf_pos]   = '\0';

        /* Count curly braces in the just-added line */
        for (int i = 0; line[i]; i++) {
            if (line[i] == '{') brace_depth++;
            else if (line[i] == '}') brace_depth--;
        }
        if (brace_depth < 0) brace_depth = 0;

        /* Execute when braces are balanced */
        if (brace_depth == 0) {
            const char* p = repl_buf;
            if (exec_block_body(&p) != 0)
                vm_println("[chivm] statement failed");
            buf_pos      = 0;
            repl_buf[0]  = '\0';
        }
    }

    vm_println("Leaving ChibiVM.");
}
