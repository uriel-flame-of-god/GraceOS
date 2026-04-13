#ifndef GRACEOS_STDDEF_H
#define GRACEOS_STDDEF_H

#include <int.h>

typedef int64_t ptrdiff_t;

#ifndef NULL
#define NULL ((void*)0)
#endif

#define offsetof(type, member) __builtin_offsetof(type, member)

#endif
