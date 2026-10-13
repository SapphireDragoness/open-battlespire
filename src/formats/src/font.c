#include "bs/formats/font.h"

#include <string.h>

#include "bs/core/reader.h"

#define MAX_GLYPHS 4096u

bs_result bs_font_parse(const uint8_t *data, size_t size, bs_arena *arena, bs_font *out)
{
    memset(out, 0, sizeof *out);
    bs_reader r = bs_reader_make(data, size);
    uint32_t count = bs_read_u32le(&r);
    if (!bs_reader_ok(&r) || count == 0 || count > MAX_GLYPHS || (uint64_t)count * 8 > size - 4)
        return BS_ERR_FORMAT;

    bs_font_glyph *glyphs = BS_ARENA_ARRAY(arena, bs_font_glyph, count);
    if (!glyphs)
        return BS_ERR_NOMEM;
    for (uint32_t i = 0; i < count; i++) {
        glyphs[i].width = bs_read_u16le(&r);
        glyphs[i].height = bs_read_u16le(&r);
    }
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = bs_read_u32le(&r);
        uint64_t bytes = (uint64_t)glyphs[i].width * glyphs[i].height * 2;
        if (!bs_reader_ok(&r))
            return BS_ERR_FORMAT;
        if (off == size)
            continue; /* absent glyph */
        if (off > size || bytes > size - off)
            return BS_ERR_FORMAT;
        glyphs[i].pixels = data + off;
        if (glyphs[i].width > out->max_width)
            out->max_width = glyphs[i].width;
        if (glyphs[i].height > out->max_height)
            out->max_height = glyphs[i].height;
    }
    out->count = count;
    out->glyphs = glyphs;
    return BS_OK;
}

const bs_font_glyph *bs_font_glyph_for(const bs_font *font, uint8_t ch)
{
    if (ch < BS_FONT_FIRST_CHAR || (uint32_t)(ch - BS_FONT_FIRST_CHAR) >= font->count)
        return NULL;
    const bs_font_glyph *g = &font->glyphs[ch - BS_FONT_FIRST_CHAR];
    return g->pixels ? g : NULL;
}
