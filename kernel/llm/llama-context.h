#ifndef GRACE_LLM_CONTEXT_H
#define GRACE_LLM_CONTEXT_H

/*
 * llama-context.h — Core transformer inference context
 *
 * Manages:
 *   - Model tensor pointers (from llm_mmap_t)
 *   - KV cache (key and value tensors per layer, f16)
 *   - Per-inference compute scratch buffers (f32)
 *   - Batch state (current positions, token buffer)
 *
 * All large allocations use SASY segments with lock/unlock discipline.
 * Small metadata uses kmalloc.
 *
 * Usage:
 *   llm_model_t model;  llm_model_load(&model, "/models/llama.gguf");
 *   llm_ctx_t   ctx;    llm_ctx_create(&ctx, &model, 2048);
 *   llm_ctx_decode(&ctx, tokens, n_tokens);
 *   float* logits = llm_ctx_get_logits(&ctx);
 *   llm_ctx_destroy(&ctx);
 *   llm_model_unload(&model);
 */

#include "../../lib/libc/int.h"
#include "llama-arch.h"
#include "llama-mmap.h"
#include "../mm/sasy/sasy.h"

/* -------------------------------------------------------------------------
 * Token type
 * ------------------------------------------------------------------------- */
typedef int32_t llama_token;

#define LLAMA_TOKEN_NULL   (-1)
#define LLAMA_TOKEN_BOS    1    /* updated from model vocab */
#define LLAMA_TOKEN_EOS    2

/* -------------------------------------------------------------------------
 * Vocabulary (parsed from GGUF tokenizer.* metadata)
 * ------------------------------------------------------------------------- */
#define LLM_VOCAB_MAX       131072    /* max token count */
#define LLM_TOKEN_STR_MAX   64        /* max bytes per token string */

typedef enum {
    LLM_VOCAB_TYPE_BPE  = 0,
    LLM_VOCAB_TYPE_SPM  = 1,  /* SentencePiece */
    LLM_VOCAB_TYPE_WPM  = 2,  /* WordPiece */
} llm_vocab_type_t;

typedef struct {
    char     text[LLM_TOKEN_STR_MAX];
    float    score;
    uint8_t  type;     /* 1=normal, 2=unknown, 3=control, 4=user_defined, 6=byte */
} llm_vocab_entry_t;

typedef struct {
    llm_vocab_type_t type;
    uint32_t         n_tokens;
    llama_token      bos_id;
    llama_token      eos_id;
    llama_token      pad_id;
    llama_token      unk_id;
    /* Vocab table stored in a SASY segment */
    seg_handle_t     seg;
    llm_vocab_entry_t* entries;  /* sasy_lock(seg) result */
} llm_vocab_t;

/* -------------------------------------------------------------------------
 * Model (loaded weights + metadata)
 * ------------------------------------------------------------------------- */
#define LLM_MODEL_PATH_MAX  256

typedef struct {
    llm_hparams_t  hparams;
    llm_vocab_t    vocab;
    llm_mmap_t     mmap;         /* SASY segment holding all weight data */
    char           path[LLM_MODEL_PATH_MAX];
    int            loaded;
} llm_model_t;

/* -------------------------------------------------------------------------
 * KV cache
 *
 * Stores past key and value tensors to enable efficient autoregressive
 * decoding.  Each entry = f16 [n_ctx × n_embd_gqa] per layer.
 * Kept in a single large SASY SEG_DATA_AUTO segment.
 * ------------------------------------------------------------------------- */
typedef struct {
    seg_handle_t  seg;
    uint16_t*     k;         /* base pointer after sasy_lock */
    uint16_t*     v;
    uint32_t      n_ctx;     /* max context length */
    uint32_t      n_layer;
    uint32_t      n_embd_gqa;
    uint32_t      head;      /* current fill position */
    int           valid;
} llm_kvcache_t;

/* -------------------------------------------------------------------------
 * Compute buffers (f32 scratch space for the forward pass)
 * All kept in a single SEG_DATA_INST segment so they are reset on each call.
 * ------------------------------------------------------------------------- */
typedef struct {
    seg_handle_t  seg;
    float*        base;       /* sasy_lock result */
    size_t        total;      /* bytes allocated */
    size_t        used;       /* bump-allocator offset */
} llm_compute_buf_t;

/* -------------------------------------------------------------------------
 * Inference context
 * ------------------------------------------------------------------------- */
typedef struct {
    const llm_model_t*  model;
    llm_kvcache_t       kvcache;
    llm_compute_buf_t   cbuf;
    uint32_t            n_ctx;    /* configured context length */

    /* Output logits: f32[n_vocab] — points into cbuf */
    float*              logits;

    /* Position counter for RoPE */
    uint32_t            n_past;

    int                 valid;
} llm_ctx_t;

/* -------------------------------------------------------------------------
 * Token batch for a single decode step
 * ------------------------------------------------------------------------- */
#define LLM_BATCH_MAX   512
typedef struct {
    llama_token tokens[LLM_BATCH_MAX];
    uint32_t    pos[LLM_BATCH_MAX];  /* absolute position of each token */
    uint32_t    n_tokens;
} llm_batch_t;

/* -------------------------------------------------------------------------
 * Public API — Model lifecycle
 * ------------------------------------------------------------------------- */

/* Load model from GGUF file. Returns 0 on success. */
int  llm_model_load(llm_model_t* m, const char* path);

/* Unload model, release all SASY segments. */
void llm_model_unload(llm_model_t* m);

/* -------------------------------------------------------------------------
 * Public API — Context lifecycle
 * ------------------------------------------------------------------------- */

/* Create inference context for *model with given max context length.
 * Allocates KV cache and compute buffers.  Returns 0 on success. */
int  llm_ctx_create(llm_ctx_t* ctx, const llm_model_t* model, uint32_t n_ctx);

/* Destroy context and free all SASY segments. */
void llm_ctx_destroy(llm_ctx_t* ctx);

/* Reset KV cache (clears conversation history). */
void llm_ctx_reset(llm_ctx_t* ctx);

/* -------------------------------------------------------------------------
 * Public API — Inference
 * ------------------------------------------------------------------------- */

/*
 * Decode a batch of tokens.
 * After decoding, logits for the last token are accessible via
 * llm_ctx_get_logits().
 * Returns 0 on success, -1 on error.
 */
int llm_ctx_decode(llm_ctx_t* ctx, const llama_token* tokens,
                   uint32_t n_tokens);

/* Returns pointer to last-token logits (n_vocab floats). */
float* llm_ctx_get_logits(llm_ctx_t* ctx);

/* -------------------------------------------------------------------------
 * Public API — Tokenizer
 * ------------------------------------------------------------------------- */

/*
 * Encode UTF-8 text into tokens.
 * out_tokens: caller-supplied buffer of at least max_tokens elements.
 * Returns number of tokens produced, or -1 on error.
 * Special tokens (BOS etc.) are NOT prepended automatically.
 */
int llm_tokenize(const llm_ctx_t* ctx, const char* text,
                 llama_token* out_tokens, int max_tokens,
                 int add_bos);

/*
 * Convert a token id back to its UTF-8 string.
 * Writes at most buf_max-1 bytes + NUL.
 * Returns number of bytes written, or -1 if invalid.
 */
int llm_detokenize(const llm_ctx_t* ctx, llama_token tok,
                   char* buf, size_t buf_max);

#endif /* GRACE_LLM_CONTEXT_H */
