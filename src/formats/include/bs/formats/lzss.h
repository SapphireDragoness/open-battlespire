#ifndef BS_FORMATS_LZSS_H
#define BS_FORMATS_LZSS_H

#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"

/*
 * LZSS decompression for compressed BSA records (directory flag 0x0100).
 *
 * The scheme is Haruhiko Okumura's classic LZSS (LZSS.C, 1989):
 *   - 4096-byte ring buffer, pre-filled with spaces (0x20)
 *   - writing starts at ring position 0xFEE (4096 - 18)
 *   - a flag byte precedes each group of 8 items, read LSB first:
 *     1 = literal byte, 0 = 2-byte back-reference
 *   - reference bytes b0 b1: position = b0 | (b1 & 0xF0) << 4,
 *     length = (b1 & 0x0F) + 3
 *
 * The decompressed size is not stored; decoding ends when the input is
 * exhausted. A trailing incomplete reference is ignored, as in LZSS.C.
 * See docs/formats/bsa.md.
 */

#define BS_LZSS_RING_SIZE 4096
#define BS_LZSS_MAX_MATCH 18
#define BS_LZSS_THRESHOLD 2
#define BS_LZSS_FILL 0x20

/* Size the input decompresses to. */
size_t bs_lzss_decompressed_size(const uint8_t *src, size_t n);

/*
 * Decompresses into memory allocated from arena (NUL byte appended, not
 * counted in *out_size).
 */
bs_result bs_lzss_decompress(const uint8_t *src, size_t n, bs_arena *arena,
                             uint8_t **out, size_t *out_size);

#endif
