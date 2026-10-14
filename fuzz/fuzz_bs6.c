/* Fuzzes BS6 level parsing, the generic chunk iterator and the entity logic reader. */
#include "bs/formats/bs6.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static size_t walk(const bs_bs6_chunk *parent, int depth)
{
    size_t n = 0;
    bs_bs6_iter it = bs_bs6_iter_make(parent->data, parent->size);
    bs_bs6_chunk c;
    while (bs_bs6_next(&it, &c)) {
        n++;
        if (depth < 8 && c.size >= 8)
            n += walk(&c, depth + 1);
    }
    return n;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bs_bs6_chunk root = {"", data, (uint32_t)size};
    volatile size_t sink = walk(&root, 0);

    bs_arena a;
    bs_arena_init(&a, 0);
    bs_bs6_level l;
    if (bs_bs6_parse(data, size, &a, &l) == BS_OK) {
        for (uint32_t i = 0; i < l.object_count; i++) {
            float m[9];
            bs_bs6_rotation(l.objects[i].angles, m);
            sink += (size_t)l.model_names[l.objects[i].model][0] + (size_t)(m[0] > 0);
            bs_bs6_logic g;
            if (bs_bs6_entity_logic(&l.objects[i].chunk, &a, &g) == BS_OK)
                for (uint32_t k = 0; k < g.ctrl_count; k++)
                    sink += (size_t)bs_bs6_record_i32(&g.ctrls[k], k) + g.ctrls[k].size;
        }
        for (uint32_t i = 0; i < l.flat_count; i++)
            sink += (size_t)l.flats[i].name[0];
    }
    (void)sink;
    bs_arena_free(&a);
    return 0;
}
