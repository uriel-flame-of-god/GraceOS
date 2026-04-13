/*
 * llm.h — GraceOS LLM userland API
 *
 * This header and the accompanying llm.c provide thin wrappers over the
 * LLM INT 0x80 syscalls for use by userland applications.
 *
 * Usage example:
 *
 *   int model = llm_load_model("/models/mistral-7b-q4.gguf");
 *   int ctx   = llm_create_context(model, 512);
 *   char reply[1024];
 *   llm_chat_generate(ctx, "Hello!", reply, 256, 100);
 *   llm_put_string(reply);
 *   llm_destroy_context(ctx);
 *   llm_unload_model(model);
 */

#ifndef LIBGRACE_LLM_H
#define LIBGRACE_LLM_H

#include "grace.h"
#include "../../include/grace/llm_syscalls.h"

/* Opaque integer handle types (typedef to int for clarity) */
typedef int llm_model_id_t;
typedef int llm_ctx_id_t;
typedef int32_t llama_token;

/* Chat roles — must match kernel llm_chat_role_t */
#define LLM_ROLE_SYSTEM    0
#define LLM_ROLE_USER      1
#define LLM_ROLE_ASSISTANT 2

/* Chat template IDs — must match kernel llm_chat_template_t */
#define LLM_TMPL_CHATML    0
#define LLM_TMPL_LLAMA2    1
#define LLM_TMPL_LLAMA3    2
#define LLM_TMPL_MISTRAL   3
#define LLM_TMPL_VICUNA    4
#define LLM_TMPL_ALPACA    5
#define LLM_TMPL_RAW       6
#define LLM_TMPL_AUTO      (-1)  /* kernel chooses based on model arch */

/* Error sentinel */
#define LLM_INVALID_ID     (-1)

/* -----------------------------------------------------------------------
 * Model management
 * --------------------------------------------------------------------- */

/* Load a GGUF model from the BranchFS path.
 * Returns a non-negative model_id on success, LLM_INVALID_ID on error. */
llm_model_id_t llm_load_model  (const char* path);

/* Release all resources associated with the model.
 * Any contexts using this model are automatically destroyed. */
int            llm_unload_model(llm_model_id_t model_id);

/* Fill `buf` (size `buf_sz`) with a JSON summary of the model.
 * Returns bytes written or -1. */
int            llm_get_model_info(llm_model_id_t model_id,
                                  char* buf, int buf_sz);

/* -----------------------------------------------------------------------
 * Context management
 * --------------------------------------------------------------------- */

/* Create an inference context of `ctx_len` tokens backed by `model_id`.
 * Returns a non-negative ctx_id on success, LLM_INVALID_ID on error. */
llm_ctx_id_t   llm_create_context (llm_model_id_t model_id, int ctx_len);

/* Destroy the context and free all associated SASY segments. */
int            llm_destroy_context(llm_ctx_id_t ctx_id);

/* -----------------------------------------------------------------------
 * Low-level inference API
 * --------------------------------------------------------------------- */

/* Tokenise NUL-terminated `text` into `out_tokens[0..max_tokens-1]`.
 * Returns the number of tokens written, or -1 on error. */
int llm_tokenize  (llm_ctx_id_t ctx_id, const char* text,
                   llama_token* out_tokens, int max_tokens);

/* Detokenise `n_tokens` tokens into NUL-terminated `buf[0..buf_sz-1]`.
 * Returns bytes written or -1. */
int llm_detokenize(llm_ctx_id_t ctx_id, const llama_token* tokens,
                   int n_tokens, char* buf, int buf_sz);

/* Feed `n_tokens` tokens into the KV cache of `ctx_id`.
 * Must be called before llm_sample().  Returns 0 on success. */
int llm_eval      (llm_ctx_id_t ctx_id, const llama_token* tokens,
                   int n_tokens);

/* Sample the next token from the current logits.
 * Returns the token id or -1 on error. */
llama_token llm_sample(llm_ctx_id_t ctx_id);

/* -----------------------------------------------------------------------
 * High-level chat API
 * --------------------------------------------------------------------- */

/* Initialise the chat session for `ctx_id`.
 * `tmpl_id` is one of LLM_TMPL_*.  Pass LLM_TMPL_AUTO to auto-detect.
 * `system_prompt` may be NULL.  Returns 0 on success. */
int llm_chat_begin   (llm_ctx_id_t ctx_id, int tmpl_id,
                      const char* system_prompt);

/* Append a message to the chat history.
 * `role` is one of LLM_ROLE_*.  Returns 0 on success. */
int llm_chat_append  (llm_ctx_id_t ctx_id, int role, const char* content);

/* Generate an assistant reply.
 * Formats the full chat history, runs inference for up to `max_tokens`
 * new tokens, writes the response to `out_buf[0..buf_sz-1]` (NUL-
 * terminated), and automatically appends the response to chat history.
 * Returns bytes written or -1. */
int llm_chat_generate(llm_ctx_id_t ctx_id, char* out_buf,
                      int max_tokens, int buf_sz);

/* Reset chat history and KV cache for a new conversation. */
int llm_chat_reset   (llm_ctx_id_t ctx_id);

/* -----------------------------------------------------------------------
 * Convenience: single-turn completion
 * --------------------------------------------------------------------- */

/* One-shot: load model, create context, ask, destroy context.
 * `model_path` and `user_msg` are required; `system_prompt` may be NULL.
 * Writes the reply into `out_buf`.  Returns bytes written or -1.
 * The created model/context are destroyed before returning. */
int llm_ask(const char* model_path, const char* system_prompt,
            const char* user_msg, char* out_buf, int buf_sz, int max_tokens);

#endif /* LIBGRACE_LLM_H */
