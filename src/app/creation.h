#ifndef OPENBATTLESPIRE_CREATION_H
#define OPENBATTLESPIRE_CREATION_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#include "bs/assets/assets.h"
#include "bs/core/vfs.h"
#include "bs/formats/exe_text.h"
#include "bs/formats/font.h"
#include "bs/formats/rsc.h"
#include "bs/formats/save.h"
#include "bs/formats/spells.h"
#include "bs/game/creation.h"
#include "bs/render/overlay.h"
#include "loadsave.h"
#include "menus.h"

/*
 * Making a character (GAME.EXE 0x31B50; docs/re/README.md, "Character creation"): Single Player's
 * screens, each a full-screen image with buttons.
 *
 * - Select Scenario (mult02.bsi): Load Game, New Game, Back.
 * - Load Game (load00.bsi, 0x325B9): the ten save slots' pictures and names, a frame (brdr01.bsi) sliding to
 *   the one chosen; Load or a double click loads it, if there's a save there. Ours first; Page Down shows
 *   the original game's (the install's SAVE0 .. SAVE9), Page Up ours again.
 * - The name (name00.bsi), typed in.
 * - Male or female (gender.bsi).
 * - The race (char13.bsi): six pictures, a frame sliding to the one chosen (brdr00.bsi), its description
 *   below; a double click takes it.
 * - The face (face00.bsi): hair, eyes and mouth stepped through the pictures there are for the sex and
 *   race, or picked at random, on the character.
 * - The class (clas00.bsi): an existing one (clas01.bsi, a list of GAMEDATA/CLASSnnn.SCL) or a new one,
 *   either then changed in the CharacterMaker (char00.bsi) with build points (bs_creation): the class's
 *   name, wounds, attributes, its twelve skills (picked from plst00.bsi), and its pages: spells (char02,
 *   char03), equipment and magic items bought one at a time (char05, char07), advantages and
 *   disadvantages (char08 with char09's ticks and char10's greyed out choices). Saving the class isn't
 *   there yet.
 *
 * Every screen's help button shows its help page (the menus' help, help00 .. help10), as entering it does
 * while help pops up by itself.
 */

typedef enum creation_screen {
    CREATION_NONE,
    CREATION_SCENARIO,
    CREATION_NAME,
    CREATION_GENDER,
    CREATION_RACE,
    CREATION_FACE,
    CREATION_CLASS,
    CREATION_CLASS_LIST,
    CREATION_EDITOR,
    CREATION_LOAD_GAME,
} creation_screen;

/* The CharacterMaker's pages, by their background charNN.bsi. */
typedef enum creation_page {
    PAGE_MAIN = 0,
    PAGE_SPELLS = 2,
    PAGE_SPELL_PICK = 3,
    PAGE_EQUIPMENT = 5,
    PAGE_MAGIC = 7,
    PAGE_ADVANTAGES = 8,
} creation_page;

typedef enum creation_action {
    CREATION_NOTHING,
    CREATION_TO_TITLE,        /* Back from Select Scenario */
    CREATION_LOAD,            /* load the saved game in slot load_slot (load_originals: the original game's) */
    CREATION_PLAY,            /* the character is made */
} creation_action;

typedef struct creation_env {
    bs_assets *assets;
    const bs_vfs *vfs;
    const bs_font *fonts[MENU_FONTS];
    const bs_rsc *rsc;
    const bs_exe_text *text;
    const char *skill_names[BS_SKILL_COUNT];   /* 0xE34E9 */
    const char *attribute_names[8];            /* "STR" .. "LCK" */
    const char *armour_names[4];               /* "No armor allowed" .. "Heavy Armor" (0xE84D5) */
    const bs_spell *spells;                    /* the table, BS_SPELL_COUNT; NULL: none */
    const bs_item_db *db;                      /* NULL tables: no shops */
    const char *const *pictures;               /* every BSI name, in the archive's order */
    uint32_t picture_count;
    bs_rng *rng;
    uint64_t now_ms;
    menus *help;                               /* its help pages */
    const char *save_root;                     /* the install, holding SAVE0 .. SAVE9; NULL: none */
    const char *saves_dir;                     /* ours (loadsave.h); NULL: none */
    void *ctx;
    void (*message)(void *ctx, const char *text);
    void (*sound)(void *ctx, int32_t id);
    /* An item's two lines as the inventory shows them (0x4F37D). */
    void (*item_lines)(void *ctx, const bs_item *it, char *l1, char *l2, size_t cap);
} creation_env;

#define CREATION_CLASSES 128
#define CREATION_SAVES SAVE_SLOTS
#define CREATION_FACE_PARTS 64

typedef struct creation_class_file {
    char file[24];                    /* "GAMEDATA/CLASS000.SCL" */
    char line[48];                    /* "Acrobat (1)" */
} creation_class_file;

typedef struct creation {
    creation_screen screen;
    creation_page page;
    /* The character. */
    char name[32];
    bool female;
    int race;                         /* 0 Redguard, Breton, Nord, High Elf, Dark Elf, 5 Wood Elf */
    int parts[3][CREATION_FACE_PARTS];        /* hair, eyes, mouth: the pictures' numbers */
    int part_count[3], part_at[3];
    /* Existing classes. */
    creation_class_file classes[CREATION_CLASSES];
    int class_count, class_selected, class_scroll;
    /* The class being made. */
    bs_creation c;
    bs_creation_shop shops[2];        /* equipment, magic items */
    uint32_t shop_at[2];
    bs_item others[BS_SCL_MAX_ITEMS]; /* a class's items not bought with it */
    uint32_t other_count;
    bool naming_class;                /* typing the class's name */
    int skill_slot;                   /* the skill list's slot (plst00.bsi), or -1 */
    int skill_choices[BS_SCL_SKILLS], skill_choice_count;
    int list_selected, list_scroll;   /* the open list: skills to pick, spells known or to add */
    int offered[BS_SPELL_COUNT], offered_count;
    uint32_t added_now;               /* spells added since the add page opened (Cancel takes them back) */
    int32_t added_cost;
    bool confirm;                     /* Cancel's "Are you sure?" */
    /* Holding a step button repeats it every 300 ms (0x387A1). */
    int held;                         /* the button held, or -1 */
    int held_x, held_y;
    bool held_shift;
    uint64_t step_ms;
    /* The race's frame slides from where it was. */
    int slide_from;
    uint64_t slide_ms;
    /* The load screen: each slot's name and picture (0x31104), ours or the original game's. */
    save_slot saves[CREATION_SAVES];
    bool load_originals;
    int load_slot, load_from;         /* the slot chosen, and where its frame slides from */
    uint64_t load_slide_ms;
    uint64_t click_ms;                /* double clicks */
    int click_at;
} creation;

/* Starts at Select Scenario. */
void creation_start(creation *cr, const creation_env *env);

/* A key, text or mouse event; mouse positions in 640x480 screen pixels (x, y). */
creation_action creation_event(creation *cr, const creation_env *env, const SDL_Event *e, int x, int y);

/* Steps a held button again when it's time. */
void creation_update(creation *cr, const creation_env *env);

/* The screen's background image, drawn under the overlay. */
const char *creation_background(const creation *cr);

/* The item a shop shows turning in its window, or NULL; r: x0, y0, x1, y1. */
const bs_item *creation_shown_item(const creation *cr, int r[4]);

/* Draws everything over the background. */
void creation_draw(const creation *cr, const creation_env *env, bs_overlay *o);

/* Whether text is being typed (the name, the class's name). */
bool creation_typing(const creation *cr);

/* The race's two letters for picture names ("rd" .. "wd"). */
const char *creation_race_code(int race);

#endif
