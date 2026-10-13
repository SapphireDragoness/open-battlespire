#ifndef BS_CORE_PNG_H
#define BS_CORE_PNG_H

#include <stdint.h>

#include "bs/core/result.h"

/*
 * Minimal PNG writer for exported game graphics and screenshots. Uses
 * uncompressed deflate blocks, so it needs no zlib.
 */

/*
 * 8-bit indexed image, keeping the original pixel values (which modders
 * want). palette: 256 RGB triples. transparent_index: palette entry with
 * alpha 0, or -1 for none.
 */
bs_result bs_png_write_indexed(const char *path, uint32_t width, uint32_t height, const uint8_t *pixels,
                               const uint8_t palette[768], int transparent_index);

/* 8-bit RGB image, rows top to bottom, 3 bytes per pixel. */
bs_result bs_png_write_rgb(const char *path, uint32_t width, uint32_t height, const uint8_t *pixels);

#endif
