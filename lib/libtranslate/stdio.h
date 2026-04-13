#ifndef GRACEOS_STDIO_H
#define GRACEOS_STDIO_H

#include "../libc/int.h"

#ifndef __GRACE_VA_LIST_DEFINED
#define __GRACE_VA_LIST_DEFINED
typedef __builtin_va_list va_list;
#define va_start(v, l)  __builtin_va_start(v, l)
#define va_arg(v, t)    __builtin_va_arg(v, t)
#define va_end(v)       __builtin_va_end(v)
#define va_copy(d, s)   __builtin_va_copy(d, s)
#endif

int snprintf(char* str, size_t size, const char* format, ...);
int sprintf(char* str, const char* format, ...);
int vsnprintf(char* str, size_t size, const char* format, va_list ap);
int vsprintf(char* str, const char* format, va_list ap);

#endif /* GRACEOS_STDIO_H */
