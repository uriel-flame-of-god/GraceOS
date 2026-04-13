// ============================
// GraceOS Floating Point Library
// NASA SAFE_LIBC Implementation
// ============================

#ifndef GRACEOS_FLOAT_H
#define GRACEOS_FLOAT_H

#include "int.h"

/* ========================================
 * NASA SAFE_LIBC Compliant Float Library
 * ========================================
 *
 * All safe_float_ functions:
 * - Return error codes for invalid operations
 * - Check for NULL output pointers
 * - Handle edge cases (division by zero, sqrt of negative)
 * - Document all failure modes
 */

/* Legacy float operations (unsafe) */
float int_to_float(int32_t value);
int32_t float_to_int(float value);
float float_add(float a, float b);
float float_sub(float a, float b);
float float_mul(float a, float b);
float float_div(float a, float b);
int float_eq(float a, float b);
int float_lt(float a, float b);
int float_gt(float a, float b);
float float_abs(float value);
float float_sqrt(float value);

/* Safe float operations (NASA compliant) */
int safe_float_add(float a, float b, float* result);
int safe_float_sub(float a, float b, float* result);
int safe_float_mul(float a, float b, float* result);
int safe_float_div(float a, float b, float* result);
int safe_float_sqrt(float value, float* result);
int safe_float_eq(float a, float b, bool* result);
int safe_float_lt(float a, float b, bool* result);
int safe_float_gt(float a, float b, bool* result);
int safe_float_abs(float value, float* result);

#endif
