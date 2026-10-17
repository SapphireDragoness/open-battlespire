#ifndef BS_FORMATS_AUTOMAP_H
#define BS_FORMATS_AUTOMAP_H

#include <stddef.h>
#include <stdint.h>

#include "bs/core/result.h"

/*
 * The automap's table in GAME.EXE (0xF1083; docs/re/README.md, "The automap"): for each level and
 * interior with a map, its file name, the map picture (map000.bsi ...), and how level x and z map to
 * the screen. Records of 0x42 bytes: names of 13 bytes, then ten 32-bit numbers.
 */

#define BS_AUTOMAP_MAPS 29

typedef struct bs_automap_entry {
    char level[13];          /* "l1.bs6" */
    char image[13];          /* "map000.bsi" */
    int32_t world[4];        /* x at the left, z at the top, x at the right, z at the bottom */
    int32_t screen[4];       /* where those are on the 640x480 screen: x0, y0, x1, y1 */
    int32_t shift[2];        /* added twice, x and y (level 1: 1, -1) */
} bs_automap_entry;

typedef struct bs_automap_table {
    bs_automap_entry maps[BS_AUTOMAP_MAPS];
    uint32_t count;
} bs_automap_table;

/* Finds the table in GAME.EXE's bytes. */
bs_result bs_automap_table_read(const uint8_t *exe, size_t size, bs_automap_table *out);

/* The map of a level (its file name, any case), or -1 (0x57ECF). Level 3 has two: below y 700 (+y
 * down) it's the second. */
int bs_automap_find(const bs_automap_table *t, const char *level, float y);

/* Where a level point is on its map's screen (0x57D76), in whole percent steps as the game has it. */
void bs_automap_point(const bs_automap_entry *m, float x, float z, int *sx, int *sy);

#endif
