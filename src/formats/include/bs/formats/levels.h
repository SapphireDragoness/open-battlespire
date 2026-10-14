#ifndef BS_FORMATS_LEVELS_H
#define BS_FORMATS_LEVELS_H

#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"

/*
 * GAMEDATA/LEVELS.TXT: one level per line, whitespace-separated, with the
 * display name taking the rest of the line. See docs/formats/levels_txt.md.
 *
 *   l1.bs6  Lv1  1336  NNF  xxxx  555  1024  nodes.l1  1500  4096  80  The Weir Gate
 *
 * "xxxx" means "none". Fields whose meaning is not yet known are named unk_*.
 */

typedef enum bs_game_type {
    BS_GAME_CAMPAIGN = 'N',
    BS_GAME_DEATHMATCH = 'D',
    BS_GAME_CTF = 'C'
} bs_game_type;

typedef struct bs_level_info {
    char file[16];    /* level in BS6.BSA, e.g. "l1.bs6" */
    char id[8];       /* "Lv1".."Lv7", "Dm1".., "Cf1".. */
    int32_t view_distance; /* 1024..3500: fog reaches full strength here (RE: 0x6B778) */
    char mode[4];     /* 3 letters; [0] is the bs_game_type, [1] N/Y, [2] F/C */
    char sky[16];     /* sky image in BSI.BSA; "" if none */
    char haze[8];     /* suffix of the HAZE.* fog table, e.g. "555" */
    int32_t ambient;  /* 1024..31000: ambient light in 1/512 shade levels; the renderer clamps it
                         to 16128 (31.5 levels: fully lit). RE: 0x6F20D, 0x8D013 */
    char nodes[16];   /* AI node file, e.g. "nodes.l1"; "" if none */
    int32_t unk_c;    /* 1000..2500 */
    int32_t grid_cell; /* 2048..6000: spatial grid cell size; 64 x 64 cells centred on 0 (RE: 0x25B21) */
    int32_t monster_percent; /* 55..100: share of monster spawn points that get a monster (RE: 0x462CB) */
    char name[64];    /* display name */
} bs_level_info;

typedef struct bs_level_table {
    bs_level_info *levels;
    size_t count;
} bs_level_table;

/* Parses the text (need not be NUL-terminated). Blank lines are skipped. */
bs_result bs_levels_parse(const uint8_t *text, size_t size, bs_arena *arena, bs_level_table *out);

static inline bs_game_type bs_level_game_type(const bs_level_info *l)
{
    return (bs_game_type)l->mode[0];
}

/*
 * The game's level number (global 0xDA43A, RE in docs/formats/levels_txt.md).
 * Campaign levels are 1-6 and 8: the file's digit, with l2/l3 swapped (they
 * are played in the other order), and 7 raised to 8 (the game skips level
 * number 7, which also loads l8.bs6). Every multiplayer level is 4. Returns
 * 0 if a campaign file name has no digit.
 */
int bs_level_number(const bs_level_info *l);

/*
 * The CD audio track a level plays: level number + 1 (MUSIC/TrackNN.ogg).
 * The final level (number 8) asks for track 9, which the disc does not have,
 * so it is silent in the original.
 */
static inline int bs_level_music_track(const bs_level_info *l)
{
    int n = bs_level_number(l);
    return n ? n + 1 : 0;
}

#endif
