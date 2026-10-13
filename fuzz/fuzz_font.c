/* Fuzzes font parsing and glyph access. */
#include "bs/formats/font.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_font font;
    if (bs_font_parse(data, size, &a, &font) == BS_OK) {
        volatile uint16_t sink = 0;
        for (unsigned ch = 0; ch < 256; ch++) {
            const bs_font_glyph *g = bs_font_glyph_for(&font, (uint8_t)ch);
            if (g && g->width && g->height)
                sink ^= bs_font_pixel(g, g->width - 1u, g->height - 1u);
        }
        (void)sink;
    }
    bs_arena_free(&a);
    return 0;
}
