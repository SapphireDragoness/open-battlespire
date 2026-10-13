/* Fuzzes BSA directory parsing, lookup, and reading/decompressing every record. */
#include "bs/formats/bsa.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bs_stream *s;
    if (bs_stream_open_memory(data, size, &s) != BS_OK)
        return 0;
    bs_bsa *bsa;
    if (bs_bsa_open(s, &bsa) != BS_OK)
        return 0;
    bs_arena a;
    bs_arena_init(&a, 0);
    for (uint32_t i = 0; i < bsa->count; i++) {
        const bs_bsa_record *rec = &bsa->records[i];
        uint8_t *out;
        size_t n;
        bs_bsa_read(bsa, rec, &a, &out, &n);
        bs_bsa_read_raw(bsa, rec, &a, &out, &n);
        if (rec->name[0] && bs_bsa_find(bsa, rec->name) == NULL)
            __builtin_trap();
        bs_arena_reset(&a);
    }
    bs_bsa_find_id(bsa, 507);
    bs_arena_free(&a);
    bs_bsa_close(bsa);
    return 0;
}
