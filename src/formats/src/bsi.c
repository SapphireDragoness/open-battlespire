#include "bs/formats/bsi.h"

#include <string.h>

#include "bs/core/reader.h"
#include "bs/formats/palette.h"

#define MAX_PIXELS (64u << 20)

static bool tag_is(const uint8_t *tag, const char *s)
{
    return memcmp(tag, s, 4) == 0;
}

bs_result bs_bsi_decode_pixels(const uint8_t *data, size_t size, uint16_t encoding,
                               uint32_t width, uint32_t height, uint32_t frames, uint8_t *out)
{
    uint64_t total = (uint64_t)width * height * frames;
    if (total > MAX_PIXELS)
        return BS_ERR_UNSUPPORTED;

    if (encoding == BS_BSI_ENC_RAW) {
        if (size < total)
            return BS_ERR_FORMAT;
        memcpy(out, data, (size_t)total);
        return BS_OK;
    }
    if (encoding != BS_BSI_ENC_ROWS && encoding != BS_BSI_ENC_ROWS_RLE)
        return BS_ERR_UNSUPPORTED;

    /* One u32 offset per row of every frame, relative to the start of DATA. */
    uint64_t rows = (uint64_t)height * frames;
    if (rows * 4 > size)
        return BS_ERR_FORMAT;
    bs_reader table = bs_reader_make(data, (size_t)rows * 4);
    for (uint64_t row = 0; row < rows; row++) {
        uint32_t entry = bs_read_u32le(&table);
        uint32_t off = entry & ~BS_BSI_ROW_RLE;
        uint8_t *dst = out + row * width;
        if (off > size)
            return BS_ERR_FORMAT;

        if (!(entry & BS_BSI_ROW_RLE)) {
            if (width > size - off)
                return BS_ERR_FORMAT;
            memcpy(dst, data + off, width);
            continue;
        }
        /* RLE: c < 0x80 -> c literal bytes; c >= 0x80 -> next byte repeated (c & 0x7F) times. */
        size_t p = off;
        uint32_t x = 0;
        while (x < width) {
            if (p >= size)
                return BS_ERR_FORMAT;
            uint8_t c = data[p++];
            uint32_t n = c & 0x7Fu;
            if (n > width - x)
                return BS_ERR_FORMAT;
            if (c & 0x80) {
                if (p >= size)
                    return BS_ERR_FORMAT;
                memset(dst + x, data[p++], n);
            } else {
                if (n > size - p)
                    return BS_ERR_FORMAT;
                memcpy(dst + x, data + p, n);
                p += n;
            }
            x += n;
        }
    }
    return BS_OK;
}

/* Walks the chunks; with images == NULL only counts images (DATA chunks). */
static bs_result walk(const uint8_t *data, size_t size, bs_arena *arena, bs_bsi_image *images, size_t *count)
{
    bs_reader r = bs_reader_make(data, size);
    if (size >= 8 && tag_is(data, "BSIF"))
        bs_reader_skip(&r, 8); /* container tag + 4 bytes (not a reliable length) */

    size_t n = 0;
    bs_bsi_image *cur = NULL;
    bool have_bhdr = false, have_data = true; /* "have_data" so the first chunk starts an image */

    while (bs_reader_remaining(&r) >= 8) {
        const uint8_t *tag = bs_read_span(&r, 4);
        uint32_t len = bs_read_u32be(&r);
        if (tag_is(tag, "END "))
            break; /* some files have a garbage END length */
        const uint8_t *body = bs_read_span(&r, len);
        if (!body)
            return BS_ERR_FORMAT;

        /* A new image starts after DATA, or at a second BHDR/IFHD. */
        bool starts = have_data || ((tag_is(tag, "BHDR") || tag_is(tag, "IFHD")) && have_bhdr);
        if (starts) {
            if (!have_data)
                return BS_ERR_FORMAT; /* previous image had no DATA */
            if (images) {
                cur = &images[n];
                memset(cur, 0, sizeof *cur);
            }
            have_bhdr = have_data = false;
        }

        if (tag_is(tag, "BHDR")) {
            if (len != 26)
                return BS_ERR_FORMAT;
            have_bhdr = true;
            if (cur) {
                bs_reader h = bs_reader_make(body, len);
                for (int i = 0; i < 13; i++)
                    cur->bhdr[i] = bs_read_i16le(&h);
                cur->x = cur->bhdr[0];
                cur->y = cur->bhdr[1];
                if (cur->bhdr[2] < 0 || cur->bhdr[3] < 0 || cur->bhdr[7] < 0)
                    return BS_ERR_FORMAT;
                cur->width = (uint16_t)cur->bhdr[2];
                cur->height = (uint16_t)cur->bhdr[3];
                cur->frames = (uint16_t)cur->bhdr[7];
                cur->encoding = (uint16_t)cur->bhdr[12];
                cur->transparent = cur->bhdr[4] == 3;
            }
        } else if (tag_is(tag, "DATA")) {
            if (!have_bhdr)
                return BS_ERR_FORMAT;
            have_data = true;
            if (cur) {
                size_t total = (size_t)cur->width * cur->height * cur->frames;
                if (total > MAX_PIXELS)
                    return BS_ERR_UNSUPPORTED;
                cur->pixels = bs_arena_alloc(arena, total ? total : 1, 16);
                if (!cur->pixels)
                    return BS_ERR_NOMEM;
                bs_result res = bs_bsi_decode_pixels(body, len, cur->encoding, cur->width, cur->height,
                                                     cur->frames, cur->pixels);
                if (res != BS_OK)
                    return res;
            }
            n++;
        } else if (cur && tag_is(tag, "NAME")) {
            size_t k = 0;
            while (k < len && k + 1 < sizeof cur->name && body[k]) {
                cur->name[k] = (char)body[k];
                k++;
            }
            cur->name[k] = '\0';
        } else if (cur && tag_is(tag, "HICL")) {
            if (len != 256)
                return BS_ERR_FORMAT;
            bs_reader h = bs_reader_make(body, len);
            for (int i = 0; i < 128; i++)
                cur->hicl[i] = bs_read_u16le(&h);
            cur->has_hicl = true;
        } else if (cur && tag_is(tag, "HTBL")) {
            if (len != 8192)
                return BS_ERR_FORMAT;
            cur->htbl = body;
        } else if (cur && tag_is(tag, "CMAP")) {
            if (len != 768)
                return BS_ERR_FORMAT;
            memcpy(cur->cmap, body, 768);
            cur->has_cmap = true;
        }
        /* IFHD and unknown chunks are skipped. */
    }
    if (!have_data)
        return BS_ERR_FORMAT; /* trailing image without DATA */
    *count = n;
    return BS_OK;
}

bs_result bs_bsi_parse(const uint8_t *data, size_t size, bs_arena *arena, bs_bsi *out)
{
    memset(out, 0, sizeof *out);
    size_t count;
    bs_result r = walk(data, size, arena, NULL, &count);
    if (r != BS_OK)
        return r;
    bs_bsi_image *images = BS_ARENA_ARRAY(arena, bs_bsi_image, count ? count : 1);
    if (!images)
        return BS_ERR_NOMEM;
    size_t filled;
    r = walk(data, size, arena, images, &filled);
    if (r != BS_OK)
        return r;
    out->images = images;
    out->count = filled;
    return BS_OK;
}

void bs_bsi_pixel_rgb(const bs_bsi_image *img, uint8_t pixel, uint8_t rgb[3])
{
    if (!img->has_hicl) {
        rgb[0] = rgb[1] = rgb[2] = 0;
        return;
    }
    bs_hicl_to_rgb(img->hicl[pixel >> 1], rgb);
}
