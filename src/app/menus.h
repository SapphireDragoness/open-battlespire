#ifndef OPENBATTLESPIRE_MENUS_H
#define OPENBATTLESPIRE_MENUS_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#include "bs/assets/assets.h"
#include "bs/formats/automap.h"
#include "bs/formats/bsi.h"
#include "bs/formats/exe_text.h"
#include "bs/formats/font.h"
#include "bs/game/levelling.h"
#include "bs/game/use.h"
#include "bs/render/overlay.h"

/*
 * The screens the interface bar's buttons open (docs/re/README.md, "The bar's screens"); the world
 * waits while one is open.
 *
 * - Help (0x20D33): full-screen pages help00 .. help22.bsi with the game shown small in their window,
 *   Previous / Next / Done, and whether such pages should pop up by themselves (Yes / No).
 * - The character sheet (0x558DC, F9): cscr00.bsi with the paper doll, name, the eight attributes
 *   (red when lowered, green when raised), health, spell points, what is carried, and the skills on a
 *   scroll. After a level it is the level-up screen (cscr01.bsi, bs/game/levelling.h): a click on an
 *   attribute's upper half raises it, on its lower half lowers it; left of a skill lowers it, right of it
 *   raises it; Shift does ten; the points left show below. Automap, Inventory and F9 do nothing there.
 * - The automap (0x57415, F11): map030.bsi with the level's map (bs_automap_table) where the player
 *   has been, 16-pixel cells revealed around them, and the player's blinking marker.
 * - The options bar (0x2EA54): options.bsi in place of the bar: Fullscreen (the full view), the
 *   sound and music volumes, End Game and Drop to DOS (each asked "Are you sure?", sure00.bsi), Exit
 *   and help. Screen Size, Controls, Load and Save aren't there yet.
 */

#define MENU_FONTS 11                 /* FONT0001 .. FONT0010 by number; 0 unused */
#define AUTOMAP_COLS 40
#define AUTOMAP_ROWS 30

typedef enum menu_kind {
    MENU_NONE,
    MENU_HELP,
    MENU_SHEET,
    MENU_AUTOMAP,
    MENU_OPTIONS,
} menu_kind;

typedef enum menu_action {
    MENU_NOTHING,
    MENU_CLOSED,
    MENU_TO_INVENTORY,   /* the sheet's Inventory button */
    MENU_FULL_VIEW,      /* Fullscreen: the full view toggles (0x2ECA3) */
    MENU_VOLUME,         /* a volume changed */
    MENU_END_GAME,       /* End Game, confirmed */
    MENU_QUIT,           /* Drop to DOS, confirmed */
    MENU_LOAD,           /* Load (0x2EF60): the load screen; the bar is closed */
    MENU_SAVE,           /* Save (0x2EF9A) */
    MENU_NOT_THERE,      /* Screen Size, Controls */
} menu_action;

typedef struct menu_env {
    bs_assets *assets;
    const bs_font *fonts[MENU_FONTS];
    bs_character *character;          /* changed only by the level-up screen */
    const char *name;
    const bs_bsi_image *const *doll;  /* the paper doll's images (inventory_doll_images) */
    int doll_count;
    int32_t carried;                  /* what is carried, in half units (0x547C5) */
    const char *skill_names[BS_SKILL_COUNT];   /* 0xE34E9 */
    const char *attribute_names[8];            /* "STR" .. "LCK" (0xE8B15) */
    /* The automap: the level's map (NULL: none), the cells seen, where the player is. */
    const bs_automap_entry *map;
    const uint8_t *revealed;          /* AUTOMAP_COLS x AUTOMAP_ROWS */
    float player_x, player_z;
    int32_t player_yaw;               /* 2048 a turn */
    bool full_view;
    uint64_t now_ms;
    /* Messages the level-up screen gives (the game's message table). */
    const bs_exe_text *text;
    void (*message)(void *ctx, const char *text);
    void *ctx;
} menu_env;

typedef struct menus {
    menu_kind open;
    menu_kind under;                  /* help opened from a screen goes back to it */
    int help_first, help_count, help_page;
    bool auto_help;                   /* "Automatically pop up similar help screen in future?" (0x16FFCD) */
    menu_action confirm;              /* MENU_END_GAME or MENU_QUIT while "Are you sure?" shows */
    int skill_scroll;
    bool level_up;                    /* the sheet is the level-up screen (0x3FC1EE) */
    bs_level_up lu;
    int dragging;                     /* the volume slider held: 0 sound, 1 music, -1 none */
    int sound_volume, music_volume;   /* 0..127 (0xDA404, 0xDA400) */
} menus;

void menus_init(menus *m);

/* Opens a screen. Help pages go with menus_help. */
void menus_open(menus *m, menu_kind k);

/* Opens the sheet as the level-up screen with a level's points (0x558DC with points; bs_level_up_begin). */
void menus_level_up(menus *m, bs_character *c, int32_t points);

/* Help pages help<first> .. help<first + count - 1> (0x20D33). */
void menus_help(menus *m, int first, int count);

/* A key or a mouse event; mouse positions in 640x480 screen pixels (x, y). */
menu_action menus_event(menus *m, const menu_env *env, const SDL_Event *e, int x, int y);

/* Draws the open screen into the overlay. */
void menus_draw(const menus *m, const menu_env *env, bs_overlay *o);

/* Where the game shows on the screen while a screen is open: the whole view (the options bar), a small
 * window (help: r = x, y, w, h), or nothing (full-screen pages). Returns 0 nothing, 1 whole, 2 small. */
int menus_view(const menus *m, int r[4]);

/* Reveals the automap's cells around a level point (0x57AC2). */
void automap_reveal(uint8_t revealed[AUTOMAP_COLS * AUTOMAP_ROWS], const bs_automap_entry *map, float x, float z);

#endif
