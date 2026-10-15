/* Fuzzes the book layout and line filling. */
#include <string.h>

#include "bs/formats/book.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static int advance(void *ctx, uint8_t c)
{
    (void)ctx;
    return 3 + c % 9;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_book b;
    if (bs_book_layout(data, size, advance, NULL, 16, &a, &b) == BS_OK) {
        for (uint32_t i = 0; i < b.line_count; i++) {
            char line[64];
            bs_book_line(b.lines[i], "Ayla Stone", "Josian", "Kaid", line, sizeof line);
            if (!memchr(line, 0, sizeof line))
                __builtin_trap();
        }
        uint32_t total = 0, first;
        for (uint32_t p = 0; p < b.page_count; p++)
            total += bs_book_page(&b, p, &first);
        if (total != b.line_count)
            __builtin_trap();
    }
    bs_arena_free(&a);
    return 0;
}
