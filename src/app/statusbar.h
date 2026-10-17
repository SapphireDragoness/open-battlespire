#ifndef OPENBATTLESPIRE_STATUSBAR_H
#define OPENBATTLESPIRE_STATUSBAR_H

#include <stdbool.h>
#include <stdint.h>

#include "bs/assets/assets.h"
#include "bs/formats/bsi.h"
#include "bs/game/use.h"
#include "bs/render/overlay.h"

/*
 * The interface bar across the bottom of the screen (GAME.EXE 0x2E57C; docs/re/README.md, "The
 * interface bar"): panel.bsi over the bottom 100 lines of the view, with the character's face, the
 * health (blue, engy01.bsi) and spell point (yellow, engy02.bsi) vials, four buttons (spell book,
 * inventory, automap, options) and the compass (comp00.bsi, turning with the player). The face is
 * a button for the character sheet, the compass one for the hot key list, and a "?" one for help.
 *
 * Tab shows the full view (0x2ECA3): only the two vials, at the left, and the compass in its own
 * ring (comp01.bsi).
 */

typedef enum statusbar_button {
    STATUSBAR_NONE = -1,
    STATUSBAR_SHEET,
    STATUSBAR_SPELLBOOK,
    STATUSBAR_INVENTORY,
    STATUSBAR_AUTOMAP,
    STATUSBAR_OPTIONS,
    STATUSBAR_HOTKEYS,
    STATUSBAR_HELP,
    STATUSBAR_BUTTONS,
} statusbar_button;

#define STATUSBAR_FACE_LAYERS 32

typedef struct statusbar {
    const bs_bsi *panel;                       /* the bar, its left end, the four buttons */
    const bs_bsi_image *health, *spell_points; /* 32 frames, full to empty */
    const bs_bsi_image *compass, *compass_full;  /* 36 frames, 10 degrees apart */
    const bs_bsi_image *face[STATUSBAR_FACE_LAYERS]; /* the paper doll's images, bottom first */
    const bs_bsi_image *face_frame;            /* fram00.bsi, over the face */
    int face_count;
    int face_dx, face_dy;                      /* the face's offset into the bar */
    bool full_view;
} statusbar;

/* The bar's pictures. */
void statusbar_load(statusbar *s, bs_assets *a);

/* The face: the paper doll's images, bottom first (inventory_doll_images), cut out around its eyes
 * image (0x5678A): what the character wears shows too, a helmet instead of the hair. */
void statusbar_set_face(statusbar *s, const bs_bsi_image *const *layers, int count, const bs_bsi_image *eyes);

/* Draws the bar (or the full view's vials and compass) into the overlay; yaw is the player's
 * (2048 a turn). */
void statusbar_draw(const statusbar *s, bs_overlay *o, const bs_character *c, int32_t yaw);

/* The button at a screen point (640x480), or STATUSBAR_NONE. */
statusbar_button statusbar_at(const statusbar *s, int x, int y);

/* Whether a screen point is on the bar (it hides the view there). */
bool statusbar_covers(const statusbar *s, int x, int y);

#endif
