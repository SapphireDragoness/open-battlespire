/* Fuzzes TEXT.RSC parsing, lookup and UTF-8 rendering; also Windows-1252 conversion. */
#include "bs/core/text.h"
#include "bs/formats/rsc.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_cp1252_to_utf8(&a, data, size);
    bs_rsc rsc;
    if (bs_rsc_parse(data, size, &a, &rsc) == BS_OK) {
        for (size_t i = 0; i < rsc.count; i++) {
            if (bs_rsc_find(&rsc, rsc.records[i].id) != &rsc.records[i])
                __builtin_trap();
            bs_rsc_to_utf8(&a, &rsc.records[i]);
        }
    }
    bs_arena_free(&a);
    return 0;
}
