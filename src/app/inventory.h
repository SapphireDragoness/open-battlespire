#ifndef OPENBATTLESPIRE_INVENTORY_H
#define OPENBATTLESPIRE_INVENTORY_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#include "bs/assets/assets.h"
#include "bs/formats/book.h"
#include "bs/formats/exe_text.h"
#include "bs/formats/font.h"
#include "bs/formats/spells.h"
#include "bs/game/items.h"
#include "bs/game/use.h"
#include "bs/render/level_renderer.h"
#include "bs/render/overlay.h"
#include "bs/render/screen.h"

/*
 * The inventory screen (GAME.EXE 0x4E4C1; docs/formats/items.md, "The
 * inventory screen"): char12.bsi with two lists of items, the carried ones on
 * the left and a container or the floor on the right, each with the open
 * container's box on top and four slots of spinning item models below.
 * Items are dragged between the lists; double-clicking a container opens
 * it, double-clicking the box goes back out. On the left the character
 * wears what is equipped (the paper doll, 0x56510): items dropped on it are
 * equipped, dragged off it taken off; below it the secondary weapon and the
 * arrows ready. Double-clicking any other item uses it (0x501B4): books
 * and scrolls open the book screen (textbk.bsi), potions are drunk, spell
 * scrolls teach their spell, other potions cast theirs (env->drink). Not
 * yet: magic items' spells, hot keys, help.
 */

#define INVENTORY_DEPTH 16

/* What the screen works on; set up by the game for each call. */
typedef struct inventory_env {
    bs_items *items;
    const bs_item_db *db;
    int32_t strength;
    float drop_pos[3];               /* where dropped items land: by the player's feet */
    bs_level_renderer *renderer;
    uint32_t model_first;            /* renderer object of the first item model (see inventory_models) */
    const float *model_radius;       /* per item model */
    bs_overlay *overlay;
    bs_screen *screen;
    const bs_font *font_info;        /* FONT0008: the item's lines */
    const bs_exe_text *text;
    const bs_bsi_image *filter_on[6];   /* filt00-05: a filter that is on */
    const bs_bsi_image *arrow_off[2];   /* point0, point1: a list that can't scroll that way */
    void (*message)(void *ctx, const char *text);
    void *ctx;
    /* The character, for the paper doll and equipping. */
    const bs_equip_rules *rules;
    bool female;
    const char *race;                /* "rd", "br", "no", "hi", "dk", "wd" */
    int hair, eyes, mouth;           /* player record +0x23E, +0x23F, +0x240 */
    int32_t hand_to_hand;            /* the Hand to Hand skill (+0x132): the damage shown is n/7+2 - n/7+4 */
    const bs_bsi_image *(*image)(void *ctx, const char *name);  /* a BSI image by name, or NULL */
    /* Using items. */
    bs_character *character;
    const bs_spell *spells;          /* BS_SPELL_COUNT, or NULL */
    int level_number;
    uint32_t now_ms;
    bs_rng *rng;
    const char *name;                /* the character's, for books */
    const bs_font *font_book;        /* FONT0003 */
    const uint8_t *(*book_text)(void *ctx, int32_t number, size_t *size);  /* bk?_NNN.txt, or NULL */
    void (*drink)(void *ctx, int32_t spell);  /* a potion's spell on the drinker, or NULL */
} inventory_env;

typedef struct inventory {
    bool open;
    bs_item_place stack[2][INVENTORY_DEPTH];  /* each list's open container, outermost first */
    int depth[2];
    int scroll[2];
    uint8_t filters;                 /* bits: weapons, magic, misc, clothing, potions, amulets (0xEF8DC) */
    uint32_t selected;               /* the item whose lines show, or UINT32_MAX */
    uint32_t held;                   /* the item being dragged, or UINT32_MAX */
    bool pressed;
    int press_x, press_y;
    int mouse_x, mouse_y;            /* screen pixels (640 x 480) */
    bool changed;                    /* items moved: the world needs updating */
    /* The book screen over it (0x54078). */
    bool reading;
    bs_book book;
    bs_arena book_arena;
    uint32_t page;
} inventory;

/*
 * The item models the screen and dropped items use, in renderer object order
 * after model_first: the 40 base item models then sigila..sigilz. Fills
 * names (66 entries) and returns the count.
 */
#define INVENTORY_MODELS (BS_ITEM_MODEL_COUNT + 26)
uint32_t inventory_models(const bs_item_tables *t, char names[INVENTORY_MODELS][16]);

/* The model object of an item, or UINT32_MAX. */
uint32_t inventory_item_object(const inventory_env *env, uint32_t item);

void inventory_init(inventory *inv);

/* Opens with the carried items on the left and `other` on the right. */
void inventory_open(inventory *inv, const inventory_env *env, bs_item_place other);
void inventory_close(inventory *inv);

/* Handles an event; returns false when the screen closed. window_w/h: the window size in pixels. */
bool inventory_event(inventory *inv, const inventory_env *env, const SDL_Event *e, int window_w, int window_h);

/* Draws the screen. time_ms: a clock for the spinning models. */
void inventory_draw(inventory *inv, const inventory_env *env, int window_w, int window_h, uint64_t time_ms);

#endif
