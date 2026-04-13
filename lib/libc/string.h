#ifndef GRACEOS_STRING_H
#define GRACEOS_STRING_H

#include "int.h"

/* ========================================
 * NASA SAFE_LIBC Compliant String Library
 * ========================================
 *
 * All safe_ functions:
 * - Check for NULL pointers
 * - Require explicit capacity bounds
 * - Return error codes (not pointers)
 * - Never cause buffer overflows
 * - Document all failure modes
 */

/* Memory functions */
void* memset(void* dest, int val, size_t len);
void* memcpy(void* dest, const void* src, size_t len);
void* memmove(void* dest, const void* src, size_t len);
int   memcmp(const void* a, const void* b, size_t len);
void* memchr(const void* s, int c, size_t n);

/* Safe memory functions */
int safe_memset(void* dest, size_t dest_size, int val, size_t count);
int safe_memcpy(void* dest, size_t dest_size, const void* src, size_t src_size);

/* String functions */
size_t strlen(const char* str);
char*  strcpy(char* dest, const char* src);
char*  strncpy(char* dest, const char* src, size_t n);
char*  strcat(char* dest, const char* src);
int    strcmp(const char* a, const char* b);
int    strncmp(const char* a, const char* b, size_t n);
char*  strchr(const char* s, int c);
char*  strrchr(const char* s, int c);

/* Safe string functions (NASA SAFE_LIBC compliant) */
int safe_strlen(const char* str, size_t max_len, size_t* out_len);
int safe_strcpy(char* dest, size_t dest_capacity, const char* src);
int safe_strncpy(char* dest, size_t dest_capacity, const char* src, size_t count);
int safe_strcat(char* dest, size_t dest_capacity, const char* src);
int safe_strcmp(const char* a, const char* b, int* result);

#endif /* GRACEOS_STRING_H */
