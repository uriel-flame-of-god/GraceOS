#include "stdlib.h"
#include "../libc/string.h"
#include "ctype.h"

#undef malloc
#undef free
#undef realloc
#undef calloc

void* malloc(size_t size);
void free(void* ptr);

struct lt_alloc_header {
    uint64_t magic;
    size_t size;
};

#define LT_ALLOC_MAGIC 0x4C54414C4C4F4300ULL

static struct lt_alloc_header* lt_get_header(void* ptr)
{
    return (struct lt_alloc_header*)ptr - 1;
}

void* lt_malloc(size_t size)
{
    size_t total = size + sizeof(struct lt_alloc_header);
    struct lt_alloc_header* header = (struct lt_alloc_header*)malloc(total);
    if (!header) {
        return NULL;
    }

    header->magic = LT_ALLOC_MAGIC;
    header->size = size;

    return (void*)(header + 1);
}

void lt_free(void* ptr)
{
    if (!ptr) {
        return;
    }

    struct lt_alloc_header* header = lt_get_header(ptr);
    if (header->magic != LT_ALLOC_MAGIC) {
        return;
    }

    header->magic = 0;
    free(header);
}

void* lt_realloc(void* ptr, size_t size)
{
    if (!ptr) {
        return lt_malloc(size);
    }

    if (size == 0) {
        lt_free(ptr);
        return NULL;
    }

    struct lt_alloc_header* header = lt_get_header(ptr);
    if (header->magic != LT_ALLOC_MAGIC) {
        return NULL;
    }

    size_t old_size = header->size;
    if (size <= old_size) {
        header->size = size;
        return ptr;
    }

    void* new_ptr = lt_malloc(size);
    if (!new_ptr) {
        return NULL;
    }

    memcpy(new_ptr, ptr, old_size);
    lt_free(ptr);

    return new_ptr;
}

void* lt_calloc(size_t nmemb, size_t size)
{
    size_t total;

    if (nmemb == 0 || size == 0) {
        return lt_malloc(0);
    }

    total = nmemb * size;
    if (total / nmemb != size) {
        return NULL;
    }

    void* ptr = lt_malloc(total);
    if (!ptr) {
        return NULL;
    }

    memset(ptr, 0, total);
    return ptr;
}

int abs(int v)
{
    return v < 0 ? -v : v;
}

static int digit_value(int c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 10;
    }
    return -1;
}

long strtol(const char* nptr, char** endptr, int base)
{
    if (!nptr) {
        if (endptr) {
            *endptr = NULL;
        }
        return 0;
    }

    const char* s = nptr;
    while (isspace(*s)) {
        s++;
    }

    if (base != 0 && (base < 2 || base > 36)) {
        if (endptr) {
            *endptr = (char*)nptr;
        }
        return 0;
    }

    int neg = 0;
    if (*s == '+' || *s == '-') {
        if (*s == '-') {
            neg = 1;
        }
        s++;
    }

    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            base = 16;
            s += 2;
        } else if (s[0] == '0') {
            base = 8;
            s++;
        } else {
            base = 10;
        }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    unsigned long long acc = 0;
    int any = 0;

    while (*s) {
        int v = digit_value(*s);
        if (v < 0 || v >= base) {
            break;
        }
        acc = acc * (unsigned long long)base + (unsigned long long)v;
        any = 1;
        s++;
    }

    if (!any) {
        if (endptr) {
            *endptr = (char*)nptr;
        }
        return 0;
    }

    if (endptr) {
        *endptr = (char*)s;
    }

    return neg ? -(long)acc : (long)acc;
}

unsigned long strtoul(const char* nptr, char** endptr, int base)
{
    if (!nptr) {
        if (endptr) {
            *endptr = NULL;
        }
        return 0;
    }

    const char* s = nptr;
    while (isspace(*s)) {
        s++;
    }

    if (base != 0 && (base < 2 || base > 36)) {
        if (endptr) {
            *endptr = (char*)nptr;
        }
        return 0;
    }

    if (*s == '+') {
        s++;
    }

    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            base = 16;
            s += 2;
        } else if (s[0] == '0') {
            base = 8;
            s++;
        } else {
            base = 10;
        }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    unsigned long long acc = 0;
    int any = 0;

    while (*s) {
        int v = digit_value(*s);
        if (v < 0 || v >= base) {
            break;
        }
        acc = acc * (unsigned long long)base + (unsigned long long)v;
        any = 1;
        s++;
    }

    if (!any) {
        if (endptr) {
            *endptr = (char*)nptr;
        }
        return 0;
    }

    if (endptr) {
        *endptr = (char*)s;
    }

    return (unsigned long)acc;
}

static void swap_bytes(uint8_t* a, uint8_t* b, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        uint8_t tmp = a[i];
        a[i] = b[i];
        b[i] = tmp;
    }
}

static size_t partition(uint8_t* base, size_t low, size_t high, size_t size,
                        int (*compar)(const void*, const void*))
{
    uint8_t* pivot = base + high * size;
    size_t i = low;

    for (size_t j = low; j < high; j++) {
        uint8_t* elem = base + j * size;
        if (compar(elem, pivot) < 0) {
            swap_bytes(base + i * size, elem, size);
            i++;
        }
    }

    swap_bytes(base + i * size, pivot, size);
    return i;
}

static void qsort_rec(uint8_t* base, size_t low, size_t high, size_t size,
                      int (*compar)(const void*, const void*))
{
    if (high <= low) {
        return;
    }

    size_t pivot = partition(base, low, high, size, compar);
    if (pivot > 0) {
        qsort_rec(base, low, pivot - 1, size, compar);
    }
    qsort_rec(base, pivot + 1, high, size, compar);
}

void qsort(void* base, size_t nmemb, size_t size,
           int (*compar)(const void*, const void*))
{
    if (!base || !compar || nmemb < 2 || size == 0) {
        return;
    }

    qsort_rec((uint8_t*)base, 0, nmemb - 1, size, compar);
}
