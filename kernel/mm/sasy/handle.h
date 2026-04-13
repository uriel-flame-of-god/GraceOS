// ============================
// GraceOS SASY Handle Manager
// Handle-based segment access
// ============================

#ifndef GRACEOS_SASY_HANDLE_H
#define GRACEOS_SASY_HANDLE_H

#include "types.h"

/* ============================
   Handle Configuration
   ============================ */

#define MAX_HANDLES     8192
#define INVALID_HANDLE  ((seg_handle_t)-1)

/* ============================
   Handle Types
   ============================ */

typedef uint32_t seg_handle_t;

typedef struct {
    segment_t* seg;         /* Pointer to segment descriptor */
    uint32_t refs;          /* Handle reference count */
    uint32_t owner_pid;     /* Owning process ID (0 = kernel) */
    uint8_t valid;          /* 1 if handle is valid */
} handle_t;

/* ============================
   Handle Table API
   ============================ */

/* Initialize handle system */
void handle_init(void);

/* Allocate a new handle for a segment */
seg_handle_t handle_alloc(segment_t* seg, uint32_t owner_pid);

/* Free a handle */
void handle_free(seg_handle_t h);

/* Get segment from handle */
segment_t* handle_get_segment(seg_handle_t h);

/* Add reference to handle */
void handle_addref(seg_handle_t h);

/* Release reference from handle */
void handle_release(seg_handle_t h);

/* Validate handle */
int handle_valid(seg_handle_t h);

/* Get handle count for a segment */
uint32_t handle_count_for_segment(segment_t* seg);

/* Duplicate handle for another process */
seg_handle_t handle_dup(seg_handle_t h, uint32_t new_owner_pid);

/* Get handle statistics */
uint32_t handle_get_total(void);
uint32_t handle_get_free(void);

#endif /* GRACEOS_SASY_HANDLE_H */
