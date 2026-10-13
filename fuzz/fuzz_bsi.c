/* Fuzzes BSI chunk parsing and pixel decoding (all encodings). */
#include "bs/formats/bsi.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_bsi bsi;
    if (bs_bsi_parse(data, size, &a, &bsi) == BS_OK) {
        volatile uint8_t sink = 0;
        for (size_t i = 0; i < bsi.count; i++) {
            const bs_bsi_image *img = &bsi.images[i];
            size_t n = (size_t)img->width * img->height * img->frames;
            if (n)
                sink ^= img->pixels[0] ^ img->pixels[n - 1];
            uint8_t rgb[3];
            bs_bsi_pixel_rgb(img, 255, rgb);
        }
        (void)sink;
    }
    bs_arena_free(&a);
    return 0;
}
