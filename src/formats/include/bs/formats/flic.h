#ifndef BS_FORMATS_FLIC_H
#define BS_FORMATS_FLIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"

/*
 * Autodesk FLIC animations (FLC.BSA: the conversation talking heads, all
 * 640x480, 8-bit). See docs/formats/flic.md.
 *
 * Supported frame sub-chunks: COLOR_256 (4), COLOR_64 (11), DELTA_FLI (12),
 * BLACK (13), BYTE_RUN (15), COPY (16), DELTA_FLC (7); PSTAMP (18) and
 * unknown chunks are skipped. Battlespire's files use 4, 7, 15 and 18.
 */

#define BS_FLIC_MAGIC_FLI 0xAF11
#define BS_FLIC_MAGIC_FLC 0xAF12

typedef struct bs_flic {
    const uint8_t *data;   /* whole file; must outlive the flic */
    size_t size;
    uint16_t magic;
    uint16_t width, height;
    uint16_t frames;       /* frame count, not including the ring frame */
    uint32_t speed;        /* FLC: milliseconds per frame; FLI: 1/70 s units */
    size_t first_frame;    /* file offset of the first frame chunk */
} bs_flic;

/* Validates the header and finds the first frame. */
bs_result bs_flic_open(const uint8_t *data, size_t size, bs_flic *out);

/* Playback state: the current image and palette. */
typedef struct bs_flic_player {
    const bs_flic *flic;
    size_t pos;            /* offset of the next frame chunk */
    uint32_t frame;        /* frames decoded so far */
    uint8_t *pixels;       /* width * height */
    uint8_t palette[768];  /* 8-bit RGB */
} bs_flic_player;

bs_result bs_flic_player_init(bs_flic_player *p, const bs_flic *flic, bs_arena *arena);

/*
 * Decodes the next frame chunk in file order. When none remain, sets *ended
 * and returns BS_OK without changing the image.
 *
 * FLC files usually end with a "ring frame" (frame frames+1): a delta from
 * the last frame back to frame 1. To loop, keep decoding through the ring
 * frame, then restart decoding at frame 2.
 */
bs_result bs_flic_next_frame(bs_flic_player *p, bool *ended);

#endif
