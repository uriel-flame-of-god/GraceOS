/*
 * runtime.c — GraceOS LLM Runtime handler implementations
 *
 * Provides the sys_llm_* syscall handlers that the syscall dispatcher
 * calls.  All state lives in two global fixed-size tables to avoid
 * dynamic allocation at the runtime level.
 */

#include "runtime.h"
#include "llama-impl.h"
#include "../log/klog.h"
#include "../proc/sched.h"

/* -------------------------------------------------------------------------
 * Global tables
 * ------------------------------------------------------------------------- */

llm_model_slot_t g_llm_models  [LLM_MAX_MODELS];
llm_ctx_slot_t   g_llm_contexts[LLM_MAX_CONTEXTS];

/* -------------------------------------------------------------------------
 * Initialisation
 * ------------------------------------------------------------------------- */

void llm_runtime_init(void)
{
    llm_zero(g_llm_models,   sizeof(g_llm_models));
    llm_zero(g_llm_contexts, sizeof(g_llm_contexts));
    klog_log("[llm-runtime] initialised");
}

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static llm_model_slot_t* model_slot(int id)
{
    if (id < 0 || id >= LLM_MAX_MODELS) return (void*)0;
    return &g_llm_models[id];
}

static llm_ctx_slot_t* ctx_slot(int id)
{
    if (id < 0 || id >= LLM_MAX_CONTEXTS) return (void*)0;
    return &g_llm_contexts[id];
}

static int find_free_model(void)
{
    for (int i = 0; i < LLM_MAX_MODELS; i++)
        if (!g_llm_models[i].in_use) return i;
    return -1;
}

static int find_free_ctx(void)
{
    for (int i = 0; i < LLM_MAX_CONTEXTS; i++)
        if (!g_llm_contexts[i].in_use) return i;
    return -1;
}

/* -------------------------------------------------------------------------
 * SYS_LLM_LOAD_MODEL (150)
 * a1 = path (const char*)
 * ------------------------------------------------------------------------- */

long sys_llm_load_model(long a1, long a2, long a3, long a4, long a5, long a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    const char* path = (const char*)(uintptr_t)a1;
    if (!path) return -1;

    int id = find_free_model();
    if (id < 0) { klog_warn("[llm-runtime] model table full"); return -1; }

    llm_model_slot_t* slot = &g_llm_models[id];
    llm_zero(&slot->model, sizeof(slot->model));

    if (llm_model_load(&slot->model, path) != 0) {
        klog_error("[llm-runtime] model load failed");
        return -1;
    }

    slot->in_use = 1;
    char msg[80];
    llm_snprintf(msg, sizeof(msg), "[llm-runtime] model %d loaded from %s", id, path);
    klog_log(msg);
    return (long)id;
}

/* -------------------------------------------------------------------------
 * SYS_LLM_UNLOAD_MODEL (151)
 * a1 = model_id
 * ------------------------------------------------------------------------- */

long sys_llm_unload_model(long a1, long a2, long a3, long a4, long a5, long a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    llm_model_slot_t* slot = model_slot((int)a1);
    if (!slot || !slot->in_use) return -1;

    /* Destroy any contexts using this model first */
    for (int i = 0; i < LLM_MAX_CONTEXTS; i++) {
        if (g_llm_contexts[i].in_use && g_llm_contexts[i].model_id == (int)a1) {
            llm_ctx_destroy(&g_llm_contexts[i].ctx);
            g_llm_contexts[i].in_use = 0;
        }
    }

    llm_model_unload(&slot->model);
    slot->in_use = 0;
    return 0;
}

/* -------------------------------------------------------------------------
 * SYS_LLM_CREATE_CONTEXT (152)
 * a1 = model_id, a2 = ctx_len
 * ------------------------------------------------------------------------- */

long sys_llm_create_context(long a1, long a2, long a3, long a4, long a5, long a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    llm_model_slot_t* mslot = model_slot((int)a1);
    if (!mslot || !mslot->in_use) return -1;

    int ctx_len = (int)a2;
    if (ctx_len <= 0) ctx_len = 512;

    int id = find_free_ctx();
    if (id < 0) { klog_warn("[llm-runtime] context table full"); return -1; }

    llm_ctx_slot_t* slot = &g_llm_contexts[id];
    llm_zero(slot, sizeof(*slot));

    if (llm_ctx_create(&slot->ctx, &mslot->model, ctx_len) != 0) {
        klog_error("[llm-runtime] context create failed");
        return -1;
    }

    /* Set up a default sampler chain: temperature 0.8, top_k 40, top_p 0.9 */
    llm_sampler_chain_init(&slot->sampler);
    llm_sampler_chain_add(&slot->sampler, llm_sampler_temperature(0.8f));
    llm_sampler_chain_add(&slot->sampler, llm_sampler_top_k(40));
    llm_sampler_chain_add(&slot->sampler, llm_sampler_top_p(0.9f, 1.0f));
    llm_sampler_chain_add(&slot->sampler, llm_sampler_repetition(1.1f, 0.0f, 0.0f));
    llm_sampler_chain_add(&slot->sampler, llm_sampler_seed(0xdeadbeefcafe1234ULL));

    llm_chat_template_t tmpl = llm_chat_template_detect(
        llm_arch_to_string(mslot->model.hparams.arch));
    if (tmpl == LLM_CHAT_TEMPLATE_UNKNOWN) tmpl = LLM_CHAT_TEMPLATE_CHATML;
    llm_chat_ctx_init(&slot->chat, tmpl, (void*)0);

    slot->model_id = (int)a1;
    slot->in_use   = 1;
    slot->rng      = 0xdeadbeefULL ^ (uint64_t)id;
    return (long)id;
}

/* -------------------------------------------------------------------------
 * SYS_LLM_DESTROY_CONTEXT (153)
 * a1 = ctx_id
 * ------------------------------------------------------------------------- */

long sys_llm_destroy_context(long a1, long a2, long a3, long a4, long a5, long a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    llm_ctx_slot_t* slot = ctx_slot((int)a1);
    if (!slot || !slot->in_use) return -1;
    llm_ctx_destroy(&slot->ctx);
    slot->in_use = 0;
    return 0;
}

/* -------------------------------------------------------------------------
 * SYS_LLM_TOKENIZE (154)
 * a1=ctx_id, a2=text, a3=out_tokens, a4=max_tokens
 * ------------------------------------------------------------------------- */

long sys_llm_tokenize(long a1, long a2, long a3, long a4, long a5, long a6)
{
    (void)a5; (void)a6;
    llm_ctx_slot_t* slot = ctx_slot((int)a1);
    if (!slot || !slot->in_use) return -1;

    const char*  text       = (const char*)(uintptr_t)a2;
    llama_token* out_tokens = (llama_token*)(uintptr_t)a3;
    int          max_tokens = (int)a4;

    if (!text || !out_tokens || max_tokens <= 0) return -1;

    return (long)llm_tokenize(&slot->ctx, text, out_tokens, max_tokens, 1);
}

/* -------------------------------------------------------------------------
 * SYS_LLM_DETOKENIZE (155)
 * a1=ctx_id, a2=tokens, a3=n_tokens, a4=buf, a5=buf_sz
 * ------------------------------------------------------------------------- */

long sys_llm_detokenize(long a1, long a2, long a3, long a4, long a5, long a6)
{
    (void)a6;
    llm_ctx_slot_t*      slot = ctx_slot((int)a1);
    if (!slot || !slot->in_use) return -1;

    const llama_token* tokens  = (const llama_token*)(uintptr_t)a2;
    int                n_tok   = (int)a3;
    char*              buf     = (char*)(uintptr_t)a4;
    int                buf_sz  = (int)a5;

    if (!tokens || n_tok <= 0 || !buf || buf_sz <= 0) return -1;

    int written = 0;
    for (int i = 0; i < n_tok && written < buf_sz - 1; i++) {
        char piece[128];
        int plen = llm_detokenize(&slot->ctx, tokens[i], piece, sizeof(piece));
        if (plen <= 0) continue;
        for (int j = 0; j < plen && written < buf_sz - 1; j++)
            buf[written++] = piece[j];
    }
    buf[written] = '\0';
    return (long)written;
}

/* -------------------------------------------------------------------------
 * SYS_LLM_EVAL (156)
 * a1=ctx_id, a2=tokens, a3=n_tokens
 * ------------------------------------------------------------------------- */

long sys_llm_eval(long a1, long a2, long a3, long a4, long a5, long a6)
{
    (void)a4; (void)a5; (void)a6;
    llm_ctx_slot_t* slot = ctx_slot((int)a1);
    if (!slot || !slot->in_use) return -1;

    const llama_token* tokens  = (const llama_token*)(uintptr_t)a2;
    int                n_tok   = (int)a3;
    if (!tokens || n_tok <= 0) return -1;

    if (n_tok > LLM_BATCH_MAX) n_tok = LLM_BATCH_MAX;
    return (long)llm_ctx_decode(&slot->ctx, tokens, (uint32_t)n_tok);
}

/* -------------------------------------------------------------------------
 * SYS_LLM_SAMPLE (157)
 * a1=ctx_id
 * ------------------------------------------------------------------------- */

long sys_llm_sample(long a1, long a2, long a3, long a4, long a5, long a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    llm_ctx_slot_t* slot = ctx_slot((int)a1);
    if (!slot || !slot->in_use) return -1;
    if (!slot->ctx.logits) return -1;

    int n_vocab = slot->ctx.model->hparams.n_vocab;
    llama_token tok = llm_sampler_chain_sample(
        &slot->sampler, slot->ctx.logits, n_vocab);
    llm_sampler_chain_accept(&slot->sampler, tok);
    return (long)tok;
}

/* -------------------------------------------------------------------------
 * SYS_LLM_CHAT_BEGIN (158)
 * a1=ctx_id, a2=template_id, a3=system_prompt
 * ------------------------------------------------------------------------- */

long sys_llm_chat_begin(long a1, long a2, long a3, long a4, long a5, long a6)
{
    (void)a4; (void)a5; (void)a6;
    llm_ctx_slot_t* slot = ctx_slot((int)a1);
    if (!slot || !slot->in_use) return -1;

    llm_chat_template_t tmpl = (llm_chat_template_t)(int)a2;
    const char* sys_prompt = (const char*)(uintptr_t)a3;

    llm_chat_ctx_init(&slot->chat, tmpl, sys_prompt);
    return 0;
}

/* -------------------------------------------------------------------------
 * SYS_LLM_CHAT_APPEND (159)
 * a1=ctx_id, a2=role, a3=content
 * ------------------------------------------------------------------------- */

long sys_llm_chat_append(long a1, long a2, long a3, long a4, long a5, long a6)
{
    (void)a4; (void)a5; (void)a6;
    llm_ctx_slot_t* slot = ctx_slot((int)a1);
    if (!slot || !slot->in_use) return -1;

    llm_chat_role_t role    = (llm_chat_role_t)(int)a2;
    const char*     content = (const char*)(uintptr_t)a3;
    if (!content) return -1;

    return (long)llm_chat_ctx_append(&slot->chat, role, content);
}

/* -------------------------------------------------------------------------
 * SYS_LLM_CHAT_GENERATE (160)
 * a1=ctx_id, a2=out_buf, a3=max_tokens, a4=buf_sz
 *
 * Formats the chat session, tokenises the prompt, runs inference for
 * up to max_tokens new tokens, and detokenises the result into out_buf.
 * ------------------------------------------------------------------------- */

#define CHAT_PROMPT_MAX  4096
#define CHAT_TOKEN_MAX   2048

long sys_llm_chat_generate(long a1, long a2, long a3, long a4, long a5, long a6)
{
    (void)a5; (void)a6;
    llm_ctx_slot_t* slot = ctx_slot((int)a1);
    if (!slot || !slot->in_use) return -1;

    char*  out_buf   = (char*)(uintptr_t)a2;
    int    max_tokens = (int)a3;
    int    buf_sz    = (int)a4;
    if (!out_buf || buf_sz <= 0) return -1;
    if (max_tokens <= 0 || max_tokens > CHAT_TOKEN_MAX)
        max_tokens = CHAT_TOKEN_MAX;

    /* Format chat prompt into a stack buffer */
    static char prompt_buf[CHAT_PROMPT_MAX];
    int prompt_len = llm_chat_format(&slot->chat, prompt_buf, CHAT_PROMPT_MAX);
    if (prompt_len < 0) { klog_warn("[llm-runtime] chat prompt overflow"); return -1; }

    /* Tokenise */
    static llama_token tokens_in[CHAT_TOKEN_MAX];
    int n_prompt = llm_tokenize(&slot->ctx, prompt_buf, tokens_in, CHAT_TOKEN_MAX, 1);
    if (n_prompt <= 0) return -1;
    if (n_prompt > LLM_BATCH_MAX) n_prompt = LLM_BATCH_MAX;

    /* Eval prompt */
    if (llm_ctx_decode(&slot->ctx, tokens_in, (uint32_t)n_prompt) != 0) return -1;

    /* Generate */
    int n_vocab  = slot->ctx.model->hparams.n_vocab;
    int written  = 0;
    int n_gen    = 0;
    llama_token eos = slot->ctx.model->vocab.eos_id;

    while (n_gen < max_tokens && written < buf_sz - 1) {
        llama_token tok = llm_sampler_chain_sample(
            &slot->sampler, slot->ctx.logits, n_vocab);
        llm_sampler_chain_accept(&slot->sampler, tok);

        if (tok == eos) break;

        /* Append detokenised piece to output */
        char piece[128];
        int plen = llm_detokenize(&slot->ctx, tok, piece, sizeof(piece));
        if (plen > 0) {
            for (int pi = 0; pi < plen && written < buf_sz - 1; pi++)
                out_buf[written++] = piece[pi];
        }

        /* Eval the new token */
        if (llm_ctx_decode(&slot->ctx, &tok, 1) != 0) break;

        n_gen++;
        /* Yield to scheduler after every 16 generated tokens */
        if ((n_gen & 15) == 0) sched_yield();
    }

    out_buf[written] = '\0';
    return (long)written;
}

/* -------------------------------------------------------------------------
 * SYS_LLM_CHAT_RESET (161)
 * a1=ctx_id
 * ------------------------------------------------------------------------- */

long sys_llm_chat_reset(long a1, long a2, long a3, long a4, long a5, long a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    llm_ctx_slot_t* slot = ctx_slot((int)a1);
    if (!slot || !slot->in_use) return -1;
    llm_chat_ctx_reset(&slot->chat);
    llm_sampler_chain_reset(&slot->sampler);
    slot->ctx.n_past = 0;
    return 0;
}

/* -------------------------------------------------------------------------
 * SYS_LLM_GET_INFO (162)
 * a1=model_id, a2=out_buf, a3=buf_sz
 * ------------------------------------------------------------------------- */

long sys_llm_get_info(long a1, long a2, long a3, long a4, long a5, long a6)
{
    (void)a4; (void)a5; (void)a6;
    llm_model_slot_t* mslot = model_slot((int)a1);
    if (!mslot || !mslot->in_use) return -1;

    char*  buf    = (char*)(uintptr_t)a2;
    int    buf_sz = (int)a3;
    if (!buf || buf_sz <= 0) return -1;

    const llm_hparams_t* p = &mslot->model.hparams;
    int written = llm_snprintf(buf, buf_sz,
        "{"
        "\"arch\":\"%s\","
        "\"n_vocab\":%d,"
        "\"n_embd\":%d,"
        "\"n_layers\":%d,"
        "\"n_heads\":%d,"
        "\"n_ctx\":%d,"
        "\"path\":\"%s\""
        "}",
        llm_arch_to_string(p->arch),
        p->n_vocab, p->n_embd, p->n_layer, p->n_head, p->n_ctx_train,
        mslot->model.path);
    return (long)written;
}

/* -------------------------------------------------------------------------
 * llm_syscalls_init — wire all LLM syscalls into the syscall table
 * Called from syscall_init() in kernel/sys/syscall.c after all other
 * syscalls have been registered.
 * ------------------------------------------------------------------------- */

/*
 * llm_syscalls_init — called from syscall_init() in kernel/sys/syscall.c
 * before the LLM table entries are written.  Do not register entries here;
 * that is done by syscall.c after this call returns.
 */
void llm_syscalls_init(void)
{
    llm_runtime_init();
    klog_log("[llm-runtime] syscalls registered");
}
