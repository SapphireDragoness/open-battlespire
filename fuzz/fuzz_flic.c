/* Fuzzes FLIC header parsing and frame decoding (all chunk types). */
#include "bs/formats/flic.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bs_flic f;
    if (bs_flic_open(data, size, &f) != BS_OK)
        return 0;
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_flic_player p;
    if (bs_flic_player_init(&p, &f, &a) == BS_OK) {
        for (int i = 0; i < 64; i++) {
            bool ended;
            if (bs_flic_next_frame(&p, &ended) != BS_OK || ended)
                break;
        }
    }
    bs_arena_free(&a);
    return 0;
}
