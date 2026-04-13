/*
 * llm.c — GraceOS LLM userland wrapper library
 *
 * Thin wrappers over the LLM INT 0x80 syscalls.
 * No dynamic allocation; all buffers are caller-supplied.
 */

#include "llm.h"

/* -----------------------------------------------------------------------
 * Syscall stubs declared in syscall.asm (grace.h pulls these in)
 * --------------------------------------------------------------------- */
extern long __syscall1(long num, long a1);
extern long __syscall2(long num, long a1, long a2);
extern long __syscall3(long num, long a1, long a2, long a3);
extern long __syscall4(long num, long a1, long a2, long a3, long a4);
extern long __syscall5(long num, long a1, long a2, long a3, long a4, long a5);

/* -----------------------------------------------------------------------
 * Model management
 * --------------------------------------------------------------------- */

llm_model_id_t llm_load_model(const char* path)
{
    return (llm_model_id_t)__syscall1(SYS_LLM_LOAD_MODEL, (long)path);
}

int llm_unload_model(llm_model_id_t model_id)
{
    return (int)__syscall1(SYS_LLM_UNLOAD_MODEL, (long)model_id);
}

int llm_get_model_info(llm_model_id_t model_id, char* buf, int buf_sz)
{
    return (int)__syscall3(SYS_LLM_GET_INFO,
                           (long)model_id, (long)buf, (long)buf_sz);
}

/* -----------------------------------------------------------------------
 * Context management
 * --------------------------------------------------------------------- */

llm_ctx_id_t llm_create_context(llm_model_id_t model_id, int ctx_len)
{
    return (llm_ctx_id_t)__syscall2(SYS_LLM_CREATE_CONTEXT,
                                    (long)model_id, (long)ctx_len);
}

int llm_destroy_context(llm_ctx_id_t ctx_id)
{
    return (int)__syscall1(SYS_LLM_DESTROY_CONTEXT, (long)ctx_id);
}

/* -----------------------------------------------------------------------
 * Low-level inference
 * --------------------------------------------------------------------- */

int llm_tokenize(llm_ctx_id_t ctx_id, const char* text,
                 llama_token* out_tokens, int max_tokens)
{
    return (int)__syscall4(SYS_LLM_TOKENIZE,
                           (long)ctx_id, (long)text,
                           (long)out_tokens, (long)max_tokens);
}

int llm_detokenize(llm_ctx_id_t ctx_id, const llama_token* tokens,
                   int n_tokens, char* buf, int buf_sz)
{
    return (int)__syscall5(SYS_LLM_DETOKENIZE,
                           (long)ctx_id, (long)tokens,
                           (long)n_tokens, (long)buf, (long)buf_sz);
}

int llm_eval(llm_ctx_id_t ctx_id, const llama_token* tokens, int n_tokens)
{
    return (int)__syscall3(SYS_LLM_EVAL,
                           (long)ctx_id, (long)tokens, (long)n_tokens);
}

llama_token llm_sample(llm_ctx_id_t ctx_id)
{
    return (llama_token)__syscall1(SYS_LLM_SAMPLE, (long)ctx_id);
}

/* -----------------------------------------------------------------------
 * Chat API
 * --------------------------------------------------------------------- */

int llm_chat_begin(llm_ctx_id_t ctx_id, int tmpl_id,
                   const char* system_prompt)
{
    /* If LLM_TMPL_AUTO (-1) is requested the kernel will auto-detect;
     * map it to 7 (LLM_CHAT_TEMPLATE_UNKNOWN) so the kernel handles it. */
    int tmpl = (tmpl_id == LLM_TMPL_AUTO) ? 7 : tmpl_id;
    return (int)__syscall3(SYS_LLM_CHAT_BEGIN,
                           (long)ctx_id, (long)tmpl, (long)system_prompt);
}

int llm_chat_append(llm_ctx_id_t ctx_id, int role, const char* content)
{
    return (int)__syscall3(SYS_LLM_CHAT_APPEND,
                           (long)ctx_id, (long)role, (long)content);
}

int llm_chat_generate(llm_ctx_id_t ctx_id, char* out_buf,
                      int max_tokens, int buf_sz)
{
    return (int)__syscall4(SYS_LLM_CHAT_GENERATE,
                           (long)ctx_id, (long)out_buf,
                           (long)max_tokens, (long)buf_sz);
}

int llm_chat_reset(llm_ctx_id_t ctx_id)
{
    return (int)__syscall1(SYS_LLM_CHAT_RESET, (long)ctx_id);
}

/* -----------------------------------------------------------------------
 * Convenience: one-shot ask
 * --------------------------------------------------------------------- */

int llm_ask(const char* model_path, const char* system_prompt,
            const char* user_msg, char* out_buf, int buf_sz, int max_tokens)
{
    if (!model_path || !user_msg || !out_buf || buf_sz <= 0) return -1;

    llm_model_id_t model = llm_load_model(model_path);
    if (model == LLM_INVALID_ID) return -1;

    llm_ctx_id_t ctx = llm_create_context(model, 512);
    if (ctx == LLM_INVALID_ID) {
        llm_unload_model(model);
        return -1;
    }

    /* Use ChatML by default; auto-detect would require an extra info call */
    llm_chat_begin(ctx, LLM_TMPL_CHATML, system_prompt);
    llm_chat_append(ctx, LLM_ROLE_USER, user_msg);

    int bytes = llm_chat_generate(ctx, out_buf, max_tokens, buf_sz);

    llm_destroy_context(ctx);
    llm_unload_model(model);
    return bytes;
}
