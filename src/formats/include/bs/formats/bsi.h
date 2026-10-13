#ifndef BS_FORMATS_BSI_H
#define BS_FORMATS_BSI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"

/*
 * BSI images (BSI.BSA): textures, sprites, UI screens. See docs/formats/bsi.md.
 *
 * A file is a sequence of chunks, each a 4-character tag and a big-endian
 * u32 length:
 *
 *   [BSIF + 4 bytes]               optional container header (multi-image files)
 *   IFHD(44) [NAME] BHDR(26) [HICL(256)] [HTBL(8192)] CMAP(768) DATA   one image
 *   ... more images ...
 *   END
 *
 * Pixels are bytes whose values are always even: colour = HICL[pixel / 2],
 * where HICL is 128 colours stored as RGB555 shifted left by one bit.
 */

/* BHDR encodings (field 12). */
#define BS_BSI_ENC_RAW 0       /* width * height * frames bytes */
#define BS_BSI_ENC_ROWS 4      /* u32 row offset table, raw rows (identical rows shared) */
#define BS_BSI_ENC_ROWS_RLE 6  /* u32 row offset table; bit 31 = RLE row, else raw */

#define BS_BSI_ROW_RLE 0x80000000u

typedef struct bs_bsi_image {
    char name[32];          /* NAME chunk; "" if absent */
    int16_t bhdr[13];       /* raw BHDR fields, for the ones not yet understood */
    int16_t x, y;           /* BHDR 0, 1: placement offset (UI position for screen parts) */
    uint16_t width, height; /* BHDR 2, 3 */
    uint16_t frames;        /* BHDR 7 */
    uint16_t encoding;      /* BHDR 12 */
    bool transparent;       /* BHDR 4 == 3; inferred: sprites and cut-outs use pixel 0 as transparent */

    bool has_hicl;
    uint16_t hicl[128];     /* colour table: RGB555 << 1, see bs_hicl_to_rgb() */
    const uint8_t *htbl;    /* 8192-byte shade table, or NULL: 32 levels x 128 RGB555 colours,
                               level 31 = HICL (shade level L of pixel p: entry L * 128 + p / 2) */
    bool has_cmap;
    uint8_t cmap[768];      /* 6-bit VGA palette; does not match the pixels, likely an art-tool leftover */

    uint8_t *pixels;        /* width * height * frames, frame after frame */
} bs_bsi_image;

typedef struct bs_bsi {
    bs_bsi_image *images;
    size_t count;
} bs_bsi;

/* Parses and decodes all images. Pixels are allocated from arena. */
bs_result bs_bsi_parse(const uint8_t *data, size_t size, bs_arena *arena, bs_bsi *out);

/* RGB for a pixel value via the image's HICL table (black if the image has none). */
void bs_bsi_pixel_rgb(const bs_bsi_image *img, uint8_t pixel, uint8_t rgb[3]);

/*
 * Decodes BSI pixel data of the given encoding into out (width * height *
 * frames bytes). Exposed for tests and fuzzing.
 */
bs_result bs_bsi_decode_pixels(const uint8_t *data, size_t size, uint16_t encoding,
                               uint32_t width, uint32_t height, uint32_t frames, uint8_t *out);

#endif
