/*
 * runtime.h — GraceOS LLM Runtime
 *
 * Maintains a fixed-size table of loaded models and inference contexts.
 * All sys_llm_* handlers are declared here; they are wired into the
 * syscall dispatcher in kernel/sys/syscall.c.
 */

#ifndef LLM_RUNTIME_H
#define LLM_RUNTIME_H

#include "../../lib/libc/int.h"
#include "llama-context.h"
#include "llama-sampler.h"
#include "llama-chat.h"
#include "llama-adapter.h"

/* -------------------------------------------------------------------------
 * Global limits
 * ------------------------------------------------------------------------- */

#define LLM_MAX_MODELS   4
#define LLM_MAX_CONTEXTS 8

/* -------------------------------------------------------------------------
 * Runtime model slot
 * The model itself is allocated inline; only n_ctx is stored here.
 * ------------------------------------------------------------------------- */

typedef struct {
    llm_model_t  model;
    int          in_use;
} llm_model_slot_t;

/* -------------------------------------------------------------------------
 * Runtime context slot
 * Pairs a context with a sampler chain and an optional chat session.
 * ------------------------------------------------------------------------- */

typedef struct {
    llm_ctx_t           ctx;
    llm_sampler_chain_t sampler;
    llm_chat_ctx_t      chat;
    int                 model_id;
    int                 in_use;
    uint64_t            rng;
} llm_ctx_slot_t;

/* -------------------------------------------------------------------------
 * Global tables (defined in runtime.c)
 * ------------------------------------------------------------------------- */

extern llm_model_slot_t g_llm_models[LLM_MAX_MODELS];
extern llm_ctx_slot_t   g_llm_contexts[LLM_MAX_CONTEXTS];

/* -------------------------------------------------------------------------
 * Runtime initialisation (called from llm_syscalls_init())
 * ------------------------------------------------------------------------- */
void llm_runtime_init(void);

/* -------------------------------------------------------------------------
 * sys_llm_* handlers — called from syscall dispatcher
 * All take up to 5 uint64_t arguments (matching the GraceOS syscall ABI).
 * Return value: 0 or positive = success, negative = error code.
 * -------------------------------------------------------------------------
 *
 *  SYS_LLM_LOAD_MODEL      (150)
 *    a1 = (uint64_t)(const char*) path   — NUL-terminated BranchFS path
 *    returns: model_id (0..3) on success, -1 on error
 *
 *  SYS_LLM_UNLOAD_MODEL    (151)
 *    a1 = model_id
 *    returns: 0 on success, -1 on error
 *
 *  SYS_LLM_CREATE_CONTEXT  (152)
 *    a1 = model_id
 *    a2 = ctx_len  (max tokens in context window, clamped to model max)
 *    returns: ctx_id (0..7) on success, -1 on error
 *
 *  SYS_LLM_DESTROY_CONTEXT (153)
 *    a1 = ctx_id
 *    returns: 0 on success, -1 on error
 *
 *  SYS_LLM_TOKENIZE        (154)
 *    a1 = ctx_id
 *    a2 = (uint64_t)(const char*) text
 *    a3 = (uint64_t)(llama_token*) out_tokens   — caller-allocated array
 *    a4 = max_tokens (capacity of out_tokens)
 *    returns: number of tokens written, -1 on error
 *
 *  SYS_LLM_DETOKENIZE      (155)
 *    a1 = ctx_id
 *    a2 = (uint64_t)(const llama_token*) tokens
 *    a3 = n_tokens
 *    a4 = (uint64_t)(char*) buf
 *    a5 = buf_len
 *    returns: bytes written (excl. NUL), -1 on error
 *
 *  SYS_LLM_EVAL            (156)
 *    a1 = ctx_id
 *    a2 = (uint64_t)(const llama_token*) tokens
 *    a3 = n_tokens
 *    returns: 0 on success, -1 on error
 *
 *  SYS_LLM_SAMPLE          (157)
 *    a1 = ctx_id
 *    returns: next token id on success, -1 on error
 *
 *  SYS_LLM_CHAT_BEGIN      (158)
 *    a1 = ctx_id
 *    a2 = template_id (llm_chat_template_t)
 *    a3 = (uint64_t)(const char*) system_prompt  (may be 0)
 *    returns: 0 on success, -1 on error
 *
 *  SYS_LLM_CHAT_APPEND     (159)
 *    a1 = ctx_id
 *    a2 = role  (llm_chat_role_t)
 *    a3 = (uint64_t)(const char*) content
 *    returns: 0 on success, -1 on error
 *
 *  SYS_LLM_CHAT_GENERATE   (160)
 *    a1 = ctx_id
 *    a2 = (uint64_t)(char*) out_buf
 *    a3 = max_tokens
 *    a4 = buf_sz
 *    returns: bytes written (excl. NUL), -1 on error
 *
 *  SYS_LLM_CHAT_RESET      (161)
 *    a1 = ctx_id
 *    returns: 0 on success, -1 on error
 *
 *  SYS_LLM_GET_INFO        (162)
 *    a1 = model_id
 *    a2 = (uint64_t)(char*) out_buf   — filled with JSON-like summary
 *    a3 = buf_sz
 *    returns: bytes written, -1 on error
 */

long sys_llm_load_model     (long a1, long a2, long a3, long a4, long a5, long a6);
long sys_llm_unload_model   (long a1, long a2, long a3, long a4, long a5, long a6);
long sys_llm_create_context (long a1, long a2, long a3, long a4, long a5, long a6);
long sys_llm_destroy_context(long a1, long a2, long a3, long a4, long a5, long a6);
long sys_llm_tokenize       (long a1, long a2, long a3, long a4, long a5, long a6);
long sys_llm_detokenize     (long a1, long a2, long a3, long a4, long a5, long a6);
long sys_llm_eval           (long a1, long a2, long a3, long a4, long a5, long a6);
long sys_llm_sample         (long a1, long a2, long a3, long a4, long a5, long a6);
long sys_llm_chat_begin     (long a1, long a2, long a3, long a4, long a5, long a6);
long sys_llm_chat_append    (long a1, long a2, long a3, long a4, long a5, long a6);
long sys_llm_chat_generate  (long a1, long a2, long a3, long a4, long a5, long a6);
long sys_llm_chat_reset     (long a1, long a2, long a3, long a4, long a5, long a6);
long sys_llm_get_info       (long a1, long a2, long a3, long a4, long a5, long a6);

/* Register all LLM syscalls into the GraceOS syscall table. */
void llm_syscalls_init(void);

#endif /* LLM_RUNTIME_H */
