#ifndef BS_GAME_GAME_SAVE_H
#define BS_GAME_GAME_SAVE_H

#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"
#include "bs/formats/spells.h"
#include "bs/game/save_import.h"

/*
 * OpenBattlespire's own saved games: a bs_saved_game (what the original's saves import to, and what the game
 * keeps) as a text file people can read and change (bs/core/ini.h; docs/formats/game_save.md):
 *
 *     [save]      version, name, level, level_number, clock ...
 *     [player]    the character: name, sex, race, health, attributes, skills, class ...
 *     [spell]     one for each spell known, with the spell book's choices
 *     [effect]    one for each timed spell on the player
 *     [item]      one for each item, wherever it is
 *     [monster]   one for each monster alive
 *     [object]    the level's objects that moved, locks, used gems
 *     [controller], [sigils], [automap], [dialogue], [variables], [locals]
 *
 * Numbers are decimal; flags may be hex (0x...). Unknown sections or keys are errors, so a typo doesn't go
 * unnoticed.
 */

#define BS_GAME_SAVE_VERSION 1
#define BS_GAME_SAVE_ERROR 160

/* Names for comments and for reading names back; any may be NULL. */
typedef struct bs_game_save_names {
    const char *(*item)(void *ctx, uint32_t base);       /* a base item's name */
    const char *(*creature)(void *ctx, uint32_t type);   /* a creature's name */
    const char *(*variable)(void *ctx, uint32_t hash);   /* a dialogue variable's name, or NULL */
    void *ctx;
} bs_game_save_names;

/* The text (malloc'd, NUL-terminated). */
bs_result bs_game_save_write(const bs_saved_game *g, const bs_game_save_names *names, char **text, size_t *size);

/*
 * Reads one. spells: the spell table, which known spells start from (the file keeps the spell book's choices),
 * or NULL. Base items may be named (with names->item). Arrays go in arena. On BS_ERR_FORMAT err says which line
 * and why.
 */
bs_result bs_game_save_read(const char *text, size_t size, const bs_spell *spells, const bs_game_save_names *names,
                            bs_arena *arena, bs_saved_game *out, char err[BS_GAME_SAVE_ERROR]);

#endif
