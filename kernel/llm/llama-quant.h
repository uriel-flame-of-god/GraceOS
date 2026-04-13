#ifndef GRACE_LLM_QUANT_H
#define GRACE_LLM_QUANT_H

/*
 * llama-quant.h — Weight dequantization for the GraceOS LLM runtime
 *
 * Implements scalar (and, where available, SIMD) dequantization kernels for
 * all GGML quantisation formats needed for inference:
 *
 *   Q4_0  — 4-bit symmetric  (32 elem/block, f16 scale)
 *   Q4_1  — 4-bit asymmetric (32 elem/block, f16 scale+min)
 *   Q5_0  — 5-bit symmetric  (32 elem/block, f16 scale)
 *   Q8_0  — 8-bit symmetric  (32 elem/block, f16 scale)
 *   Q4_K  — 4-bit K-quant    (256 elem super-block)
 *   Q5_K  — 5-bit K-quant    (256 elem super-block)
 *   Q6_K  — 6-bit K-quant    (256 elem super-block)
 *   F16   — Half-precision passthrough
 *   F32   — No-op (identity)
 *
 * Dequantize functions fill a float32 output buffer given a pointer to the
 * raw quantised data.  They are used during the forward pass when a tensor
 * needs to be multiplied with an f32 activation vector.
 *
 * Quantized dot-product (QDOT) kernels allow computing the dot product of a
 * dequantized row with an f32 vector without materialising the full f32 row,
 * saving memory bandwidth and improving throughput.
 */

#include "../../lib/libc/int.h"
#include "llama-arch.h"

/* -------------------------------------------------------------------------
 * Block structures (must match ggml on-disk layout)
 * ------------------------------------------------------------------------- */

/* Q4_0: 32 values per block, scale in f16 */
typedef struct { uint16_t d; uint8_t qs[16]; } block_q4_0_t;  /* 18 bytes */

/* Q4_1: 32 values, scale + min in f16 */
typedef struct { uint16_t d; uint16_t m; uint8_t qs[16]; } block_q4_1_t; /* 20 bytes */

/* Q5_0: 32 values, f16 scale, 5th bits in qh (4 bytes) */
typedef struct { uint16_t d; uint8_t qh[4]; uint8_t qs[16]; } block_q5_0_t; /* 22 bytes */

/* Q8_0: 32 values, f16 scale */
typedef struct { uint16_t d; int8_t  qs[32]; } block_q8_0_t;  /* 34 bytes */

/* Q4_K super-block (256 values, 8 sub-blocks of 32) */
typedef struct {
    uint16_t d;           /* super-block scale (f16) */
    uint16_t dmin;        /* super-block min scale (f16) */
    uint8_t  scales[12];  /* sub-block scales and mins packed */
    uint8_t  qs[128];     /* nibbles */
} block_q4_k_t;           /* 144 bytes */

/* Q5_K super-block (256 values) */
typedef struct {
    uint16_t d;
    uint16_t dmin;
    uint8_t  scales[12];
    uint8_t  qh[32];      /* high bits */
    uint8_t  qs[128];
} block_q5_k_t;            /* 176 bytes */

/* Q6_K super-block (256 values) */
typedef struct {
    uint8_t  ql[128];     /* low 4 bits */
    uint8_t  qh[64];      /* high 2 bits */
    int8_t   scales[16];
    uint16_t d;
} block_q6_k_t;            /* 210 bytes */

/* -------------------------------------------------------------------------
 * CPU capability flags (detected once at init)
 * ------------------------------------------------------------------------- */
#define QUANT_CPU_NONE   0x00
#define QUANT_CPU_SSE2   0x01
#define QUANT_CPU_AVX    0x02
#define QUANT_CPU_AVX2   0x04
#define QUANT_CPU_FMA    0x08
#define QUANT_CPU_AVX512 0x10

extern uint32_t quant_cpu_caps; /* set by quant_init() */

/* Detect CPU features and cache in quant_cpu_caps. */
void quant_init(void);

/* -------------------------------------------------------------------------
 * Dequantise: quantised block → f32 array
 *
 * 'raw'   — pointer to first block
 * 'dst'   — output float array (n_elem floats)
 * 'n_elem'— number of elements (must be multiple of block size)
 * ------------------------------------------------------------------------- */
void dequantize_q4_0(const void* raw, float* dst, int64_t n_elem);
void dequantize_q4_1(const void* raw, float* dst, int64_t n_elem);
void dequantize_q5_0(const void* raw, float* dst, int64_t n_elem);
void dequantize_q8_0(const void* raw, float* dst, int64_t n_elem);
void dequantize_q4_k(const void* raw, float* dst, int64_t n_elem);
void dequantize_q5_k(const void* raw, float* dst, int64_t n_elem);
void dequantize_q6_k(const void* raw, float* dst, int64_t n_elem);
void dequantize_f16 (const void* raw, float* dst, int64_t n_elem);
void dequantize_f32 (const void* raw, float* dst, int64_t n_elem);

/* Generic dispatch based on ggml_type_t */
void dequantize(ggml_type_t type, const void* raw,
                float* dst, int64_t n_elem);

/* -------------------------------------------------------------------------
 * Quantised dot-product: dot(dequant(row), vec)
 *
 * 'row'   — pointer to first block of the quantised row
 * 'vec'   — float32 input vector of length n_elem
 * 'n_elem'— number of elements (must be multiple of block size)
 * Returns dot product as float.
 * ------------------------------------------------------------------------- */
float qdot_q4_0(const void* row, const float* vec, int64_t n_elem);
float qdot_q8_0(const void* row, const float* vec, int64_t n_elem);
float qdot_q4_k(const void* row, const float* vec, int64_t n_elem);
float qdot_f32 (const void* row, const float* vec, int64_t n_elem);
float qdot_f16 (const void* row, const float* vec, int64_t n_elem);

/* Generic dispatch */
float qdot(ggml_type_t type, const void* row,
           const float* vec, int64_t n_elem);

/* -------------------------------------------------------------------------
 * Quantised matrix-vector multiply: y = W * x
 *
 * W is a (n_rows × n_cols) quantised matrix stored row-major.
 * x is an f32 vector of length n_cols.
 * y is an f32 output vector of length n_rows.
 *
 * Calls qdot() for each row.
 * ------------------------------------------------------------------------- */
void qmatvec(ggml_type_t type,
             const void* W, int64_t n_rows, int64_t n_cols,
             const float* x, float* y);

#endif /* GRACE_LLM_QUANT_H */
