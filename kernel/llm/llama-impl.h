#ifndef GRACE_LLM_IMPL_H
#define GRACE_LLM_IMPL_H

/*
 * llama-impl.h — Internal utilities for the GraceOS LLM runtime
 *
 * Provides logging macros that forward to the kernel klog subsystem,
 * a minimal integer-based snprintf replacement, and helper math.
 */

#include "../../lib/libc/int.h"
#include "../../lib/libc/string.h"

/* -------------------------------------------------------------------------
 * Logging macros (map to klog_*)
 * ------------------------------------------------------------------------- */

void llm_log_info (const char* fmt, ...);
void llm_log_warn (const char* fmt, ...);
void llm_log_error(const char* fmt, ...);

#define LLAMA_LOG_INFO(...)   llm_log_info(__VA_ARGS__)
#define LLAMA_LOG_WARN(...)   llm_log_warn(__VA_ARGS__)
#define LLAMA_LOG_ERROR(...)  llm_log_error(__VA_ARGS__)

/* -------------------------------------------------------------------------
 * Minimal formatted string builder (no heap allocation)
 *
 * Writes at most 'max' bytes (including NUL) to 'buf'.
 * Supports: %s %d %u %x %lld %llu %llx %f %%
 * Returns number of characters written (excluding NUL).
 * ------------------------------------------------------------------------- */
int llm_snprintf(char* buf, size_t max, const char* fmt, ...);

/* -------------------------------------------------------------------------
 * Bitwise float helpers (no libm dependency)
 * ------------------------------------------------------------------------- */

/* Half-precision (float16) → float32 */
static inline float llm_f16_to_f32(uint16_t h)
{
    uint32_t sign = (uint32_t)(h >> 15);
    uint32_t exp  = (uint32_t)((h >> 10) & 0x1F);
    uint32_t mant = (uint32_t)(h & 0x3FF);
    uint32_t f;

    if (exp == 0) {
        if (mant == 0) {
            f = sign << 31;
        } else {
            /* Denormal: normalise */
            exp = 1;
            while (!(mant & 0x400)) { mant <<= 1; exp--; }
            mant &= 0x3FF;
            f = (sign << 31) | ((exp - 15 + 127) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        /* Inf / NaN */
        f = (sign << 31) | 0x7F800000 | (mant << 13);
    } else {
        f = (sign << 31) | ((exp - 15 + 127) << 23) | (mant << 13);
    }

    float result;
    __builtin_memcpy(&result, &f, sizeof(f));
    return result;
}

/* float32 → float16 (round-to-nearest-even) */
static inline uint16_t llm_f32_to_f16(float x)
{
    uint32_t b;
    __builtin_memcpy(&b, &x, sizeof(b));
    uint32_t sign = (b >> 31) & 0x1;
    int32_t  exp  = (int32_t)((b >> 23) & 0xFF) - 127;
    uint32_t mant = b & 0x007FFFFF;

    if (exp >= 16)  return (uint16_t)((sign << 15) | 0x7C00);  /* Inf */
    if (exp < -24)  return (uint16_t)(sign << 15);             /* 0   */
    if (exp < -14) {
        /* Denormal */
        mant = (mant | 0x800000) >> (uint32_t)(-14 - exp);
        return (uint16_t)((sign << 15) | (uint16_t)(mant >> 13));
    }
    uint16_t h_exp  = (uint16_t)((exp + 15) << 10);
    uint16_t h_mant = (uint16_t)(mant >> 13);
    return (uint16_t)((sign << 15) | h_exp | h_mant);
}

/* Integer square root (Newton's method, integer arithmetic) */
uint32_t llm_isqrt(uint32_t n);

/* Fast approximation of expf for softmax */
float llm_fast_expf(float x);

/* Fast approximation of tanhf */
float llm_fast_tanhf(float x);

/* SiLU: x * sigmoid(x) */
static inline float llm_silu(float x)
{
    /* sigmoid = 1 / (1 + exp(-x)) */
    float s;
    if (x >= 0.0f) {
        s = 1.0f / (1.0f + llm_fast_expf(-x));
    } else {
        float ex = llm_fast_expf(x);
        s = ex / (1.0f + ex);
    }
    return x * s;
}

/* GELU approximation: 0.5 * x * (1 + tanh(sqrt(2/pi) * (x + 0.044715*x^3))) */
static inline float llm_gelu(float x)
{
    float c = 0.7978845608f;          /* sqrt(2/pi) */
    float inner = c * (x + 0.044715f * x * x * x);
    return 0.5f * x * (1.0f + llm_fast_tanhf(inner));
}

/* -------------------------------------------------------------------------
 * Memory helpers (delegate to lib/libc/string.h memset/memcpy)
 * ------------------------------------------------------------------------- */

static inline void llm_zero(void* dst, size_t n)
{
    memset(dst, 0, n);
}

static inline void llm_copy(void* dst, const void* src, size_t n)
{
    memcpy(dst, src, n);
}

#endif /* GRACE_LLM_IMPL_H */
