/*
 * unicode-data.c — Static Unicode 15.0 character property tables
 *
 * Tables are derived from the Unicode Character Database and compressed into
 * contiguous ranges that share the same property-flag bitmask.  This avoids
 * storing one entry per codepoint (1,114,112 entries) while still giving
 * O(log N) resolution with binary search.
 *
 * Coverage:
 *   - Range flags: whitespace, alpha, digit, upper, lower, punct, ctrl,
 *                  combining, letter-number, surrogate.
 *   - Lowercase map: common Latin, Greek, Cyrillic uppercase → lowercase.
 *   - Uppercase map: common lowercase → uppercase (reverse of the above).
 */

#include "unicode-data.h"
#include "unicode.h"

/* -------------------------------------------------------------------------
 * Range-flags table
 *
 * Each row covers [start, end] inclusive.  Ranges are sorted by 'start'
 * and must not overlap.  Codepoints not covered by any range return 0.
 * ------------------------------------------------------------------------- */

const unicode_range_t unicode_ranges_flags[] = {
    /* C0 controls (U+0000–U+001F) */
    { 0x0000, 0x001F, UNICODE_FLAG_CTRL },
    /* Space U+0020 */
    { 0x0020, 0x0020, UNICODE_FLAG_WHITESPACE | UNICODE_FLAG_PUNCT },
    /* ASCII punctuation U+0021–U+002F */
    { 0x0021, 0x002F, UNICODE_FLAG_PUNCT },
    /* ASCII digits U+0030–U+0039 */
    { 0x0030, 0x0039, UNICODE_FLAG_DIGIT },
    /* More ASCII punctuation U+003A–U+0040 */
    { 0x003A, 0x0040, UNICODE_FLAG_PUNCT },
    /* ASCII uppercase U+0041–U+005A */
    { 0x0041, 0x005A, UNICODE_FLAG_ALPHA | UNICODE_FLAG_UPPER },
    /* ASCII punctuation U+005B–U+0060 */
    { 0x005B, 0x0060, UNICODE_FLAG_PUNCT },
    /* ASCII lowercase U+0061–U+007A */
    { 0x0061, 0x007A, UNICODE_FLAG_ALPHA | UNICODE_FLAG_LOWER },
    /* ASCII punctuation / delete U+007B–U+007E */
    { 0x007B, 0x007E, UNICODE_FLAG_PUNCT },
    /* DEL U+007F */
    { 0x007F, 0x007F, UNICODE_FLAG_CTRL },
    /* C1 controls U+0080–U+009F */
    { 0x0080, 0x009F, UNICODE_FLAG_CTRL },
    /* Latin-1 supplement punctuation / special U+00A0–U+00BF */
    { 0x00A0, 0x00A0, UNICODE_FLAG_WHITESPACE },  /* NBSP */
    { 0x00A1, 0x00BF, UNICODE_FLAG_PUNCT },
    /* Latin-1 uppercase U+00C0–U+00D6 */
    { 0x00C0, 0x00D6, UNICODE_FLAG_ALPHA | UNICODE_FLAG_UPPER },
    /* × U+00D7 */
    { 0x00D7, 0x00D7, UNICODE_FLAG_PUNCT },
    /* Latin-1 uppercase U+00D8–U+00DE */
    { 0x00D8, 0x00DE, UNICODE_FLAG_ALPHA | UNICODE_FLAG_UPPER },
    /* Latin-1 lowercase U+00DF–U+00F6 */
    { 0x00DF, 0x00F6, UNICODE_FLAG_ALPHA | UNICODE_FLAG_LOWER },
    /* ÷ U+00F7 */
    { 0x00F7, 0x00F7, UNICODE_FLAG_PUNCT },
    /* Latin-1 lowercase U+00F8–U+00FF */
    { 0x00F8, 0x00FF, UNICODE_FLAG_ALPHA | UNICODE_FLAG_LOWER },
    /* Latin Extended-A uppercase U+0100–U+012E (even) uppercase, odd lowercase, simplified */
    { 0x0100, 0x017E, UNICODE_FLAG_ALPHA },
    /* Latin Extended-B U+0180–U+024F */
    { 0x0180, 0x024F, UNICODE_FLAG_ALPHA },
    /* IPA extensions U+0250–U+02AF */
    { 0x0250, 0x02AF, UNICODE_FLAG_ALPHA },
    /* Spacing modifier letters U+02B0–U+02FF */
    { 0x02B0, 0x02FF, UNICODE_FLAG_ALPHA },
    /* Combining diacritical marks U+0300–U+036F */
    { 0x0300, 0x036F, UNICODE_FLAG_COMBINING },
    /* Greek uppercase U+0391–U+03A9 */
    { 0x0391, 0x03A9, UNICODE_FLAG_ALPHA | UNICODE_FLAG_UPPER },
    /* Greek lowercase U+03B1–U+03C9 */
    { 0x03B1, 0x03C9, UNICODE_FLAG_ALPHA | UNICODE_FLAG_LOWER },
    /* More Greek letters */
    { 0x03CA, 0x03FF, UNICODE_FLAG_ALPHA },
    /* Cyrillic uppercase U+0410–U+042F */
    { 0x0410, 0x042F, UNICODE_FLAG_ALPHA | UNICODE_FLAG_UPPER },
    /* Cyrillic lowercase U+0430–U+044F */
    { 0x0430, 0x044F, UNICODE_FLAG_ALPHA | UNICODE_FLAG_LOWER },
    /* More Cyrillic U+0450–U+04FF */
    { 0x0450, 0x04FF, UNICODE_FLAG_ALPHA },
    /* Hebrew U+05D0–U+05EA */
    { 0x05D0, 0x05EA, UNICODE_FLAG_ALPHA },
    /* Arabic U+0600–U+06FF */
    { 0x0600, 0x06FF, UNICODE_FLAG_ALPHA },
    /* Devanagari U+0900–U+097F */
    { 0x0900, 0x097F, UNICODE_FLAG_ALPHA },
    /* CJK Unified Ideographs U+4E00–U+9FFF */
    { 0x4E00, 0x9FFF, UNICODE_FLAG_ALPHA },
    /* CJK Extension A U+3400–U+4DBF */
    { 0x3400, 0x4DBF, UNICODE_FLAG_ALPHA },
    /* Hangul Syllables U+AC00–U+D7A3 */
    { 0xAC00, 0xD7A3, UNICODE_FLAG_ALPHA },
    /* Surrogates U+D800–U+DFFF */
    { 0xD800, 0xDFFF, UNICODE_FLAG_SURR },
    /* General punctuation U+2000–U+206F (includes various spaces) */
    { 0x2000, 0x200A, UNICODE_FLAG_WHITESPACE },
    { 0x200B, 0x206F, UNICODE_FLAG_PUNCT },
    /* Line/paragraph separators */
    { 0x2028, 0x2029, UNICODE_FLAG_WHITESPACE | UNICODE_FLAG_CTRL },
    /* Ideographic space U+3000 */
    { 0x3000, 0x3000, UNICODE_FLAG_WHITESPACE },
    /* Fullwidth forms U+FF01–U+FF60 */
    { 0xFF01, 0xFF60, UNICODE_FLAG_PUNCT },
    /* Fullwidth latin uppercase U+FF21–U+FF3A */
    { 0xFF21, 0xFF3A, UNICODE_FLAG_ALPHA | UNICODE_FLAG_UPPER },
    /* Fullwidth latin lowercase U+FF41–U+FF5A */
    { 0xFF41, 0xFF5A, UNICODE_FLAG_ALPHA | UNICODE_FLAG_LOWER },
};

const size_t unicode_ranges_flags_count =
    sizeof(unicode_ranges_flags) / sizeof(unicode_ranges_flags[0]);

/* -------------------------------------------------------------------------
 * Lowercase map  (uppercase codepoint → lowercase codepoint)
 *
 * Latin, Greek, and Cyrillic blocks only.  The map is sorted by 'src'.
 * ------------------------------------------------------------------------- */

const unicode_case_map_t unicode_lower_map[] = {
    /* ASCII */
    { 0x0041, 0x0061 }, { 0x0042, 0x0062 }, { 0x0043, 0x0063 },
    { 0x0044, 0x0064 }, { 0x0045, 0x0065 }, { 0x0046, 0x0066 },
    { 0x0047, 0x0067 }, { 0x0048, 0x0068 }, { 0x0049, 0x0069 },
    { 0x004A, 0x006A }, { 0x004B, 0x006B }, { 0x004C, 0x006C },
    { 0x004D, 0x006D }, { 0x004E, 0x006E }, { 0x004F, 0x006F },
    { 0x0050, 0x0070 }, { 0x0051, 0x0071 }, { 0x0052, 0x0072 },
    { 0x0053, 0x0073 }, { 0x0054, 0x0074 }, { 0x0055, 0x0075 },
    { 0x0056, 0x0076 }, { 0x0057, 0x0077 }, { 0x0058, 0x0078 },
    { 0x0059, 0x0079 }, { 0x005A, 0x007A },
    /* Latin-1 supplement */
    { 0x00C0, 0x00E0 }, { 0x00C1, 0x00E1 }, { 0x00C2, 0x00E2 },
    { 0x00C3, 0x00E3 }, { 0x00C4, 0x00E4 }, { 0x00C5, 0x00E5 },
    { 0x00C6, 0x00E6 }, { 0x00C7, 0x00E7 }, { 0x00C8, 0x00E8 },
    { 0x00C9, 0x00E9 }, { 0x00CA, 0x00EA }, { 0x00CB, 0x00EB },
    { 0x00CC, 0x00EC }, { 0x00CD, 0x00ED }, { 0x00CE, 0x00EE },
    { 0x00CF, 0x00EF }, { 0x00D0, 0x00F0 }, { 0x00D1, 0x00F1 },
    { 0x00D2, 0x00F2 }, { 0x00D3, 0x00F3 }, { 0x00D4, 0x00F4 },
    { 0x00D5, 0x00F5 }, { 0x00D6, 0x00F6 }, { 0x00D8, 0x00F8 },
    { 0x00D9, 0x00F9 }, { 0x00DA, 0x00FA }, { 0x00DB, 0x00FB },
    { 0x00DC, 0x00FC }, { 0x00DD, 0x00FD }, { 0x00DE, 0x00FE },
    /* Greek uppercase */
    { 0x0391, 0x03B1 }, { 0x0392, 0x03B2 }, { 0x0393, 0x03B3 },
    { 0x0394, 0x03B4 }, { 0x0395, 0x03B5 }, { 0x0396, 0x03B6 },
    { 0x0397, 0x03B7 }, { 0x0398, 0x03B8 }, { 0x0399, 0x03B9 },
    { 0x039A, 0x03BA }, { 0x039B, 0x03BB }, { 0x039C, 0x03BC },
    { 0x039D, 0x03BD }, { 0x039E, 0x03BE }, { 0x039F, 0x03BF },
    { 0x03A0, 0x03C0 }, { 0x03A1, 0x03C1 }, { 0x03A3, 0x03C3 },
    { 0x03A4, 0x03C4 }, { 0x03A5, 0x03C5 }, { 0x03A6, 0x03C6 },
    { 0x03A7, 0x03C7 }, { 0x03A8, 0x03C8 }, { 0x03A9, 0x03C9 },
    /* Cyrillic uppercase U+0410–U+042F → U+0430–U+044F */
    { 0x0410, 0x0430 }, { 0x0411, 0x0431 }, { 0x0412, 0x0432 },
    { 0x0413, 0x0433 }, { 0x0414, 0x0434 }, { 0x0415, 0x0435 },
    { 0x0416, 0x0436 }, { 0x0417, 0x0437 }, { 0x0418, 0x0438 },
    { 0x0419, 0x0439 }, { 0x041A, 0x043A }, { 0x041B, 0x043B },
    { 0x041C, 0x043C }, { 0x041D, 0x043D }, { 0x041E, 0x043E },
    { 0x041F, 0x043F }, { 0x0420, 0x0440 }, { 0x0421, 0x0441 },
    { 0x0422, 0x0442 }, { 0x0423, 0x0443 }, { 0x0424, 0x0444 },
    { 0x0425, 0x0445 }, { 0x0426, 0x0446 }, { 0x0427, 0x0447 },
    { 0x0428, 0x0448 }, { 0x0429, 0x0449 }, { 0x042A, 0x044A },
    { 0x042B, 0x044B }, { 0x042C, 0x044C }, { 0x042D, 0x044D },
    { 0x042E, 0x044E }, { 0x042F, 0x044F },
};

const size_t unicode_lower_map_count =
    sizeof(unicode_lower_map) / sizeof(unicode_lower_map[0]);

/* -------------------------------------------------------------------------
 * Uppercase map (lowercase codepoint → uppercase codepoint)
 * Only ASCII + Latin-1 for now; extended as needed.
 * ------------------------------------------------------------------------- */

const unicode_case_map_t unicode_upper_map[] = {
    /* ASCII */
    { 0x0061, 0x0041 }, { 0x0062, 0x0042 }, { 0x0063, 0x0043 },
    { 0x0064, 0x0044 }, { 0x0065, 0x0045 }, { 0x0066, 0x0046 },
    { 0x0067, 0x0047 }, { 0x0068, 0x0048 }, { 0x0069, 0x0049 },
    { 0x006A, 0x004A }, { 0x006B, 0x004B }, { 0x006C, 0x004C },
    { 0x006D, 0x004D }, { 0x006E, 0x004E }, { 0x006F, 0x004F },
    { 0x0070, 0x0050 }, { 0x0071, 0x0051 }, { 0x0072, 0x0052 },
    { 0x0073, 0x0053 }, { 0x0074, 0x0054 }, { 0x0075, 0x0055 },
    { 0x0076, 0x0056 }, { 0x0077, 0x0057 }, { 0x0078, 0x0058 },
    { 0x0079, 0x0059 }, { 0x007A, 0x005A },
    /* Latin-1 supplement */
    { 0x00E0, 0x00C0 }, { 0x00E1, 0x00C1 }, { 0x00E2, 0x00C2 },
    { 0x00E3, 0x00C3 }, { 0x00E4, 0x00C4 }, { 0x00E5, 0x00C5 },
    { 0x00E6, 0x00C6 }, { 0x00E7, 0x00C7 }, { 0x00E8, 0x00C8 },
    { 0x00E9, 0x00C9 }, { 0x00EA, 0x00CA }, { 0x00EB, 0x00CB },
    { 0x00EC, 0x00CC }, { 0x00ED, 0x00CD }, { 0x00EE, 0x00CE },
    { 0x00EF, 0x00CF }, { 0x00F0, 0x00D0 }, { 0x00F1, 0x00D1 },
    { 0x00F2, 0x00D2 }, { 0x00F3, 0x00D3 }, { 0x00F4, 0x00D4 },
    { 0x00F5, 0x00D5 }, { 0x00F6, 0x00D6 }, { 0x00F8, 0x00D8 },
    { 0x00F9, 0x00D9 }, { 0x00FA, 0x00DA }, { 0x00FB, 0x00DB },
    { 0x00FC, 0x00DC }, { 0x00FD, 0x00DD }, { 0x00FE, 0x00DE },
};

const size_t unicode_upper_map_count =
    sizeof(unicode_upper_map) / sizeof(unicode_upper_map[0]);
