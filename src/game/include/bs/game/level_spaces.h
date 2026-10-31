#ifndef BS_GAME_LEVEL_SPACES_H
#define BS_GAME_LEVEL_SPACES_H

#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"
#include "bs/formats/bs6.h"

/*
 * A campaign level's spaces as the original keeps them (docs/formats/save.md, "Interiors"): the game
 * loads every interior a door leads to into the level's world, each under its door, so an original save
 * holds them all.
 *
 * - **Order:** the campaign level's doors in object order, then each loaded interior's doors, in the
 *   order the interiors were loaded (nested interiors come after all of the level's own).
 * - **Ids:** an interior's entities have its BS6 ids plus a base. A counter starts at the level's
 *   highest entity id + 100. A door to a file not loaded yet gives the file the counter as its base and
 *   adds the file's highest id + 100; any other door (the level itself, an interior already loaded, a
 *   name that doesn't load) adds 100.
 * - **Records:** an interior's entities hang under the door that loaded it (parent = the door's id).
 * - **Controllers:** the save's chain table is the level's chains, then each interior's, in load order.
 * - **The player** keeps the id of the door of the space they are in at +0x298 (0: the level), and each
 *   automap its space's door id at +0x21.
 *
 * Space 0 is the campaign level itself, with base 0 and no door.
 */

#define BS_LEVEL_SPACES_MAX 32

typedef struct bs_level_space {
    char file[32];                    /* the BS6 file, as the door names it plus ".bs6" */
    uint32_t door;                    /* the id of the door that loaded it; 0 for the campaign level */
    uint32_t base;                    /* added to the file's BS6 ids */
    uint32_t last_id;                 /* the file's highest entity id */
    uint32_t first_chain;             /* its controller chains in the save's table */
    uint32_t chain_count;
} bs_level_space;

typedef struct bs_level_spaces {
    bs_level_space spaces[BS_LEVEL_SPACES_MAX];
    uint32_t count;
    uint32_t *chain_lengths;          /* every space's chains' controller counts, in the save's order */
    uint32_t chain_count;
} bs_level_spaces;

/* Reads a level file (a name as a door gives it plus ".bs6"). Not found is not an error for the caller. */
typedef bs_result (*bs_level_read_fn)(void *ctx, const char *file, bs_arena *arena, bs_bs6_level *out);

/*
 * Finds the spaces of a campaign level: file is its name, level its parsed BS6. read loads the
 * interiors; arrays come from arena. Interiors past BS_LEVEL_SPACES_MAX are left out (none of the
 * campaign's are).
 */
bs_result bs_level_spaces_build(const char *file, const bs_bs6_level *level, bs_level_read_fn read, void *ctx,
                                bs_arena *arena, bs_level_spaces *out);

/* The space a door loaded (0 for door 0), or -1. */
int bs_level_spaces_by_door(const bs_level_spaces *s, uint32_t door);

/* The space whose entity ids hold id, with the id in its own file; -1 if none does. */
int bs_level_spaces_of_id(const bs_level_spaces *s, uint32_t id, uint32_t *local);

#endif
