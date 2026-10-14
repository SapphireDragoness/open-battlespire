#ifndef BS_FORMATS_NODES_H
#define BS_FORMATS_NODES_H

#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"

/*
 * AI navigation graphs: GAMEDATA/NODES.L1 .. nodes.m6, named by the nodes
 * field of LEVELS.TXT. A u32 node count, then per node a u32 size and that
 * many bytes: the game's in-memory node, pointers included. Positions are in
 * level units, like BS6 object positions. See docs/formats/nodes.md.
 *
 * Besides AI pathing, the game uses the nodes as emitters for ambient sounds.
 */

typedef struct bs_nav_link {
    uint16_t distance;  /* to the target, Euclidean rounded down */
    uint16_t target;    /* node index */
} bs_nav_link;

typedef struct bs_nav_node {
    float pos[3];       /* x, y (+y down), z */
    uint32_t unk_14;    /* varies per node */
    uint32_t unk_1c;    /* 0 or 1 */
    uint16_t group;     /* +0x20; the game can filter nodes by it */
    uint16_t link_count;
    const bs_nav_link *links;
} bs_nav_node;

typedef struct bs_nav_graph {
    bs_nav_node *nodes;
    uint32_t count;
} bs_nav_graph;

/* Parses a NODES file. Links pointing outside the graph are an error. */
bs_result bs_nodes_parse(const uint8_t *data, size_t size, bs_arena *arena, bs_nav_graph *out);

#endif
