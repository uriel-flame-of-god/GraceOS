#include "../../lib/libc/int.h"

#define PSF2_MAGIC 0x72B54A86

static inline uint32_t le32_to_cpu(const uint8_t* p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

struct font {
    uint32_t width;
    uint32_t height;
    uint32_t glyph_count;
    uint32_t bytes_per_glyph;
    uint8_t* glyphs;
};

int font_init(struct font* font, const uint8_t* font_data)
{
    if (!font || !font_data)
        return -1;

    uint32_t magic      = le32_to_cpu(font_data + 0);
    uint32_t version    = le32_to_cpu(font_data + 4);
    uint32_t headersize = le32_to_cpu(font_data + 8);
    uint32_t flags      = le32_to_cpu(font_data + 12);
    uint32_t length     = le32_to_cpu(font_data + 16);
    uint32_t charsize   = le32_to_cpu(font_data + 20);
    uint32_t height     = le32_to_cpu(font_data + 24);
    uint32_t width      = le32_to_cpu(font_data + 28);

    (void)version;
    (void)flags;

    if (magic != 0x864AB572)
        return -1;

    if (headersize < 32)
        return -1;

    if (width == 0 || height == 0 || width > 32 || height > 32)
        return -1;

    if (charsize == 0 || charsize > 128)
        return -1;

    font->width           = width;
    font->height          = height;
    font->glyph_count     = length;
    font->bytes_per_glyph = charsize;
    font->glyphs          = (uint8_t*)(font_data + headersize);

    return 0;
}

uint8_t* font_get_glyph(struct font* font, uint32_t codepoint)
{
    if (!font || !font->glyphs)
        return NULL;

    if (codepoint >= font->glyph_count)
        codepoint = '?';

    return font->glyphs + (codepoint * font->bytes_per_glyph);
}

uint32_t font_width(struct font* font)
{
    if (!font)
        return 8;
    return font->width;
}

uint32_t font_height(struct font* font)
{
    if (!font)
        return 16;
    return font->height;
}
