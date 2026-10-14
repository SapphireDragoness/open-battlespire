/* Fuzzes Smacker header, tree, palette, audio and video decoding. */
#include "bs/formats/smacker.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_smacker s;
    if (bs_smk_open(data, size, &a, &s) == BS_OK) {
        volatile uint32_t sink = 0;
        /* Bound the work per input: large dimensions times many frames is slow, not wrong. */
        uint32_t limit = (s.width * s.height > 65536u) ? 4u : 64u;
        for (uint32_t i = 0; i < limit && bs_smk_next_frame(&s) == BS_OK; i++) {
            sink += s.pixels[0] + s.palette[3];
            for (int t = 0; t < BS_SMK_TRACKS; t++)
                if (s.audio_bytes[t])
                    sink += s.audio[t][s.audio_bytes[t] - 1];
        }
        (void)sink;
        bs_smk_close(&s);
    }
    bs_arena_free(&a);
    return 0;
}
