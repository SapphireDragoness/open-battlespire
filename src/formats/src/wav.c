#include "bs/formats/wav.h"

#include <stdbool.h>
#include <string.h>

#include "bs/core/reader.h"

#define WAVE_FORMAT_PCM 1

bs_result bs_wav_parse(const uint8_t *data, size_t size, bs_wav *out)
{
    memset(out, 0, sizeof *out);
    bs_reader r = bs_reader_make(data, size);
    const uint8_t *riff = bs_read_span(&r, 4);
    bs_read_u32le(&r); /* RIFF size: some files get it wrong, so it isn't checked */
    const uint8_t *wave = bs_read_span(&r, 4);
    if (!riff || !wave || memcmp(riff, "RIFF", 4) != 0 || memcmp(wave, "WAVE", 4) != 0)
        return BS_ERR_FORMAT;

    bool have_fmt = false;
    while (bs_reader_remaining(&r) >= 8) {
        const uint8_t *tag = bs_read_span(&r, 4);
        uint32_t len = bs_read_u32le(&r);
        if (memcmp(tag, "fmt ", 4) == 0) {
            bs_reader f = bs_reader_sub(&r, len);
            uint16_t format = bs_read_u16le(&f);
            out->channels = bs_read_u16le(&f);
            out->rate = bs_read_u32le(&f);
            bs_read_u32le(&f); /* byte rate */
            bs_read_u16le(&f); /* block align */
            out->bits = bs_read_u16le(&f);
            if (!bs_reader_ok(&f) || !bs_reader_ok(&r))
                return BS_ERR_FORMAT;
            if (format != WAVE_FORMAT_PCM || (out->bits != 8 && out->bits != 16) || out->channels == 0 ||
                out->channels > 8 || out->rate == 0)
                return BS_ERR_UNSUPPORTED;
            have_fmt = true;
        } else if (memcmp(tag, "data", 4) == 0) {
            if (!have_fmt)
                return BS_ERR_FORMAT;
            /* A data length running past the end is clipped (seen in other games' files). */
            size_t n = len < bs_reader_remaining(&r) ? len : bs_reader_remaining(&r);
            out->samples = bs_read_span(&r, n);
            out->sample_bytes = n;
            out->frame_count = (uint32_t)(n / ((size_t)out->channels * (out->bits / 8u)));
            return BS_OK;
        } else {
            bs_reader_skip(&r, len + (len & 1u));
        }
    }
    return BS_ERR_FORMAT;
}
