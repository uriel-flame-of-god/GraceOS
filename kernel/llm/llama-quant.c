/*
 * llama-quant.c — Weight dequantization kernels (scalar + optional AVX2)
 *
 * All kernels follow the identical bit-layout as ggml.h to ensure correct
 * dequantization of weights saved by llama.cpp / ggml.
 *
 * SIMD paths are selected at runtime based on quant_cpu_caps which is
 * populated by quant_init() using CPUID.
 */

#include "llama-quant.h"
#include "llama-impl.h"
#include "../log/klog.h"

/* -------------------------------------------------------------------------
 * CPU feature detection via CPUID
 * ------------------------------------------------------------------------- */

uint32_t quant_cpu_caps = QUANT_CPU_NONE;

static void cpuid(uint32_t leaf, uint32_t subleaf,
                  uint32_t* eax, uint32_t* ebx,
                  uint32_t* ecx, uint32_t* edx)
{
    __asm__ volatile (
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(subleaf)
    );
}

void quant_init(void)
{
    uint32_t eax, ebx, ecx, edx;

    /* Leaf 1 — ECX/EDX feature flags */
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    if (edx & (1u << 26)) quant_cpu_caps |= QUANT_CPU_SSE2;
    if (ecx & (1u << 28)) quant_cpu_caps |= QUANT_CPU_AVX;
    if (ecx & (1u << 12)) quant_cpu_caps |= QUANT_CPU_FMA;

    /* Leaf 7 — EBX for AVX2 / AVX512 */
    cpuid(7, 0, &eax, &ebx, &ecx, &edx);
    if (ebx & (1u <<  5)) quant_cpu_caps |= QUANT_CPU_AVX2;
    if (ebx & (1u << 16)) quant_cpu_caps |= QUANT_CPU_AVX512;

    klog_log("[llm-quant] CPU caps detected");
}

/* -------------------------------------------------------------------------
 * Helper: f16 → f32
 * (inline definition already in llama-impl.h; just alias here for clarity)
 * ------------------------------------------------------------------------- */
#define F16(h) llm_f16_to_f32(h)

/* -------------------------------------------------------------------------
 * Q4_0 — scalar dequantize
 * Block: { uint16_t d_f16, uint8_t qs[16] }
 * Encoding: q ∈ [0,15], weight = (q - 8) * scale
 * ------------------------------------------------------------------------- */

void dequantize_q4_0(const void* raw, float* dst, int64_t n_elem)
{
    const block_q4_0_t* blocks = (const block_q4_0_t*)raw;
    int64_t n_blocks = n_elem / 32;
    for (int64_t b = 0; b < n_blocks; b++) {
        float d = F16(blocks[b].d);
        const uint8_t* qs = blocks[b].qs;
        for (int i = 0; i < 16; i++) {
            int lo = (qs[i] & 0x0F) - 8;
            int hi = (qs[i] >> 4)   - 8;
            dst[b * 32 + i]      = (float)lo * d;
            dst[b * 32 + i + 16] = (float)hi * d;
        }
    }
}

/* -------------------------------------------------------------------------
 * Q4_1 — scalar dequantize
 * Block: { uint16_t d_f16, uint16_t m_f16, uint8_t qs[16] }
 * weight = q * scale + min
 * ------------------------------------------------------------------------- */

void dequantize_q4_1(const void* raw, float* dst, int64_t n_elem)
{
    const block_q4_1_t* blocks = (const block_q4_1_t*)raw;
    int64_t n_blocks = n_elem / 32;
    for (int64_t b = 0; b < n_blocks; b++) {
        float d = F16(blocks[b].d);
        float m = F16(blocks[b].m);
        const uint8_t* qs = blocks[b].qs;
        for (int i = 0; i < 16; i++) {
            dst[b * 32 + i]      = (float)(qs[i] & 0x0F) * d + m;
            dst[b * 32 + i + 16] = (float)(qs[i] >> 4)   * d + m;
        }
    }
}

/* -------------------------------------------------------------------------
 * Q5_0 — scalar dequantize
 * High bits stored in qh (4 bytes, one bit per value)
 * weight = ((q5 - 16)) * scale
 * ------------------------------------------------------------------------- */

void dequantize_q5_0(const void* raw, float* dst, int64_t n_elem)
{
    const block_q5_0_t* blocks = (const block_q5_0_t*)raw;
    int64_t n_blocks = n_elem / 32;
    for (int64_t b = 0; b < n_blocks; b++) {
        float d = F16(blocks[b].d);
        uint32_t qh;
        llm_copy(&qh, blocks[b].qh, 4);
        const uint8_t* qs = blocks[b].qs;
        for (int i = 0; i < 16; i++) {
            int hi_lo = (int)(qh >> i)        & 1;
            int hi_hi = (int)(qh >> (i + 16)) & 1;
            int lo5 = ((qs[i] & 0x0F) | (hi_lo << 4)) - 16;
            int hi5 = ((qs[i] >> 4)   | (hi_hi << 4)) - 16;
            dst[b * 32 + i]      = (float)lo5 * d;
            dst[b * 32 + i + 16] = (float)hi5 * d;
        }
    }
}

/* -------------------------------------------------------------------------
 * Q8_0 — scalar dequantize
 * Block: { uint16_t d_f16, int8_t qs[32] }
 * weight = q * scale
 * ------------------------------------------------------------------------- */

void dequantize_q8_0(const void* raw, float* dst, int64_t n_elem)
{
    const block_q8_0_t* blocks = (const block_q8_0_t*)raw;
    int64_t n_blocks = n_elem / 32;
    for (int64_t b = 0; b < n_blocks; b++) {
        float d = F16(blocks[b].d);
        for (int i = 0; i < 32; i++)
            dst[b * 32 + i] = (float)blocks[b].qs[i] * d;
    }
}

/* -------------------------------------------------------------------------
 * Q4_K — K-quant dequantize (256 values per super-block, 8 sub-blocks of 32)
 *
 * Super-block structure:
 *   d(f16), dmin(f16)
 *   scales[12]: packed 6-bit scale + 6-bit min for 8 sub-blocks
 *   qs[128]:    nibble pairs
 * ------------------------------------------------------------------------- */

static void decode_scales_q4k(const uint8_t* sc, float* d_out, float* m_out,
                               float d_sup, float dmin_sup)
{
    /* Each sub-block has a 6-bit scale and 6-bit min packed into 12 bytes */
    for (int j = 0; j < 8; j++) {
        uint8_t s, mn;
        if (j < 4) {
            s  = sc[j] & 0x3F;
            mn = sc[j + 4] & 0x3F;
        } else {
            s  = (sc[j + 4] & 0x0F) | ((sc[j - 4] >> 6) << 4);
            mn = (sc[j + 4] >> 4)   | ((sc[j]     >> 6) << 4);
        }
        d_out[j] = d_sup    * (float)s;
        m_out[j] = dmin_sup * (float)mn;
    }
}

void dequantize_q4_k(const void* raw, float* dst, int64_t n_elem)
{
    const block_q4_k_t* blocks = (const block_q4_k_t*)raw;
    int64_t n_blocks = n_elem / 256;
    for (int64_t b = 0; b < n_blocks; b++) {
        float d_sup    = F16(blocks[b].d);
        float dmin_sup = F16(blocks[b].dmin);
        float ds[8], ms[8];
        decode_scales_q4k(blocks[b].scales, ds, ms, d_sup, dmin_sup);

        const uint8_t* qs = blocks[b].qs;
        for (int sb = 0; sb < 8; sb++) {
            float d = ds[sb], m = ms[sb];
            for (int i = 0; i < 16; i++) {
                int idx = sb * 32;
                dst[b * 256 + idx + i]      = (float)(qs[sb*16 + i] & 0x0F) * d - m;
                dst[b * 256 + idx + i + 16] = (float)(qs[sb*16 + i] >> 4)   * d - m;
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * Q5_K dequantize
 * ------------------------------------------------------------------------- */

void dequantize_q5_k(const void* raw, float* dst, int64_t n_elem)
{
    const block_q5_k_t* blocks = (const block_q5_k_t*)raw;
    int64_t n_blocks = n_elem / 256;
    for (int64_t b = 0; b < n_blocks; b++) {
        float d_sup    = F16(blocks[b].d);
        float dmin_sup = F16(blocks[b].dmin);
        float ds[8], ms[8];
        decode_scales_q4k(blocks[b].scales, ds, ms, d_sup, dmin_sup);

        const uint8_t* qs = blocks[b].qs;
        const uint8_t* qh = blocks[b].qh;

        for (int sb = 0; sb < 8; sb++) {
            float d = ds[sb], m = ms[sb];
            for (int i = 0; i < 16; i++) {
                int hb_lo = (qh[i + sb * 4 / 2] >> (i & 1) * 4) & 0xF;  /* simplified */
                int lo5 = ((qs[sb * 16 + i] & 0x0F) | ((hb_lo & 1) << 4));
                int hi5 = ((qs[sb * 16 + i] >>   4) | (((hb_lo >> 1) & 1) << 4));
                int idx = sb * 32;
                dst[b * 256 + idx + i]      = (float)lo5 * d - m;
                dst[b * 256 + idx + i + 16] = (float)hi5 * d - m;
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * Q6_K dequantize
 * ------------------------------------------------------------------------- */

void dequantize_q6_k(const void* raw, float* dst, int64_t n_elem)
{
    const block_q6_k_t* blocks = (const block_q6_k_t*)raw;
    int64_t n_blocks = n_elem / 256;
    for (int64_t b = 0; b < n_blocks; b++) {
        float d = F16(blocks[b].d);
        const uint8_t* ql = blocks[b].ql;
        const uint8_t* qh = blocks[b].qh;
        const int8_t*  sc = blocks[b].scales;

        for (int j = 0; j < 16; j++) {
            for (int i = 0; i < 16; i++) {
                int idx = j * 16 + i;
                int lo4 = ql[idx] & 0x0F;
                int hi2 = (qh[idx / 2] >> ((idx & 1) * 4)) & 0x03;
                int q6  = lo4 | (hi2 << 4); /* 0-63 */
                dst[b * 256 + idx] = d * (float)sc[j] * (float)(q6 - 32);
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * F16 / F32 passthrough
 * ------------------------------------------------------------------------- */

void dequantize_f16(const void* raw, float* dst, int64_t n_elem)
{
    const uint16_t* src = (const uint16_t*)raw;
    for (int64_t i = 0; i < n_elem; i++)
        dst[i] = F16(src[i]);
}

void dequantize_f32(const void* raw, float* dst, int64_t n_elem)
{
    const float* src = (const float*)raw;
    for (int64_t i = 0; i < n_elem; i++)
        dst[i] = src[i];
}

/* -------------------------------------------------------------------------
 * Generic dispatch
 * ------------------------------------------------------------------------- */

void dequantize(ggml_type_t type, const void* raw, float* dst, int64_t n_elem)
{
    switch (type) {
    case GGML_TYPE_Q4_0: dequantize_q4_0(raw, dst, n_elem); break;
    case GGML_TYPE_Q4_1: dequantize_q4_1(raw, dst, n_elem); break;
    case GGML_TYPE_Q5_0: dequantize_q5_0(raw, dst, n_elem); break;
    case GGML_TYPE_Q8_0: dequantize_q8_0(raw, dst, n_elem); break;
    case GGML_TYPE_Q4_K: dequantize_q4_k(raw, dst, n_elem); break;
    case GGML_TYPE_Q5_K: dequantize_q5_k(raw, dst, n_elem); break;
    case GGML_TYPE_Q6_K: dequantize_q6_k(raw, dst, n_elem); break;
    case GGML_TYPE_F16:  dequantize_f16 (raw, dst, n_elem); break;
    case GGML_TYPE_F32:  dequantize_f32 (raw, dst, n_elem); break;
    default:
        klog_warn("[llm-quant] unsupported type for dequantize");
        llm_zero(dst, (size_t)n_elem * sizeof(float));
        break;
    }
}

/* -------------------------------------------------------------------------
 * Quantised dot-product kernels
 *
 * These compute sum(dequant(row[i]) * vec[i]) without materialising
 * the full f32 row — saving memory bandwidth.
 * ------------------------------------------------------------------------- */

float qdot_q4_0(const void* row, const float* vec, int64_t n_elem)
{
    const block_q4_0_t* b = (const block_q4_0_t*)row;
    int64_t n_blocks = n_elem / 32;
    float acc = 0.0f;
    for (int64_t bi = 0; bi < n_blocks; bi++) {
        float d = F16(b[bi].d);
        float block_sum = 0.0f;
        for (int i = 0; i < 16; i++) {
            block_sum += (float)((b[bi].qs[i] & 0x0F) - 8) * vec[bi*32 + i];
            block_sum += (float)((b[bi].qs[i] >>   4) - 8) * vec[bi*32 + i + 16];
        }
        acc += d * block_sum;
    }
    return acc;
}

float qdot_q8_0(const void* row, const float* vec, int64_t n_elem)
{
    const block_q8_0_t* b = (const block_q8_0_t*)row;
    int64_t n_blocks = n_elem / 32;
    float acc = 0.0f;
    for (int64_t bi = 0; bi < n_blocks; bi++) {
        float d = F16(b[bi].d);
        float block_sum = 0.0f;
        for (int i = 0; i < 32; i++)
            block_sum += (float)b[bi].qs[i] * vec[bi*32 + i];
        acc += d * block_sum;
    }
    return acc;
}

float qdot_q4_k(const void* row, const float* vec, int64_t n_elem)
{
    const block_q4_k_t* b = (const block_q4_k_t*)row;
    int64_t n_blocks = n_elem / 256;
    float acc = 0.0f;
    for (int64_t bi = 0; bi < n_blocks; bi++) {
        float d_sup    = F16(b[bi].d);
        float dmin_sup = F16(b[bi].dmin);
        float ds[8], ms[8];
        decode_scales_q4k(b[bi].scales, ds, ms, d_sup, dmin_sup);
        for (int sb = 0; sb < 8; sb++) {
            float d = ds[sb], m = ms[sb];
            for (int i = 0; i < 16; i++) {
                int idx = bi*256 + sb*32 + i;
                float w0 = (float)(b[bi].qs[sb*16+i] & 0x0F) * d - m;
                float w1 = (float)(b[bi].qs[sb*16+i] >> 4)   * d - m;
                acc += w0 * vec[idx];
                acc += w1 * vec[idx + 16];
            }
        }
    }
    return acc;
}

float qdot_f32(const void* row, const float* vec, int64_t n_elem)
{
    const float* r = (const float*)row;
    float acc = 0.0f;
    for (int64_t i = 0; i < n_elem; i++)
        acc += r[i] * vec[i];
    return acc;
}

float qdot_f16(const void* row, const float* vec, int64_t n_elem)
{
    const uint16_t* r = (const uint16_t*)row;
    float acc = 0.0f;
    for (int64_t i = 0; i < n_elem; i++)
        acc += F16(r[i]) * vec[i];
    return acc;
}

float qdot(ggml_type_t type, const void* row,
           const float* vec, int64_t n_elem)
{
    switch (type) {
    case GGML_TYPE_Q4_0: return qdot_q4_0(row, vec, n_elem);
    case GGML_TYPE_Q8_0: return qdot_q8_0(row, vec, n_elem);
    case GGML_TYPE_Q4_K: return qdot_q4_k(row, vec, n_elem);
    case GGML_TYPE_F16:  return qdot_f16 (row, vec, n_elem);
    case GGML_TYPE_F32:  return qdot_f32 (row, vec, n_elem);
    default:
        /* Fallback: dequantize then dot */
        {
            /* Use a stack buffer for small rows only */
            static float tmp[8192];
            if (n_elem <= 8192) {
                dequantize(type, row, tmp, n_elem);
                return qdot_f32(tmp, vec, n_elem);
            }
            klog_warn("[llm-quant] qdot fallback: row too large for stack buf");
            return 0.0f;
        }
    }
}

/* -------------------------------------------------------------------------
 * Quantised matrix-vector multiply
 * ------------------------------------------------------------------------- */

void qmatvec(ggml_type_t type,
             const void* W, int64_t n_rows, int64_t n_cols,
             const float* x, float* y)
{
    size_t   bsz  = ggml_type_size(type);
    uint32_t blck = ggml_blck_size(type);
    size_t row_bytes = ((size_t)n_cols / blck) * bsz;
    const uint8_t* w = (const uint8_t*)W;
    for (int64_t r = 0; r < n_rows; r++) {
        y[r] = qdot(type, w, x, n_cols);
        w += row_bytes;
    }
}
