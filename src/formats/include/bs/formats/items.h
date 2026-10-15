#ifndef BS_FORMATS_ITEMS_H
#define BS_FORMATS_ITEMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"
#include "bs/formats/bs6.h"
#include "bs/formats/le.h"

/*
 * The item tables. See docs/formats/items.md.
 *
 * GAME.EXE holds the 43 base items (0x2F2-byte records), the weapon stats
 * just before them and the material names; they are found by their content,
 * so both the CD and the patched executable work. TXT.BSA holds the magic
 * enchantments (MG0_GEN.TXT, MG2_SPC.TXT) as text records.
 *
 * Names are Windows-1252, as stored.
 */

#define BS_ITEM_BASE_COUNT 43
#define BS_ITEM_WEAPON_COUNT 16   /* the 15 weapons (base items 0-14) and bare hands */
#define BS_ITEM_MATERIAL_COUNT 10
#define BS_ITEM_MODEL_COUNT 40    /* the item models, by a base's image number */

/* Base items the game treats specially. */
enum {
    BS_ITEM_LAST_WEAPON = 14,     /* 0-14: Dagger .. Spear */
    BS_ITEM_POTION = 15,
    BS_ITEM_SACK = 16,
    BS_ITEM_CLOTHES = 19,
    BS_ITEM_SCROLL = 21,
    BS_ITEM_ARROW = 22,
    BS_ITEM_LANTERN = 23,
    BS_ITEM_HELMET = 24,          /* 24-29: armour, Helmet .. Gauntlets */
    BS_ITEM_GAUNTLETS = 29,
    BS_ITEM_SIGIL_AMULET = 31,
    BS_ITEM_BOOK = 32,
};

typedef struct bs_item_base {
    char name[32];
    char icon[10];          /* +0x22: inventory image name ("mwpns600", "hlmt"); "" for many */
    int32_t condition;      /* +0x2B: condition when new (+0x2F: the maximum, the same) */
    uint16_t image;         /* +0x33; in an item this field holds the material */
    uint16_t weight;        /* +0x35 */
    bool container;         /* +0x42 bit 0: Sack, chests, Coffer of Restoration */
} bs_item_base;

typedef struct bs_weapon_stats {
    int32_t unk0;           /* 2-30: grows with the weapon's size */
    int32_t hands;          /* 1 or 2 */
    int32_t unk8;
    int32_t damage_min, damage_max;  /* the damage roll; the material adds to both */
    int32_t skill;          /* the skill used: 14 Short Blade .. 19 Missile, 16 Hand to Hand */
} bs_weapon_stats;

typedef struct bs_item_tables {
    bs_item_base bases[BS_ITEM_BASE_COUNT];
    bs_weapon_stats weapons[BS_ITEM_WEAPON_COUNT];
    char materials[BS_ITEM_MATERIAL_COUNT][16];  /* Iron, Steel, ... Daedric */
    char models[BS_ITEM_MODEL_COUNT][16];        /* "bataxe.3d" ... "arrow.3d"; "" if not read */
    char hand_models[BS_ITEM_LAST_WEAPON + 1][16]; /* the weapon in hand by base: "edagger.3d" ...; "" if not read */
} bs_item_tables;

/* Finds the tables in GAME.EXE's bytes. BS_ERR_NOT_FOUND if they aren't there. */
bs_result bs_item_tables_read(const uint8_t *exe, size_t size, bs_item_tables *out);

/*
 * The item model names (a table of string pointers at 0xEFD89 in v1.5,
 * loaded at 0x5152F), from the loaded executable: some names are shared, so
 * the strings alone don't give the order.
 */
bs_result bs_item_models_read(const bs_le_image *img, bs_item_tables *out);

/* The models of the weapons in hand, by base item (string pointers at 0xE34AD, loaded at 0x48D9B). */
bs_result bs_item_hand_models_read(const bs_le_image *img, bs_item_tables *out);

/*
 * The model an item is shown with, in the inventory and when dropped: the
 * base's image number into the model table, or for a Sigil Amulet its own
 * model "sigila.3d" .. "sigilz.3d" (id - 9001, less 26 past 25; 'x' and 'y'
 * are made 'z'; 0x515A1, 0x52430). "" if there is none.
 */
const char *bs_item_model_name(const bs_item_tables *t, uint32_t base, int32_t sigil_id, char buf[16]);

static inline bool bs_item_is_weapon(uint32_t base)
{
    return base <= BS_ITEM_LAST_WEAPON;
}

static inline bool bs_item_is_armour(uint32_t base)
{
    return base >= BS_ITEM_HELMET && base <= BS_ITEM_GAUNTLETS;
}

/*
 * A magic enchantment: a text record such as
 *
 *   Name    "of the Moon Reiver"
 *   ID#     8011
 *   Item    0
 *   Spell   -1, 28, -1, 8, 4
 *   App     15, 30, 100
 *   Adv     36, 37
 *   Uses    10
 *   Level   0
 */
#define BS_MAGIC_LIST_MAX 20

typedef struct bs_magic {
    char name[48];          /* without its quotes */
    int32_t id;
    int32_t item;           /* the kind of item it goes on (0 weapon, 1 armour, ...); see items.md */
    int32_t spell[5];
    int32_t app[BS_MAGIC_LIST_MAX];
    int32_t adv[BS_MAGIC_LIST_MAX];
    uint8_t app_count, adv_count;
    int32_t uses;
    int32_t level;
} bs_magic;

typedef struct bs_magic_table {
    bs_magic *records;
    size_t count;
} bs_magic_table;

/* Parses an MG*.TXT file (need not be NUL-terminated). */
bs_result bs_magic_parse(const uint8_t *text, size_t size, bs_arena *arena, bs_magic_table *out);

/* The record with an id, or NULL. */
const bs_magic *bs_magic_find(const bs_magic_table *t, int32_t id);

/* The BS6 properties that place items (docs/formats/bs6.md). */
enum {
    BS_BS6_PROP_ITEM = 105,       /* base, ?, ?, number */
    BS_BS6_PROP_BOOK = 107,       /* text number */
    BS_BS6_PROP_POTION = 115,     /* potion type */
    BS_BS6_PROP_CONTENTS = 116,   /* a, b, ?, ?: one item in a container (or a monster's loot) */
};

/* Whether an entity places an item or fills a container. */
bool bs_bs6_logic_has_items(const bs_bs6_logic *logic);

#endif
