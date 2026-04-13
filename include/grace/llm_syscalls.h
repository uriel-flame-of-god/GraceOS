/*
 * llm_syscalls.h — LLM runtime syscall numbers
 *
 * Syscall numbers 150–162 are reserved for the GraceOS LLM runtime.
 * These definitions are shared between kernel (kernel/llm/runtime.h)
 * and userland (lib/libgrace/llm.h).
 *
 * ABI: INT 0x80 / RAX=num, RDI=a1, RSI=a2, RDX=a3, R10=a4, R8=a5
 */

#ifndef GRACE_LLM_SYSCALLS_H
#define GRACE_LLM_SYSCALLS_H

#define SYS_LLM_LOAD_MODEL       150  /* (path)                        → model_id / -1 */
#define SYS_LLM_UNLOAD_MODEL     151  /* (model_id)                    → 0 / -1        */
#define SYS_LLM_CREATE_CONTEXT   152  /* (model_id, ctx_len)           → ctx_id / -1   */
#define SYS_LLM_DESTROY_CONTEXT  153  /* (ctx_id)                      → 0 / -1        */
#define SYS_LLM_TOKENIZE         154  /* (ctx_id, text, toks, max)     → n / -1        */
#define SYS_LLM_DETOKENIZE       155  /* (ctx_id, toks, n, buf, sz)    → bytes / -1    */
#define SYS_LLM_EVAL             156  /* (ctx_id, toks, n)             → 0 / -1        */
#define SYS_LLM_SAMPLE           157  /* (ctx_id)                      → token / -1    */
#define SYS_LLM_CHAT_BEGIN       158  /* (ctx_id, tmpl_id, sys_prompt) → 0 / -1        */
#define SYS_LLM_CHAT_APPEND      159  /* (ctx_id, role, content)       → 0 / -1        */
#define SYS_LLM_CHAT_GENERATE    160  /* (ctx_id, buf, max_tok, sz)    → bytes / -1    */
#define SYS_LLM_CHAT_RESET       161  /* (ctx_id)                      → 0 / -1        */
#define SYS_LLM_GET_INFO         162  /* (model_id, buf, sz)           → bytes / -1    */

#endif /* GRACE_LLM_SYSCALLS_H */
