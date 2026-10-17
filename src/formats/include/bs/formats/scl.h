#ifndef BS_FORMATS_SCL_H
#define BS_FORMATS_SCL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bs/core/result.h"
#include "bs/formats/spells.h"

/*
 * Character classes, GAMEDATA/CLASSnnn.SCL (docs/formats/scl.md): the game's object records as its saves
 * write them, the player's first and its children after (known spells, carried items, the automap).
 *
 * A record is a 32-bit length (not counting itself), a type byte and the rest of a 65-byte header, then
 * the body. The player's body (type 3, 852 bytes) is the player record the game keeps at 0x16BF90:
 * offsets below are into it. Spells (type 9) are spell table records; items (type 2) item records.
 */

#define BS_SCL_SKILLS 21
#define BS_SCL_CLASS_SKILLS 12        /* three primary, three major, six minor */
#define BS_SCL_MAX_SPELLS 32
#define BS_SCL_MAX_ITEMS 32
#define BS_SCL_NO_SKILL 0xFF

typedef struct bs_scl_item {
    char name[32];
    uint16_t base;
    char picture[10];                 /* "fHLMT502", "" for none */
    int32_t condition, condition_max;
    uint16_t material;
    int32_t number;                   /* a potion's spell (+0x39), -1 none */
    int32_t quantity;                 /* arrows; 0 for single items */
    int32_t magic;                    /* the enchantment (+0x2E2), 0 none */
    int32_t charges, charges_max;     /* the enchantment's (+0x2D2, +0x2D6) */
    bool container;                   /* +0x42 */
    uint8_t flags;                    /* the object's +0x14: 0x10 bought when the class was made, 0x20 worn */
} bs_scl_item;

typedef struct bs_class {
    char class_name[24];              /* +0x17A */
    int level;                        /* +0x29F; 0 counts as 1 */
    int32_t points_left;              /* +0x20A */
    uint8_t points_hundreds;          /* +0x2E1: the build points / 100 it was made with for network play, 0 */
    int32_t attributes[8];            /* +0x40, base values */
    int32_t wounds;                   /* +0x6E, the base */
    int32_t skills[BS_SCL_SKILLS];    /* +0x7A + 12 n, base values */
    uint8_t class_skills[BS_SCL_CLASS_SKILLS];   /* +0x1A6, skill numbers or BS_SCL_NO_SKILL */
    /* Advantages and disadvantages (+0x192 ..). */
    uint8_t resists, immune;          /* element bits as bs_element_bit: 1 magic, 2 fire, 4 cold, 8 shock, 16 poison */
    bool acute_hearing, spell_absorption, rapid_healing, regenerate_health, regenerate_sp, athleticism;
    int32_t magery;                   /* +0x19A: spell points per Intelligence, x 256 */
    bool adrenaline_rush;
    uint8_t low_tolerance, critical_weakness;
    bool no_sp_regen;                 /* +0x1A1 */
    uint8_t forbidden_weapons;        /* +0x1A2: 1 short blades, 2 long, 4 blunt, 8 axes, 16 missile */
    uint8_t max_armour;               /* +0x1A3: 0 none, 1 light, 2 medium, 3 heavy */
    bool forbidden_shield;            /* +0x1A4 */
    uint8_t max_material;             /* +0x1A5: 0 Iron .. 9 Daedric */
    bs_spell spells[BS_SCL_MAX_SPELLS];
    uint32_t spell_count;
    bs_scl_item items[BS_SCL_MAX_ITEMS];
    uint32_t item_count;
} bs_class;

/* Reads a class file. */
bs_result bs_class_parse(const uint8_t *data, size_t size, bs_class *out);

/* The class fields of a player record's body (0x317 bytes), and an item record's body (a save's too). */
void bs_class_player_parse(const uint8_t body[0x317], bs_class *out);
void bs_scl_item_parse(uint8_t flags, const uint8_t *body, size_t size, bs_scl_item *out);

#endif
