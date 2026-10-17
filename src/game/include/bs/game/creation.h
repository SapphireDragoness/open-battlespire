#ifndef BS_GAME_CREATION_H
#define BS_GAME_CREATION_H

#include <stdbool.h>
#include <stdint.h>

#include "bs/formats/scl.h"
#include "bs/formats/spells.h"
#include "bs/game/items.h"
#include "bs/game/use.h"

/*
 * Making a character's class with build points (GAME.EXE 0x372DE, the CharacterMaker; docs/re/README.md,
 * "Character creation"). A single player character has 5000 points. A new class starts with every
 * attribute 50, every skill 5, 50 wounds, any armour and material, and Increase Magery x 1, which costs
 * 300. Points buy:
 *
 * - attributes, 10 .. 75: raising v costs v x 80 / 100 + 40 (0x38B8A); none may drop below a skill it
 *   governs;
 * - wounds, 50 .. 200: raising w costs w x 50 / 200 + 40 (0x37FDE);
 * - the twelve class skills (three primary, three major, six minor; not Medical, Lockpicking or Running),
 *   which start at 30, 20 and 15 for nothing, then 5 .. 60 and not above their attribute: raising v costs
 *   m x v / 100 + 10, m 50 for primary, 60 major, 70 minor (0x387A1);
 * - advantages, and disadvantages that give points back (0x39BCC ..);
 * - spells, 8 x their cost (0x396AA), and equipment and magic items (0x398D9, 0x39AEF).
 *
 * Lowering anything gives back what raising it to there cost. The fixed-point steps (x 256 / d, then
 * >> 8) are kept, so every cost is the game's to the point.
 */

#define BS_CREATION_POINTS 5000
#define BS_CREATION_MAGERY_STEPS 4

typedef enum bs_creation_list { BS_LIST_PRIMARY, BS_LIST_MAJOR, BS_LIST_MINOR } bs_creation_list;

typedef enum bs_creation_result {
    BS_CREATE_OK,
    BS_CREATE_NO_POINTS,      /* "You do not have enough points." */
    BS_CREATE_LIMIT,          /* at its limit: nothing happens */
    BS_CREATE_ATTRIBUTE,      /* a skill can't pass its attribute ("%s %s.") */
    BS_CREATE_SKILL_BELOW,    /* an attribute can't drop below a skill it governs */
    BS_CREATE_CONFLICT,       /* excluded by another choice */
} bs_creation_result;

/* The advantages and disadvantages toggled on and off (0x39D50 ..), by the class's fields. */
typedef enum bs_creation_flag {
    BS_ADV_ACUTE_HEARING,     /* 200 */
    BS_ADV_SPELL_ABSORPTION,  /* 2500 */
    BS_ADV_RAPID_HEALING,     /* 700 */
    BS_ADV_REGENERATE_HEALTH, /* 2000 */
    BS_ADV_REGENERATE_SP,     /* 2000; not with No Regeneration */
    BS_ADV_ATHLETICISM,       /* 400 */
    BS_ADV_ADRENALINE_RUSH,   /* 200 */
    BS_DIS_NO_SP_REGEN,       /* gives 1000; not with Regenerate SPs */
    BS_DIS_NO_SHIELD,         /* costs 300 (there are no shields) */
    BS_CREATION_FLAGS,
} bs_creation_flag;

/* The element advantages and disadvantages, a bit per element (bs_element_bit). */
typedef enum bs_tolerance_kind {
    BS_TOL_RESISTANCE,        /* 800 */
    BS_TOL_IMMUNITY,          /* 1600 */
    BS_TOL_LOW,               /* gives 800 */
    BS_TOL_CRITICAL,          /* gives 1600 */
    BS_TOLERANCES,
} bs_tolerance_kind;

typedef struct bs_creation {
    bs_class cls;             /* what is being made; spells and items are what it starts with */
    int32_t points;           /* left; may go below 0 only through items and spells kept from a class */
} bs_creation;

/* A new class (0x3B273). */
void bs_creation_new(bs_creation *c);

/* An existing class to change (0x3BDC3): its points left are its own. */
void bs_creation_from_class(bs_creation *c, const bs_class *cls);

/* Costs of one more point from v. */
int32_t bs_creation_attribute_cost(int32_t v);
int32_t bs_creation_wounds_cost(int32_t w);
int32_t bs_creation_skill_cost(int32_t v, bs_creation_list list);

/* Changes by 1 (up true) or down. */
bs_creation_result bs_creation_attribute(bs_creation *c, int attribute, bool up);
bs_creation_result bs_creation_wounds(bs_creation *c, bool up);
/* Slot 0..11 of the class skills (0-2 primary, 3-5 major, 6-11 minor). */
bs_creation_result bs_creation_skill(bs_creation *c, int slot, bool up);

/* The skills a slot may take (0x3B8BE): not Medical, Lockpicking or Running, not already a class skill.
 * Returns how many. */
int bs_creation_skill_choices(const bs_creation *c, int out[BS_SCL_SKILLS]);
/* Puts a skill in a slot, for nothing (0x38618): in an empty slot it starts at 30 (primary), 20 (major) or 15
 * (minor); in place of another it takes that one's value and the other goes back to 5. */
bool bs_creation_set_skill(bs_creation *c, int slot, int skill);

bs_creation_result bs_creation_toggle(bs_creation *c, bs_creation_flag f);
bs_creation_result bs_creation_tolerance(bs_creation *c, bs_tolerance_kind t, int element_bit);
/* Whether a choice is out (0x3AEE1): resistance, immunity, low tolerance and critical weakness to an element
 * exclude each other; Regenerate SPs and No Regeneration too. */
bool bs_creation_excluded_flag(const bs_creation *c, bs_creation_flag f);
bool bs_creation_excluded_tolerance(const bs_creation *c, bs_tolerance_kind t, int element_bit);

/* Increase Magery x 1, 1.5, 2, 3 (0x3A022, 0x3A0FA): 300, 1000, 1800, 2500 points. */
bs_creation_result bs_creation_magery(bs_creation *c, bool up);
/* The heaviest armour (0 none .. 3 heavy) and best material (0 Iron .. 9 Daedric) allowed: limiting
 * them gives points (0xEE5B3, 0xEE5C3). */
bs_creation_result bs_creation_armour(bs_creation *c, bool up);
bs_creation_result bs_creation_material(bs_creation *c, bool up);
/* A weapon group (bit: 1 short blades, 2 long, 4 blunt, 8 axes, 16 missile) forbidden or allowed again:
 * forbidding gives 400, 800, 600, 400, 1000 (0x3A4CE ..). */
bs_creation_result bs_creation_forbid_weapon(bs_creation *c, uint8_t bit);

/* A spell to start with costs 8 x its cost (0x396AA). */
int32_t bs_creation_spell_cost(const bs_spell *s);
bs_creation_result bs_creation_add_spell(bs_creation *c, const bs_spell *s);
void bs_creation_remove_spell(bs_creation *c, uint32_t index);
/* Whether a spell may be bought (0x2A3AC): not Dispel Sigil, not the unused ones, not one already there. */
bool bs_creation_spell_offered(const bs_creation *c, const bs_spell *s);

/* Equipment (0x398D9): clothes free, sacks and chests 10, else condition / 5 (10 if that's 0) +
 * 50 x material. Magic items (0x39AEF): potions 60, armour 500 + 50 x material, anything else past the
 * weapons 500. */
int32_t bs_creation_equipment_cost(const bs_item *it);
int32_t bs_creation_magic_cost(const bs_item *it);

/* The two shops (0x3B62A), made as for level 8: the equipment, five Clothes, every weapon (another in
 * place of the twelfth), arrows, a sack or chest and the six armour pieces; the magic items, ten potions
 * (0xE34A3) and twelve enchanted items (0xEE592). Items the class has are added as bought. */
#define BS_CREATION_STOCK 40
typedef struct bs_creation_shop {
    bs_item items[BS_CREATION_STOCK];
    bool bought[BS_CREATION_STOCK];
    uint32_t count;
    bool magic;                        /* the magic items' shop: its costs */
} bs_creation_shop;

void bs_creation_stock(const bs_item_db *db, bs_rng *r, bs_creation_shop *equipment, bs_creation_shop *magic);
/* A class's items as items: those bought when it was made go to the shops (equipment, or magic for enchanted
 * items and potions, 0x3BDC3), the rest to `others` (returns how many). Pictures get the character's sex
 * letter for weapons, clothes and armour (0x54219). */
uint32_t bs_creation_class_items(const bs_class *cls, bool female, bs_creation_shop *equipment,
                                 bs_creation_shop *magic, bs_item *others, uint32_t max);
/* An item of a class file as an item. */
void bs_creation_item(const bs_scl_item *in, bool female, bs_item *out);
int32_t bs_creation_shop_cost(const bs_creation_shop *s, uint32_t i);
/* Buys (0x39872) or gives back (0x39970) a shop's item. */
bs_creation_result bs_creation_buy(bs_creation *c, bs_creation_shop *s, uint32_t i);
void bs_creation_sell(bs_creation *c, bs_creation_shop *s, uint32_t i);

typedef enum bs_creation_check {
    BS_CHECK_READY,
    BS_CHECK_NO_NAME,         /* "You need to name your class." */
    BS_CHECK_SKILLS,          /* "You must pick all twelve skills." */
    BS_CHECK_POINTS,          /* "You cannot exit while you have a negative point total." */
} bs_creation_check;

/* Whether the class can be played (0x3923A, 0x3B969). */
bs_creation_check bs_creation_ready(const bs_creation *c);

/* The race's skill bonuses (0x3BA58), added to the skill bases when the game starts. */
void bs_race_skill_bonus(int race, int32_t out[BS_SCL_SKILLS]);

/* The character the class makes (0x37508 and 0x3BA58): attributes, skills with the race's bonuses, wounds,
 * spell points (Intelligence x magery), advantages; and the rules for equipping. Spells and items are
 * the caller's (they need the game's tables). */
void bs_creation_apply(const bs_class *cls, int race, bs_character *out, bs_equip_rules *rules);

#endif
