#include "bs/formats/book.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define END 0xFF

/* One line from p (0x3CAFE): cuts it in the buffer and returns where the next starts. */
static uint8_t *next_line(uint8_t *p, bs_book_advance advance, void *ctx)
{
    if (*p == '\r' || *p == '$') {
        *p = 0;
        return p[1] == END ? p + 1 : p + 2;
    }
    int width = 0;
    uint8_t *space = NULL, *start = p;
    while (*p != END && width < BS_BOOK_WIDTH) {
        if (*p == '\r' && p - start >= 2 && p[-2] == '\r') {
            p[-2] = 0; /* the paragraph's line ends at the first break; the second starts an empty line */
            return p;
        }
        if (*p == '^' || *p == '$') {
            *p = 0;
            return p + 1;
        }
        if (*p == ' ')
            space = p;
        if (*p >= 0x20)
            width += advance(ctx, *p);
        p++;
    }
    if (*p == END)
        return p;
    if (*p == ' ' || !space)
        space = p;
    *space = 0;
    return space + 1;
}

bs_result bs_book_layout(const uint8_t *text, size_t size, bs_book_advance advance, void *ctx, int line_height,
                         bs_arena *arena, bs_book *out)
{
    memset(out, 0, sizeof *out);
    if (line_height <= 0)
        return BS_ERR_ARGS;
    /* A working copy ending in 0xFF, as the game's. */
    uint8_t *buf = bs_arena_alloc(arena, size + 2, 1);
    if (!buf)
        return BS_ERR_NOMEM;
    memcpy(buf, text, size);
    for (size_t i = 0; i < size; i++)
        if (buf[i] == END)
            buf[i] = ' ';
    buf[size] = END;
    buf[size + 1] = 0;
    /* At most a line per byte, a page per line. */
    out->lines = BS_ARENA_ARRAY(arena, char *, size + 2);
    out->page_first = BS_ARENA_ARRAY(arena, uint32_t, size + 2);
    if (!out->lines || !out->page_first)
        return BS_ERR_NOMEM;
    uint8_t *p = buf;
    while (*p != END) {
        out->page_first[out->page_count++] = out->line_count;
        for (int y = BS_BOOK_TOP; *p != END && BS_BOOK_BOTTOM - line_height > y; y += line_height) {
            out->lines[out->line_count++] = (char *)p;
            uint8_t *next = next_line(p, advance, ctx);
            if (next == p) {
                /* No progress: a line that can't be cut. Take one byte. */
                next = p + 1;
            }
            p = next;
        }
    }
    /* Each line ends at its 0 or at the 0xFF: make the 0xFF a terminator now. */
    buf[size] = 0;
    return BS_OK;
}

uint32_t bs_book_page(const bs_book *b, uint32_t page, uint32_t *first)
{
    if (page >= b->page_count) {
        *first = 0;
        return 0;
    }
    *first = b->page_first[page];
    uint32_t end = page + 1 < b->page_count ? b->page_first[page + 1] : b->line_count;
    return end - *first;
}

void bs_book_line(const char *line, const char *name, const char *companion_first, const char *companion_second,
                  char *out, size_t cap)
{
    size_t n = 0;
    if (!cap)
        return;
    for (const uint8_t *p = (const uint8_t *)line; *p && *p != END && n + 1 < cap; p++) {
        if (*p == '%' && p[1] && p[2] && p[3]) {
            const char *with = NULL;
            char first[64];
            if (strncmp((const char *)p + 1, "PCF", 3) == 0) {
                size_t k = 0;
                while (name[k] && name[k] != ' ' && k + 1 < sizeof first) {
                    first[k] = name[k];
                    k++;
                }
                first[k] = 0;
                with = first;
            } else if (strncmp((const char *)p + 1, "PCN", 3) == 0) {
                with = name;
            } else if (strncmp((const char *)p + 1, "APF", 3) == 0) {
                with = companion_first;
            } else if (strncmp((const char *)p + 1, "APS", 3) == 0) {
                with = companion_second;
            }
            if (with) {
                while (*with && n + 1 < cap)
                    out[n++] = *with++;
                p += 3;
                continue;
            }
        }
        if (*p == '\r')
            out[n++] = ' ';
        else if (*p >= 0x20)
            out[n++] = (char)*p;
    }
    out[n] = 0;
}
