/* Fuzzes WAV parsing. */
#include "bs/formats/wav.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bs_wav w;
    if (bs_wav_parse(data, size, &w) == BS_OK && w.sample_bytes) {
        volatile uint8_t sink = w.samples[0] ^ w.samples[w.sample_bytes - 1];
        (void)sink;
    }
    return 0;
}
