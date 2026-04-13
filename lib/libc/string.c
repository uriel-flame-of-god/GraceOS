// ============================
// GraceOS String / Memory Lib
// NASA SAFE_LIBC Implementation
// ============================

#include "string.h"

/* ========================================
 * Legacy Memory Functions (Unsafe)
 * ======================================== */

void* memset(void* dest, int val, size_t len)
{
    unsigned char* ptr = dest;
    size_t i;

    /* Phase 1: Bounded loop */
    for (i = 0; i < len; i++) {
        ptr[i] = (unsigned char)val;
    }

    return dest;
}

void* memcpy(void* dest, const void* src, size_t len)
{
    unsigned char* d = dest;
    const unsigned char* s = src;
    size_t i;

    /* Phase 1: Bounded loop */
    for (i = 0; i < len; i++) {
        d[i] = s[i];
    }

    return dest;
}

void* memmove(void* dest, const void* src, size_t len)
{
    unsigned char* d = dest;
    const unsigned char* s = src;
    size_t i;

    /* Handle overlapping regions safely */
    if (d < s) {
        /* Copy forward */
        for (i = 0; i < len; i++) {
            d[i] = s[i];
        }
    } else if (d > s) {
        /* Copy backward to avoid overwriting source */
        for (i = len; i > 0; i--) {
            d[i-1] = s[i-1];
        }
    }
    /* If d == s, no copy needed */

    return dest;
}

int memcmp(const void* a, const void* b, size_t len)
{
    const unsigned char* x = a;
    const unsigned char* y = b;
    size_t i;

    /* Phase 1: Bounded comparison */
    for (i = 0; i < len; i++) {
        if (x[i] != y[i]) {
            return x[i] - y[i];
        }
    }

    return 0;
}

void* memchr(const void* s, int c, size_t n)
{
    const unsigned char* p = s;
    unsigned char uc = (unsigned char)c;
    size_t i;

    for (i = 0; i < n; i++) {
        if (p[i] == uc) {
            return (void*)(p + i);
        }
    }

    return NULL;
}

/* ========================================
 * Safe Memory Functions (NASA Compliant)
 * ======================================== */

int safe_memset(void* dest, size_t dest_size, int val, size_t count)
{
    unsigned char* ptr;
    size_t i;

    /* Phase 1: Validation */
    if (dest == NULL) {
        return ENULLPTR;
    }

    if (count > dest_size) {
        return EOVERFLOW;
    }

    /* Phase 2: Safe copy with bounds */
    ptr = (unsigned char*)dest;
    for (i = 0; i < count; i++) {
        ptr[i] = (unsigned char)val;
    }

    return EOK;
}

int safe_memcpy(void* dest, size_t dest_size, const void* src, size_t src_size)
{
    unsigned char* d;
    const unsigned char* s;
    size_t copy_size;
    size_t i;

    /* Phase 1: Validation */
    if (dest == NULL || src == NULL) {
        return ENULLPTR;
    }

    if (src_size > dest_size) {
        return EOVERFLOW;
    }

    /* Phase 2: Safe copy with bounds */
    d = (unsigned char*)dest;
    s = (const unsigned char*)src;
    copy_size = src_size;

    for (i = 0; i < copy_size; i++) {
        d[i] = s[i];
    }

    return EOK;
}

/* ========================================
 * Legacy String Functions (Unsafe)
 * ======================================== */

size_t strlen(const char* str)
{
    size_t len = 0;

    /* Unbounded loop - legacy compatibility */
    while (str[len]) {
        len++;
    }

    return len;
}

char* strcpy(char* dest, const char* src)
{
    size_t i = 0;

    /* Unbounded copy - legacy compatibility */
    while ((dest[i] = src[i]) != '\0') {
        i++;
    }

    return dest;
}

int strcmp(const char* a, const char* b)
{
    size_t i = 0;

    /* Unbounded comparison - legacy compatibility */
    while (a[i] && (a[i] == b[i])) {
        i++;
    }

    return (unsigned char)a[i] - (unsigned char)b[i];
}

int strncmp(const char* a, const char* b, size_t n)
{
    size_t i;

    /* Phase 1: Bounded comparison */
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return (unsigned char)a[i] - (unsigned char)b[i];
        }
        if (a[i] == '\0') {
            return 0;
        }
    }

    return 0;
}

char* strchr(const char* s, int c)
{
    char ch = (char)c;

    while (*s) {
        if (*s == ch) {
            return (char*)s;
        }
        s++;
    }

    if (ch == '\0') {
        return (char*)s;
    }

    return NULL;
}

char* strrchr(const char* s, int c)
{
    char ch = (char)c;
    const char* last = NULL;

    while (*s) {
        if (*s == ch) {
            last = s;
        }
        s++;
    }

    if (ch == '\0') {
        return (char*)s;
    }

    return (char*)last;
}

char* strncpy(char* dest, const char* src, size_t n)
{
    size_t i;

    /* Phase 1: Copy up to n characters */
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }

    /* Phase 2: Pad with zeros */
    for (; i < n; i++) {
        dest[i] = '\0';
    }

    return dest;
}

char* strcat(char* dest, const char* src)
{
    size_t dest_len = 0;
    size_t i = 0;

    /* Phase 1: Find end of dest */
    while (dest[dest_len]) {
        dest_len++;
    }

    /* Phase 2: Append src */
    while ((dest[dest_len + i] = src[i]) != '\0') {
        i++;
    }

    return dest;
}

/* ========================================
 * Safe String Functions (NASA Compliant)
 * ======================================== */

int safe_strlen(const char* str, size_t max_len, size_t* out_len)
{
    size_t len = 0;

    /* Phase 1: Validation */
    if (str == NULL || out_len == NULL) {
        return ENULLPTR;
    }

    if (max_len == 0) {
        return EINVAL;
    }

    /* Phase 2: Bounded length calculation */
    for (len = 0; len < max_len; len++) {
        if (str[len] == '\0') {
            *out_len = len;
            return EOK;
        }
    }

    /* Phase 3: String not terminated within bounds */
    return ERANGE;
}

int safe_strcpy(char* dest, size_t dest_capacity, const char* src)
{
    size_t i;

    /* Phase 1: Validation */
    if (dest == NULL || src == NULL) {
        return ENULLPTR;
    }

    if (dest_capacity == 0) {
        return EINVAL;
    }

    /* Phase 2: Bounded copy */
    for (i = 0; i < dest_capacity - 1; i++) {
        dest[i] = src[i];
        if (src[i] == '\0') {
            return EOK;
        }
    }

    /* Phase 3: Ensure null termination */
    dest[dest_capacity - 1] = '\0';

    /* Phase 4: Check if source was fully copied */
    if (src[i] != '\0') {
        return EOVERFLOW;
    }

    return EOK;
}

int safe_strncpy(char* dest, size_t dest_capacity, const char* src, size_t count)
{
    size_t i;
    size_t copy_len;

    /* Phase 1: Validation */
    if (dest == NULL || src == NULL) {
        return ENULLPTR;
    }

    if (dest_capacity == 0) {
        return EINVAL;
    }

    /* Phase 2: Determine copy length */
    copy_len = count;
    if (copy_len >= dest_capacity) {
        copy_len = dest_capacity - 1;
    }

    /* Phase 3: Bounded copy */
    for (i = 0; i < copy_len; i++) {
        dest[i] = src[i];
        if (src[i] == '\0') {
            return EOK;
        }
    }

    /* Phase 4: Null terminate */
    dest[i] = '\0';

    return EOK;
}

int safe_strcat(char* dest, size_t dest_capacity, const char* src)
{
    size_t dest_len = 0;
    size_t i;

    /* Phase 1: Validation */
    if (dest == NULL || src == NULL) {
        return ENULLPTR;
    }

    if (dest_capacity == 0) {
        return EINVAL;
    }

    /* Phase 2: Find end of dest (bounded) */
    for (dest_len = 0; dest_len < dest_capacity; dest_len++) {
        if (dest[dest_len] == '\0') {
            break;
        }
    }

    if (dest_len >= dest_capacity) {
        return EOVERFLOW; /* dest not null-terminated */
    }

    /* Phase 3: Bounded append */
    for (i = 0; dest_len + i < dest_capacity - 1; i++) {
        dest[dest_len + i] = src[i];
        if (src[i] == '\0') {
            return EOK;
        }
    }

    /* Phase 4: Ensure null termination */
    dest[dest_capacity - 1] = '\0';

    /* Phase 5: Check if source was fully appended */
    if (src[i] != '\0') {
        return EOVERFLOW;
    }

    return EOK;
}

int safe_strcmp(const char* a, const char* b, int* result)
{
    size_t i = 0;

    /* Phase 1: Validation */
    if (a == NULL || b == NULL || result == NULL) {
        return ENULLPTR;
    }

    /* Phase 2: Unbounded comparison (safe because we check terminator) */
    while (a[i] && (a[i] == b[i])) {
        i++;
    }

    /* Phase 3: Return result */
    *result = (unsigned char)a[i] - (unsigned char)b[i];

    return EOK;
}

