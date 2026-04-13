#ifndef GRACE_LLM_ARCH_H
#define GRACE_LLM_ARCH_H

#include "../../lib/libc/int.h"

/* -------------------------------------------------------------------------
 * Model architecture identifiers
 * Determined from GGUF metadata key "general.architecture".
 * ------------------------------------------------------------------------- */
typedef enum {
    LLM_ARCH_UNKNOWN   = 0,
    LLM_ARCH_LLAMA     = 1,
    LLM_ARCH_MISTRAL   = 2,
    LLM_ARCH_MIXTRAL   = 3,
    LLM_ARCH_GEMMA     = 4,
    LLM_ARCH_GEMMA2    = 5,
    LLM_ARCH_QWEN2     = 6,
    LLM_ARCH_QWEN2MOE  = 7,
    LLM_ARCH_PHI2      = 8,
    LLM_ARCH_PHI3      = 9,
    LLM_ARCH_FALCON    = 10,
    LLM_ARCH_BLOOM     = 11,
    LLM_ARCH_GPT2      = 12,
    LLM_ARCH_STABLELM  = 13,
    LLM_ARCH_STARCODER = 14,
    LLM_ARCH_ORION     = 15,
    LLM_ARCH_INTERNLM2 = 16,
    LLM_ARCH_MINICPM   = 17,
    LLM_ARCH_COUNT
} llm_arch_t;

/* -------------------------------------------------------------------------
 * Tensor identifiers
 * Maps semantic tensor role to a compact integer.
 * ------------------------------------------------------------------------- */
typedef enum {
    LLM_TENSOR_UNKNOWN        = 0,
    /* Token embeddings */
    LLM_TENSOR_TOKEN_EMBD     = 1,
    LLM_TENSOR_TOKEN_EMBD_NORM= 2,
    LLM_TENSOR_TOKEN_TYPES    = 3,
    /* Output */
    LLM_TENSOR_OUTPUT         = 4,
    LLM_TENSOR_OUTPUT_NORM    = 5,
    /* Per-layer attention */
    LLM_TENSOR_ATTN_NORM      = 10,
    LLM_TENSOR_ATTN_NORM_2    = 11,
    LLM_TENSOR_ATTN_Q         = 12,
    LLM_TENSOR_ATTN_K         = 13,
    LLM_TENSOR_ATTN_V         = 14,
    LLM_TENSOR_ATTN_OUT       = 15,
    LLM_TENSOR_ATTN_QKV       = 16,
    LLM_TENSOR_ATTN_Q_NORM    = 17,
    LLM_TENSOR_ATTN_K_NORM    = 18,
    LLM_TENSOR_ATTN_ROT_EMBD  = 19,
    /* Per-layer FFN */
    LLM_TENSOR_FFN_NORM       = 20,
    LLM_TENSOR_FFN_GATE       = 21,
    LLM_TENSOR_FFN_DOWN       = 22,
    LLM_TENSOR_FFN_UP         = 23,
    LLM_TENSOR_FFN_ACT        = 24,
    LLM_TENSOR_FFN_GATE_INP   = 25,
    LLM_TENSOR_FFN_EXPERT      = 26,
    LLM_TENSOR_FFN_EXPERT_BIAS = 27,
    /* Cross-attention / pooling */
    LLM_TENSOR_CROSS_ATTN_K   = 30,
    LLM_TENSOR_CROSS_ATTN_V   = 31,
    LLM_TENSOR_POOLING_PROJ   = 32,
    LLM_TENSOR_COUNT
} llm_tensor_t;

/* -------------------------------------------------------------------------
 * GGML quantisation type IDs (matches ggml_type in the GGUF spec)
 * ------------------------------------------------------------------------- */
typedef enum {
    GGML_TYPE_F32  = 0,
    GGML_TYPE_F16  = 1,
    GGML_TYPE_Q4_0 = 2,
    GGML_TYPE_Q4_1 = 3,
    GGML_TYPE_Q5_0 = 6,
    GGML_TYPE_Q5_1 = 7,
    GGML_TYPE_Q8_0 = 8,
    GGML_TYPE_Q8_1 = 9,
    GGML_TYPE_Q2_K = 10,
    GGML_TYPE_Q3_K = 11,
    GGML_TYPE_Q4_K = 12,
    GGML_TYPE_Q5_K = 13,
    GGML_TYPE_Q6_K = 14,
    GGML_TYPE_Q8_K = 15,
    GGML_TYPE_I8   = 16,
    GGML_TYPE_I16  = 17,
    GGML_TYPE_I32  = 18,
    GGML_TYPE_COUNT
} ggml_type_t;

/* -------------------------------------------------------------------------
 * Activation function identifiers
 * ------------------------------------------------------------------------- */
typedef enum {
    LLM_ACT_GELU      = 0,
    LLM_ACT_SILU      = 1,
    LLM_ACT_RELU      = 2,
    LLM_ACT_TANH      = 3,
    LLM_ACT_GELU_FAST = 4,
} llm_act_t;

/* -------------------------------------------------------------------------
 * Pooling types (for embedding models)
 * ------------------------------------------------------------------------- */
typedef enum {
    LLM_POOLING_NONE  = 0,
    LLM_POOLING_MEAN  = 1,
    LLM_POOLING_CLS   = 2,
} llm_pooling_t;

/* -------------------------------------------------------------------------
 * Rope type
 * ------------------------------------------------------------------------- */
typedef enum {
    LLM_ROPE_DEFAULT = 0,
    LLM_ROPE_GPT_NEOX= 1,
    LLM_ROPE_GLM     = 2,
} llm_rope_type_t;

/* -------------------------------------------------------------------------
 * Tensor name entry: maps an architecture+tensor pair to a pattern string.
 * The pattern uses "%d" as a placeholder for layer index.
 * ------------------------------------------------------------------------- */
typedef struct {
    llm_arch_t    arch;
    llm_tensor_t  tensor;
    const char*   name_pattern;
} llm_tensor_name_t;

/* -------------------------------------------------------------------------
 * Hyperparameters parsed from GGUF metadata
 * ------------------------------------------------------------------------- */
typedef struct {
    llm_arch_t arch;

    uint32_t n_vocab;       /* vocabulary size */
    uint32_t n_ctx_train;   /* context window used during training */
    uint32_t n_embd;        /* embedding dimension */
    uint32_t n_head;        /* number of attention heads */
    uint32_t n_head_kv;     /* number of KV heads (GQA) */
    uint32_t n_layer;       /* transformer layers */
    uint32_t n_ff;          /* feed-forward intermediate dimension */
    uint32_t n_rot;         /* rotary dimension */
    uint32_t n_expert;      /* MoE: number of experts */
    uint32_t n_expert_used; /* MoE: active experts per token */

    float    norm_eps;      /* RMS-norm epsilon */
    float    norm_rms_eps;  /* alternative RMS-norm epsilon */
    float    rope_freq_base;
    float    rope_freq_scale;
    float    f_max_alibi_bias;

    llm_act_t       act;
    llm_rope_type_t rope_type;
    llm_pooling_t   pooling;

    /* Derived */
    uint32_t n_embd_head_k;  /* d_head for K (n_embd / n_head) */
    uint32_t n_embd_head_v;  /* d_head for V */
    uint32_t n_embd_gqa;     /* n_embd_head_k * n_head_kv */
} llm_hparams_t;

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/* Map GGUF architecture string to llm_arch_t */
llm_arch_t  llm_arch_from_string(const char* str);

/* Map llm_arch_t back to its GGUF architecture string (e.g. "llama"). */
const char* llm_arch_to_string(llm_arch_t arch);

/* Map GGUF tensor name (possibly with layer number) to llm_tensor_t */
llm_tensor_t llm_tensor_from_name(llm_arch_t arch, const char* name);

/* Get canonical name pattern for a tensor in the given architecture. */
const char* llm_tensor_name_pattern(llm_arch_t arch, llm_tensor_t tensor);

/* Derive computed fields in hparams (call after filling primary fields). */
void llm_hparams_derive(llm_hparams_t* hp);

/* Return bytes per element for a ggml_type. */
size_t ggml_type_size(ggml_type_t t);

/* Return elements per block for block-quantised types (1 for F32/F16). */
uint32_t ggml_blck_size(ggml_type_t t);

/* Human-readable name for ggml_type. */
const char* ggml_type_name(ggml_type_t t);

#endif /* GRACE_LLM_ARCH_H */
