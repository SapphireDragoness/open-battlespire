#include <string.h>

#include "bs/formats/bsi.h"
#include "bs/formats/palette.h"
#include "bs_test.h"
#include "synth.h"

static void test_single(void)
{
    uint8_t buf[2048];
    size_t n = synth_bsi_single(buf, sizeof buf);
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_bsi bsi;
    BS_CHECK_EQ_INT(bs_bsi_parse(buf, n, &a, &bsi), BS_OK);
    BS_CHECK_EQ_INT(bsi.count, 1);
    if (bsi.count == 1) {
        const bs_bsi_image *img = &bsi.images[0];
        BS_CHECK(strcmp(img->name, "ONE") == 0);
        BS_CHECK(img->x == 5 && img->y == 7 && img->width == 2 && img->height == 2 && img->frames == 1);
        BS_CHECK(img->transparent && img->has_hicl && img->has_cmap && img->htbl == NULL);
        BS_CHECK(memcmp(img->pixels, "\x00\x02\x02\x00", 4) == 0);
        uint8_t rgb[3];
        bs_bsi_pixel_rgb(img, 2, rgb); /* HICL[1] = 0xF800 */
        BS_CHECK(rgb[0] == 255 && rgb[1] == 0 && rgb[2] == 0);
        bs_bsi_pixel_rgb(img, 3, rgb); /* odd values share the entry */
        BS_CHECK(rgb[0] == 255);
    }
    bs_arena_free(&a);
}

static void test_container(void)
{
    uint8_t buf[4096];
    size_t n = synth_bsi_container(buf, sizeof buf);
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_bsi bsi;
    BS_CHECK_EQ_INT(bs_bsi_parse(buf, n, &a, &bsi), BS_OK);
    BS_CHECK_EQ_INT(bsi.count, 2);
    if (bsi.count == 2) {
        BS_CHECK(bsi.images[0].pixels[0] == 4 && !bsi.images[0].transparent);
        const bs_bsi_image *img = &bsi.images[1];
        BS_CHECK(img->width == 4 && img->height == 2 && img->frames == 2 && img->encoding == 6);
        static const uint8_t want[16] = { 2, 2, 2, 2, 0, 2, 4, 6, 8, 10, 0, 0, 0, 2, 4, 6 };
        BS_CHECK(memcmp(img->pixels, want, 16) == 0);
    }
    bs_arena_free(&a);
}

static void test_empty_and_malformed(void)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_bsi bsi;

    /* Only an END chunk (BIP.BSI): valid, no images. */
    static const uint8_t end_only[8] = { 'E', 'N', 'D', ' ', 0, 0, 0, 0 };
    BS_CHECK_EQ_INT(bs_bsi_parse(end_only, 8, &a, &bsi), BS_OK);
    BS_CHECK_EQ_INT(bsi.count, 0);

    uint8_t buf[4096], bad[4096];
    size_t n = synth_bsi_container(buf, sizeof buf);

    /* Truncated before the second image's DATA. */
    BS_CHECK_EQ_INT(bs_bsi_parse(buf, n - 40, &a, &bsi), BS_ERR_FORMAT);

    /* Run longer than the row: change the "\x84" run (4) to "\x85" (5) in a 4-wide row. */
    memcpy(bad, buf, n);
    uint8_t *run = memchr(bad + n - 24, 0x84, 16);
    BS_CHECK(run != NULL);
    if (run) {
        *run = 0x85;
        BS_CHECK_EQ_INT(bs_bsi_parse(bad, n, &a, &bsi), BS_ERR_FORMAT);
    }

    /* Unknown encoding. */
    uint8_t px[4];
    BS_CHECK_EQ_INT(bs_bsi_decode_pixels(buf, 4, 5, 2, 2, 1, px), BS_ERR_UNSUPPORTED);
    /* Row offset table larger than the data. */
    BS_CHECK_EQ_INT(bs_bsi_decode_pixels(buf, 4, 6, 2, 2, 1, px), BS_ERR_FORMAT);
    bs_arena_free(&a);
}

static void test_palettes(void)
{
    uint8_t rgb[3];
    bs_hicl_to_rgb(0xFFFF, rgb);
    BS_CHECK(rgb[0] == 255 && rgb[1] == 255 && rgb[2] == 255);
    bs_hicl_to_rgb(0x07C0, rgb); /* RGB555 0x03E0: green */
    BS_CHECK(rgb[0] == 0 && rgb[1] == 255 && rgb[2] == 0);
    bs_hicl_to_rgb(0x003E, rgb); /* RGB555 0x001F: blue; bit 0 is ignored */
    BS_CHECK(rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 255);
    /* A real HUD colour checked against a DOSBox screenshot: 0x31CE -> (48, 56, 56) on screen. */
    bs_hicl_to_rgb(0x31CE, rgb);
    BS_CHECK(rgb[0] / 8 == 6 && rgb[1] / 8 == 7 && rgb[2] / 8 == 7);
    BS_CHECK_EQ_INT(bs_vga6_to_8(63), 255);
    BS_CHECK_EQ_INT(bs_vga6_to_8(0), 0);

    uint8_t vga[768];
    memset(vga, 63, sizeof vga);
    bs_palette p;
    BS_CHECK_EQ_INT(bs_palette_from_vga6(vga, sizeof vga, &p), BS_OK);
    BS_CHECK(p.rgb[255][2] == 255);
    BS_CHECK_EQ_INT(bs_palette_from_vga6(vga, 100, &p), BS_ERR_FORMAT);

    uint8_t col[776] = { 0x08, 0x03, 0, 0, 0x23, 0xB1, 0, 0 };
    col[8 + 3] = 200;
    BS_CHECK_EQ_INT(bs_palette_from_col(col, sizeof col, &p), BS_OK);
    BS_CHECK(p.rgb[1][0] == 200);
    BS_CHECK_EQ_INT(bs_palette_from_col(col, 775, &p), BS_ERR_FORMAT);
}

int main(void)
{
    BS_RUN(test_single);
    BS_RUN(test_container);
    BS_RUN(test_empty_and_malformed);
    BS_RUN(test_palettes);
    return BS_TEST_RESULT();
}
