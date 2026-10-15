#ifndef BS_FORMATS_SPELLS_H
#define BS_FORMATS_SPELLS_H

#include <stddef.h>
#include <stdint.h>

#include "bs/core/result.h"

/*
 * The spell effects in GAME.EXE: 31 records of 0x77 bytes (0xDA614 in v1.5),
 * from Monster Summoning to Dispel Sigil, found by content. Spell n (1-31)
 * is record n - 1; a potion of type n casts spell n, a spell scroll
 * 10000 + n teaches it. See docs/formats/spells.md.
 */

#define BS_SPELL_COUNT 31

enum {
    BS_SPELL_MONSTER_SUMMONING = 1,
    BS_SPELL_DETECT_SPELL = 2,
    BS_SPELL_DETECT_ENEMY = 3,
    BS_SPELL_DETECT_INVISIBILITY = 4,
    BS_SPELL_INVISIBILITY = 5,
    BS_SPELL_SHADOW = 6,
    BS_SPELL_CHAMELEON = 7,
    BS_SPELL_SLOW_FALL = 8,
    BS_SPELL_CONTINUOUS_DAMAGE = 9,
    BS_SPELL_POISON = 10,
    BS_SPELL_CONFUSION = 11,
    BS_SPELL_VAMPIRIC_DRAIN = 12,
    BS_SPELL_DELAYED_DAMAGE = 13,
    BS_SPELL_DISPEL_MAGIC = 14,
    BS_SPELL_REFLECTION = 15,
    BS_SPELL_RESISTANCE = 16,
    BS_SPELL_ABSORPTION = 17,
    BS_SPELL_CAUSE_DAMAGE = 18,
    BS_SPELL_FIRE_SHIELD = 19,
    BS_SPELL_CURE_POISON = 20,
    BS_SPELL_CURE_HEALTH = 21,
    BS_SPELL_JUMPING = 22,
    BS_SPELL_RUNNING = 23,
    BS_SPELL_ETHEREALNESS = 24,
    BS_SPELL_TELEPORT = 25,
    BS_SPELL_SHIELD = 26,
    BS_SPELL_ELEMENT_RESISTANCE = 27,
    BS_SPELL_SLOW = 28,
    BS_SPELL_HASTE = 29,
    BS_SPELL_STRENGTH = 30,
    BS_SPELL_DISPEL_SIGIL = 31,
};

/* The elements (+0x4D), in the order of the spell book's button (messages 75-79). */
enum {
    BS_ELEMENT_FIRE,
    BS_ELEMENT_COLD,
    BS_ELEMENT_POISON,
    BS_ELEMENT_SHOCK,
    BS_ELEMENT_MAGIC,
    BS_ELEMENT_COUNT
};

/* An element's bit in a creature's tolerances (0xED681). */
static inline uint8_t bs_element_bit(int element)
{
    static const uint8_t bits[BS_ELEMENT_COUNT] = {2, 4, 16, 8, 1};
    return element >= 0 && element < BS_ELEMENT_COUNT ? bits[element] : 0;
}

/* Deliveries (+0x4E, messages 70-74). */
enum {
    BS_DELIVERY_CASTER,
    BS_DELIVERY_TOUCH,
    BS_DELIVERY_RANGE,        /* a single creature at range */
    BS_DELIVERY_AREA_CASTER,  /* the area around the caster */
    BS_DELIVERY_AREA_RANGE,   /* an area at range */
    BS_DELIVERY_COUNT
};

/* What a missile does on hitting the level (+0x6B, messages 58-60). */
enum {
    BS_DETONATE_ANY,          /* bursts */
    BS_DETONATE_ORGANIC,      /* bounces; bursts on creatures */
    BS_DETONATE_ORGANIC_DROP, /* falls, bounces and slows down; bursts on creatures */
    BS_DETONATION_COUNT
};

/* Targets (+0x21). */
enum {
    BS_TARGET_SELF,           /* on the caster only: the delivery can't change */
    BS_TARGET_OTHER,          /* never on the caster */
    BS_TARGET_EITHER,
};

typedef struct bs_spell {
    char name[32];
    uint8_t effect;            /* +0x20: the spell's number, 1-31 */
    uint8_t target;            /* +0x21: BS_TARGET_* */
    uint8_t icon;              /* +0x22: the status icon (symbNN.bsi, 0-26) shown while it lasts */
    /* +0x23: duration = (base + caster level / step x increase) x 5 s (0x28A1E); base 0: none */
    int32_t duration_base, duration_increase, duration_step;
    /* +0x2F: magnitude = min..max + (inc_min..inc_max) x (level number / step); min 0: none */
    int32_t magnitude_min, magnitude_max, increase_min, increase_max, magnitude_step;
    uint8_t element;           /* +0x4D: BS_ELEMENT_* */
    uint8_t delivery;          /* +0x4E: BS_DELIVERY_* (the table's: touch for hostile spells) */
    int32_t cost;              /* +0x5F: the base cost */
    int32_t school;            /* +0x67: 0-5, Alteration, Restoration, Destruction, Mysticism, Thaumaturgy,
                                  Illusion */
    int32_t detonation;        /* +0x6B: BS_DETONATE_* */
    int32_t skill;             /* the school's skill (the table before the spells, 0xDA60A), -1 if none */
} bs_spell;

/* The players' spell records are the same, changed by the spell book: icon, element, delivery and
 * detonation are the caster's choices. */

/* Reads the 31 spells from GAME.EXE's bytes. BS_ERR_NOT_FOUND if they aren't there. */
bs_result bs_spells_read(const uint8_t *exe, size_t size, bs_spell out[BS_SPELL_COUNT]);

#endif
