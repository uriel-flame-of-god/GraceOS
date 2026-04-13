// ============================
// GraceOS Dynamic Array Library
// NASA SAFE_LIBC Implementation
// ============================

#ifndef GRACEOS_ARRAY_H
#define GRACEOS_ARRAY_H

#include "int.h"

/* ========================================
 * NASA SAFE_LIBC Compliant Array Library
 * ========================================
 *
 * All array functions:
 * - Check for NULL pointers
 * - Validate bounds explicitly
 * - Return error codes for failures
 * - Never cause out-of-bounds access
 * - Use static allocation only
 */

// Dynamic array structure
struct array {
    void** data;        // Array of pointers (static buffer)
    size_t size;        // Current number of elements
    size_t capacity;    // Allocated capacity (immutable)
};

/* Initialize an array with static buffer */
int array_init(struct array* arr, void** buffer, size_t capacity);

/* Add element to end (returns error if full) */
int array_push(struct array* arr, void* element);

/* Remove element from end (returns error if empty) */
int array_pop(struct array* arr, void** out_element);

/* Get element at index (returns error if out of bounds) */
int array_get(struct array* arr, size_t index, void** out_element);

/* Set element at index (returns error if out of bounds) */
int array_set(struct array* arr, size_t index, void* element);

/* Insert element at index (returns error if full or out of bounds) */
int array_insert(struct array* arr, size_t index, void* element);

/* Remove element at index (returns error if out of bounds) */
int array_remove(struct array* arr, size_t index, void** out_element);

/* Get current size */
int array_size(struct array* arr, size_t* out_size);

/* Check if empty */
int array_empty(struct array* arr, bool* out_empty);

/* Clear array (reset size to 0) */
int array_clear(struct array* arr);

#endif
