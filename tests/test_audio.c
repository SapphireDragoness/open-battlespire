/* WAV and Smacker on synthetic data. */

#include <string.h>

#include "bs/formats/smacker.h"
#include "bs/formats/wav.h"
#include "bs_test.h"
#include "synth.h"

static uint8_t buf[1024];

static void test_wav(void)
{
    size_t n = synth_wav(buf, sizeof buf);
    BS_CHECK(n > 0);
    bs_wav w;
    BS_CHECK_EQ_INT(bs_wav_parse(buf, n, &w), BS_OK);
    BS_CHECK_EQ_INT(w.rate, 11025);
    BS_CHECK_EQ_INT(w.bits, 8);
    BS_CHECK_EQ_INT(w.channels, 1);
    BS_CHECK_EQ_INT(w.frame_count, 10);
    BS_CHECK(w.samples && w.samples[0] == 128 && w.samples[9] == 137);
    /* Truncated data chunk: clipped to what's there. */
    BS_CHECK_EQ_INT(bs_wav_parse(buf, n - 4, &w), BS_OK);
    BS_CHECK_EQ_INT(w.frame_count, 6);
    for (size_t len = 0; len < 44; len++)
        BS_CHECK(bs_wav_parse(buf, len, &w) != BS_OK);
    buf[20] = 3; /* format 3 = float */
    BS_CHECK_EQ_INT(bs_wav_parse(buf, n, &w), BS_ERR_UNSUPPORTED);
}

static void test_smk(void)
{
    size_t n = synth_smk(buf, sizeof buf);
    BS_CHECK(n > 0);
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_smacker s;
    BS_CHECK_EQ_INT(bs_smk_open(buf, n, &a, &s), BS_OK);
    BS_CHECK_EQ_INT(s.width, 4);
    BS_CHECK_EQ_INT(s.frames, 2);
    BS_CHECK(s.frame_ms == 100.0);
    BS_CHECK(s.tracks[0].present && !s.tracks[0].compressed && s.tracks[0].rate == 11025);

    BS_CHECK_EQ_INT(bs_smk_next_frame(&s), BS_OK);
    for (int i = 0; i < 16; i++)
        BS_CHECK_EQ_INT(s.pixels[i], 0x55);
    BS_CHECK(s.palette[0x55 * 3] == 255 && s.palette[0x55 * 3 + 1] == 0);
    BS_CHECK_EQ_INT(s.audio_bytes[0], 0);

    BS_CHECK_EQ_INT(bs_smk_next_frame(&s), BS_OK);
    BS_CHECK_EQ_INT(s.audio_bytes[0], 4);
    BS_CHECK(s.audio[0] && memcmp(s.audio[0], "\x01\x02\x03\x04", 4) == 0);
    BS_CHECK_EQ_INT(s.pixels[15], 0x55);
    BS_CHECK_EQ_INT(bs_smk_next_frame(&s), BS_ERR_NOT_FOUND);

    bs_smk_rewind(&s);
    BS_CHECK_EQ_INT(bs_smk_next_frame(&s), BS_OK);
    BS_CHECK_EQ_INT(s.pixels[0], 0x55);
    bs_smk_close(&s);

    /* A huge largest-audio-chunk hint is not allocated up front (found by fuzzing). */
    buf[24] = 0xF0;
    buf[25] = buf[26] = buf[27] = 0xFF;
    BS_CHECK_EQ_INT(bs_smk_open(buf, n, &a, &s), BS_OK);
    BS_CHECK(s.audio_cap[0] <= (1u << 24));
    BS_CHECK_EQ_INT(bs_smk_next_frame(&s), BS_OK);
    BS_CHECK_EQ_INT(bs_smk_next_frame(&s), BS_OK);
    BS_CHECK_EQ_INT(s.audio_bytes[0], 4);
    bs_smk_close(&s);
    n = synth_smk(buf, sizeof buf);

    /* Every truncation of the header fails cleanly. */
    for (size_t len = 0; len < n - 16; len++) {
        bs_smacker t;
        bs_arena_rewind(&a, bs_arena_get_mark(&a));
        BS_CHECK(bs_smk_open(buf, len, &a, &t) != BS_OK);
    }
    bs_arena_free(&a);
}

int main(void)
{
    BS_RUN(test_wav);
    BS_RUN(test_smk);
    return BS_TEST_RESULT();
}
