/*
 * llama-chat.h — Chat template formatting
 *
 * Converts a sequence of user/assistant messages into a single prompt string
 * compatible with the model's native chat format (ChatML, Llama-2/3, Mistral,
 * Vicuna, or raw).
 *
 * All operations write into a caller-supplied buffer; no dynamic allocation.
 */

#ifndef LLAMA_CHAT_H
#define LLAMA_CHAT_H

#include "../../lib/libc/int.h"

/* Supported chat template families */
typedef enum {
    LLM_CHAT_TEMPLATE_CHATML    = 0,  /* <|im_start|>role\ncontent<|im_end|>\n  */
    LLM_CHAT_TEMPLATE_LLAMA2    = 1,  /* [INST] user [/INST] assistant </s>      */
    LLM_CHAT_TEMPLATE_LLAMA3    = 2,  /* <|start_header_id|>role<|end_header_id|> */
    LLM_CHAT_TEMPLATE_MISTRAL   = 3,  /* [INST] user [/INST] assistant </s>      */
    LLM_CHAT_TEMPLATE_VICUNA    = 4,  /* USER: msg\nASSISTANT: msg\n             */
    LLM_CHAT_TEMPLATE_ALPACA    = 5,  /* ### Instruction:\n...\n### Response:\n  */
    LLM_CHAT_TEMPLATE_RAW       = 6,  /* No formatting; concatenate as-is        */
    LLM_CHAT_TEMPLATE_UNKNOWN   = 7,
    LLM_CHAT_TEMPLATE__COUNT
} llm_chat_template_t;

/* Role identifiers */
typedef enum {
    LLM_CHAT_ROLE_SYSTEM    = 0,
    LLM_CHAT_ROLE_USER      = 1,
    LLM_CHAT_ROLE_ASSISTANT = 2,
    LLM_CHAT_ROLE__COUNT
} llm_chat_role_t;

/* Single chat message */
typedef struct {
    llm_chat_role_t role;
    const char*     content;   /* NUL-terminated, must remain valid during format */
} llm_chat_message_t;

/* Chat session context (stack-allocated) */
#define LLM_CHAT_MAX_MESSAGES 64

typedef struct {
    llm_chat_template_t  tmpl;
    llm_chat_message_t   messages[LLM_CHAT_MAX_MESSAGES];
    int                  n_messages;
    const char*          system_prompt;
} llm_chat_ctx_t;

/* ---------------------------------------------------------------------- */
/* API                                                                      */
/* ---------------------------------------------------------------------- */

/* Detect template from GGUF "tokenizer.chat_template" or model arch string.
 * Returns LLM_CHAT_TEMPLATE_UNKNOWN if unrecognized. */
llm_chat_template_t llm_chat_template_detect(const char* template_or_arch);

/* Initialize a chat context with the given template. */
void llm_chat_ctx_init(llm_chat_ctx_t* ctx, llm_chat_template_t tmpl,
                       const char* system_prompt);

/* Append a message to the context.  Returns 0 on success, -1 if full. */
int  llm_chat_ctx_append(llm_chat_ctx_t* ctx, llm_chat_role_t role,
                         const char* content);

/* Clear all messages (retains template and system prompt). */
void llm_chat_ctx_reset(llm_chat_ctx_t* ctx);

/* Format all messages in the context into `buf` (size `buf_sz`).
 * Appends the generation prefix so the model produces the next assistant turn.
 * Returns number of bytes written (excluding NUL), or -1 on overflow. */
int  llm_chat_format(const llm_chat_ctx_t* ctx, char* buf, int buf_sz);

/* Format a single user query into buf (no history).
 * Convenience wrapper around llm_chat_ctx_*. */
int  llm_chat_format_single(llm_chat_template_t tmpl,
                             const char* system_prompt,
                             const char* user_msg,
                             char* buf, int buf_sz);

/* Return human-readable name of a template. */
const char* llm_chat_template_name(llm_chat_template_t tmpl);

#endif /* LLAMA_CHAT_H */
