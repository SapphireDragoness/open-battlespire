#ifndef BS_GAME_SAVE_EDIT_H
#define BS_GAME_SAVE_EDIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bs/core/result.h"
#include "bs/formats/save.h"
#include "bs/game/items.h"

/*
 * Editing the original game's saves (docs/formats/save.md): SAVETREE.DAT's records held as their bytes,
 * changed in place, and written back with every byte not touched as it was. SAVEVARS.DAT starts with a copy
 * of the player record's body; writing makes the two the same again.
 *
 * The player's fields have names (bs_save_edit_fields): the attributes and skills by bs_attribute_keys and
 * bs_skill_keys, and name, class, health, health_max, spell_points, spell_points_max, level, points, female,
 * race, hair, eyes, mouth, x, y, z, facing, progress, level_number.
 */

typedef struct bs_save_edit_record {
    uint8_t *object;                  /* malloc'd: the header and the body */
    uint32_t size;
} bs_save_edit_record;

typedef struct bs_save_edit {
    uint32_t version;
    bs_save_edit_record *records;
    uint32_t count, cap;
    uint8_t *controls;                /* malloc'd; the level's controllers, kept as they are */
    size_t controls_size;
    uint8_t vars[BS_SAVE_VARS_SIZE];
} bs_save_edit;

/* Takes copies of SAVETREE.DAT and SAVEVARS.DAT. */
bs_result bs_save_edit_open(bs_save_edit *e, const uint8_t *tree, size_t tree_size, const uint8_t *vars,
                            size_t vars_size);
void bs_save_edit_free(bs_save_edit *e);

/* SAVETREE.DAT as it is now (malloc'd), and e->vars made to match the player record. */
bs_result bs_save_edit_write(bs_save_edit *e, uint8_t **tree, size_t *tree_size);

/* The index of the record with an id, or -1. */
int32_t bs_save_edit_find(const bs_save_edit *e, uint32_t id);

/* ---- the player's fields ---- */

#define BS_SAVE_EDIT_ERROR 128

/* The names of the fields, in a useful order; NULL past the last. */
const char *bs_save_edit_field(uint32_t i);

/* A field's value as text. False if there's no such field. */
bool bs_save_edit_get(const bs_save_edit *e, const char *key, char *out, size_t cap);

/* Sets a field from text. False with a reason in err if the field or the value is wrong. */
bool bs_save_edit_set(bs_save_edit *e, const char *key, const char *value, char err[BS_SAVE_EDIT_ERROR]);

/* ---- items ---- */

/*
 * An item record's body for a new item, as the item maker makes one (0x521AB): the base record from GAME.EXE
 * (bs_item_base_record), then the material (or a scroll's, book's or Sigil Amulet's number), the condition,
 * an arrow count, a potion's type and the picture. magic: an enchantment's 0x233 bytes to copy (from an item
 * with it, +0xB3), or NULL.
 */
#define BS_SAVE_MAGIC_SIZE 0x233
#define BS_SAVE_MAGIC_AT 0xB3
void bs_save_item_body(const uint8_t base_record[0x2F2], const bs_item *it, const uint8_t *magic,
                       uint8_t out[BS_SAVE_ITEM_BODY]);

/* Adds an item record under a parent (the player, BS_SAVE_PLAYER_ID, or a container item's id) at the parent's
 * place; it gets an id past every other. Returns the id, 0 if out of memory or no such parent. */
uint32_t bs_save_edit_give(bs_save_edit *e, const uint8_t body[BS_SAVE_ITEM_BODY], uint32_t parent);

/* An item's enchantment bytes (BS_SAVE_MAGIC_SIZE) from any item with that enchantment, or NULL. */
const uint8_t *bs_save_edit_find_magic(const bs_save_edit *e, int32_t magic);

/* Removes an item or a monster with everything under it; what pointed at it (the weapons, hot keys) no
 * longer does. False if there's no such item or monster. */
bool bs_save_edit_remove(bs_save_edit *e, uint32_t id, char err[BS_SAVE_EDIT_ERROR]);

/* Sets an item's field: condition, condition_max, quantity, material, number (a scroll's text, a potion's
 * type ...), worn (yes / no: a weapon becomes the one in hand), secondary (yes / no). */
bool bs_save_edit_item_set(bs_save_edit *e, uint32_t id, const char *key, const char *value,
                           char err[BS_SAVE_EDIT_ERROR]);

/* ---- the dialogue's variables ---- */

/* Sets a global variable by name (or hash, as a number), adding it if it's not there. */
bool bs_save_edit_set_var(bs_save_edit *e, const char *name, int32_t value, char err[BS_SAVE_EDIT_ERROR]);

#endif
