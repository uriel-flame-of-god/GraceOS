// ============================
// GraceOS Simple Fallback Allocator
// Emergency allocation when PMM is offline
// ============================

#ifndef GRACEOS_SIMPLE_FALLBACK_H
#define GRACEOS_SIMPLE_FALLBACK_H

#include "../../../lib/libc/int.h"

/* Allocate a page using the simple fallback allocator */
uint64_t simple_fallback_alloc(void);

#endif /* GRACEOS_SIMPLE_FALLBACK_H */
