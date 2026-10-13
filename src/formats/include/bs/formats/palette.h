#ifndef BS_FORMATS_PALETTE_H
#define BS_FORMATS_PALETTE_H

#include <stddef.h>
#include <stdint.h>

#include "bs/core/result.h"

/*
 * Palettes and colour conversion. See docs/formats/palettes.md.
 *
 * Battlespire draws on a 15-bit RGB555 screen. Images carry their own
 * 128-colour table (BSI HICL chunk) whose entries are RGB555 shifted left by
 * one bit; fonts and the colour lookup tables (TINT.555, LAYER.DAT) use plain
 * RGB555. The 256-colour VGA palettes (PAL.RAW, 6 bits per channel) and .COL
 * files (8-byte header + 8-bit RGB) are used elsewhere.
 */

typedef struct bs_palette {
    uint8_t rgb[256][3];  /* 8 bits per channel */
} bs_palette;

/* Expands 0..63 to 0..255. */
static inline uint8_t bs_vga6_to_8(uint8_t v)
{
    return (uint8_t)((v & 63) * 255 / 63);
}

/*
 * BSI HICL colours (and model face colours): RGB555 in bits 15..1, bit 0
 * unused. Verified pixel for pixel against DOSBox screenshots of the HUD.
 * (Read as RGB565, red and green come out nearly right but blue doubles.)
 */
void bs_hicl_to_rgb(uint16_t c, uint8_t rgb[3]);

/* RGB555 -> 8-bit RGB (fonts, colour lookup tables). Bit 15 is ignored. */
void bs_rgb555_to_rgb(uint16_t c, uint8_t rgb[3]);

/* 768 bytes of 6-bit VGA RGB (PAL.RAW, BSI CMAP). */
bs_result bs_palette_from_vga6(const uint8_t *data, size_t size, bs_palette *out);

/* Daggerfall-style .COL: u32 file size, u32 version, then 768 bytes of 8-bit RGB. */
bs_result bs_palette_from_col(const uint8_t *data, size_t size, bs_palette *out);

#endif
