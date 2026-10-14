#include "bs/formats/nodes.h"

#include <string.h>

#include "bs/core/reader.h"

#define NODE_HEADER 0x25u     /* bytes before the links */
#define MAX_NODES 65536u      /* link targets are 16-bit */

static float read_f32(bs_reader *r)
{
    uint32_t u = bs_read_u32le(r);
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}

bs_result bs_nodes_parse(const uint8_t *data, size_t size, bs_arena *arena, bs_nav_graph *out)
{
    memset(out, 0, sizeof *out);
    bs_reader r = bs_reader_make(data, size);
    uint32_t count = bs_read_u32le(&r);
    if (!bs_reader_ok(&r) || count > MAX_NODES || count > bs_reader_remaining(&r) / (4 + NODE_HEADER))
        return BS_ERR_FORMAT;
    bs_nav_node *nodes = BS_ARENA_ARRAY(arena, bs_nav_node, count ? count : 1);
    if (!nodes)
        return BS_ERR_NOMEM;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t len = bs_read_u32le(&r);
        if (!bs_reader_ok(&r) || len < NODE_HEADER || len > bs_reader_remaining(&r))
            return BS_ERR_FORMAT;
        bs_reader n = bs_reader_sub(&r, len);
        bs_nav_node *node = &nodes[i];
        bs_reader_skip(&n, 8); /* next/previous pointers from the saving process */
        for (int k = 0; k < 3; k++)
            node->pos[k] = read_f32(&n);
        node->unk_14 = bs_read_u32le(&n);
        bs_reader_skip(&n, 4);
        node->unk_1c = bs_read_u32le(&n);
        node->group = bs_read_u16le(&n);
        bs_read_u16le(&n); /* the node's own index */
        node->link_count = bs_read_u8(&n);
        if (len != NODE_HEADER + 4u * node->link_count)
            return BS_ERR_FORMAT;
        bs_nav_link *links = BS_ARENA_ARRAY(arena, bs_nav_link, node->link_count ? node->link_count : 1);
        if (!links)
            return BS_ERR_NOMEM;
        for (uint16_t k = 0; k < node->link_count; k++) {
            links[k].distance = bs_read_u16le(&n);
            links[k].target = bs_read_u16le(&n);
            if (links[k].target >= count)
                return BS_ERR_FORMAT;
        }
        if (!bs_reader_ok(&n))
            return BS_ERR_FORMAT;
        node->links = links;
    }
    if (!bs_reader_eof(&r))
        return BS_ERR_FORMAT;
    out->nodes = nodes;
    out->count = count;
    return BS_OK;
}
