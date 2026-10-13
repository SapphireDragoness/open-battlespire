#include "bs/formats/palette.h"

#include <string.h>

#include "bs/core/reader.h"

void bs_hicl_to_rgb(uint16_t c, uint8_t rgb[3])
{
    bs_rgb555_to_rgb((uint16_t)(c >> 1), rgb);
}

void bs_rgb555_to_rgb(uint16_t c, uint8_t rgb[3])
{
    rgb[0] = (uint8_t)(((c >> 10) & 31) * 255 / 31);
    rgb[1] = (uint8_t)(((c >> 5) & 31) * 255 / 31);
    rgb[2] = (uint8_t)((c & 31) * 255 / 31);
}

bs_result bs_palette_from_vga6(const uint8_t *data, size_t size, bs_palette *out)
{
    if (size < 768)
        return BS_ERR_FORMAT;
    for (int i = 0; i < 256; i++) {
        for (int k = 0; k < 3; k++)
            out->rgb[i][k] = bs_vga6_to_8(data[i * 3 + k]);
    }
    return BS_OK;
}

bs_result bs_palette_from_col(const uint8_t *data, size_t size, bs_palette *out)
{
    bs_reader r = bs_reader_make(data, size);
    uint32_t file_size = bs_read_u32le(&r);
    bs_read_u32le(&r);
    const uint8_t *rgb = bs_read_span(&r, 768);
    if (!rgb || file_size != size)
        return BS_ERR_FORMAT;
    memcpy(out->rgb, rgb, 768);
    return BS_OK;
}
