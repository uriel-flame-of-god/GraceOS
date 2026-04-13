#ifndef GRACEOS_STDLIB_H
#define GRACEOS_STDLIB_H

#include "../libc/int.h"

void* lt_malloc(size_t size);
void  lt_free(void* ptr);
void* lt_realloc(void* ptr, size_t size);
void* lt_calloc(size_t nmemb, size_t size);

int abs(int v);

long strtol(const char* nptr, char** endptr, int base);
unsigned long strtoul(const char* nptr, char** endptr, int base);

void qsort(void* base, size_t nmemb, size_t size,
           int (*compar)(const void*, const void*));

#ifndef LIBTRANSLATE_NO_WRAP_ALLOC
#define malloc  lt_malloc
#define free    lt_free
#define realloc lt_realloc
#define calloc  lt_calloc
#endif

#endif /* GRACEOS_STDLIB_H */
