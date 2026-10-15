#ifndef BS_FORMATS_CREATURES_H
#define BS_FORMATS_CREATURES_H

#include <stddef.h>
#include <stdint.h>

#include "bs/core/result.h"
#include "bs/formats/items.h"

/*
 * The creature tables in GAME.EXE's data (docs/formats/creatures.md), v1.5
 * addresses:
 *
 * - 0xE3559: 16 creatures of 0xCE bytes (Scamp .. Daedra Lord): the stats
 *   the spawner (0x46A51) copies into a new monster;
 * - 0xE4239, right after: per creature 47 image names of 13 bytes, five view
 *   angles per animation (walk, attack, ..., stand), then the death image;
 * - 0xE80A1: four creatures per level number, picked at random for spawn
 *   points without a creature of their own (0x461BE);
 * - 0xEF25F: per creature four SPIRE.SND sounds (0x48749).
 *
 * Creature numbers here are 0-based, as the game indexes the table; spawn
 * records (BS6 STRU 106) count from 1.
 */

#define BS_CREATURE_COUNT 16
#define BS_CREATURE_IMAGES 47
#define BS_CREATURE_LEVELS 9    /* level numbers 0..8 */
#define BS_CREATURE_SPELLS 8    /* room for a spell list and its 0 */

/* Image slots: five view angles each (0 front .. 4 back) from these. */
enum {
    BS_CREATURE_IMAGE_WALK = 0,
    BS_CREATURE_IMAGE_ATTACK = 5,
    BS_CREATURE_IMAGE_STAND = 40,
    BS_CREATURE_IMAGE_DEATH = 45,  /* one image, no views */
};

/* The sounds (0x48749's kinds). */
enum {
    BS_CREATURE_SOUND_IDLE,
    BS_CREATURE_SOUND_ATTACK,
    BS_CREATURE_SOUND_HURT,
    BS_CREATURE_SOUND_DEATH,
    BS_CREATURE_SOUNDS
};

/* Creatures the game treats specially. */
enum {
    BS_CREATURE_WRAITH = 6,        /* hits harder the healthier it is (0x4268A) */
    BS_CREATURE_HERNE = 10,        /* on level 5: peaceful and unhurt by most weapons */
};

typedef struct bs_creature {
    char name[32];
    int32_t attributes[8];         /* +0x20: Strength .. Luck, as the player's */
    int32_t spell_points;          /* +0x40 (u16) */
    int32_t health;                /* +0x44 */
    int32_t skill;                 /* +0x48: every skill has this value */
    uint32_t flags;                /* +0x4C: copied to the monster's +0x172 */
    int32_t level;                 /* +0x6C: copied to +0x176, the player's level field */
    int32_t armour;                /* +0x70: the armour of every hit location */
    /* +0x74, +0x78: pointers to two lists of the spells it casts, 1-based, 0-terminated (0x47645 picks
     * from the second one time in five) */
    uint8_t spells[2][BS_CREATURE_SPELLS];
    bool spell_caster;             /* either list has a spell */
    uint16_t unk_7c;               /* 20 .. 230 (0 for Ghost and Wraith) */
    bs_weapon_stats weapon;        /* +0x7E: the damage roll and the skill used, as a weapon's */
    /* +0x96 (+0x17A of a monster) holds the tolerances of the elements, as bits of BS_ELEMENT_BIT
     * (0x2AA5D): */
    uint8_t resists;               /* +0xAE: the chance to resist is half again */
    uint8_t immune;                /* +0xAF: takes nothing */
    uint8_t low_tolerance;         /* +0xBB: the chance to resist is halved */
    uint8_t critical_weakness;     /* +0xBC: never resists */
    char images[BS_CREATURE_IMAGES][13];
} bs_creature;

typedef struct bs_creature_tables {
    bs_creature creatures[BS_CREATURE_COUNT];
    uint8_t level_creatures[BS_CREATURE_LEVELS][4];
    int32_t sounds[BS_CREATURE_COUNT][BS_CREATURE_SOUNDS];
} bs_creature_tables;

/* Finds the tables in GAME.EXE's bytes. BS_ERR_NOT_FOUND if they aren't there. */
bs_result bs_creature_tables_read(const uint8_t *exe, size_t size, bs_creature_tables *out);

#endif
