#ifndef GRACEOS_INT_H
#define GRACEOS_INT_H


/* Fixed-width integers */

typedef signed char        int8_t;
typedef unsigned char      uint8_t;

typedef signed short       int16_t;
typedef unsigned short     uint16_t;

typedef signed int         int32_t;
typedef unsigned int       uint32_t;

typedef signed long long   int64_t;
typedef unsigned long long uint64_t;


/* Size types */

typedef uint64_t size_t;
typedef int64_t  ssize_t;

/* Pointer-sized integer types */

typedef uint64_t uintptr_t;
typedef int64_t  intptr_t;


/* NULL pointer */

#define NULL ((void*)0)


/* Limits */

#define INT8_MIN   (-128)
#define INT8_MAX   (127)
#define UINT8_MAX  (255)

#define INT16_MIN  (-32768)
#define INT16_MAX  (32767)
#define UINT16_MAX (65535)

#define INT32_MIN  (-2147483648)
#define INT32_MAX  (2147483647)
#define UINT32_MAX (4294967295U)

#define INT64_MIN  (-9223372036854775807LL - 1)
#define INT64_MAX  (9223372036854775807LL)
#define UINT64_MAX (18446744073709551615ULL)


/* Variadic argument support (compiler builtins — no <stdarg.h> needed) */
#ifndef __GRACE_VA_LIST_DEFINED
#define __GRACE_VA_LIST_DEFINED
typedef __builtin_va_list va_list;
#define va_start(v, l)  __builtin_va_start(v, l)
#define va_arg(v, t)    __builtin_va_arg(v, t)
#define va_end(v)       __builtin_va_end(v)
#define va_copy(d, s)   __builtin_va_copy(d, s)
#endif

/* Error codes (NASA SAFE_LIBC compliance) */

#define EOK            0   /* Success */
#define EINVAL        -1   /* Invalid argument */
#define ENOMEM        -2   /* Out of memory */
#define ERANGE        -3   /* Result out of range */
#define EOVERFLOW     -4   /* Buffer overflow would occur */
#define ENULLPTR      -5   /* NULL pointer argument */
#define EBOUND        -6   /* Out of bounds access */


/* Boolean type */

typedef int bool;
#define true  1
#define false 0


#endif /* GRACEOS_INT_H */
