#ifndef BS_FORMATS_BOOK_H
#define BS_FORMATS_BOOK_H

#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"

/*
 * Books (TXT.BSA bk?_NNN.txt, e.g. bk1_993.txt) laid out in pages as the
 * book screen does (GAME.EXE 0x3C89B, lines 0x3CAFE): text from x 80
 * wrapping before x 560, so lines are at most 480 pixels; one line break
 * (CR LF) is a space, two end a paragraph with an empty line, '^' or '$'
 * end a line; pages hold the lines from y 53 while y < 410 - line height.
 * See docs/formats/txt_bsa.md, "Books".
 */

#define BS_BOOK_WIDTH 480     /* 560 - 80 */
#define BS_BOOK_TOP 53
#define BS_BOOK_BOTTOM 410

typedef struct bs_book {
    char **lines;             /* Windows-1252, '%' codes not filled in; a CR is shown as a space */
    uint32_t line_count;
    uint32_t *page_first;     /* the first line of each page */
    uint32_t page_count;
} bs_book;

/* advance(ctx, c): how far a character moves the pen (the font's, 0x13A65). */
typedef int (*bs_book_advance)(void *ctx, uint8_t c);

bs_result bs_book_layout(const uint8_t *text, size_t size, bs_book_advance advance, void *ctx, int line_height,
                         bs_arena *arena, bs_book *out);

/* The lines of a page: returns the count and sets *first. */
uint32_t bs_book_page(const bs_book *b, uint32_t page, uint32_t *first);

/*
 * A line ready to show (0x3CA1B, codes 0x3CEDF): CRs become spaces, other
 * control bytes go, and the codes %PCF (the character's first name), %PCN
 * (full name), %APF and %APS (the companions: Josian and Kaid for a female
 * character, Vatasha and Trenelle for a male one) are filled in.
 */
void bs_book_line(const char *line, const char *name, const char *companion_first, const char *companion_second,
                  char *out, size_t cap);

#endif
