#ifndef GRACEOS_CHIVM_H
#define GRACEOS_CHIVM_H

/* ============================
   ChibiVM
   Scripting VM for GraceOS userland.

   Types:
     int      — 32-bit signed integer
     bool     — true / false
     string   — up to 127-char UTF-8 text
     char     — single character
     array    — heterogeneous array  (array x = [1, "hi", true];)
     T[]      — homogeneous array    (int[] n = [1, 2, 3];)

   Control flow:
     if (cond) { ... }
     if (cond) { ... } else if (cond) { ... } else { ... }

   I/O:
     cprintf("text ${var}\n");   — output with variable interpolation
     dinput("prompt", var);      — input (type inferred from var)

   Comparison operators in conditions: == != < > <= >=
   Unary negation in conditions: !value
   ============================ */

void chivm_run(void);
int  chivm_run_file(const char* filename);
int  chivm_compile_file(const char* src_filename, const char* out_cmdt_filename);
int  chivm_exec_cmdt(const char* filename);

#endif /* GRACEOS_CHIVM_H */
