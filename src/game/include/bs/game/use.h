#ifndef BS_GAME_USE_H
#define BS_GAME_USE_H

#include <stdbool.h>
#include <stdint.h>

#include "bs/formats/spells.h"
#include "bs/game/items.h"

/*
 * Using items (the inventory's double-click, GAME.EXE 0x501B4; see
 * docs/formats/items.md, "Using items"), and the character record: the
 * player's attributes, skills, spells and the effects of spells on them
 * (bs/game/magic.h casts them).
 */

/* The character record's skills: 21 of 12 bytes from +0x72, the value first. Those the code names. */
#define BS_SKILL_COUNT 21
enum {
    BS_SKILL_JUMPING = 1,              /* +0x7E */
    BS_SKILL_DESTRUCTION = 8,          /* the schools of magic: 8-13 */
    BS_SKILL_DODGING = 6,              /* +0xBA: lowers the chance to be hit (0x42805) */
    BS_SKILL_HAND_TO_HAND = 16,        /* +0x132 */
    BS_SKILL_CRITICAL = 20,            /* +0x162: the chance of a critical blow (0x428C6) */
};

/* A timed spell on the player: the game's effect objects (type 0x42, made or lengthened by 0x295A0, one per
 * spell; run every frame by 0x293EF). */
#define BS_EFFECTS 32
typedef struct bs_effect {
    uint8_t spell;                     /* the spell's number, 0: a free slot */
    uint8_t icon;                      /* +0x1A0 - 1: symbNN.bsi */
    uint32_t flags;                    /* +0x1A8: OR'ed into the player's flags while it lasts */
    uint32_t until;                    /* +0x1A4: the clock when it ends */
    int32_t tick_damage;               /* +0x194: health added every ... */
    uint32_t tick_ms, tick_last;       /* +0x19C ms, last at +0x198 */
    int32_t attributes[8];             /* +0x20 (12 bytes each, 16.16): added to the attributes */
    int32_t pools[3];                  /* +0x1B4, +0x1B8, +0x1BC: added to the player's spell pools */
} bs_effect;

/* The player's flags (+0x172) that spells set: the effects' flags OR'ed together every frame (0x29461). */
enum {
    BS_FLAG_DETECT_SPELL = 0x4,
    BS_FLAG_DETECT_ENEMY = 0x8,
    BS_FLAG_DETECT_INVISIBILITY = 0x10,
    BS_FLAG_INVISIBLE = 0x20,          /* monsters without Detect Invisibility don't see it (0x4613C) */
    BS_FLAG_SHADOW = 0x40,
    BS_FLAG_CHAMELEON = 0x80,
    BS_FLAG_SLOW_FALL = 0x100,
    BS_FLAG_CONTINUOUS_DAMAGE = 0x200,
    BS_FLAG_POISONED = 0x400,
    BS_FLAG_REFLECT = 0x8000,          /* also creatures' flags: Daedra Lord, Dark Seducer */
    BS_FLAG_RESIST_SPELLS = 0x10000,   /* Seducer, Dark Seducer */
    BS_FLAG_ABSORB = 0x20000,
    BS_FLAG_FIRE_SHIELD = 0x80000,
    BS_FLAG_JUMPING = 0x400000,
    BS_FLAG_RUNNING = 0x800000,
    BS_FLAG_ETHEREAL = 0x1000000,
    BS_FLAG_IMMUNE_FIRST = 27,         /* bit 27 + element: Resistance to that element (0xED686) */
};

/* The spell pools (+0x2A4, +0x2A8, +0x2AC): spells costing no more are resisted, absorbed or reflected. */
enum { BS_POOL_RESIST, BS_POOL_ABSORB, BS_POOL_REFLECT };

#define BS_HOTKEYS 8

typedef struct bs_character {
    int32_t attributes[8];             /* +0x40 (their base values; +0x20 holds them with the effects'):
                                          Strength, Intelligence, Willpower, Agility, Endurance, Personality,
                                          Speed, Luck */
    int32_t bonus[8];                  /* what effects add (made every frame, 0x2948C) */
    int32_t skills[BS_SKILL_COUNT];    /* +0x72 */
    int32_t level;                     /* +0x176 */
    int32_t health, health_max;        /* +0x66, +0x6A */
    int32_t spell_points, spell_points_max;   /* +0x60, +0x62 */
    bool no_spell_point_potions;       /* +0x1A1: Restoration doesn't work */
    bool double_healing;               /* +0x196: healing counts twice */
    bool absorbs_spells;               /* +0x195: may absorb hostile spells, a roll under Willpower (0x27E31) */
    uint8_t resists, immune, low_tolerance, critical_weakness;  /* +0x192, +0x193, +0x19F, +0x1A0 (as a
                                                                   creature's, see bs_element_bit) */
    uint32_t spells_known;             /* bit n - 1 for spell n */
    bs_spell book[BS_SPELL_COUNT];     /* the known spells as the spell book set them (the game's spell objects,
                                          0x2A144) */
    uint8_t hotkeys[BS_HOTKEYS];       /* +0x1FE: F1-F8, a spell's number or 0 */
    bs_effect effects[BS_EFFECTS];
    uint32_t flags;                    /* +0x172: BS_FLAG_*, made every frame */
    int32_t pools[3];                  /* BS_POOL_*, made every frame */
    int32_t shield;                    /* +0x16E: the Shield spell's points; blows take them first */
    uint32_t confused_until;           /* +0x30B */
    bool has_anchor;                   /* the Teleport spell's anchor (+0x2B0) */
    float anchor[3];
    uint32_t water_breathing_until;    /* +0x2C0 */
} bs_character;

/* A character until there is character creation: level 1, 50 health, 50 spell points, every attribute and
 * skill 50. */
void bs_character_default(bs_character *c);

/* An attribute with the effects', at least 10 (0x292CB). */
static inline int32_t bs_character_attribute(const bs_character *c, int i)
{
    int32_t v = c->attributes[i] + c->bonus[i];
    return v < 10 ? 10 : v;
}

/* Whether a timed effect of spell n lasts at now_ms. */
bool bs_character_has(const bs_character *c, int spell, uint32_t now_ms);

/* Learns spell n (1-31) as the table has it. Returns false if it was known. */
bool bs_character_learn(bs_character *c, const bs_spell *s);

/* A spell's duration in ms for a caster level, and its magnitude rolled for a level number (0x28A1E). */
uint32_t bs_spell_duration(const bs_spell *s, int32_t caster_level);
int32_t bs_spell_magnitude(const bs_spell *s, int level_number, bs_rng *r);

/* The cost of casting s with `skill` in its school (0x29791): base x (110 - skill) / 100 x the delivery's
 * 80, 90, 100, 110 or 120 % (0xED6A5), at least 5. */
int32_t bs_spell_cost(const bs_spell *s, int32_t skill);

/* The delivery after `delivery` for the spell book's button (0x29A8C): self-only spells stay on the caster,
 * spells for others skip it. */
uint8_t bs_spell_next_delivery(const bs_spell *s, uint8_t delivery);

/* Messages a use leaves: game message numbers with up to three numbers to fill in. */
#define BS_USE_MESSAGES 4
typedef struct bs_use_message {
    uint32_t id;
    int32_t args[3];
} bs_use_message;

typedef struct bs_use_result {
    enum { BS_USE_NOTHING, BS_USE_DONE, BS_USE_READ } kind;
    int32_t book;                      /* READ: the book's number (bk?_NNN.txt) */
    int32_t spell;                     /* DONE: a potion's spell to cast on the drinker (0x5042C), or 0 */
    bs_use_message messages[BS_USE_MESSAGES];
    uint32_t message_count;
} bs_use_result;

/*
 * Uses an item (0x501B4):
 * - a spell scroll (number 10000 + n) teaches spell n and is used up
 *   ("Spell has been added to your spell list.", or "You already have that
 *   spell.");
 * - a book or other scroll is read: kind READ with its number;
 * - a potion: Restoration (40) restores the spell points, Water Breathing
 *   (39) lasts 5 minutes, any other is to cast its spell on the drinker
 *   (`spell`, for bs_magic_cast_on_player; "You drink the potion."); one is
 *   used up.
 * Magic items' spells aren't used yet; other items do nothing.
 */
bs_use_result bs_items_use(bs_items *it, const bs_item_db *db, uint32_t i, bs_character *c,
                           const bs_spell spells[BS_SPELL_COUNT], int level_number, uint32_t now_ms, bs_rng *r);

#endif
