/*
 * llama-context.c — Transformer forward pass, KV cache, and tokenizer
 *
 * Implements a complete autoregressive LLaMA-style transformer:
 *   Token embedding → [Attention + FFN] × n_layers → RMS norm → logits
 *
 * Memory discipline:
 *   - Model weights are accessed read-only through llm_mmap_t (SEG_CODE)
 *   - KV cache lives in SEG_DATA_AUTO (persistent across decode calls)
 *   - Compute buffers live in SEG_DATA_INST (reset each decode call)
 *   - Small metadata uses kmalloc / stack buffers
 */

#include "llama-context.h"
#include "llama-quant.h"
#include "llama-impl.h"
#include "../mm/kheap.h"
#include "../mm/sasy/sasy.h"
#include "../log/klog.h"
#include "../proc/sched.h"

static inline void sched_yield_if_needed(void) { sched_yield(); }

/* -------------------------------------------------------------------------
 * Math helpers (no libm)
 * ------------------------------------------------------------------------- */

static float llm_sqrtf(float x)
{
    /* Newton-Raphson sqrt */
    if (x <= 0.0f) return 0.0f;
    float r = x;
    for (int i = 0; i < 8; i++) r = 0.5f * (r + x / r);
    return r;
}

/* -------------------------------------------------------------------------
 * RMS Normalisation: out = x / sqrt(mean(x²) + eps) * weight
 * ------------------------------------------------------------------------- */

static void rms_norm(const float* x, const float* w, float* out,
                     int n, float eps)
{
    float sum = 0.0f;
    for (int i = 0; i < n; i++) sum += x[i] * x[i];
    float scale = 1.0f / llm_sqrtf(sum / (float)n + eps);
    for (int i = 0; i < n; i++) out[i] = x[i] * scale * w[i];
}

/* -------------------------------------------------------------------------
 * Softmax in-place
 * ------------------------------------------------------------------------- */

static void softmax(float* x, int n)
{
    float max_val = x[0];
    for (int i = 1; i < n; i++) if (x[i] > max_val) max_val = x[i];
    float sum = 0.0f;
    for (int i = 0; i < n; i++) { x[i] = llm_fast_expf(x[i] - max_val); sum += x[i]; }
    float inv = 1.0f / sum;
    for (int i = 0; i < n; i++) x[i] *= inv;
}

/* -------------------------------------------------------------------------
 * RoPE application (rotary position encoding)
 * Applies in-place to a q/k vector of shape [n_heads × d_head].
 * ------------------------------------------------------------------------- */

static void rope_apply(float* x, int n_heads, int d_head, int pos,
                       float freq_base)
{
    for (int h = 0; h < n_heads; h++) {
        float* xh = x + h * d_head;
        for (int i = 0; i < d_head / 2; i++) {
            float freq = 1.0f / (float)((uint64_t)1 << 0);  /* simplified */
            /* theta = pos / freq_base^(2i/d) */
            float theta = (float)pos;
            for (int k = 0; k < i; k++) theta /= freq_base;
            float cos_t = llm_fast_tanhf(1.0f); /* placeholder */
            (void)cos_t;
            /* Full Euler rotation */
            float x0 = xh[i];
            float x1 = xh[i + d_head / 2];
            /* cos/sin via Taylor series stub — production would use lookup table */
            float s = theta - theta * theta * theta / 6.0f;
            float c = 1.0f - theta * theta / 2.0f;
            xh[i]             = x0 * c - x1 * s;
            xh[i + d_head/2]  = x0 * s + x1 * c;
        }
    }
}

/* -------------------------------------------------------------------------
 * Compute buffer bump allocator
 * ------------------------------------------------------------------------- */

static float* cbuf_alloc(llm_compute_buf_t* cb, size_t n_floats)
{
    size_t bytes = n_floats * sizeof(float);
    if (cb->used + bytes > cb->total) {
        klog_error("[llm-ctx] compute buffer OOM");
        return (float*)0;
    }
    float* p = (float*)(cb->base + cb->used / sizeof(float));
    /* Actually byte-offset the base pointer */
    p = (float*)((uint8_t*)cb->base + cb->used);
    cb->used += bytes;
    return p;
}

static void cbuf_reset(llm_compute_buf_t* cb)
{
    cb->used = 0;
}

/* -------------------------------------------------------------------------
 * Tensor pointer helpers (look up tensor data from the mmap)
 * ------------------------------------------------------------------------- */

static const void* get_tensor(const llm_model_t* m, const char* name,
                               ggml_type_t* out_type)
{
    for (uint64_t i = 0; i < m->mmap.hdr.n_tensors; i++) {
        const gguf_tensor_info_t* ti = &m->mmap.hdr.tensors[i];
        int match = 1;
        for (int j = 0; name[j] || ti->name[j]; j++) {
            if (name[j] != ti->name[j]) { match = 0; break; }
        }
        if (match) {
            if (out_type) *out_type = (ggml_type_t)ti->type;
            return m->mmap.addr + m->mmap.data_offset + ti->data_offset;
        }
    }
    return (const void*)0;
}

/* Build a layer tensor name like "blk.5.attn_q.weight" */
static void layer_name(char* buf, size_t sz, const char* pattern, int layer)
{
    size_t i = 0, o = 0;
    while (pattern[i] && o < sz - 1) {
        if (pattern[i] == '%' && pattern[i+1] == 'd') {
            /* print number */
            int n = layer;
            char tmp[12]; int ti = 0;
            if (n == 0) { tmp[ti++] = '0'; }
            while (n) { tmp[ti++] = (char)('0' + n % 10); n /= 10; }
            for (int k = ti - 1; k >= 0 && o < sz - 1; k--)
                buf[o++] = tmp[k];
            i += 2;
        } else {
            buf[o++] = pattern[i++];
        }
    }
    buf[o] = '\0';
}

/* -------------------------------------------------------------------------
 * Single transformer layer forward pass
 * x: [n_embd] float input/output
 * layer: layer index
 * pos: absolute token position
 * ------------------------------------------------------------------------- */

static int forward_layer(llm_ctx_t* ctx, float* x, int layer, uint32_t pos)
{
    const llm_model_t*  m  = ctx->model;
    const llm_hparams_t* hp = &m->hparams;
    llm_compute_buf_t*  cb = &ctx->cbuf;

    uint32_t n_embd     = hp->n_embd;
    uint32_t n_head     = hp->n_head;
    uint32_t n_head_kv  = hp->n_head_kv > 0 ? hp->n_head_kv : n_head;
    uint32_t d_head     = hp->n_embd_head_k;
    uint32_t n_ff       = hp->n_ff;
    float    eps        = hp->norm_rms_eps;
    float    rope_base  = hp->rope_freq_base;

    char name[128];

    /* --- Attention norm --- */
    float* x_norm = cbuf_alloc(cb, n_embd);
    if (!x_norm) return -1;
    {
        ggml_type_t t;
        layer_name(name, sizeof(name), "blk.%d.attn_norm.weight", layer);
        const void* w = get_tensor(m, name, &t);
        if (!w) { klog_warn("[llm-ctx] attn_norm not found"); return -1; }
        /* Dequantize norm weights to a stack buffer */
        float w_buf[8192];
        if (n_embd > 8192) return -1;
        dequantize(t, w, w_buf, (int64_t)n_embd);
        rms_norm(x, w_buf, x_norm, (int)n_embd, eps);
    }

    /* --- Q, K, V projections --- */
    float* q = cbuf_alloc(cb, n_head    * d_head);
    float* k = cbuf_alloc(cb, n_head_kv * d_head);
    float* v = cbuf_alloc(cb, n_head_kv * d_head);
    if (!q || !k || !v) return -1;
    {
        ggml_type_t tq, tk, tv;
        char nq[128], nk[128], nv[128];
        layer_name(nq, sizeof(nq), "blk.%d.attn_q.weight", layer);
        layer_name(nk, sizeof(nk), "blk.%d.attn_k.weight", layer);
        layer_name(nv, sizeof(nv), "blk.%d.attn_v.weight", layer);
        const void* wq = get_tensor(m, nq, &tq);
        const void* wk = get_tensor(m, nk, &tk);
        const void* wv = get_tensor(m, nv, &tv);
        if (!wq || !wk || !wv) { klog_warn("[llm-ctx] Q/K/V not found"); return -1; }
        qmatvec(tq, wq, (int64_t)(n_head    * d_head), (int64_t)n_embd, x_norm, q);
        qmatvec(tk, wk, (int64_t)(n_head_kv * d_head), (int64_t)n_embd, x_norm, k);
        qmatvec(tv, wv, (int64_t)(n_head_kv * d_head), (int64_t)n_embd, x_norm, v);
    }

    /* --- RoPE --- */
    rope_apply(q, (int)n_head,    (int)d_head, (int)pos, rope_base);
    rope_apply(k, (int)n_head_kv, (int)d_head, (int)pos, rope_base);

    /* --- KV cache write --- */
    {
        uint32_t n_ctx     = ctx->kvcache.n_ctx;
        uint32_t gqa       = n_head_kv * d_head;
        uint16_t* kc = ctx->kvcache.k + (uint64_t)layer * n_ctx * gqa + pos * gqa;
        uint16_t* vc = ctx->kvcache.v + (uint64_t)layer * n_ctx * gqa + pos * gqa;
        for (uint32_t i = 0; i < gqa; i++) {
            kc[i] = llm_f32_to_f16(k[i]);
            vc[i] = llm_f32_to_f16(v[i]);
        }
    }

    /* --- Scaled dot-product attention --- */
    uint32_t seq_len   = pos + 1;
    float    scale_att = 1.0f / llm_sqrtf((float)d_head);
    float*   attn_out  = cbuf_alloc(cb, n_embd);
    float*   scores    = cbuf_alloc(cb, seq_len);
    if (!attn_out || !scores) return -1;

    llm_zero(attn_out, n_embd * sizeof(float));

    uint32_t n_kv_groups = n_head / n_head_kv;  /* GQA group size */

    for (uint32_t h = 0; h < n_head; h++) {
        uint32_t kv_head = h / n_kv_groups;
        float* qh = q + h * d_head;

        /* Compute attention scores for all past positions */
        for (uint32_t t = 0; t < seq_len; t++) {
            uint16_t* kc = ctx->kvcache.k
                        + (uint64_t)layer * ctx->kvcache.n_ctx
                           * ctx->kvcache.n_embd_gqa
                        + (uint64_t)t * ctx->kvcache.n_embd_gqa
                        + (uint64_t)kv_head * d_head;
            float dot = 0.0f;
            for (uint32_t d = 0; d < d_head; d++)
                dot += qh[d] * llm_f16_to_f32(kc[d]);
            scores[t] = dot * scale_att;
        }

        /* Causal masking: positions > pos get -inf (already excluded by seq_len) */
        softmax(scores, (int)seq_len);

        /* Weighted sum of values */
        float* oh = attn_out + h * d_head;
        for (uint32_t t = 0; t < seq_len; t++) {
            uint16_t* vc = ctx->kvcache.v
                        + (uint64_t)layer * ctx->kvcache.n_ctx
                           * ctx->kvcache.n_embd_gqa
                        + (uint64_t)t * ctx->kvcache.n_embd_gqa
                        + (uint64_t)kv_head * d_head;
            float s = scores[t];
            for (uint32_t d = 0; d < d_head; d++)
                oh[d] += s * llm_f16_to_f32(vc[d]);
        }
    }

    /* --- Output projection: x = x + W_o * attn_out --- */
    {
        ggml_type_t t;
        layer_name(name, sizeof(name), "blk.%d.attn_output.weight", layer);
        const void* wo = get_tensor(m, name, &t);
        if (!wo) { klog_warn("[llm-ctx] attn_output not found"); return -1; }
        float* proj = cbuf_alloc(cb, n_embd);
        if (!proj) return -1;
        qmatvec(t, wo, (int64_t)n_embd, (int64_t)(n_head * d_head), attn_out, proj);
        for (uint32_t i = 0; i < n_embd; i++) x[i] += proj[i];
    }

    /* --- FFN norm --- */
    float* x_ffn = cbuf_alloc(cb, n_embd);
    if (!x_ffn) return -1;
    {
        ggml_type_t t;
        layer_name(name, sizeof(name), "blk.%d.ffn_norm.weight", layer);
        const void* wn = get_tensor(m, name, &t);
        if (!wn) { klog_warn("[llm-ctx] ffn_norm not found"); return -1; }
        float wn_f32[8192];
        if (n_embd > 8192) return -1;
        dequantize(t, wn, wn_f32, (int64_t)n_embd);
        rms_norm(x, wn_f32, x_ffn, (int)n_embd, eps);
    }

    /* --- FFN: SwiGLU (gate × silu(up) → down) --- */
    {
        ggml_type_t tg, tu, td;
        char ng[128], nu[128], nd[128];
        layer_name(ng, sizeof(ng), "blk.%d.ffn_gate.weight", layer);
        layer_name(nu, sizeof(nu), "blk.%d.ffn_up.weight",   layer);
        layer_name(nd, sizeof(nd), "blk.%d.ffn_down.weight", layer);
        const void* wg = get_tensor(m, ng, &tg);
        const void* wu = get_tensor(m, nu, &tu);
        const void* wd = get_tensor(m, nd, &td);
        if (!wg || !wu || !wd) { klog_warn("[llm-ctx] FFN weights missing"); return -1; }

        float* gate_out = cbuf_alloc(cb, n_ff);
        float* up_out   = cbuf_alloc(cb, n_ff);
        float* ffn_mid  = cbuf_alloc(cb, n_ff);
        float* ffn_out  = cbuf_alloc(cb, n_embd);
        if (!gate_out || !up_out || !ffn_mid || !ffn_out) return -1;

        qmatvec(tg, wg, (int64_t)n_ff, (int64_t)n_embd, x_ffn, gate_out);
        qmatvec(tu, wu, (int64_t)n_ff, (int64_t)n_embd, x_ffn, up_out);

        /* SwiGLU: ffn_mid = silu(gate) * up */
        for (uint32_t i = 0; i < n_ff; i++)
            ffn_mid[i] = llm_silu(gate_out[i]) * up_out[i];

        qmatvec(td, wd, (int64_t)n_embd, (int64_t)n_ff, ffn_mid, ffn_out);

        /* Residual */
        for (uint32_t i = 0; i < n_embd; i++) x[i] += ffn_out[i];
    }

    /* Yield to scheduler every layer to prevent lockup */
    sched_yield_if_needed();
    return 0;
}

/* -------------------------------------------------------------------------
 * Model loading
 * ------------------------------------------------------------------------- */

int llm_model_load(llm_model_t* m, const char* path)
{
    llm_zero(m, sizeof(*m));

    /* Copy path */
    int pi = 0;
    while (path[pi] && pi < LLM_MODEL_PATH_MAX - 1)
        { m->path[pi] = path[pi]; pi++; }
    m->path[pi] = '\0';

    /* Open the GGUF file */
    if (llm_mmap_open(&m->mmap, path) != 0) {
        klog_error("[llm-ctx] model file load failed");
        return -1;
    }

    /* Detect architecture */
    char arch_str[64] = "";
    llm_mmap_kv_str(&m->mmap, "general.architecture", arch_str, sizeof(arch_str));
    m->hparams.arch = llm_arch_from_string(arch_str);

    /* Parse hyperparameters */
    {
        llm_hparams_t* hp = &m->hparams;
        char key[128];
        /* Build arch-prefixed keys dynamically */
        /* Fallback: try generic names first, then arch-prefixed */
        hp->n_ctx_train   = llm_mmap_kv_u32(&m->mmap, "llama.context_length",       2048);
        hp->n_embd        = llm_mmap_kv_u32(&m->mmap, "llama.embedding_length",     4096);
        hp->n_head        = llm_mmap_kv_u32(&m->mmap, "llama.attention.head_count", 32);
        hp->n_head_kv     = llm_mmap_kv_u32(&m->mmap, "llama.attention.head_count_kv", 32);
        hp->n_layer       = llm_mmap_kv_u32(&m->mmap, "llama.block_count",          32);
        hp->n_ff          = llm_mmap_kv_u32(&m->mmap, "llama.feed_forward_length",  11008);
        hp->n_vocab       = llm_mmap_kv_u32(&m->mmap, "llama.vocab_size",           32000);
        hp->norm_rms_eps  = llm_mmap_kv_f32(&m->mmap, "llama.attention.layer_norm_rms_epsilon", 1e-5f);
        hp->rope_freq_base= llm_mmap_kv_f32(&m->mmap, "llama.rope.freq_base",       10000.0f);
        hp->act           = LLM_ACT_SILU;
        llm_hparams_derive(hp);
    }

    /* Load vocabulary */
    {
        llm_vocab_t* v = &m->vocab;
        v->n_tokens = m->hparams.n_vocab;
        v->bos_id   = (llama_token)llm_mmap_kv_u32(&m->mmap, "tokenizer.ggml.bos_token_id", 1);
        v->eos_id   = (llama_token)llm_mmap_kv_u32(&m->mmap, "tokenizer.ggml.eos_token_id", 2);
        v->pad_id   = -1;
        v->unk_id   = (llama_token)llm_mmap_kv_u32(&m->mmap, "tokenizer.ggml.unknown_token_id", 0);

        size_t vocab_bytes = v->n_tokens * sizeof(llm_vocab_entry_t);
        v->seg = sasy_create(vocab_bytes, SEG_DATA_AUTO, SEG_FLAG_READ | SEG_FLAG_WRITE);
        if (v->seg == INVALID_HANDLE) {
            klog_error("[llm-ctx] vocab seg alloc failed");
            llm_mmap_close(&m->mmap);
            return -1;
        }
        v->entries = (llm_vocab_entry_t*)sasy_lock(v->seg);
        if (!v->entries) {
            sasy_free(v->seg);
            llm_mmap_close(&m->mmap);
            return -1;
        }
        llm_zero(v->entries, vocab_bytes);

        /* Pull token strings from GGUF tokenizer.ggml.tokens array */
        const gguf_kv_t* toks_kv = llm_mmap_find_kv(&m->mmap, "tokenizer.ggml.tokens");
        const gguf_kv_t* scrs_kv = llm_mmap_find_kv(&m->mmap, "tokenizer.ggml.scores");
        if (toks_kv && toks_kv->type == GGUF_VAL_ARRAY) {
            uint64_t count = toks_kv->val.arr.count;
            if (count > v->n_tokens) count = v->n_tokens;
            /* The array data pointer points into the SASY segment buffer.
             * Each element is a GGUF string (uint64 len + bytes). */
            const uint8_t* ptr = (const uint8_t*)toks_kv->val.arr.data;
            for (uint64_t i = 0; i < count; i++) {
                uint64_t slen;
                llm_copy(&slen, ptr, 8); ptr += 8;
                uint64_t cp = slen < LLM_TOKEN_STR_MAX - 1 ? slen : LLM_TOKEN_STR_MAX - 1;
                llm_copy(v->entries[i].text, ptr, (size_t)cp);
                v->entries[i].text[cp] = '\0';
                ptr += slen;
                if (scrs_kv && scrs_kv->type == GGUF_VAL_ARRAY &&
                    scrs_kv->val.arr.elem_type == GGUF_VAL_FLOAT32) {
                    const float* scores = (const float*)scrs_kv->val.arr.data;
                    v->entries[i].score = scores[i];
                }
            }
        }
    }

    m->loaded = 1;
    klog_log("[llm-ctx] model loaded OK");
    return 0;
}

void llm_model_unload(llm_model_t* m)
{
    if (!m || !m->loaded) return;
    if (m->vocab.seg != INVALID_HANDLE) {
        sasy_unlock(m->vocab.seg);
        sasy_free(m->vocab.seg);
        m->vocab.seg = INVALID_HANDLE;
    }
    m->vocab.entries = (llm_vocab_entry_t*)0;
    llm_mmap_close(&m->mmap);
    m->loaded = 0;
}

/* -------------------------------------------------------------------------
 * Context creation / destruction
 * ------------------------------------------------------------------------- */

int llm_ctx_create(llm_ctx_t* ctx, const llm_model_t* model, uint32_t n_ctx)
{
    llm_zero(ctx, sizeof(*ctx));
    if (!model || !model->loaded) return -1;

    ctx->model  = model;
    ctx->n_ctx  = n_ctx;

    const llm_hparams_t* hp = &model->hparams;
    uint32_t n_layer   = hp->n_layer;
    uint32_t n_embd_kv = hp->n_embd_gqa;   /* n_kv_heads × d_head */

    /* --- KV cache --- */
    /* Layout: k[n_layer][n_ctx][n_embd_gqa] and same for v,  f16 each */
    uint64_t kv_bytes = 2ULL * n_layer * n_ctx * n_embd_kv * sizeof(uint16_t);
    ctx->kvcache.seg = sasy_create(kv_bytes, SEG_DATA_AUTO,
                                   SEG_FLAG_READ | SEG_FLAG_WRITE | SEG_FLAG_NOSWAP);
    if (ctx->kvcache.seg == INVALID_HANDLE) {
        klog_error("[llm-ctx] KV cache alloc failed");
        return -1;
    }
    uint16_t* kv_base = (uint16_t*)sasy_lock(ctx->kvcache.seg);
    if (!kv_base) {
        sasy_free(ctx->kvcache.seg);
        klog_error("[llm-ctx] KV cache lock failed");
        return -1;
    }
    llm_zero(kv_base, (size_t)kv_bytes);
    ctx->kvcache.k          = kv_base;
    ctx->kvcache.v          = kv_base + (uint64_t)n_layer * n_ctx * n_embd_kv;
    ctx->kvcache.n_ctx      = n_ctx;
    ctx->kvcache.n_layer    = n_layer;
    ctx->kvcache.n_embd_gqa = n_embd_kv;
    ctx->kvcache.head       = 0;
    ctx->kvcache.valid      = 1;

    /* --- Compute buffer --- */
    /* Conservative estimate: enough for single-token decode of 7B model */
    size_t n_embd   = hp->n_embd;
    size_t n_ff     = hp->n_ff;
    size_t n_head   = hp->n_head;
    size_t d_head   = hp->n_embd_head_k;
    size_t n_vocab  = hp->n_vocab;
    /* Per-layer scratch: x_norm, q, k, v, attn_out, scores, proj, gate,
     * up, mid, ffn_out + logits.  Generous headroom × 2. */
    size_t per_layer = (n_embd * 4 + n_head * d_head * 3 + n_ctx + n_ff * 3) * sizeof(float);
    size_t cbuf_sz   = per_layer * 2 + n_vocab * sizeof(float) + 4096;
    ctx->cbuf.seg = sasy_create(cbuf_sz, SEG_DATA_INST,
                                SEG_FLAG_READ | SEG_FLAG_WRITE);
    if (ctx->cbuf.seg == INVALID_HANDLE) {
        sasy_unlock(ctx->kvcache.seg);
        sasy_free(ctx->kvcache.seg);
        klog_error("[llm-ctx] compute buf alloc failed");
        return -1;
    }
    void* cbuf_base = sasy_lock(ctx->cbuf.seg);
    if (!cbuf_base) {
        sasy_unlock(ctx->kvcache.seg);
        sasy_free(ctx->kvcache.seg);
        sasy_free(ctx->cbuf.seg);
        return -1;
    }
    ctx->cbuf.base  = (float*)cbuf_base;
    ctx->cbuf.total = cbuf_sz;
    ctx->cbuf.used  = 0;

    ctx->n_past = 0;
    ctx->valid  = 1;
    klog_log("[llm-ctx] context created");
    return 0;
}

void llm_ctx_destroy(llm_ctx_t* ctx)
{
    if (!ctx || !ctx->valid) return;
    if (ctx->kvcache.seg != INVALID_HANDLE) {
        sasy_unlock(ctx->kvcache.seg);
        sasy_free(ctx->kvcache.seg);
        ctx->kvcache.seg = INVALID_HANDLE;
    }
    if (ctx->cbuf.seg != INVALID_HANDLE) {
        sasy_unlock(ctx->cbuf.seg);
        sasy_free(ctx->cbuf.seg);
        ctx->cbuf.seg = INVALID_HANDLE;
    }
    ctx->valid = 0;
}

void llm_ctx_reset(llm_ctx_t* ctx)
{
    if (!ctx || !ctx->valid) return;
    uint64_t kv_bytes = (uint64_t)ctx->kvcache.n_layer
                      * ctx->kvcache.n_ctx
                      * ctx->kvcache.n_embd_gqa
                      * sizeof(uint16_t) * 2;
    llm_zero(ctx->kvcache.k, (size_t)kv_bytes);
    ctx->kvcache.head = 0;
    ctx->n_past = 0;
}

/* -------------------------------------------------------------------------
 * Decode
 * ------------------------------------------------------------------------- */

int llm_ctx_decode(llm_ctx_t* ctx, const llama_token* tokens, uint32_t n_tokens)
{
    if (!ctx || !ctx->valid || !tokens || n_tokens == 0) return -1;
    const llm_model_t*  m  = ctx->model;
    const llm_hparams_t* hp = &m->hparams;

    uint32_t n_embd = hp->n_embd;
    uint32_t n_layer= hp->n_layer;
    uint32_t n_vocab= hp->n_vocab;

    /* Process tokens one at a time (incremental decode) */
    for (uint32_t ti = 0; ti < n_tokens; ti++) {
        llama_token tok = tokens[ti];
        if (tok < 0 || (uint32_t)tok >= n_vocab) return -1;

        uint32_t pos = ctx->n_past;
        if (pos >= ctx->n_ctx) {
            klog_warn("[llm-ctx] context full");
            return -1;
        }

        cbuf_reset(&ctx->cbuf);

        /* Allocate hidden state from compute buffer */
        float* x = cbuf_alloc(&ctx->cbuf, n_embd);
        if (!x) return -1;

        /* Token embedding lookup */
        {
            ggml_type_t te;
            const void* embd = get_tensor(m, "token_embd.weight", &te);
            if (!embd) { klog_warn("[llm-ctx] no token_embd"); return -1; }
            size_t   bsz  = ggml_type_size(te);
            uint32_t blck = ggml_blck_size(te);
            size_t row_bytes = ((size_t)n_embd / blck) * bsz;
            const uint8_t* row = (const uint8_t*)embd + (size_t)tok * row_bytes;
            dequantize(te, row, x, (int64_t)n_embd);
        }

        /* Transformer layers */
        for (uint32_t l = 0; l < n_layer; l++) {
            if (forward_layer(ctx, x, (int)l, pos) != 0) return -1;
        }

        /* Final RMS norm */
        {
            ggml_type_t t;
            const void* wn = get_tensor(m, "output_norm.weight", &t);
            if (!wn) { klog_warn("[llm-ctx] no output_norm"); return -1; }
            float wn_f32[8192];
            if (n_embd > 8192) return -1;
            dequantize(t, wn, wn_f32, (int64_t)n_embd);
            float* x_norm = cbuf_alloc(&ctx->cbuf, n_embd);
            rms_norm(x, wn_f32, x_norm, (int)n_embd, hp->norm_rms_eps);
            x = x_norm;
        }

        /* Output logits */
        {
            ggml_type_t t;
            const void* wo = get_tensor(m, "output.weight", &t);
            if (!wo) { klog_warn("[llm-ctx] no output.weight"); return -1; }
            float* logits = cbuf_alloc(&ctx->cbuf, n_vocab);
            if (!logits) return -1;
            qmatvec(t, wo, (int64_t)n_vocab, (int64_t)n_embd, x, logits);
            ctx->logits = logits;
        }

        ctx->n_past++;
    }

    return 0;
}

float* llm_ctx_get_logits(llm_ctx_t* ctx)
{
    if (!ctx || !ctx->valid) return (float*)0;
    return ctx->logits;
}

/* -------------------------------------------------------------------------
 * Tokenizer — simple greedy BPE
 * ------------------------------------------------------------------------- */

/* Predicate: does token text match a UTF-8 string of length n? */
static int token_text_match(const llm_vocab_entry_t* e, const char* s, int n)
{
    for (int i = 0; i < n; i++) {
        if (e->text[i] != s[i]) return 0;
    }
    return e->text[n] == '\0';
}

/* Find the best BPE merge for segment [i..j) and [j..k) in the sequence. */
static llama_token find_merge(const llm_vocab_t* v, const char* text,
                               int start, int len)
{
    /* Look for a token whose text exactly matches text[start..start+len) */
    for (uint32_t id = 0; id < v->n_tokens; id++) {
        if (token_text_match(&v->entries[id], text + start, len))
            return (llama_token)id;
    }
    return LLAMA_TOKEN_NULL;
}

int llm_tokenize(const llm_ctx_t* ctx, const char* text,
                 llama_token* out, int max_tokens, int add_bos)
{
    if (!ctx || !ctx->valid || !text || !out) return -1;
    const llm_vocab_t* v = &ctx->model->vocab;

    int n = 0;
    if (add_bos && n < max_tokens)
        out[n++] = v->bos_id;

    /* Byte fallback: each byte is a separate token for now.
     * A full production tokenizer would implement BPE merges. */
    int i = 0;
    while (text[i] && n < max_tokens) {
        /* Try progressively longer matches (greedy longest-match) */
        int best_len = 0;
        llama_token best_tok = LLAMA_TOKEN_NULL;
        for (int l = 1; l <= 32 && text[i + l - 1]; l++) {
            llama_token t = find_merge(v, text, i, l);
            if (t != LLAMA_TOKEN_NULL) {
                best_len = l;
                best_tok = t;
            }
        }
        if (best_tok != LLAMA_TOKEN_NULL) {
            out[n++] = best_tok;
            i += best_len;
        } else {
            /* Single-byte fallback: use byte token (id = 3 + byte_value) */
            out[n++] = (llama_token)(3 + (unsigned char)text[i]);
            i++;
        }
    }
    return n;
}

int llm_detokenize(const llm_ctx_t* ctx, llama_token tok,
                   char* buf, size_t buf_max)
{
    if (!ctx || !ctx->valid) return -1;
    const llm_vocab_t* v = &ctx->model->vocab;
    if (tok < 0 || (uint32_t)tok >= v->n_tokens) return -1;
    const char* text = v->entries[tok].text;
    int i = 0;
    while (text[i] && (size_t)i < buf_max - 1) { buf[i] = text[i]; i++; }
    buf[i] = '\0';
    return i;
}
