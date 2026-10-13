#ifndef BS_FORMATS_FONT_H
#define BS_FORMATS_FONT_H

#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"

/*
 * Game fonts GAMEDATA/FONT0001-0010.FNT. See docs/formats/fonts.md.
 *
 *   u32 count                         241 in all shipped fonts
 *   count x { u16 width; u16 height; }
 *   count x u32 offset                absolute file offset of the glyph's pixels
 *   pixels                            width * height little-endian u16 RGB555; 0 = transparent
 *
 * Glyph i is character 33 + i in DOS code page 437 order ('!' first, then
 * 'Ç' at 128, ...). Glyphs whose offset equals the file size are absent;
 * shipped fonts define 134 glyphs (characters 33-166). Glyphs may share pixels.
 */

#define BS_FONT_FIRST_CHAR 33

typedef struct bs_font_glyph {
    uint16_t width, height;
    const uint8_t *pixels;  /* width * height u16 LE RGB555 in the parsed buffer; NULL if absent */
} bs_font_glyph;

typedef struct bs_font {
    uint32_t count;
    bs_font_glyph *glyphs;
    uint16_t max_width, max_height;  /* over present glyphs */
} bs_font;

/* Glyphs point into data, which must outlive the font. */
bs_result bs_font_parse(const uint8_t *data, size_t size, bs_arena *arena, bs_font *out);

/* Glyph for a code page 437 byte, or NULL if the font has none (e.g. space). */
const bs_font_glyph *bs_font_glyph_for(const bs_font *font, uint8_t ch);

static inline uint16_t bs_font_pixel(const bs_font_glyph *g, uint32_t x, uint32_t y)
{
    const uint8_t *p = g->pixels + ((size_t)y * g->width + x) * 2;
    return (uint16_t)(p[0] | (p[1] << 8));
}

#endif
