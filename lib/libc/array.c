// ============================
// GraceOS Dynamic Array Library
// NASA SAFE_LIBC Implementation
// ============================

#include "array.h"
#include "string.h"

/* ========================================
 * NASA Compliant Array Implementation
 * ======================================== */

int array_init(struct array* arr, void** buffer, size_t capacity)
{
    /* Phase 1: Validation */
    if (arr == NULL || buffer == NULL) {
        return ENULLPTR;
    }

    if (capacity == 0) {
        return EINVAL;
    }

    /* Phase 2: Initialization */
    arr->data = buffer;
    arr->size = 0;
    arr->capacity = capacity;

    return EOK;
}

int array_push(struct array* arr, void* element)
{
    /* Phase 1: Validation */
    if (arr == NULL) {
        return ENULLPTR;
    }

    /* Phase 2: Capacity check */
    if (arr->size >= arr->capacity) {
        return EOVERFLOW;
    }

    /* Phase 3: Insert element */
    arr->data[arr->size] = element;
    arr->size++;

    return EOK;
}

int array_pop(struct array* arr, void** out_element)
{
    /* Phase 1: Validation */
    if (arr == NULL || out_element == NULL) {
        return ENULLPTR;
    }

    /* Phase 2: Empty check */
    if (arr->size == 0) {
        return ERANGE;
    }

    /* Phase 3: Remove element */
    arr->size--;
    *out_element = arr->data[arr->size];

    return EOK;
}

int array_get(struct array* arr, size_t index, void** out_element)
{
    /* Phase 1: Validation */
    if (arr == NULL || out_element == NULL) {
        return ENULLPTR;
    }

    /* Phase 2: Bounds check */
    if (index >= arr->size) {
        return EBOUND;
    }

    /* Phase 3: Retrieve element */
    *out_element = arr->data[index];

    return EOK;
}

int array_set(struct array* arr, size_t index, void* element)
{
    /* Phase 1: Validation */
    if (arr == NULL) {
        return ENULLPTR;
    }

    /* Phase 2: Bounds check */
    if (index >= arr->size) {
        return EBOUND;
    }

    /* Phase 3: Set element */
    arr->data[index] = element;

    return EOK;
}

int array_insert(struct array* arr, size_t index, void* element)
{
    size_t i;
    size_t insert_pos;

    /* Phase 1: Validation */
    if (arr == NULL) {
        return ENULLPTR;
    }

    /* Phase 2: Capacity check */
    if (arr->size >= arr->capacity) {
        return EOVERFLOW;
    }

    /* Phase 3: Determine insert position */
    insert_pos = index;
    if (insert_pos > arr->size) {
        insert_pos = arr->size;
    }

    /* Phase 4: Shift elements right (bounded loop) */
    for (i = arr->size; i > insert_pos; i--) {
        arr->data[i] = arr->data[i - 1];
    }

    /* Phase 5: Insert element */
    arr->data[insert_pos] = element;
    arr->size++;

    return EOK;
}

int array_remove(struct array* arr, size_t index, void** out_element)
{
    size_t i;

    /* Phase 1: Validation */
    if (arr == NULL) {
        return ENULLPTR;
    }

    /* Phase 2: Bounds check */
    if (index >= arr->size) {
        return EBOUND;
    }

    /* Phase 3: Get element if requested */
    if (out_element != NULL) {
        *out_element = arr->data[index];
    }

    /* Phase 4: Shift elements left (bounded loop) */
    for (i = index; i < arr->size - 1; i++) {
        arr->data[i] = arr->data[i + 1];
    }

    /* Phase 5: Update size */
    arr->size--;

    return EOK;
}

int array_size(struct array* arr, size_t* out_size)
{
    /* Phase 1: Validation */
    if (arr == NULL || out_size == NULL) {
        return ENULLPTR;
    }

    /* Phase 2: Return size */
    *out_size = arr->size;

    return EOK;
}

int array_empty(struct array* arr, bool* out_empty)
{
    /* Phase 1: Validation */
    if (arr == NULL || out_empty == NULL) {
        return ENULLPTR;
    }

    /* Phase 2: Check empty */
    *out_empty = (arr->size == 0);

    return EOK;
}

int array_clear(struct array* arr)
{
    /* Phase 1: Validation */
    if (arr == NULL) {
        return ENULLPTR;
    }

    /* Phase 2: Clear (no deallocation in static mode) */
    arr->size = 0;

    return EOK;
}
