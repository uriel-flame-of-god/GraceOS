// ============================
// GraceOS Floating Point Library
// Simplified software implementation
// ============================

#include "float.h"

// Convert integer to float
float int_to_float(int32_t value)
{
    return (float)value;
}

// Convert float to integer
int32_t float_to_int(float value)
{
    return (int32_t)value;
}

// Float addition
float float_add(float a, float b)
{
    return a + b;
}

// Float subtraction
float float_sub(float a, float b)
{
    return a - b;
}

// Float multiplication
float float_mul(float a, float b)
{
    return a * b;
}

// Float division
float float_div(float a, float b)
{
    if (b == 0.0f)
        return 0.0f;
    return a / b;
}

// Float equality (with small epsilon)
int float_eq(float a, float b)
{
    float diff = a - b;
    if (diff < 0.0f)
        diff = -diff;
    return diff < 0.00001f;
}

// Float less than
int float_lt(float a, float b)
{
    return a < b;
}

// Float greater than
int float_gt(float a, float b)
{
    return a > b;
}

// Float absolute value
float float_abs(float value)
{
    return value < 0.0f ? -value : value;
}

// Fast inverse square root (Quake III algorithm)
static float fast_inv_sqrt(float number)
{
    union {
        float f;
        uint32_t i;
    } conv;
    
    float x2;
    const float threehalfs = 1.5f;
    
    x2 = number * 0.5f;
    conv.f = number;
    conv.i = 0x5f3759df - (conv.i >> 1);
    conv.f = conv.f * (threehalfs - (x2 * conv.f * conv.f));
    
    return conv.f;
}

// Square root using Newton-Raphson
float float_sqrt(float value)
{
    if (value <= 0.0f)
        return 0.0f;
    
    // Use fast inverse sqrt then invert
    float inv = fast_inv_sqrt(value);
    return value * inv;
}

/* ========================================
 * NASA Compliant Safe Float Operations
 * ======================================== */

int safe_float_add(float a, float b, float* result)
{
    /* Phase 1: Validation */
    if (result == NULL) {
        return ENULLPTR;
    }

    /* Phase 2: Perform operation */
    *result = a + b;

    return EOK;
}

int safe_float_sub(float a, float b, float* result)
{
    /* Phase 1: Validation */
    if (result == NULL) {
        return ENULLPTR;
    }

    /* Phase 2: Perform operation */
    *result = a - b;

    return EOK;
}

int safe_float_mul(float a, float b, float* result)
{
    /* Phase 1: Validation */
    if (result == NULL) {
        return ENULLPTR;
    }

    /* Phase 2: Perform operation */
    *result = a * b;

    return EOK;
}

int safe_float_div(float a, float b, float* result)
{
    /* Phase 1: Validation */
    if (result == NULL) {
        return ENULLPTR;
    }

    /* Phase 2: Division by zero check */
    if (b == 0.0f) {
        return EINVAL;
    }

    /* Phase 3: Perform operation */
    *result = a / b;

    return EOK;
}

int safe_float_sqrt(float value, float* result)
{
    float inv;

    /* Phase 1: Validation */
    if (result == NULL) {
        return ENULLPTR;
    }

    /* Phase 2: Negative value check */
    if (value < 0.0f) {
        return EINVAL;
    }

    /* Phase 3: Zero check */
    if (value == 0.0f) {
        *result = 0.0f;
        return EOK;
    }

    /* Phase 4: Compute square root */
    inv = fast_inv_sqrt(value);
    *result = value * inv;

    return EOK;
}

int safe_float_eq(float a, float b, bool* result)
{
    float diff;

    /* Phase 1: Validation */
    if (result == NULL) {
        return ENULLPTR;
    }

    /* Phase 2: Compute difference */
    diff = a - b;
    if (diff < 0.0f) {
        diff = -diff;
    }

    /* Phase 3: Compare with epsilon */
    *result = (diff < 0.00001f);

    return EOK;
}

int safe_float_lt(float a, float b, bool* result)
{
    /* Phase 1: Validation */
    if (result == NULL) {
        return ENULLPTR;
    }

    /* Phase 2: Perform comparison */
    *result = (a < b);

    return EOK;
}

int safe_float_gt(float a, float b, bool* result)
{
    /* Phase 1: Validation */
    if (result == NULL) {
        return ENULLPTR;
    }

    /* Phase 2: Perform comparison */
    *result = (a > b);

    return EOK;
}

int safe_float_abs(float value, float* result)
{
    /* Phase 1: Validation */
    if (result == NULL) {
        return ENULLPTR;
    }

    /* Phase 2: Compute absolute value */
    *result = (value < 0.0f) ? -value : value;

    return EOK;
}
