#ifndef GRACEOS_CHIVM_H
#define GRACEOS_CHIVM_H

/* ============================
   ChibiVM (Step 1)
   Minimal VM runtime for:
   - int variables
   - cprintf("...") output
   - dinput("...", var) input
   ============================ */

void chivm_run(void);
int chivm_run_file(const char* filename);
int chivm_compile_file(const char* src_filename, const char* out_cmdt_filename);
int chivm_exec_cmdt(const char* filename);

#endif /* GRACEOS_CHIVM_H */
