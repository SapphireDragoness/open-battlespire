/* Fuzzes NODES graph parsing. */
#include "bs/formats/nodes.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_nav_graph g;
    if (bs_nodes_parse(data, size, &a, &g) == BS_OK) {
        volatile uint32_t sink = 0;
        for (uint32_t i = 0; i < g.count; i++)
            for (uint16_t k = 0; k < g.nodes[i].link_count; k++)
                sink += g.nodes[g.nodes[i].links[k].target].group;
        (void)sink;
    }
    bs_arena_free(&a);
    return 0;
}
