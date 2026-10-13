/* Fuzzes LZSS decompression with arbitrary input. */
#include "bs/formats/lzss.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    uint8_t *out;
    size_t n;
    if (bs_lzss_decompress(data, size, &a, &out, &n) == BS_OK && n != bs_lzss_decompressed_size(data, size))
        __builtin_trap();
    bs_arena_free(&a);
    return 0;
}
