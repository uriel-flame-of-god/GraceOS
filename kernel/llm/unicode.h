#ifndef GRACE_LLM_UNICODE_H
#define GRACE_LLM_UNICODE_H

#include "../../lib/libc/int.h"

/* Unicode codepoint property flags stored in uint16_t */
#define UNICODE_FLAG_WHITESPACE  (1 << 0)
#define UNICODE_FLAG_ALPHA       (1 << 1)
#define UNICODE_FLAG_DIGIT       (1 << 2)
#define UNICODE_FLAG_UPPER       (1 << 3)
#define UNICODE_FLAG_LOWER       (1 << 4)
#define UNICODE_FLAG_PUNCT       (1 << 5)
#define UNICODE_FLAG_CTRL        (1 << 6)
#define UNICODE_FLAG_COMBINING   (1 << 7)
#define UNICODE_FLAG_LETTER_NUM  (1 << 8)  /* Nl category (letter numbers) */
#define UNICODE_FLAG_SURR        (1 << 9)  /* Surrogate pair range */

/* UTF-8 decode result codes */
#define UNICODE_DECODE_OK        0
#define UNICODE_DECODE_INCOMPLETE  1
#define UNICODE_DECODE_INVALID     2

/* Unicode replacement character */
#define UNICODE_REPLACEMENT      0xFFFD

/* Maximum UTF-8 bytes for one codepoint */
#define UNICODE_MAX_UTF8_BYTES   4

/*
 * Decode one UTF-8 codepoint from 'src' (up to max_len bytes).
 * Returns number of bytes consumed, or 0 on error.
 * *codepoint is set to decoded value (UNICODE_REPLACEMENT on error).
 */
int unicode_utf8_decode(const uint8_t* src, size_t max_len, uint32_t* codepoint);

/*
 * Encode a codepoint to UTF-8 buf (must be at least 4 bytes).
 * Returns number of bytes written, or 0 on invalid codepoint.
 */
int unicode_utf8_encode(uint32_t codepoint, uint8_t* buf);

/* Count codepoints in a UTF-8 string of 'len' bytes. */
size_t unicode_utf8_len(const uint8_t* s, size_t len);

/*
 * Retrieve codepoint property flags.
 * Uses a compressed range lookup table from unicode-data.
 */
uint16_t unicode_flags(uint32_t codepoint);

/* Convenience predicates */
static inline int unicode_is_whitespace(uint32_t cp)
    { return (unicode_flags(cp) & UNICODE_FLAG_WHITESPACE) != 0; }
static inline int unicode_is_alpha(uint32_t cp)
    { return (unicode_flags(cp) & UNICODE_FLAG_ALPHA) != 0; }
static inline int unicode_is_digit(uint32_t cp)
    { return (unicode_flags(cp) & UNICODE_FLAG_DIGIT) != 0; }
static inline int unicode_is_upper(uint32_t cp)
    { return (unicode_flags(cp) & UNICODE_FLAG_UPPER) != 0; }
static inline int unicode_is_lower(uint32_t cp)
    { return (unicode_flags(cp) & UNICODE_FLAG_LOWER) != 0; }
static inline int unicode_is_punct(uint32_t cp)
    { return (unicode_flags(cp) & UNICODE_FLAG_PUNCT) != 0; }
static inline int unicode_is_control(uint32_t cp)
    { return (unicode_flags(cp) & UNICODE_FLAG_CTRL) != 0; }

/*
 * Simple lowercase conversion (ASCII + common Latin).
 * Returns the lowercased codepoint, or the original if no mapping exists.
 */
uint32_t unicode_to_lower(uint32_t codepoint);

/*
 * Normalize a UTF-8 byte sequence by stripping BOM and normalizing line
 * endings to LF.  Writes at most dst_max bytes.  Returns output length.
 */
size_t unicode_normalize_lf(const uint8_t* src, size_t src_len,
                             uint8_t* dst, size_t dst_max);

#endif /* GRACE_LLM_UNICODE_H */
