/*
 * unicode.c — UTF-8 codec and codepoint property lookup
 *
 * All data comes from unicode-data.c static tables.
 * Binary search is used for range lookups (O(log N)).
 */

#include "unicode.h"
#include "unicode-data.h"

/* -------------------------------------------------------------------------
 * UTF-8 codec
 * ------------------------------------------------------------------------- */

int unicode_utf8_decode(const uint8_t* src, size_t max_len, uint32_t* codepoint)
{
    if (!src || max_len == 0) {
        *codepoint = UNICODE_REPLACEMENT;
        return 0;
    }

    uint8_t b0 = src[0];

    /* Fast path: ASCII */
    if (b0 < 0x80) {
        *codepoint = b0;
        return 1;
    }

    /* Determine sequence length and leading bits */
    int seq_len;
    uint32_t cp;
    if ((b0 & 0xE0) == 0xC0) {          /* 110xxxxx → 2 bytes */
        seq_len = 2;
        cp = b0 & 0x1F;
    } else if ((b0 & 0xF0) == 0xE0) {   /* 1110xxxx → 3 bytes */
        seq_len = 3;
        cp = b0 & 0x0F;
    } else if ((b0 & 0xF8) == 0xF0) {   /* 11110xxx → 4 bytes */
        seq_len = 4;
        cp = b0 & 0x07;
    } else {
        /* Invalid lead byte */
        *codepoint = UNICODE_REPLACEMENT;
        return 1;
    }

    if ((int)max_len < seq_len) {
        *codepoint = UNICODE_REPLACEMENT;
        return 0;  /* incomplete */
    }

    /* Consume continuation bytes */
    for (int i = 1; i < seq_len; i++) {
        uint8_t b = src[i];
        if ((b & 0xC0) != 0x80) {
            /* Invalid continuation */
            *codepoint = UNICODE_REPLACEMENT;
            return i;
        }
        cp = (cp << 6) | (b & 0x3F);
    }

    /* Overlong checks */
    if ((seq_len == 2 && cp < 0x80)  ||
        (seq_len == 3 && cp < 0x800) ||
        (seq_len == 4 && cp < 0x10000)) {
        *codepoint = UNICODE_REPLACEMENT;
        return seq_len;
    }

    /* Surrogate range (U+D800–U+DFFF) is invalid in UTF-8 */
    if (cp >= 0xD800 && cp <= 0xDFFF) {
        *codepoint = UNICODE_REPLACEMENT;
        return seq_len;
    }

    /* Maximum codepoint */
    if (cp > 0x10FFFF) {
        *codepoint = UNICODE_REPLACEMENT;
        return seq_len;
    }

    *codepoint = cp;
    return seq_len;
}

int unicode_utf8_encode(uint32_t cp, uint8_t* buf)
{
    if (cp < 0x80) {
        buf[0] = (uint8_t)cp;
        return 1;
    }
    if (cp < 0x800) {
        buf[0] = (uint8_t)(0xC0 | (cp >> 6));
        buf[1] = (uint8_t)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        buf[0] = (uint8_t)(0xE0 | (cp >> 12));
        buf[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (uint8_t)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF) {
        buf[0] = (uint8_t)(0xF0 | (cp >> 18));
        buf[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (uint8_t)(0x80 | ((cp >> 6)  & 0x3F));
        buf[3] = (uint8_t)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

size_t unicode_utf8_len(const uint8_t* s, size_t len)
{
    size_t count = 0;
    size_t i = 0;
    while (i < len) {
        uint32_t cp;
        int n = unicode_utf8_decode(s + i, len - i, &cp);
        if (n <= 0) break;
        i += (size_t)n;
        count++;
    }
    return count;
}

/* -------------------------------------------------------------------------
 * Binary search over range table
 * ------------------------------------------------------------------------- */

uint16_t unicode_flags(uint32_t codepoint)
{
    /* Fast path for ASCII (covers ~95% of text in practice) */
    if (codepoint < 0x80) {
        if (codepoint < 0x20)            return UNICODE_FLAG_CTRL;
        if (codepoint == 0x20)           return UNICODE_FLAG_WHITESPACE | UNICODE_FLAG_PUNCT;
        if (codepoint <= 0x2F)           return UNICODE_FLAG_PUNCT;
        if (codepoint <= 0x39)           return UNICODE_FLAG_DIGIT;
        if (codepoint <= 0x40)           return UNICODE_FLAG_PUNCT;
        if (codepoint <= 0x5A)           return UNICODE_FLAG_ALPHA | UNICODE_FLAG_UPPER;
        if (codepoint <= 0x60)           return UNICODE_FLAG_PUNCT;
        if (codepoint <= 0x7A)           return UNICODE_FLAG_ALPHA | UNICODE_FLAG_LOWER;
        if (codepoint <= 0x7E)           return UNICODE_FLAG_PUNCT;
        return UNICODE_FLAG_CTRL;  /* 0x7F */
    }

    /* Binary search over sorted range table */
    int lo = 0;
    int hi = (int)unicode_ranges_flags_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const unicode_range_t* r = &unicode_ranges_flags[mid];
        if (codepoint < r->start) {
            hi = mid - 1;
        } else if (codepoint > r->end) {
            lo = mid + 1;
        } else {
            return r->flags;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * Case conversion (binary search over case-map tables)
 * ------------------------------------------------------------------------- */

uint32_t unicode_to_lower(uint32_t cp)
{
    int lo = 0;
    int hi = (int)unicode_lower_map_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (cp < unicode_lower_map[mid].src)       hi = mid - 1;
        else if (cp > unicode_lower_map[mid].src)  lo = mid + 1;
        else return unicode_lower_map[mid].dst;
    }
    return cp;
}

/* -------------------------------------------------------------------------
 * Line-ending normalisation (CRLF / CR → LF, strip BOM)
 * ------------------------------------------------------------------------- */

size_t unicode_normalize_lf(const uint8_t* src, size_t src_len,
                             uint8_t* dst, size_t dst_max)
{
    size_t di = 0;
    size_t si = 0;

    /* Strip UTF-8 BOM (EF BB BF) */
    if (src_len >= 3 &&
        src[0] == 0xEF && src[1] == 0xBB && src[2] == 0xBF) {
        si = 3;
    }

    while (si < src_len && di < dst_max) {
        uint8_t c = src[si++];
        if (c == '\r') {
            /* CR or CRLF → LF */
            if (si < src_len && src[si] == '\n') si++;
            dst[di++] = '\n';
        } else {
            dst[di++] = c;
        }
    }

    if (di < dst_max) dst[di] = '\0';
    return di;
}
