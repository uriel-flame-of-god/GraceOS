/*
 * llama-adapter.c — LoRA adapter loading and weight application
 *
 * A GGUF LoRA file has the same header format as a normal GGUF model.
 * Tensor names end in ".lora_a" / ".lora_b".
 * The KV "adapter_type" should equal "lora".
 * Per-tensor alpha is stored as "<name>.alpha" in the KV section.
 */

#include "llama-adapter.h"
#include "llama-context.h"
#include "llama-mmap.h"
#include "llama-impl.h"
#include "../log/klog.h"
#include "../mm/kheap.h"
#include "../../lib/libc/string.h"

/* Check if `s` ends with `suffix`. */
static int str_ends_with(const char* s, const char* suffix)
{
    int slen = (int)strlen(s);
    int flen = (int)strlen(suffix);
    if (flen > slen) return 0;
    return strcmp(s + slen - flen, suffix) == 0;
}

/* Copy `name` minus its trailing `suffix` into `base`. */
static void strip_suffix(const char* name, const char* suffix, char* base, int base_sz)
{
    int slen   = (int)strlen(name);
    int flen   = (int)strlen(suffix);
    int newlen = slen - flen;
    if (newlen <= 0 || newlen >= base_sz) newlen = base_sz - 1;
    strncpy(base, name, (size_t)newlen);
    base[newlen] = '\0';
}

/* -------------------------------------------------------------------------
 * llm_adapter_load
 * ------------------------------------------------------------------------- */

int llm_adapter_load(llm_adapter_t* adapter, const char* path)
{
    if (!adapter || !path) return -1;

    llm_zero(adapter, sizeof(*adapter));
    strncpy(adapter->path, path, LLM_LORA_PATH_MAX);
    adapter->path[LLM_LORA_PATH_MAX - 1] = '\0';
    adapter->global_scale = 1.0f;

    /* Open the GGUF file */
    llm_mmap_t mmap;
    llm_zero(&mmap, sizeof(mmap));
    int rc = llm_mmap_open(&mmap, path);
    if (rc != 0) {
        klog_error("[lora] failed to open adapter file");
        return -1;
    }

    /* Verify it is a LoRA adapter (optional — tolerate missing key) */
    char atype_buf[32];
    if (llm_mmap_kv_str(&mmap, "adapter_type", atype_buf, sizeof(atype_buf)) == 0) {
        if (strcmp(atype_buf, "lora") != 0) {
            klog_warn("[lora] adapter_type is not 'lora'");
        }
    }

    /* Read global scale from KV if present */
    float global_alpha = llm_mmap_kv_f32(&mmap, "adapter.alpha", -1.0f);
    if (global_alpha > 0.0f) adapter->global_scale = global_alpha;

    /* Iterate tensors: match ".lora_a" / ".lora_b" pairs */
    int n_tensors = mmap.hdr.n_tensors;
    for (int ti = 0; ti < n_tensors && adapter->n_tensors < LLM_LORA_MAX_TENSORS; ti++) {
        const gguf_tensor_info_t* info = &mmap.hdr.tensors[ti];

        /* Only process lora_a tensors; we find the matching lora_b later */
        if (!str_ends_with(info->name, ".lora_a")) continue;

        /* Base weight name */
        char base[LLM_LORA_TENSOR_NAME_MAX];
        strip_suffix(info->name, ".lora_a", base, sizeof(base));

        /* Find matching lora_b tensor */
        char name_b[LLM_LORA_TENSOR_NAME_MAX + 8];
        strncpy(name_b, base, sizeof(name_b) - 8);
        name_b[sizeof(name_b) - 8] = '\0';
        strcat(name_b, ".lora_b");

        const gguf_tensor_info_t* info_b = (void*)0;
        for (int j = 0; j < n_tensors; j++) {
            if (strcmp(mmap.hdr.tensors[j].name, name_b) == 0) {
                info_b = &mmap.hdr.tensors[j];
                break;
            }
        }
        if (!info_b) {
            klog_warn("[lora] no matching lora_b for tensor");
            continue;
        }

        /* Extract dimensions: A is [r × in_dim], B is [out_dim × r] */
        /* gguf tensors: dims[0] = fastest-varying */
        int r       = (int)(info->n_dims >= 2 ? info->dims[1] : info->dims[0]);
        int in_dim  = (int)info->dims[0];
        int out_dim = (int)(info_b->n_dims >= 2 ? info_b->dims[1] : info_b->dims[0]);

        /* Compute element counts and byte sizes */
        uint64_t a_elem = (uint64_t)r * in_dim;
        uint64_t b_elem = (uint64_t)out_dim * r;
        uint64_t a_bytes = a_elem * sizeof(float);
        uint64_t b_bytes = b_elem * sizeof(float);

        llm_lora_tensor_t* lt = &adapter->tensors[adapter->n_tensors];
        strncpy(lt->name, base, LLM_LORA_TENSOR_NAME_MAX);
        lt->name[LLM_LORA_TENSOR_NAME_MAX - 1] = '\0';
        lt->r       = r;
        lt->in_dim  = in_dim;
        lt->out_dim = out_dim;

        /* Allocate SASY segments for the fp32 A and B matrices */
        lt->seg_a = sasy_create(a_bytes, SEG_DATA_AUTO, SEG_FLAG_ZEROED);
        lt->seg_b = sasy_create(b_bytes, SEG_DATA_AUTO, SEG_FLAG_ZEROED);

        if (lt->seg_a == INVALID_HANDLE || lt->seg_b == INVALID_HANDLE) {
            klog_error("[lora] SASY allocation failed for LoRA tensors");
            llm_mmap_close(&mmap);
            return -1;
        }

        /* Fill A data from mmap */
        float* ptr_a = (float*)sasy_lock(lt->seg_a);
        if (ptr_a) {
            /* Get raw tensor data from mmap and dequantize if needed */
            uint64_t raw_a_bytes;
            const void* raw_a = llm_mmap_tensor_data(&mmap, info->name, &raw_a_bytes);
            if (raw_a) {
                if ((ggml_type_t)info->type == GGML_TYPE_F32) {
                    llm_copy(ptr_a, raw_a, (int)a_bytes);
                } else {
                    dequantize((ggml_type_t)info->type, raw_a, ptr_a, (int64_t)a_elem);
                }
            }
            sasy_unlock(lt->seg_a);
        }

        /* Fill B data from mmap */
        float* ptr_b = (float*)sasy_lock(lt->seg_b);
        if (ptr_b) {
            uint64_t raw_b_bytes;
            const void* raw_b = llm_mmap_tensor_data(&mmap, info_b->name, &raw_b_bytes);
            if (raw_b) {
                if ((ggml_type_t)info_b->type == GGML_TYPE_F32) {
                    llm_copy(ptr_b, raw_b, (int)b_bytes);
                } else {
                    dequantize((ggml_type_t)info_b->type, raw_b, ptr_b, (int64_t)b_elem);
                }
            }
            sasy_unlock(lt->seg_b);
        }

        /* Per-tensor alpha: "<base>.alpha" */
        char alpha_key[LLM_LORA_TENSOR_NAME_MAX + 8];
        strncpy(alpha_key, base, sizeof(alpha_key) - 8);
        alpha_key[sizeof(alpha_key) - 8] = '\0';
        strcat(alpha_key, ".alpha");
        float alpha = llm_mmap_kv_f32(&mmap, alpha_key, -1.0f);
        if (alpha <= 0.0f) alpha = (float)r;  /* default: alpha = r */
        lt->scale = alpha / (float)r;

        adapter->n_tensors++;
    }

    llm_mmap_close(&mmap);
    adapter->loaded = 1;

    char msg[64];
    llm_snprintf(msg, sizeof(msg), "[lora] loaded %d LoRA pairs from %s",
                 adapter->n_tensors, path);
    klog_log(msg);
    return 0;
}

/* -------------------------------------------------------------------------
 * llm_adapter_unload
 * ------------------------------------------------------------------------- */

void llm_adapter_unload(llm_adapter_t* adapter)
{
    if (!adapter || !adapter->loaded) return;
    for (int i = 0; i < adapter->n_tensors; i++) {
        if (adapter->tensors[i].seg_a != INVALID_HANDLE)
            sasy_free(adapter->tensors[i].seg_a);
        if (adapter->tensors[i].seg_b != INVALID_HANDLE)
            sasy_free(adapter->tensors[i].seg_b);
    }
    adapter->n_tensors = 0;
    adapter->loaded    = 0;
}

/* -------------------------------------------------------------------------
 * llm_adapter_apply_tensor
 *
 * Computes  W += (B @ A) * (scale * global_scale)  in float32.
 * B is [out_dim × r], A is [r × in_dim], W is [out_dim × in_dim].
 * Simple triple-loop GEMM — acceptable for small r (typically 4–64).
 * ------------------------------------------------------------------------- */

int llm_adapter_apply_tensor(const llm_adapter_t* adapter,
                             const char* name,
                             float* W,
                             int out_dim, int in_dim)
{
    const llm_lora_tensor_t* lt = (void*)0;
    for (int i = 0; i < adapter->n_tensors; i++) {
        if (strcmp(adapter->tensors[i].name, name) == 0) {
            lt = &adapter->tensors[i];
            break;
        }
    }
    if (!lt) return 0;
    if (lt->out_dim != out_dim || lt->in_dim != in_dim) {
        klog_warn("[lora] dimension mismatch on apply");
        return 0;
    }

    const float* A = (const float*)sasy_lock(lt->seg_a);
    const float* B = (const float*)sasy_lock(lt->seg_b);
    if (!A || !B) {
        if (A) sasy_unlock(lt->seg_a);
        if (B) sasy_unlock(lt->seg_b);
        return 0;
    }

    float effective_scale = lt->scale * adapter->global_scale;
    int r = lt->r;

    /* W[i][j] += sum_k B[i][k] * A[k][j] * scale */
    for (int i = 0; i < out_dim; i++) {
        for (int j = 0; j < in_dim; j++) {
            float acc = 0.0f;
            for (int k = 0; k < r; k++) {
                acc += B[i * r + k] * A[k * in_dim + j];
            }
            W[i * in_dim + j] += acc * effective_scale;
        }
    }

    sasy_unlock(lt->seg_a);
    sasy_unlock(lt->seg_b);
    return 1;
}

/* -------------------------------------------------------------------------
 * llm_adapter_apply_model
 *
 * Iterates over all adapter tensors and applies them to matching model
 * weights.  Currently a stub — full integration requires access to the
 * model's weight map (planned for llm_model_t extension).
 * Returns the number of successfully applied LoRA tensors.
 * ------------------------------------------------------------------------- */

int llm_adapter_apply_model(const llm_adapter_t* adapter,
                            llm_model_t*         model)
{
    if (!adapter || !adapter->loaded || !model) return 0;

    int applied = 0;
    /* Walk each LoRA tensor and attempt to find the matching weight in the
     * model's mmap, dequantize it to float32, apply LoRA, and write back.
     * This is a best-effort approach; unsupported weight formats are skipped. */
    for (int i = 0; i < adapter->n_tensors; i++) {
        const llm_lora_tensor_t* lt = &adapter->tensors[i];

        /* Look up the tensor in the model mmap */
        uint64_t raw_bytes;
        const void* raw = llm_mmap_tensor_data(&model->mmap, lt->name, &raw_bytes);
        if (!raw) continue;

        /* Find tensor info for type and size */
        const gguf_tensor_info_t* tinfo = (void*)0;
        for (uint64_t j = 0; j < model->mmap.hdr.n_tensors; j++) {
            if (strcmp(model->mmap.hdr.tensors[j].name, lt->name) == 0) {
                tinfo = &model->mmap.hdr.tensors[j];
                break;
            }
        }
        if (!tinfo) continue;
        if ((ggml_type_t)tinfo->type != GGML_TYPE_F32) {
            /* Quantized on-disk weights — skip in-place LoRA (use runtime
             * injection via llm_adapter_apply_tensor() at forward-pass time). */
            continue;
        }

        /* Float32 tensor — apply directly */
        applied += llm_adapter_apply_tensor(
            adapter, lt->name,
            (float*)(uintptr_t)raw,  /* mmap segment is already locked */
            lt->out_dim, lt->in_dim);
    }

    char msg[64];
    llm_snprintf(msg, sizeof(msg), "[lora] applied %d tensors to model", applied);
    klog_log(msg);
    return applied;
}
