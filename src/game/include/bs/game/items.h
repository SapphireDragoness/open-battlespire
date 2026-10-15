#ifndef BS_GAME_ITEMS_H
#define BS_GAME_ITEMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"
#include "bs/formats/bs6.h"
#include "bs/formats/items.h"

/*
 * Items as GAME.EXE makes them (docs/formats/items.md): an item is a base
 * item with a material (weapons and armour), optionally an enchantment, and
 * per-kind numbers (a potion's type, a book's text, an arrow count).
 *
 * The levels place items on objects (BS6 STRU 105, 107, 115) and fill
 * containers (116); bs_items_add_level makes them all when a level loads.
 */

/* Everything needed to make and name items. */
typedef struct bs_item_db {
    const bs_item_tables *tables;
    const bs_magic_table *magic[2];  /* MG0_GEN.TXT, MG2_SPC.TXT (either may be empty) */
    /* The images worn items can show on the character (BSI names without
     * ".BSI", e.g. "MHLMT503"), and the character's sex: new items pick their
     * picture from these. NULL: items get none. */
    const char *const *pictures;
    uint32_t picture_count;
    bool female;
} bs_item_db;

const bs_magic *bs_item_db_magic(const bs_item_db *db, int32_t id);

typedef struct bs_item {
    uint16_t base;          /* 0..42 */
    uint16_t material;      /* weapons and armour: 0-9 (Iron .. Daedric) */
    int32_t magic;          /* enchantment id, 0 if none */
    int32_t number;         /* potion type, book or scroll text, sigil id; 0 if none */
    int32_t quantity;       /* arrows; 1 otherwise */
    int32_t condition, condition_max;
    char picture[10];       /* the image it shows worn on the character ("MHLMT503"), "" if none */
} bs_item;

/* The game's random numbers; any 32-bit generator will do (the original's isn't reproduced). */
typedef struct bs_rng {
    uint32_t state;
} bs_rng;

uint32_t bs_rng_next(bs_rng *r);
/* A number from lo to hi inclusive (the game's 0x1D8A3). */
int32_t bs_rng_range(bs_rng *r, int32_t lo, int32_t hi);

/*
 * The material for a new weapon or armour piece (0x51634): a roll of
 * 1-100 plus 8 per level number above 4 (plus 20 on multiplayer levels),
 * against thresholds that make Iron the most common and Daedric the rarest.
 */
uint16_t bs_item_material_for_roll(int32_t roll);
uint16_t bs_item_roll_material(int level_number, bool multiplayer, bs_rng *r);

/* The level placement kinds. */
typedef enum bs_item_spec_kind {
    BS_ITEM_SPEC_PLACED,    /* STRU 105: base, ?, ?, number (or 107/115 turned into it) */
    BS_ITEM_SPEC_CONTENT,   /* STRU 116: a, b, ?, ? */
} bs_item_spec_kind;

/*
 * Makes the item for a placement's four values (the item maker, 0x26C73).
 * Returns false (and logs) if it names an unknown enchantment or base.
 */
bool bs_item_make(const bs_item_db *db, bs_item_spec_kind kind, const int32_t v[4], int level_number,
                  bool multiplayer, bs_rng *r, bs_item *out);

/* A plain item of a base (0x521AB). */
void bs_item_make_plain(const bs_item_db *db, uint32_t base, int level_number, bool multiplayer, bs_rng *r,
                        bs_item *out);

/*
 * The item's name as the inventory shows it: weapons and armour as
 * "Material Base suffix", others as "Base suffix" (0x4F3FC). Windows-1252.
 * Returns the length (truncated to cap - 1).
 */
size_t bs_item_name(const bs_item_db *db, const bs_item *it, char *buf, size_t cap);

/* The weight towards what the player can carry (0x5484D): arrows count/2 + 1. */
int32_t bs_item_weight(const bs_item_db *db, const bs_item *it);

/*
 * Picks the picture a new item shows when worn (0x52CB4): weapons the base's
 * icon with the sex letter ("FWPNS603"); Clothes one of armb, cape, pnts,
 * shrt at random; other items with an icon one of the images starting with
 * the sex letter and the icon ("MHLMT*"), at random.
 */
void bs_item_pick_picture(const bs_item_db *db, bs_item *it, bs_rng *r);

/*
 * An armour piece's value (0x544A4): the picture's last digit mod 4, plus 1,
 * plus the material (less 1, halved above 1); at least 1.
 */
int32_t bs_item_armour_value(const bs_item *it);

/* What the character's class allows (player record +0x1A2, +0x1A3, +0x1A5). */
typedef struct bs_equip_rules {
    uint8_t forbidden_weapons;   /* bits by weapon group: skill 14 short blade .. 19 missile (table 0xEFE91) */
    uint8_t armour;              /* the heaviest armour type, 0 none .. 3 */
    uint8_t material;            /* the best material, 0 Iron .. 9 Daedric */
} bs_equip_rules;

/* No restrictions: until characters have classes. */
static inline bs_equip_rules bs_equip_rules_any(void)
{
    return (bs_equip_rules){0, 3, 9};
}

/* Whether the class may equip an item (0x54984); some quest items always (0x5459A). */
bool bs_item_may_equip(const bs_item_db *db, const bs_item *it, const bs_equip_rules *rules);

/* ---- where items are ---- */

typedef enum bs_item_where {
    BS_ITEM_GONE,           /* used up, or joined another stack */
    BS_ITEM_FLOOR,          /* lying in the level */
    BS_ITEM_IN_OBJECT,      /* in a level object filled with items (a chest) */
    BS_ITEM_IN_ITEM,        /* in a container item (a sack) */
    BS_ITEM_CARRIED,        /* in the player's inventory */
} bs_item_where;

/* A place items are in: one list of the inventory screen. */
typedef struct bs_item_place {
    bs_item_where where;
    uint32_t ref;           /* FLOOR: grid cell (bs_items_cell); IN_OBJECT: level object; IN_ITEM: the container
                               item; CARRIED: 0 */
} bs_item_place;

typedef struct bs_world_item {
    bs_item item;
    bs_item_place place;
    bool equipped;          /* worn or wielded (object +0x14 bit 0x20) */
    bool secondary;         /* the secondary weapon (+0x42 bit 8) */
    int32_t object;         /* FLOOR: the level object that shows it (placed by the level), or -1: drawn at pos */
    float pos[3];           /* FLOOR: where it lies */
    uint32_t seq;           /* lists show items in the order they came */
} bs_world_item;

/*
 * Every item of the level and the player's. In the game items are objects
 * in a tree (parent +0x39, children +0x35): the player and containers hold
 * theirs, and loose ones are in the cells of the level's object grid.
 */
typedef struct bs_items {
    bs_world_item *items;   /* malloc'd; indices stay valid until bs_items_keep_carried */
    uint32_t count, cap;
    uint32_t seq;
    int32_t grid_cell;      /* the level's object grid cell size (LEVELS.TXT), for floor places */
    uint32_t *containers;   /* level objects filled with items, sorted */
    uint32_t container_count;
} bs_items;

void bs_items_free(bs_items *it);

/* Adds an item; returns its index, or UINT32_MAX if out of memory. */
uint32_t bs_items_add(bs_items *it, const bs_item *item, bs_item_place place, int32_t object, const float pos[3]);

/* The object grid cell of a position (0x3E16D): 64 x 64 cells of grid_cell
 * units, x / cell + 32 by z / cell + 32; everything outside shares one. */
#define BS_ITEM_CELL_OUTSIDE 4096u
uint32_t bs_items_cell(int32_t grid_cell, const float pos[3]);

/*
 * Makes every placed item and container of a level's objects (the item
 * maker, 0x26C73): placed items lie on the floor shown by their object,
 * contents go in their object.
 */
bs_result bs_items_add_level(bs_items *it, const bs_bs6_level *level, const bs_item_db *db, int level_number,
                             bool multiplayer, int32_t grid_cell, bs_rng *r);

/* Keeps only what the player carries (for the next level). */
void bs_items_keep_carried(bs_items *it);

/* Moves everything not carried into out (which must be empty), to keep a level's items while the
 * player is elsewhere (an interior). it keeps what is carried. */
bs_result bs_items_take_level(bs_items *it, bs_items *out);

/* Puts a level's items taken with bs_items_take_level back with what is carried. */
bs_result bs_items_put_level(bs_items *it, const bs_items *level);

/* The items in a place, in order; returns how many there are (at most max stored). */
uint32_t bs_items_list(const bs_items *it, bs_item_place place, uint32_t *out, uint32_t max);

/* Whether a level object was filled with items (STRU 116). */
bool bs_items_is_container_object(const bs_items *it, uint32_t object);

/* The first item lying on the floor shown by a level object, or UINT32_MAX. */
uint32_t bs_items_on_object(const bs_items *it, uint32_t object);

/* Whether an item is a container (+0x42 bit 0): it can hold items. */
bool bs_items_is_container(const bs_item_db *db, const bs_items *it, uint32_t i);

/* Whether a place is the inventory or inside a carried container (0x548DE). */
bool bs_items_place_carried(const bs_items *it, bs_item_place place);

/* The weight of an item with everything inside it. */
int32_t bs_items_weight(const bs_item_db *db, const bs_items *it, uint32_t i);

/* The weight the player carries (0x547C5): half units, as the weight field. */
int32_t bs_items_carried_weight(const bs_item_db *db, const bs_items *it);

/* Whether the player is over 3 x Strength (encumbered, 0x5493B). */
bool bs_items_encumbered(const bs_item_db *db, const bs_items *it, int32_t strength);

typedef enum bs_item_move {
    BS_MOVE_OK,
    BS_MOVE_JOINED,         /* arrows or potions joined a stack there ("You combine that item ...") */
    BS_MOVE_TOO_HEAVY,      /* over 4 x Strength ("You cannot carry any more stuff.", 0x5484D) */
    BS_MOVE_INTO_ITSELF,    /* a container into itself or what it holds */
    BS_MOVE_INVALID,
} bs_item_move;

/*
 * Moves an item (with what it holds) to a place. Into the inventory the
 * weight is checked against strength. To the floor it lies at floor_pos.
 * Arrows join arrows of the same material and enchantment, potions join
 * potions of the same type. A moved item is no longer worn (0x507D0).
 */
bs_item_move bs_items_move(bs_items *it, const bs_item_db *db, uint32_t i, bs_item_place to,
                           const float floor_pos[3], int32_t strength);

/* ---- equipping ---- */

typedef enum bs_equip {
    BS_EQUIP_OK,
    BS_EQUIP_NOT_ALLOWED,   /* "You are not allowed to equip this." (message 254) */
    BS_EQUIP_TOO_HEAVY,     /* not carried yet, and over the limit (message 251) */
    BS_EQUIP_NO_ARROWS,     /* a bow or crossbow without arrows ready: "You have no arrows ready." (106) */
    BS_EQUIP_INVALID,
} bs_equip;

/*
 * Equips an item (0x50EAE, 0x52A00): weapons, Clothes, armour and arrows,
 * if the class allows it; it goes into the inventory if it isn't there.
 * What was worn in its place comes off: the weapon for a weapon, arrows for
 * arrows, otherwise the piece whose picture shows the same part (the same
 * first five letters).
 */
bs_equip bs_items_equip(bs_items *it, const bs_item_db *db, uint32_t i, const bs_equip_rules *rules,
                        int32_t strength);

/* Takes an item off. */
void bs_items_unequip(bs_items *it, uint32_t i);

/* Makes a weapon the secondary one (0x55374), replacing any other; into the inventory if needed. */
bs_equip bs_items_set_secondary(bs_items *it, const bs_item_db *db, uint32_t i, const bs_equip_rules *rules,
                                int32_t strength);

/* The worn item of a base, the wielded weapon, the arrows ready, the secondary weapon: UINT32_MAX if none. */
uint32_t bs_items_worn(const bs_items *it, int32_t base);
uint32_t bs_items_weapon(const bs_items *it);
uint32_t bs_items_arrows(const bs_items *it);
uint32_t bs_items_secondary(const bs_items *it);

/* A bow or crossbow without arrows ready is put away (0x4E7A4). Returns whether it was. */
bool bs_items_check_bow(bs_items *it);

/* The six armour values the inventory shows (0x543F5): helmet, pauldrons, cuirass, greaves, gauntlets, boots. */
#define BS_ARMOUR_PLACES 6
extern const uint16_t bs_armour_places[BS_ARMOUR_PLACES];
void bs_items_armour_values(const bs_items *it, int32_t out[BS_ARMOUR_PLACES]);

#endif
