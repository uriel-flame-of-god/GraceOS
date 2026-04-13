/*
 * llama-adapter.h — LoRA adapter structures and API
 *
 * A LoRA adapter stores per-layer rank-decomposition matrices (A, B) that
 * approximate a weight update: ΔW = B @ A * scale.
 *
 * All tensor data is stored in SASY segments; metadata in kmalloc'd structs.
 */

#ifndef LLAMA_ADAPTER_H
#define LLAMA_ADAPTER_H

#include "../../lib/libc/int.h"
#include "llama-context.h"
#include "../mm/sasy/sasy.h"
#include "llama-arch.h"
#include "llama-quant.h"

/* -------------------------------------------------------------------------
 * Per-tensor LoRA pair (A and B matrices)
 * ------------------------------------------------------------------------- */

#define LLM_LORA_TENSOR_NAME_MAX 64

typedef struct {
    char         name[LLM_LORA_TENSOR_NAME_MAX]; /* e.g. "blk.0.attn_q.weight" */
    seg_handle_t seg_a;         /* [r × in]  row-major float32 */
    seg_handle_t seg_b;         /* [out × r] row-major float32 */
    int          r;             /* LoRA rank */
    int          in_dim;        /* input dimension  */
    int          out_dim;       /* output dimension */
    float        scale;         /* alpha / r        */
} llm_lora_tensor_t;

/* -------------------------------------------------------------------------
 * LoRA adapter (one per file)
 * ------------------------------------------------------------------------- */

#define LLM_LORA_MAX_TENSORS 256
#define LLM_LORA_PATH_MAX    256

typedef struct llm_adapter {
    char              path[LLM_LORA_PATH_MAX];
    llm_lora_tensor_t tensors[LLM_LORA_MAX_TENSORS];
    int               n_tensors;
    float             global_scale;  /* multiplied with per-tensor scale */
    int               loaded;
} llm_adapter_t;

/* -------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

/* Load a GGUF LoRA adapter from `path`.  Allocates SASY segments for A/B.
 * Returns 0 on success, negative on error. */
int llm_adapter_load(llm_adapter_t* adapter, const char* path);

/* Free all SASY segments held by the adapter. */
void llm_adapter_unload(llm_adapter_t* adapter);

/* Apply the adapter to a weight matrix W (in-place addition of B @ A * scale).
 * `W` is float32, shape [out_dim × in_dim] row-major.
 * `name` identifies which LoRA pair to use.
 * Returns 0 if no matching LoRA tensor is found (no-op), 1 if applied. */
int llm_adapter_apply_tensor(const llm_adapter_t* adapter,
                             const char* name,
                             float* W,
                             int out_dim, int in_dim);

/* Apply all LoRA tensors to the model's in-memory weight copies.
 * Called once after llm_model_load() before starting inference.
 * Returns number of tensors applied. */
int llm_adapter_apply_model(const llm_adapter_t* adapter,
                            llm_model_t*         model);

#endif /* LLAMA_ADAPTER_H */
