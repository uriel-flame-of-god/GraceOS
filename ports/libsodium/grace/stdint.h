#ifndef GRACEOS_STDINT_H
#define GRACEOS_STDINT_H

#include <int.h>

typedef int64_t  intmax_t;
typedef uint64_t uintmax_t;

typedef int8_t   int_fast8_t;
typedef int16_t  int_fast16_t;
typedef int32_t  int_fast32_t;
typedef int64_t  int_fast64_t;

typedef uint8_t  uint_fast8_t;
typedef uint16_t uint_fast16_t;
typedef uint32_t uint_fast32_t;
typedef uint64_t uint_fast64_t;

#define INT8_C(x)   x
#define UINT8_C(x)  x##U
#define INT16_C(x)  x
#define UINT16_C(x) x##U
#define INT32_C(x)  x
#define UINT32_C(x) x##U
#define INT64_C(x)  x##LL
#define UINT64_C(x) x##ULL

#define INTMAX_C(x)  x##LL
#define UINTMAX_C(x) x##ULL

#define UINTPTR_MAX ((uintptr_t)~(uintptr_t)0)
#define INTPTR_MAX ((intptr_t)(UINTPTR_MAX >> 1))
#define INTPTR_MIN ((intptr_t)(~INTPTR_MAX))

#endif
