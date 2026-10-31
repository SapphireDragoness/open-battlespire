#ifndef BS_GAME_SAVE_IMPORT_H
#define BS_GAME_SAVE_IMPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"
#include "bs/formats/save.h"
#include "bs/formats/spells.h"
#include "bs/game/dialogue.h"
#include "bs/game/items.h"
#include "bs/game/level_spaces.h"
#include "bs/game/use.h"

/*
 * An original saved game (SAVE0 .. SAVE9, bs/formats/save.h) turned into what the engine keeps
 * (docs/formats/save.md, "Loading"). The game loads a save over a fresh copy of its campaign level
 * (0x303FE): the level is read from its BS6 file by the saved level number, the spawn points' monsters
 * are removed, and then each record replaces the object with its id or is made anew. So:
 *
 * - The player: the character, its spells (its type-9 children) and timed effects (type 66), where it
 *   stands and looks.
 * - Every item, wherever it is: carried, in a carried container, lying in the level, in a level object
 *   (a chest, by its BS6 id), or held by a monster (their loot, in a sack).
 * - The monsters that are left; those not saved are dead. The hidden reserve isn't kept.
 * - The level's placed models and flats that were saved (those with links or controllers, and speakers that
 *   have left): where they are, their flags (locked, gone) and their clock time (a used gem's return).
 * - The player's timed spell effects.
 * - The automap's seen cells, the dialogue variables and visits, speakers leaving, the sigils dispelled,
 *   and the level script's progress.
 *
 * Times stay on the game's clock: the caller runs its clock from the saved one.
 *
 * The original keeps a level's interiors in its world (bs/game/level_spaces.h). With the level's spaces, a
 * save made in an interior names it (level_file, campaign_file), its ids are those of the interior's own
 * file, and every other space is kept as a level left through a door (other_levels): its items, monsters,
 * placed models, controllers and sigils. The controllers become ours (ctrls), each by its object.
 */

typedef enum bs_saved_place {
    BS_SAVED_CARRIED,
    BS_SAVED_IN_ITEM,                 /* ref: the container's index in items */
    BS_SAVED_FLOOR,                   /* at pos */
    BS_SAVED_IN_OBJECT,               /* ref: the level object's BS6 entity id */
    BS_SAVED_MONSTER,                 /* ref: the monster's index in monsters */
    BS_SAVED_NOWHERE,                 /* under something not kept (a reserve monster) */
} bs_saved_place;

typedef struct bs_saved_item {
    bs_item item;
    uint32_t id;                      /* the record id; a placed item's is its object's BS6 id */
    bs_saved_place place;
    uint32_t ref;
    float pos[3];
    bool worn;                        /* +0x14 bit 0x20: worn, wielded, or the arrows ready */
    bool secondary;                   /* the player's secondary weapon (+0x264) */
    int32_t level;                    /* -1: in the level the player is in; else other_levels[level] (ours) */
} bs_saved_item;

typedef struct bs_saved_monster {
    uint32_t id;                      /* the record id: a spawned monster's is its spawn flat's BS6 id */
    uint32_t type;                    /* 0-based creature (+0x24E) */
    float pos[3];
    int32_t angle;                    /* facing, 2048 a turn */
    int32_t attributes[8];            /* +0x40 */
    int32_t health, health_max, spell_points, spell_points_max;
    int32_t skill;                    /* +0x72: every skill is the same */
    uint32_t flags;                   /* +0x172 */
    int32_t state;                    /* +0x232: the AI state */
    bool greeted;                     /* +0x13 bit 0x40 */
    bool summoned;                    /* +0x226 bit 0x10 */
    bool ally;                        /* on the player's team (+0x22A) */
    int32_t level;                    /* -1: in the level the player is in; else other_levels[level] */
} bs_saved_monster;

typedef enum bs_saved_kind { BS_SAVED_KIND_OBJECT, BS_SAVED_KIND_FLAT, BS_SAVED_KIND_LIGHT } bs_saved_kind;

typedef struct bs_saved_object {
    uint32_t id;                      /* the BS6 entity id */
    bs_saved_kind kind;               /* the original's saves have placed models and flats */
    bool active;                      /* its controllers run */
    float pos[3];
    int32_t angles[3];                /* 2048 a turn */
    uint16_t flags;                   /* +0x13: byte +0x13 bit 8 locked */
    uint32_t time;                    /* +0x21: a used gem's return on the game's clock */
    bool gone;                        /* a speaker that has left (TELEPORT or KILL): +0x14 bit 2 */
    int32_t level;                    /* -1: in the level the player is in; else other_levels[level] */
} bs_saved_object;

#define BS_SAVED_OBJECT_LOCKED 0x0008u

/* A controller not as it began (the original's come from bs_save_tree.controls, laid out by the spaces). */
typedef struct bs_saved_ctrl {
    uint32_t object;                  /* its object's BS6 entity id */
    bs_saved_kind kind;
    uint32_t index;                   /* in the object's chain */
    bool reversed, running;
    uint32_t start_ms;
    int32_t start[3];
    int32_t level;                    /* -1: in the level the player is in; else other_levels[level] */
} bs_saved_ctrl;

#define BS_SAVED_LEVELS 32            /* a campaign level and its interiors (level 5 has 21) */
#define BS_SAVED_MAPS 29              /* BS_AUTOMAP_MAPS */

typedef struct bs_saved_game {
    char save_name[32];               /* the load screen's */
    char level_file[32];              /* the level file the player is in; "": the campaign level's by number */
    char campaign_file[32];           /* the campaign level, when level_file is an interior; else "" */
    int level_number;
    uint32_t clock;                   /* the game's millisecond clock */
    char name[33];
    bool female;
    int race;                         /* +0x25D, creation's race order */
    uint8_t hair, eyes, mouth;
    bs_character character;
    bs_equip_rules rules;
    float pos[3];                     /* the player's feet */
    int32_t yaw, pitch;               /* 2048 a turn; pitch + down */
    uint8_t script_progress;          /* +0x25E */
    bool script_done;                 /* 0x16FFB8 */
    bool beamed;                      /* +0x241: came by Dagon's transport beam, for the win */
    uint32_t captive_at;              /* +0x2DA: captivity (the beam without the armour, "ZZ") at this clock; 0 none */
    int32_t music_volume, sound_volume;
    bs_saved_item *items;
    uint32_t item_count;
    bs_saved_monster *monsters;
    uint32_t monster_count;
    bs_saved_object *objects;
    uint32_t object_count;
    uint32_t ride;                    /* the boat or balloon ridden (its record's parent is the player): its BS6 id,
                                         0 none */
    bool have_map;
    uint8_t seen[BS_SAVE_AUTOMAP_CELLS];   /* the level's automap: 40 x 30, row by row, 1 seen */
    bs_dialogue_state dialogue;
    struct {
        uint32_t id;                  /* a speaker's record id, 0 none */
        uint32_t at;                  /* when it goes (game clock) */
    } leaving[8];
    uint32_t sigils_gone[BS_SAVE_SIGILS];  /* BS6 ids of the sigils dispelled ... */
    int32_t sigil_gone_level[BS_SAVE_SIGILS];   /* ... and their level: -1 the player's, else other_levels[] */
    uint32_t sigil_gone_count;
    uint32_t links[BS_SAVE_TABLE];    /* the record ids with a dialogue link (STRU 104), for checking */
    uint32_t link_count;
    char other_levels[BS_SAVED_LEVELS][32];   /* levels left through a door (the original's: the level's
                                                 interiors and the level), whose items are kept */
    bool other_monsters[BS_SAVED_LEVELS];     /* other_levels[k]'s monsters are listed (those not are dead);
                                                 else they're made again */
    uint32_t other_level_count;
    bs_saved_ctrl *ctrls;
    uint32_t ctrl_count;
    bool have_ctrls;                  /* the controllers are listed (else as the level begins) */
    /* Ours only (empty from the original's): */
    bool have_maps;                   /* every automap's cells, not just the level's */
    uint8_t maps[BS_SAVED_MAPS][BS_SAVE_AUTOMAP_CELLS];
} bs_saved_game;

/*
 * Imports a parsed save. spaces: the saved level's (bs_level_spaces_build), or NULL to take every record as
 * the campaign level's. spells: the game's spell table (for each spell's school skill), or NULL. Arrays go
 * in arena. BS_ERR_FORMAT without a player record.
 */
bs_result bs_save_import(const bs_save_tree *tree, const bs_save_vars *vars, const bs_level_spaces *spaces,
                         const bs_spell *spells, bs_arena *arena, bs_saved_game *out);

/* A saved item record as the engine's item. */
void bs_save_import_item(const bs_save_record *r, bs_item *out);

#endif
