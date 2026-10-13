#include "bs/core/text.h"

/* Windows-1252 0x80-0x9F; 0 marks bytes undefined in the code page. */
static const uint16_t cp1252_high[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
    0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178,
};

uint32_t bs_cp1252_to_unicode(uint8_t c)
{
    if (c >= 0x80 && c <= 0x9F && cp1252_high[c - 0x80])
        return cp1252_high[c - 0x80];
    return c; /* ASCII, Latin-1 (0xA0-0xFF), and undefined bytes as C1 controls */
}

size_t bs_utf8_encode(uint32_t cp, char out[4])
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

char *bs_cp1252_to_utf8(bs_arena *arena, const uint8_t *src, size_t n)
{
    /* Every Windows-1252 byte encodes to at most 3 UTF-8 bytes. */
    if (n > (SIZE_MAX - 1) / 3)
        return NULL;
    char *out = bs_arena_alloc(arena, n * 3 + 1, 1);
    if (!out)
        return NULL;
    size_t len = 0;
    for (size_t i = 0; i < n; i++)
        len += bs_utf8_encode(bs_cp1252_to_unicode(src[i]), out + len);
    out[len] = '\0';
    return out;
}
