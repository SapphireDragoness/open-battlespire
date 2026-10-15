/* Book layout: wrapping, paragraphs, forced breaks, pages, codes. */

#include <string.h>

#include "bs/formats/book.h"
#include "bs_test.h"

/* Every character 10 pixels: 48 to a line. */
static int advance10(void *ctx, uint8_t c)
{
    (void)ctx;
    (void)c;
    return 10;
}

static void line_is(const bs_book *b, uint32_t i, const char *want)
{
    char got[256];
    bs_book_line(b->lines[i], "Ayla Stone", "Josian", "Kaid", got, sizeof got);
    if (strcmp(got, want) != 0)
        fprintf(stderr, "  line %u: '%s', want '%s'\n", i, got, want);
    BS_CHECK(strcmp(got, want) == 0);
}

static void test_layout(void)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    static const char text[] =
        "[[A heading]]\r\n\r\n"
        "one two three four five six seven eight nine ten eleven twelve\r\n"
        "and on.^Forced$Too\r\n\r\n"
        "Hello %PCF, I am %APF; %APS too. %PCN.";
    bs_book b;
    BS_CHECK_EQ_INT(bs_book_layout((const uint8_t *)text, sizeof text - 1, advance10, NULL, 16, &a, &b), BS_OK);
    line_is(&b, 0, "[[A heading]]");
    line_is(&b, 1, "");
    /* 48 characters fit (480 pixels): the line breaks at the space after them. */
    line_is(&b, 2, "one two three four five six seven eight nine ten");
    /* The single line break is a space (CR shown as one, LF dropped). */
    line_is(&b, 3, "eleven twelve and on.");
    line_is(&b, 4, "Forced");
    line_is(&b, 5, "Too");
    line_is(&b, 6, "");
    line_is(&b, 7, "Hello Ayla, I am Josian; Kaid too. Ayla Stone.");
    BS_CHECK_EQ_INT(b.line_count, 8);
    /* 16-pixel lines from 53 while y < 394: 22 to a page. */
    BS_CHECK_EQ_INT(b.page_count, 1);
    uint32_t first;
    BS_CHECK_EQ_INT(bs_book_page(&b, 0, &first), 8);
    BS_CHECK_EQ_INT(bs_book_page(&b, 1, &first), 0);
    bs_arena_free(&a);
}

static void test_pages(void)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    char text[2000];
    size_t n = 0;
    for (int i = 0; i < 50; i++)
        n += (size_t)snprintf(text + n, sizeof text - n, "Line %d^", i);
    bs_book b;
    BS_CHECK_EQ_INT(bs_book_layout((const uint8_t *)text, n, advance10, NULL, 16, &a, &b), BS_OK);
    BS_CHECK_EQ_INT(b.line_count, 50);
    BS_CHECK_EQ_INT(b.page_count, 3); /* 22 + 22 + 6 */
    uint32_t first;
    BS_CHECK_EQ_INT(bs_book_page(&b, 2, &first), 6);
    BS_CHECK_EQ_INT(first, 44);
    /* A word longer than a line is cut. */
    char longword[100];
    memset(longword, 'x', 60);
    BS_CHECK_EQ_INT(bs_book_layout((const uint8_t *)longword, 60, advance10, NULL, 16, &a, &b), BS_OK);
    BS_CHECK_EQ_INT(b.line_count, 2);
    BS_CHECK_EQ_INT(strlen(b.lines[0]), 48);
    BS_CHECK_EQ_INT(bs_book_layout((const uint8_t *)"", 0, advance10, NULL, 16, &a, &b), BS_OK);
    BS_CHECK_EQ_INT(b.page_count, 0);
    bs_arena_free(&a);
}

int main(void)
{
    BS_RUN(test_layout);
    BS_RUN(test_pages);
    return BS_TEST_RESULT();
}
