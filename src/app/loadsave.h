#ifndef OPENBATTLESPIRE_LOADSAVE_H
#define OPENBATTLESPIRE_LOADSAVE_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#include "bs/assets/assets.h"
#include "bs/formats/font.h"
#include "bs/formats/save.h"
#include "bs/render/overlay.h"
#include "menus.h"

/*
 * Saved games' slots, and the options bar's Load and Save screen (GAME.EXE 0x31A62, drawn by 0x31776,
 * buttons 0xEDA93; docs/formats/game_save.md):
 *
 * - loadsave.bsi with load.bsi or save.bsi over it; the ten slots' pictures as the scenario's load screen has
 *   them, the frame (brdr01.bsi) on the one chosen, its name below (y 397).
 * - A click chooses a slot; a double click, OK or Enter loads it, or for saving asks "Enter name of save
 *   game." and takes the name typed there (at most 23 characters, Enter to save). Exit or Esc goes back.
 * - Ours: the slots are SAVE0 .. SAVE9 in our saves folder, each GAME.BSS (bs/game/game_save.h) and the
 *   picture, IMAGE.RAW as the original keeps it. Loading also shows the original game's slots (the install's
 *   SAVE0 .. SAVE9): Page Down and Page Up switch.
 */

#define SAVE_SLOTS 10
#define SAVE_NAME_MAX 23              /* (0x31714) */
#define SAVE_FILE "GAME.BSS"

typedef struct save_slot {
    bool present;
    char name[32];
    bool have_picture;
    uint8_t rgb[BS_SAVE_IMAGE_WIDTH * BS_SAVE_IMAGE_HEIGHT * 3];
} save_slot;

/* The slots of a folder holding SAVE0 .. SAVE9: ours (GAME.BSS) or the original's (SAVENAME.DAT). */
void save_slots_read(const char *dir, bool ours, save_slot slots[SAVE_SLOTS]);

/* A slot's folder: dir/SAVEn. */
void save_slot_dir(const char *dir, int slot, char *out, size_t cap);

/* Where a slot's picture goes on the load screens (0xEDA93, 0xEDC52): x 48 + 112 (n mod 5), y 192 or 304. */
void save_slot_xy(int slot, int *x, int *y);

typedef enum loadsave_mode { LOADSAVE_LOAD, LOADSAVE_SAVE } loadsave_mode;

typedef struct loadsave_env {
    bs_assets *assets;
    const bs_font *fonts[MENU_FONTS];
    const char *saves_dir;            /* ours */
    const char *original_dir;         /* the install, or NULL */
    uint64_t now_ms;
    void (*message)(void *ctx, const char *text);
    void *ctx;
} loadsave_env;

typedef struct loadsave {
    bool open;
    loadsave_mode mode;
    bool originals;                   /* showing the original game's slots (loading only) */
    save_slot slots[SAVE_SLOTS];
    int slot, from;                   /* the slot chosen, where the frame slides from */
    uint64_t slide_ms, click_ms;
    int click_at;
    bool naming;                      /* typing a save's name */
    char name[SAVE_NAME_MAX + 1];
} loadsave;

typedef enum loadsave_result {
    LOADSAVE_NOTHING,
    LOADSAVE_CLOSED,
    LOADSAVE_DO_LOAD,                 /* load slot (originals: the original's) */
    LOADSAVE_DO_SAVE,                 /* save into slot as name */
} loadsave_result;

void loadsave_open(loadsave *ls, const loadsave_env *env, loadsave_mode mode);

/* An event; x, y in the 640x480 screen. Text input is on while naming. */
loadsave_result loadsave_event(loadsave *ls, const loadsave_env *env, const SDL_Event *e, int x, int y);

void loadsave_draw(const loadsave *ls, const loadsave_env *env, bs_overlay *o);

#endif
