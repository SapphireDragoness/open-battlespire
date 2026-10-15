#ifndef OPENBATTLESPIRE_SPELLBOOK_H
#define OPENBATTLESPIRE_SPELLBOOK_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#include "bs/formats/bsi.h"
#include "bs/formats/exe_text.h"
#include "bs/formats/font.h"
#include "bs/game/use.h"
#include "bs/render/overlay.h"

/*
 * The spell book (F12; GAME.EXE 0x2988B, docs/formats/spells.md, "The spell
 * book"): spmenu.bsi across the bottom of the screen, the view going on above
 * it. On the left the known spells ("cost - name"); for the one selected its
 * icon, delivery and element (each click changes them, 0x29A36 ..), the hot
 * key (F1-F8 pressed while the book is open), the detonation, and its cost,
 * duration and magnitude; Cast casts it and closes the book.
 */

typedef struct spellbook_env {
    bs_character *character;
    int level_number;                 /* for the magnitude shown */
    bs_overlay *overlay;
    const bs_font *font;              /* FONT0001: the values */
    const bs_font *list_font;         /* FONT0003: the list */
    const bs_exe_text *text;
    const bs_bsi *panel;              /* spmenu.bsi: 11 images */
    const bs_bsi_image *icons[27];    /* symb00-26 */
    const bs_bsi_image *deliveries[BS_DELIVERY_COUNT];  /* dlvr00-04 */
    const bs_bsi_image *elements[BS_ELEMENT_COUNT];     /* elmt00-04 */
    void (*message)(void *ctx, const char *text);
    void (*cast)(void *ctx, const bs_spell *s);   /* the Cast button */
    void *ctx;
} spellbook_env;

typedef struct spellbook {
    bool open;
    int selected;                     /* a spell's number, 0 none */
    int scroll;                       /* the list's first row */
} spellbook;

/* Opens the book; false and "You dont have any spells." if none are known. */
bool spellbook_open(spellbook *b, const spellbook_env *env);

/* Handles an event; mouse coordinates in window pixels. Returns false when the book closed. */
bool spellbook_event(spellbook *b, const spellbook_env *env, const SDL_Event *e, int window_w, int window_h);

/* Draws the book into the overlay. */
void spellbook_draw(const spellbook *b, const spellbook_env *env);

/* Whether a window point (pixels) is on the book's panel. */
bool spellbook_covers(int window_w, int window_h, float x, float y);

#endif
