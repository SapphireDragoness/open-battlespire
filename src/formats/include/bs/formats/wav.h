#ifndef BS_FORMATS_WAV_H
#define BS_FORMATS_WAV_H

#include <stddef.h>
#include <stdint.h>

#include "bs/core/result.h"

/*
 * RIFF WAVE files (SPIRE.SND and WAVES.BSA records). Every shipped sound is
 * PCM, 8-bit unsigned, mono: voices at 11025 Hz, effects at 11025, 32000 or
 * 8114 Hz. The parser accepts any PCM layout.
 * See docs/formats/bsa.md#sound-archives.
 */

typedef struct bs_wav {
    uint16_t channels;
    uint32_t rate;             /* samples per second */
    uint16_t bits;             /* 8 (unsigned) or 16 (signed little-endian) */
    const uint8_t *samples;    /* points into the parsed buffer */
    size_t sample_bytes;
    uint32_t frame_count;      /* samples per channel */
} bs_wav;

/* Parses a PCM WAVE file. The samples point into data. */
bs_result bs_wav_parse(const uint8_t *data, size_t size, bs_wav *out);

#endif
