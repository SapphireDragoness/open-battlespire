# AI navigation graphs (NODES.*)

`GAMEDATA/NODES.L1` … `NODES.L8` and `NODES.M1` … `nodes.m6` (13 files) hold
the navigation graph for each level. The nodes field of `LEVELS.TXT` names
the file. The deathmatch maps `l9`–`l11` have none, and both entries for
`dmkk.bs6` share `nodes.m1`.

Status: **layout verified**: all 13 files parse to the byte (16,558 nodes,
293,946 links), and every link's target and distance check out. Several node
fields are unknown. `bs_formats/nodes.h` is the parser.

The game uses the graph for AI movement, and also as the set of places
ambient sounds come from (see [the RE notes](../re/README.md#ambient-sounds)).

## Layout

Little-endian. A `u32` node count, then for each node a `u32` size followed
by that many bytes. The game loads each record directly as its in-memory
node structure (`0x1B032`), then clears the two pointers and sets the index.

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | pointer (saved from memory; ignored) |
| 0x04 | 4 | pointer (saved from memory; ignored) |
| 0x08 | 3×4 | position: x, y, z as `float`, in level units (+y down, like BS6) |
| 0x14 | 4 | unknown; varies per node |
| 0x18 | 4 | unknown; looks like uninitialised memory (`5e5e5e5e`, …) |
| 0x1C | 4 | unknown; 0 or 1 (1 on 633 of 16,558 nodes) |
| 0x20 | 2 | group: the ambient-sound search can filter nodes by it (the sound code never does) |
| 0x22 | 2 | the node's own index (always equals its position in the file) |
| 0x24 | 1 | link count n (up to 69) |
| 0x25 | 4×n | links: `u16` distance, `u16` target node |

The record size is always `0x25 + 4n`. Each link's distance is the Euclidean
distance between the two nodes' positions, rounded down. That holds for all
293,946 links.

## Tools

The data test `test_all_nodes` parses every file. There is no `bstool`
command yet.
