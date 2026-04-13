/*
 * llama-chat.c — Chat template formatting implementation
 *
 * Each template is implemented as a pure formatting function.
 * All output goes into caller-supplied buffers — no malloc.
 */

#include "llama-chat.h"
#include "llama-impl.h"
#include "../log/klog.h"
#include "../../lib/libc/string.h"

/* -------------------------------------------------------------------------
 * Template name table
 * ------------------------------------------------------------------------- */

static const char* const s_template_names[LLM_CHAT_TEMPLATE__COUNT] = {
    "chatml",
    "llama2",
    "llama3",
    "mistral",
    "vicuna",
    "alpaca",
    "raw",
    "unknown",
};

const char* llm_chat_template_name(llm_chat_template_t tmpl)
{
    if ((unsigned)tmpl >= LLM_CHAT_TEMPLATE__COUNT)
        return "unknown";
    return s_template_names[(int)tmpl];
}

/* -------------------------------------------------------------------------
 * Template detection
 * ------------------------------------------------------------------------- */

/* strstr equivalent using strlen/strncmp from libc/string.h */
static int str_contains(const char* haystack, const char* needle)
{
    size_t nlen = strlen(needle);
    if (!nlen) return 1;
    size_t hlen = strlen(haystack);
    for (size_t i = 0; i + nlen <= hlen; i++) {
        if (strncmp(haystack + i, needle, nlen) == 0) return 1;
    }
    return 0;
}

static int str_starts(const char* s, const char* prefix)
{
    size_t plen = strlen(prefix);
    return strncmp(s, prefix, plen) == 0;
}

llm_chat_template_t llm_chat_template_detect(const char* s)
{
    if (!s) return LLM_CHAT_TEMPLATE_UNKNOWN;
    if (str_contains(s, "im_start"))    return LLM_CHAT_TEMPLATE_CHATML;
    if (str_contains(s, "[INST]"))      return LLM_CHAT_TEMPLATE_LLAMA2;
    if (str_contains(s, "start_header_id")) return LLM_CHAT_TEMPLATE_LLAMA3;
    if (str_starts(s, "mistral"))       return LLM_CHAT_TEMPLATE_MISTRAL;
    if (str_starts(s, "vicuna"))        return LLM_CHAT_TEMPLATE_VICUNA;
    if (str_contains(s, "alpaca") || str_contains(s, "Instruction:"))
                                        return LLM_CHAT_TEMPLATE_ALPACA;
    if (str_starts(s, "llama"))         return LLM_CHAT_TEMPLATE_LLAMA2;
    return LLM_CHAT_TEMPLATE_UNKNOWN;
}

/* -------------------------------------------------------------------------
 * Context management
 * ------------------------------------------------------------------------- */

void llm_chat_ctx_init(llm_chat_ctx_t* ctx, llm_chat_template_t tmpl,
                       const char* system_prompt)
{
    llm_zero(ctx, sizeof(*ctx));
    ctx->tmpl          = tmpl;
    ctx->system_prompt = system_prompt;
}

int llm_chat_ctx_append(llm_chat_ctx_t* ctx, llm_chat_role_t role,
                        const char* content)
{
    if (ctx->n_messages >= LLM_CHAT_MAX_MESSAGES) {
        klog_warn("[llm-chat] message buffer full");
        return -1;
    }
    ctx->messages[ctx->n_messages].role    = role;
    ctx->messages[ctx->n_messages].content = content;
    ctx->n_messages++;
    return 0;
}

void llm_chat_ctx_reset(llm_chat_ctx_t* ctx)
{
    ctx->n_messages = 0;
}

/* -------------------------------------------------------------------------
 * Buffer write helpers
 * ------------------------------------------------------------------------- */

typedef struct {
    char* buf;
    int   cap;
    int   pos;
    int   overflow;
} wbuf_t;

static void wb_init(wbuf_t* w, char* buf, int cap)
{
    w->buf      = buf;
    w->cap      = cap;
    w->pos      = 0;
    w->overflow = 0;
}

static void wb_str(wbuf_t* w, const char* s)
{
    if (w->overflow || !s) return;
    for (; *s; s++) {
        if (w->pos >= w->cap - 1) { w->overflow = 1; return; }
        w->buf[w->pos++] = *s;
    }
}

static void wb_char(wbuf_t* w, char c)
{
    if (w->overflow) return;
    if (w->pos >= w->cap - 1) { w->overflow = 1; return; }
    w->buf[w->pos++] = c;
}

static void wb_finish(wbuf_t* w)
{
    if (w->pos < w->cap) w->buf[w->pos] = '\0';
    else if (w->cap > 0) w->buf[w->cap-1] = '\0';
}

/* -------------------------------------------------------------------------
 * Per-template formatting helpers (return -1 on truncation)
 * ------------------------------------------------------------------------- */

/* --- ChatML ---------------------------------------------------------------
 *  <|im_start|>system
 *  {system_prompt}<|im_end|>
 *  <|im_start|>user
 *  {user}<|im_end|>
 *  <|im_start|>assistant
 *  {assistant}<|im_end|>
 *  <|im_start|>assistant
 *  (generation prefix, no close tag)
 * ------------------------------------------------------------------------- */
static int fmt_chatml(const llm_chat_ctx_t* ctx, wbuf_t* w)
{
    static const char* role_names[] = { "system", "user", "assistant" };

    if (ctx->system_prompt) {
        wb_str(w, "<|im_start|>system\n");
        wb_str(w, ctx->system_prompt);
        wb_str(w, "<|im_end|>\n");
    }
    for (int i = 0; i < ctx->n_messages; i++) {
        const llm_chat_message_t* m = &ctx->messages[i];
        wb_str(w, "<|im_start|>");
        wb_str(w, role_names[(int)m->role]);
        wb_char(w, '\n');
        wb_str(w, m->content);
        wb_str(w, "<|im_end|>\n");
    }
    wb_str(w, "<|im_start|>assistant\n");
    return w->overflow ? -1 : w->pos;
}

/* --- Llama-2 / Mistral ---------------------------------------------------
 *  <s>[INST] <<SYS>>
 *  {system}
 *  <</SYS>>
 *
 *  {user} [/INST] {assistant} </s><s>[INST] {user2} [/INST]
 * ------------------------------------------------------------------------- */
static int fmt_llama2(const llm_chat_ctx_t* ctx, wbuf_t* w)
{
    int first_turn = 1;
    /* System prompt injected into first user turn */
    for (int i = 0; i < ctx->n_messages; i++) {
        const llm_chat_message_t* m = &ctx->messages[i];
        if (m->role == LLM_CHAT_ROLE_USER) {
            if (first_turn) {
                wb_str(w, "<s>[INST] ");
                if (ctx->system_prompt) {
                    wb_str(w, "<<SYS>>\n");
                    wb_str(w, ctx->system_prompt);
                    wb_str(w, "\n<</SYS>>\n\n");
                }
                wb_str(w, m->content);
                wb_str(w, " [/INST]");
                first_turn = 0;
            } else {
                wb_str(w, " <s>[INST] ");
                wb_str(w, m->content);
                wb_str(w, " [/INST]");
            }
        } else if (m->role == LLM_CHAT_ROLE_ASSISTANT) {
            wb_char(w, ' ');
            wb_str(w, m->content);
            wb_str(w, " </s>");
        }
    }
    wb_char(w, ' ');
    return w->overflow ? -1 : w->pos;
}

/* --- Llama-3 -------------------------------------------------------------
 *  <|begin_of_text|>
 *  <|start_header_id|>system<|end_header_id|>
 *  {system}<|eot_id|>
 *  <|start_header_id|>user<|end_header_id|>
 *  {user}<|eot_id|>
 *  <|start_header_id|>assistant<|end_header_id|>
 * ------------------------------------------------------------------------- */
static int fmt_llama3(const llm_chat_ctx_t* ctx, wbuf_t* w)
{
    static const char* role_names[] = { "system", "user", "assistant" };

    wb_str(w, "<|begin_of_text|>");
    if (ctx->system_prompt) {
        wb_str(w, "<|start_header_id|>system<|end_header_id|>\n\n");
        wb_str(w, ctx->system_prompt);
        wb_str(w, "<|eot_id|>");
    }
    for (int i = 0; i < ctx->n_messages; i++) {
        const llm_chat_message_t* m = &ctx->messages[i];
        wb_str(w, "<|start_header_id|>");
        wb_str(w, role_names[(int)m->role]);
        wb_str(w, "<|end_header_id|>\n\n");
        wb_str(w, m->content);
        wb_str(w, "<|eot_id|>");
    }
    wb_str(w, "<|start_header_id|>assistant<|end_header_id|>\n\n");
    return w->overflow ? -1 : w->pos;
}

/* --- Vicuna --------------------------------------------------------------
 *  {system?}
 *  USER: {user}
 *  ASSISTANT: {assistant}
 *  USER: {next_user}
 *  ASSISTANT:
 * ------------------------------------------------------------------------- */
static int fmt_vicuna(const llm_chat_ctx_t* ctx, wbuf_t* w)
{
    if (ctx->system_prompt) {
        wb_str(w, ctx->system_prompt);
        wb_str(w, "\n\n");
    }
    for (int i = 0; i < ctx->n_messages; i++) {
        const llm_chat_message_t* m = &ctx->messages[i];
        if (m->role == LLM_CHAT_ROLE_USER) {
            wb_str(w, "USER: ");
            wb_str(w, m->content);
            wb_char(w, '\n');
        } else if (m->role == LLM_CHAT_ROLE_ASSISTANT) {
            wb_str(w, "ASSISTANT: ");
            wb_str(w, m->content);
            wb_char(w, '\n');
        }
    }
    wb_str(w, "ASSISTANT:");
    return w->overflow ? -1 : w->pos;
}

/* --- Alpaca --------------------------------------------------------------
 *  Below is an instruction ...
 *  ### Instruction:
 *  {user}
 *  ### Response:
 * ------------------------------------------------------------------------- */
static int fmt_alpaca(const llm_chat_ctx_t* ctx, wbuf_t* w)
{
    if (ctx->system_prompt) {
        wb_str(w, ctx->system_prompt);
        wb_char(w, '\n');
    } else {
        wb_str(w, "Below is an instruction that describes a task. "
                  "Write a response that appropriately completes the request.\n");
    }
    for (int i = 0; i < ctx->n_messages; i++) {
        const llm_chat_message_t* m = &ctx->messages[i];
        if (m->role == LLM_CHAT_ROLE_USER) {
            wb_str(w, "\n### Instruction:\n");
            wb_str(w, m->content);
        } else if (m->role == LLM_CHAT_ROLE_ASSISTANT) {
            wb_str(w, "\n### Response:\n");
            wb_str(w, m->content);
        }
    }
    wb_str(w, "\n### Response:\n");
    return w->overflow ? -1 : w->pos;
}

/* --- Raw -----------------------------------------------------------------
 *  Simple concatenation: system + messages
 * ------------------------------------------------------------------------- */
static int fmt_raw(const llm_chat_ctx_t* ctx, wbuf_t* w)
{
    if (ctx->system_prompt) wb_str(w, ctx->system_prompt);
    for (int i = 0; i < ctx->n_messages; i++) {
        wb_str(w, ctx->messages[i].content);
    }
    return w->overflow ? -1 : w->pos;
}

/* -------------------------------------------------------------------------
 * Public format entry point
 * ------------------------------------------------------------------------- */

int llm_chat_format(const llm_chat_ctx_t* ctx, char* buf, int buf_sz)
{
    wbuf_t w;
    wb_init(&w, buf, buf_sz);

    int ret;
    switch (ctx->tmpl) {
    case LLM_CHAT_TEMPLATE_CHATML:   ret = fmt_chatml(ctx, &w);  break;
    case LLM_CHAT_TEMPLATE_LLAMA2:   ret = fmt_llama2(ctx, &w);  break;
    case LLM_CHAT_TEMPLATE_MISTRAL:  ret = fmt_llama2(ctx, &w);  break;
    case LLM_CHAT_TEMPLATE_LLAMA3:   ret = fmt_llama3(ctx, &w);  break;
    case LLM_CHAT_TEMPLATE_VICUNA:   ret = fmt_vicuna(ctx, &w);  break;
    case LLM_CHAT_TEMPLATE_ALPACA:   ret = fmt_alpaca(ctx, &w);  break;
    default:                          ret = fmt_raw   (ctx, &w);  break;
    }

    wb_finish(&w);
    return ret;
}

int llm_chat_format_single(llm_chat_template_t tmpl,
                           const char* system_prompt,
                           const char* user_msg,
                           char* buf, int buf_sz)
{
    llm_chat_ctx_t ctx;
    llm_chat_ctx_init(&ctx, tmpl, system_prompt);
    llm_chat_ctx_append(&ctx, LLM_CHAT_ROLE_USER, user_msg);
    return llm_chat_format(&ctx, buf, buf_sz);
}
