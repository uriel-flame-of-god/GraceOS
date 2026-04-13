/*
 * llama-arch.c — Architecture registry and tensor naming tables
 *
 * Maps GGUF string identifiers to internal enumerations.
 * Provides ggml tensor size / block-size metadata.
 */

#include "llama-arch.h"
#include "../log/klog.h"
#include "../../lib/libc/string.h"

/* -------------------------------------------------------------------------
 * Architecture string table
 * ------------------------------------------------------------------------- */

typedef struct { const char* str; llm_arch_t arch; } arch_entry_t;

static const arch_entry_t arch_table[] = {
    { "llama",       LLM_ARCH_LLAMA     },
    { "mistral",     LLM_ARCH_MISTRAL   },
    { "mixtral",     LLM_ARCH_MIXTRAL   },
    { "gemma",       LLM_ARCH_GEMMA     },
    { "gemma2",      LLM_ARCH_GEMMA2    },
    { "qwen2",       LLM_ARCH_QWEN2     },
    { "qwen2moe",    LLM_ARCH_QWEN2MOE  },
    { "phi2",        LLM_ARCH_PHI2      },
    { "phi3",        LLM_ARCH_PHI3      },
    { "falcon",      LLM_ARCH_FALCON    },
    { "bloom",       LLM_ARCH_BLOOM     },
    { "gpt2",        LLM_ARCH_GPT2      },
    { "stablelm",    LLM_ARCH_STABLELM  },
    { "starcoder",   LLM_ARCH_STARCODER },
    { "orion",       LLM_ARCH_ORION     },
    { "internlm2",   LLM_ARCH_INTERNLM2 },
    { "minicpm",     LLM_ARCH_MINICPM   },
};
static const int arch_table_count = (int)(sizeof(arch_table) / sizeof(arch_table[0]));

llm_arch_t llm_arch_from_string(const char* str)
{
    if (!str) return LLM_ARCH_UNKNOWN;
    for (int i = 0; i < arch_table_count; i++) {
        if (strcmp(arch_table[i].str, str) == 0)
            return arch_table[i].arch;
    }
    klog_warn("[llm-arch] unknown architecture string");
    return LLM_ARCH_UNKNOWN;
}

const char* llm_arch_to_string(llm_arch_t arch)
{
    for (int i = 0; i < arch_table_count; i++) {
        if (arch_table[i].arch == arch)
            return arch_table[i].str;
    }
    return "unknown";
}

/* -------------------------------------------------------------------------
 * Tensor name patterns
 *
 * Pattern format:
 *   Plain names (no "%d"): global tensors (embeddings, output).
 *   Names with "%d": per-layer tensors; layer index is substituted.
 *
 * Only the most common architectures are fully listed; others fall back to
 * the LLAMA patterns which are identical for most LLaMA derivatives.
 * ------------------------------------------------------------------------- */

static const llm_tensor_name_t tensor_names[] = {
    /* --- LLAMA / MISTRAL / MIXTRAL (same naming scheme) --- */
    { LLM_ARCH_LLAMA, LLM_TENSOR_TOKEN_EMBD,      "token_embd.weight"                  },
    { LLM_ARCH_LLAMA, LLM_TENSOR_OUTPUT_NORM,     "output_norm.weight"                 },
    { LLM_ARCH_LLAMA, LLM_TENSOR_OUTPUT,          "output.weight"                      },
    { LLM_ARCH_LLAMA, LLM_TENSOR_ATTN_NORM,       "blk.%d.attn_norm.weight"            },
    { LLM_ARCH_LLAMA, LLM_TENSOR_ATTN_Q,          "blk.%d.attn_q.weight"               },
    { LLM_ARCH_LLAMA, LLM_TENSOR_ATTN_K,          "blk.%d.attn_k.weight"               },
    { LLM_ARCH_LLAMA, LLM_TENSOR_ATTN_V,          "blk.%d.attn_v.weight"               },
    { LLM_ARCH_LLAMA, LLM_TENSOR_ATTN_OUT,        "blk.%d.attn_output.weight"          },
    { LLM_ARCH_LLAMA, LLM_TENSOR_FFN_NORM,        "blk.%d.ffn_norm.weight"             },
    { LLM_ARCH_LLAMA, LLM_TENSOR_FFN_GATE,        "blk.%d.ffn_gate.weight"             },
    { LLM_ARCH_LLAMA, LLM_TENSOR_FFN_DOWN,        "blk.%d.ffn_down.weight"             },
    { LLM_ARCH_LLAMA, LLM_TENSOR_FFN_UP,          "blk.%d.ffn_up.weight"               },
    /* Mistral reuses LLaMA patterns (matched by arch fallback below) */
    { LLM_ARCH_MISTRAL, LLM_TENSOR_TOKEN_EMBD,    "token_embd.weight"                  },
    { LLM_ARCH_MISTRAL, LLM_TENSOR_OUTPUT_NORM,   "output_norm.weight"                 },
    { LLM_ARCH_MISTRAL, LLM_TENSOR_OUTPUT,        "output.weight"                      },
    { LLM_ARCH_MISTRAL, LLM_TENSOR_ATTN_NORM,     "blk.%d.attn_norm.weight"            },
    { LLM_ARCH_MISTRAL, LLM_TENSOR_ATTN_Q,        "blk.%d.attn_q.weight"               },
    { LLM_ARCH_MISTRAL, LLM_TENSOR_ATTN_K,        "blk.%d.attn_k.weight"               },
    { LLM_ARCH_MISTRAL, LLM_TENSOR_ATTN_V,        "blk.%d.attn_v.weight"               },
    { LLM_ARCH_MISTRAL, LLM_TENSOR_ATTN_OUT,      "blk.%d.attn_output.weight"          },
    { LLM_ARCH_MISTRAL, LLM_TENSOR_FFN_NORM,      "blk.%d.ffn_norm.weight"             },
    { LLM_ARCH_MISTRAL, LLM_TENSOR_FFN_GATE,      "blk.%d.ffn_gate.weight"             },
    { LLM_ARCH_MISTRAL, LLM_TENSOR_FFN_DOWN,      "blk.%d.ffn_down.weight"             },
    { LLM_ARCH_MISTRAL, LLM_TENSOR_FFN_UP,        "blk.%d.ffn_up.weight"               },
    /* --- GEMMA --- */
    { LLM_ARCH_GEMMA, LLM_TENSOR_TOKEN_EMBD,      "token_embd.weight"                  },
    { LLM_ARCH_GEMMA, LLM_TENSOR_OUTPUT_NORM,     "output_norm.weight"                 },
    { LLM_ARCH_GEMMA, LLM_TENSOR_OUTPUT,          "token_embd.weight"                  }, /* tied */
    { LLM_ARCH_GEMMA, LLM_TENSOR_ATTN_NORM,       "blk.%d.attn_norm.weight"            },
    { LLM_ARCH_GEMMA, LLM_TENSOR_ATTN_Q,          "blk.%d.attn_q.weight"               },
    { LLM_ARCH_GEMMA, LLM_TENSOR_ATTN_K,          "blk.%d.attn_k.weight"               },
    { LLM_ARCH_GEMMA, LLM_TENSOR_ATTN_V,          "blk.%d.attn_v.weight"               },
    { LLM_ARCH_GEMMA, LLM_TENSOR_ATTN_OUT,        "blk.%d.attn_output.weight"          },
    { LLM_ARCH_GEMMA, LLM_TENSOR_FFN_NORM,        "blk.%d.ffn_norm.weight"             },
    { LLM_ARCH_GEMMA, LLM_TENSOR_FFN_GATE,        "blk.%d.ffn_gate.weight"             },
    { LLM_ARCH_GEMMA, LLM_TENSOR_FFN_DOWN,        "blk.%d.ffn_down.weight"             },
    { LLM_ARCH_GEMMA, LLM_TENSOR_FFN_UP,          "blk.%d.ffn_up.weight"               },
    /* --- GPT-2 --- */
    { LLM_ARCH_GPT2, LLM_TENSOR_TOKEN_EMBD,       "token_embd.weight"                  },
    { LLM_ARCH_GPT2, LLM_TENSOR_TOKEN_EMBD_NORM,  "token_embd_norm.weight"             },
    { LLM_ARCH_GPT2, LLM_TENSOR_OUTPUT_NORM,      "output_norm.weight"                 },
    { LLM_ARCH_GPT2, LLM_TENSOR_OUTPUT,           "output.weight"                      },
    { LLM_ARCH_GPT2, LLM_TENSOR_ATTN_NORM,        "blk.%d.attn_norm.weight"            },
    { LLM_ARCH_GPT2, LLM_TENSOR_ATTN_QKV,         "blk.%d.attn_qkv.weight"             },
    { LLM_ARCH_GPT2, LLM_TENSOR_ATTN_OUT,         "blk.%d.attn_output.weight"          },
    { LLM_ARCH_GPT2, LLM_TENSOR_FFN_NORM,         "blk.%d.ffn_norm.weight"             },
    { LLM_ARCH_GPT2, LLM_TENSOR_FFN_UP,           "blk.%d.ffn_up.weight"               },
    { LLM_ARCH_GPT2, LLM_TENSOR_FFN_DOWN,         "blk.%d.ffn_down.weight"             },
    /* --- FALCON --- */
    { LLM_ARCH_FALCON, LLM_TENSOR_TOKEN_EMBD,     "token_embd.weight"                  },
    { LLM_ARCH_FALCON, LLM_TENSOR_OUTPUT_NORM,    "output_norm.weight"                 },
    { LLM_ARCH_FALCON, LLM_TENSOR_OUTPUT,         "output.weight"                      },
    { LLM_ARCH_FALCON, LLM_TENSOR_ATTN_NORM,      "blk.%d.attn_norm.weight"            },
    { LLM_ARCH_FALCON, LLM_TENSOR_ATTN_QKV,       "blk.%d.attn_qkv.weight"             },
    { LLM_ARCH_FALCON, LLM_TENSOR_ATTN_OUT,       "blk.%d.attn_output.weight"          },
    { LLM_ARCH_FALCON, LLM_TENSOR_FFN_DOWN,       "blk.%d.ffn_down.weight"             },
    { LLM_ARCH_FALCON, LLM_TENSOR_FFN_UP,         "blk.%d.ffn_up.weight"               },
};
static const int tensor_names_count = (int)(sizeof(tensor_names) / sizeof(tensor_names[0]));

const char* llm_tensor_name_pattern(llm_arch_t arch, llm_tensor_t tensor)
{
    for (int i = 0; i < tensor_names_count; i++) {
        if (tensor_names[i].arch == arch && tensor_names[i].tensor == tensor)
            return tensor_names[i].name_pattern;
    }
    return (const char*)0;
}

llm_tensor_t llm_tensor_from_name(llm_arch_t arch, const char* name)
{
    if (!name) return LLM_TENSOR_UNKNOWN;
    for (int i = 0; i < tensor_names_count; i++) {
        if (tensor_names[i].arch != arch) continue;
        /* Direct match (for non-per-layer tensors) */
        if (strcmp(tensor_names[i].name_pattern, name) == 0)
            return tensor_names[i].tensor;
        /* Prefix match for per-layer (pattern contains "%d") */
        const char* pct = tensor_names[i].name_pattern;
        while (*pct && *pct != '%') pct++;
        if (*pct == '%') {
            /* Compare prefix before '%d' */
            size_t prefix_len = (size_t)(pct - tensor_names[i].name_pattern);
            if (strncmp(tensor_names[i].name_pattern, name, prefix_len) == 0)
                return tensor_names[i].tensor;
        }
    }
    return LLM_TENSOR_UNKNOWN;
}

/* -------------------------------------------------------------------------
 * Derived hyperparameters
 * ------------------------------------------------------------------------- */

void llm_hparams_derive(llm_hparams_t* hp)
{
    if (hp->n_head > 0)
        hp->n_embd_head_k = hp->n_embd / hp->n_head;
    else
        hp->n_embd_head_k = 0;

    hp->n_embd_head_v = hp->n_embd_head_k;

    uint32_t kv_heads = hp->n_head_kv > 0 ? hp->n_head_kv : hp->n_head;
    hp->n_embd_gqa = hp->n_embd_head_k * kv_heads;

    if (hp->n_rot == 0)
        hp->n_rot = hp->n_embd_head_k;

    if (hp->rope_freq_base == 0.0f)
        hp->rope_freq_base = 10000.0f;

    if (hp->norm_rms_eps == 0.0f)
        hp->norm_rms_eps = 1e-5f;
}

/* -------------------------------------------------------------------------
 * GGML type metadata
 * ------------------------------------------------------------------------- */

typedef struct { ggml_type_t t; size_t ele_sz; uint32_t blk; const char* name; } type_info_t;

/* ele_sz = row size in bytes per blk elements */
static const type_info_t type_table[] = {
    { GGML_TYPE_F32,  4,   1,  "f32"   },
    { GGML_TYPE_F16,  2,   1,  "f16"   },
    { GGML_TYPE_Q4_0, 18,  32, "q4_0"  },  /* 2(scale f16) + 16(nibbles) */
    { GGML_TYPE_Q4_1, 20,  32, "q4_1"  },  /* 4(scale+min f16) + 16(nibbles) */
    { GGML_TYPE_Q5_0, 22,  32, "q5_0"  },  /* 2+4+16 */
    { GGML_TYPE_Q5_1, 24,  32, "q5_1"  },
    { GGML_TYPE_Q8_0, 34,  32, "q8_0"  },  /* 2(scale f16) + 32(int8) */
    { GGML_TYPE_Q8_1, 40,  32, "q8_1"  },
    { GGML_TYPE_Q2_K, 84,  256,"q2_k"  },
    { GGML_TYPE_Q3_K, 110, 256,"q3_k"  },
    { GGML_TYPE_Q4_K, 144, 256,"q4_k"  },
    { GGML_TYPE_Q5_K, 176, 256,"q5_k"  },
    { GGML_TYPE_Q6_K, 210, 256,"q6_k"  },
    { GGML_TYPE_Q8_K, 292, 256,"q8_k"  },
    { GGML_TYPE_I8,   1,   1,  "i8"    },
    { GGML_TYPE_I16,  2,   1,  "i16"   },
    { GGML_TYPE_I32,  4,   1,  "i32"   },
};
static const int type_table_count = (int)(sizeof(type_table) / sizeof(type_table[0]));

static const type_info_t* type_info(ggml_type_t t)
{
    for (int i = 0; i < type_table_count; i++)
        if (type_table[i].t == t) return &type_table[i];
    return &type_table[0]; /* fallback F32 */
}

size_t ggml_type_size(ggml_type_t t)  { return type_info(t)->ele_sz; }
uint32_t ggml_blck_size(ggml_type_t t){ return type_info(t)->blk; }
const char* ggml_type_name(ggml_type_t t){ return type_info(t)->name; }
