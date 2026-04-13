/*
 * llama-impl.c — Internal utilities: logging, math helpers, snprintf
 */

#include "llama-impl.h"
#include "../log/klog.h"

/* -------------------------------------------------------------------------
 * Logging
 * ------------------------------------------------------------------------- */

static char llm_log_buf[256];

void llm_log_info(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    /* Build message using our own snprintf (no libc) */
    /* TODO: va-list forwarding to llm_snprintf – for now use klog directly */
    (void)ap;
    klog_log(fmt);
    va_end(ap);
}

void llm_log_warn(const char* fmt, ...)
{
    (void)fmt;
    klog_warn("[LLM]");
}

void llm_log_error(const char* fmt, ...)
{
    (void)fmt;
    klog_error("[LLM]");
}

/* -------------------------------------------------------------------------
 * Minimal snprintf
 * Supports: %s %d %u %x %lld %llu %llx %f %%
 * ------------------------------------------------------------------------- */

static int put_char(char* buf, size_t max, size_t pos, char c)
{
    if (pos + 1 < max) buf[pos] = c;
    return 1;
}

static int put_str(char* buf, size_t max, size_t pos, const char* s)
{
    int written = 0;
    while (s && *s) {
        written += put_char(buf, max, pos + (size_t)written, *s++);
    }
    return written;
}

static int put_uint64(char* buf, size_t max, size_t pos, uint64_t v, int base, int upper)
{
    char tmp[24];
    int  n = 0;
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) { tmp[n++] = '0'; }
    while (v) { tmp[n++] = digits[v % (uint64_t)base]; v /= (uint64_t)base; }
    /* Digits are reversed */
    int written = 0;
    for (int i = n - 1; i >= 0; i--)
        written += put_char(buf, max, pos + (size_t)written, tmp[i]);
    return written;
}

static int put_int64(char* buf, size_t max, size_t pos, int64_t v)
{
    int written = 0;
    if (v < 0) {
        written += put_char(buf, max, pos, '-');
        v = -v;
    }
    written += put_uint64(buf, max, pos + (size_t)written, (uint64_t)v, 10, 0);
    return written;
}

static int put_float(char* buf, size_t max, size_t pos, float f)
{
    int written = 0;
    if (f < 0.0f) { written += put_char(buf, max, pos, '-'); f = -f; }
    uint64_t ipart = (uint64_t)f;
    float    fpart = f - (float)ipart;
    written += put_uint64(buf, max, pos + (size_t)written, ipart, 10, 0);
    written += put_char  (buf, max, pos + (size_t)written, '.');
    /* 4 decimal places */
    for (int i = 0; i < 4; i++) {
        fpart *= 10.0f;
        int digit = (int)fpart;
        written += put_char(buf, max, pos + (size_t)written, (char)('0' + digit));
        fpart -= (float)digit;
    }
    return written;
}

int llm_snprintf(char* buf, size_t max, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    size_t pos  = 0;
    int written = 0;

    while (*fmt) {
        if (*fmt != '%') {
            written += put_char(buf, max, pos + (size_t)written, *fmt++);
            continue;
        }
        fmt++;  /* skip '%' */
        int is_long = 0;
        if (*fmt == 'l') { is_long++; fmt++; }
        if (*fmt == 'l') { is_long++; fmt++; }

        switch (*fmt) {
        case 's': {
            const char* s = va_arg(ap, const char*);
            written += put_str(buf, max, pos + (size_t)written, s ? s : "(null)");
            break;
        }
        case 'd': {
            int64_t v = (is_long >= 2) ? va_arg(ap, long long)
                      : (is_long == 1) ? va_arg(ap, long)
                      :                  va_arg(ap, int);
            written += put_int64(buf, max, pos + (size_t)written, v);
            break;
        }
        case 'u': {
            uint64_t v = (is_long >= 2) ? va_arg(ap, unsigned long long)
                       : (is_long == 1) ? va_arg(ap, unsigned long)
                       :                  va_arg(ap, unsigned int);
            written += put_uint64(buf, max, pos + (size_t)written, v, 10, 0);
            break;
        }
        case 'x': {
            uint64_t v = (is_long >= 2) ? va_arg(ap, unsigned long long)
                       : (is_long == 1) ? va_arg(ap, unsigned long)
                       :                  va_arg(ap, unsigned int);
            written += put_uint64(buf, max, pos + (size_t)written, v, 16, 0);
            break;
        }
        case 'X': {
            uint64_t v = (is_long >= 2) ? va_arg(ap, unsigned long long)
                       : (is_long == 1) ? va_arg(ap, unsigned long)
                       :                  va_arg(ap, unsigned int);
            written += put_uint64(buf, max, pos + (size_t)written, v, 16, 1);
            break;
        }
        case 'f': {
            float f = (float)va_arg(ap, double);
            written += put_float(buf, max, pos + (size_t)written, f);
            break;
        }
        case '%':
            written += put_char(buf, max, pos + (size_t)written, '%');
            break;
        default:
            written += put_char(buf, max, pos + (size_t)written, '?');
            break;
        }
        fmt++;
    }

    /* NUL-terminate */
    if (max > 0) {
        size_t end = pos + (size_t)written;
        buf[end < max ? end : max - 1] = '\0';
    }

    va_end(ap);
    return written;
}

/* -------------------------------------------------------------------------
 * Math helpers
 * ------------------------------------------------------------------------- */

uint32_t llm_isqrt(uint32_t n)
{
    if (n == 0) return 0;
    uint32_t x = n;
    uint32_t y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return x;
}

/* Fast expf using a polynomial approximation valid for x in [-87, 88]. */
float llm_fast_expf(float x)
{
    /* Clamp to avoid overflow */
    if (x > 88.0f)  return 3.4028235e38f;
    if (x < -88.0f) return 0.0f;

    /* Range reduction: x = n*ln2 + r, r in [0, ln2) */
    float ln2   = 0.6931471805599453f;
    float rln2  = 1.4426950408889634f;   /* 1/ln2 */
    float n_f   = x * rln2;
    int   n     = (int)n_f;
    if (n_f < 0.0f) n--;  /* floor */
    float r     = x - (float)n * ln2;

    /* Degree-5 minimax polynomial for exp(r) on [0, ln2] */
    float p = 1.0f + r * (1.0f + r * (0.5f
            + r * (0.16666667f + r * (0.04166667f
            + r *  0.00833333f))));

    /* Reconstruct: p * 2^n */
    union { float f; uint32_t u; } scale;
    scale.u = (uint32_t)(127 + n) << 23;
    return p * scale.f;
}

/* Fast tanhf via rational approximation.  Error < 0.002 on [-4, 4]. */
float llm_fast_tanhf(float x)
{
    if (x >  4.0f) return  1.0f;
    if (x < -4.0f) return -1.0f;
    float x2 = x * x;
    float num = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    float den = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return num / den;
}
