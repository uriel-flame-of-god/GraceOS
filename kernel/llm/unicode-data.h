#ifndef GRACE_LLM_UNICODE_DATA_H
#define GRACE_LLM_UNICODE_DATA_H

#include "../../lib/libc/int.h"

/*
 * unicode-data.h — Static compressed Unicode 15.0 tables
 *
 * The tables are consulted by unicode.c to resolve codepoint properties.
 * Each range entry covers a contiguous run of codepoints sharing the same
 * flags bitmask.  A binary search over the 'start' field resolves any
 * codepoint in O(log N).
 */

/* One entry in the range-flags table */
typedef struct {
    uint32_t start;   /* First codepoint in range (inclusive) */
    uint32_t end;     /* Last  codepoint in range (inclusive) */
    uint16_t flags;   /* UNICODE_FLAG_* bitmask */
} unicode_range_t;

/* One entry in the simple case-mapping tables */
typedef struct {
    uint32_t src;
    uint32_t dst;
} unicode_case_map_t;

/* Range flags table (sorted by start codepoint) */
extern const unicode_range_t  unicode_ranges_flags[];
extern const size_t           unicode_ranges_flags_count;

/* Simple uppercase → lowercase mapping */
extern const unicode_case_map_t  unicode_lower_map[];
extern const size_t              unicode_lower_map_count;

/* Simple lowercase → uppercase mapping */
extern const unicode_case_map_t  unicode_upper_map[];
extern const size_t              unicode_upper_map_count;

#endif /* GRACE_LLM_UNICODE_DATA_H */
