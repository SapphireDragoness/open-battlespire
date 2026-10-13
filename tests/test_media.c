/* FLIC and font parsing on synthetic data. */

#include <string.h>

#include "bs/formats/flic.h"
#include "bs/formats/font.h"
#include "bs_test.h"
#include "synth.h"

static void test_flic(void)
{
    uint8_t buf[1024];
    size_t n = synth_flc(buf, sizeof buf);
    BS_CHECK(n > 0);
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_flic f;
    bs_flic_player p;
    BS_CHECK_EQ_INT(bs_flic_open(buf, n, &f), BS_OK);
    BS_CHECK(f.width == 4 && f.height == 2 && f.frames == 2 && f.speed == 70);
    BS_CHECK_EQ_INT(bs_flic_player_init(&p, &f, &a), BS_OK);

    bool ended;
    BS_CHECK_EQ_INT(bs_flic_next_frame(&p, &ended), BS_OK);
    BS_CHECK(!ended && memcmp(p.pixels, "\x01\x01\x01\x01\x00\x01\x00\x01", 8) == 0);
    BS_CHECK(p.palette[3] == 255 && p.palette[4] == 0 && p.palette[7] == 255);
    BS_CHECK_EQ_INT(bs_flic_next_frame(&p, &ended), BS_OK);
    BS_CHECK(!ended && memcmp(p.pixels, "\x01\x01\x01\x01\x00\x01\x02\x02", 8) == 0);
    BS_CHECK_EQ_INT(bs_flic_next_frame(&p, &ended), BS_OK); /* ring frame */
    BS_CHECK(!ended && memcmp(p.pixels, "\x01\x01\x01\x01\x00\x01\x00\x01", 8) == 0);
    BS_CHECK_EQ_INT(bs_flic_next_frame(&p, &ended), BS_OK);
    BS_CHECK(ended && p.frame == 3);

    /* Bad magic; run past the line end in BYTE_RUN. */
    uint8_t bad[1024];
    memcpy(bad, buf, n);
    bad[4] = 0x11;
    bad[5] = 0x11;
    BS_CHECK_EQ_INT(bs_flic_open(bad, n, &f), BS_ERR_FORMAT);
    memcpy(bad, buf, n);
    uint8_t *run = memchr(bad + 128, 0x04, n - 128);
    while (run && !(run[-1] == 0x01 && run[1] == 0x01)) /* find "\x01\x04\x01" */
        run = memchr(run + 1, 0x04, (size_t)(bad + n - run - 1));
    BS_CHECK(run != NULL);
    if (run) {
        *run = 0x05;
        BS_CHECK_EQ_INT(bs_flic_open(bad, n, &f), BS_OK);
        BS_CHECK_EQ_INT(bs_flic_player_init(&p, &f, &a), BS_OK);
        BS_CHECK_EQ_INT(bs_flic_next_frame(&p, &ended), BS_ERR_FORMAT);
    }
    bs_arena_free(&a);
}

static void test_font(void)
{
    uint8_t buf[256];
    size_t n = synth_font(buf, sizeof buf);
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_font font;
    BS_CHECK_EQ_INT(bs_font_parse(buf, n, &a, &font), BS_OK);
    BS_CHECK(font.count == 3 && font.max_width == 2 && font.max_height == 1);
    const bs_font_glyph *g = bs_font_glyph_for(&font, '!');
    BS_CHECK(g && g->width == 2 && bs_font_pixel(g, 0, 0) == 0x7FFF && bs_font_pixel(g, 1, 0) == 0);
    const bs_font_glyph *q = bs_font_glyph_for(&font, '"');
    BS_CHECK(q && g && q->pixels == g->pixels);
    BS_CHECK(bs_font_glyph_for(&font, '#') == NULL); /* absent */
    BS_CHECK(bs_font_glyph_for(&font, ' ') == NULL); /* below the first glyph */
    BS_CHECK(bs_font_glyph_for(&font, '$') == NULL); /* past the end */

    uint8_t bad[256];
    memcpy(bad, buf, n);
    bad[4] = 50; /* '!' 50 pixels wide: overruns the file */
    BS_CHECK_EQ_INT(bs_font_parse(bad, n, &a, &font), BS_ERR_FORMAT);
    BS_CHECK_EQ_INT(bs_font_parse(buf, 3, &a, &font), BS_ERR_FORMAT);
    bs_arena_free(&a);
}

int main(void)
{
    BS_RUN(test_flic);
    BS_RUN(test_font);
    return BS_TEST_RESULT();
}
