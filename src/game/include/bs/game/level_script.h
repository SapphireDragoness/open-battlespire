#ifndef BS_GAME_LEVEL_SCRIPT_H
#define BS_GAME_LEVEL_SCRIPT_H

#include <stdbool.h>
#include <stdint.h>

#include "bs/game/items.h"
#include "bs/game/use.h"

/*
 * Keys, sigils and gems (GAME.EXE; see docs/re/README.md, "Keys, sigils and
 * gems").
 *
 * Keys are the levels' own scripts (0x3E578), by level number and object
 * model name: level 1 takes Cogs at objects with BS6 property 103 and level
 * 2 Voidguides, each advancing a counter texture until the last opens the
 * way; level 3's gate takes three control rods once its two buttons are
 * pressed; level 6's temple three Gatekeys; level 5's temple door six.
 *
 * Sigils are objects whose model is "sigil" + a letter: the player can't
 * pass one (it hurts) unless carrying the Sigil Amulet "... Sigil of Entry"
 * of that letter (9001 'a' .. 9026 'z'), which makes it vanish (0x5B4B8).
 *
 * Gems are objects with property 110 (health) or 111 (spell points): using
 * one adds that percentage of the maximum (or takes it, if negative); it is
 * then gone for a minute (0x4D64C).
 */

#define BS_SCRIPT_MESSAGES 2

typedef struct bs_script_result {
    bool activate;            /* send the object its use event */
    bool locked;              /* the object's own use event must not be sent (+0x13 bit 8) */
    uint32_t messages[BS_SCRIPT_MESSAGES];
    uint32_t message_count;
    int counter;              /* >= 0: show this image of the level's counter sequence */
} bs_script_result;

typedef struct bs_level_script {
    int level_number;
    uint8_t progress;         /* player record +0x25E: what has been placed */
    bool done;                /* 0x16FFB8 */
} bs_level_script;

/* The counter images of a level (level 1: 1cnt01..06, level 2: alta01..05), or 0. */
uint32_t bs_level_script_counter(int level_number, const char *const **names);

void bs_level_script_init(bs_level_script *s, int level_number);

/* Asks whether a level object (by model name) has been pressed or moved, for the level 3 gate's buttons. */
typedef bool (*bs_script_moved)(void *ctx, const char *model);

/*
 * The player uses an object: model is its model name, locked whether it has
 * property 103. Takes what the script needs from the inventory.
 */
bs_script_result bs_level_script_use(bs_level_script *s, bs_items *it, const char *model, bool locked,
                                     bs_script_moved moved, void *ctx);

/* ---- level exits ---- */

/* What the exits look at (0x3E578 called with no object, each frame the player stands on one, 0x40511). */
typedef struct bs_exit_query {
    const char *floor_model;     /* model of the level object stood on, or NULL */
    bool floor_locked;           /* it has property 103 */
    const char *floor_texture;   /* texture of the floor face stood on ("" if none) */
    uint32_t doorpans_open;      /* level 1: 1doorpan objects whose linked doors have opened (0x3EBB9) */
} bs_exit_query;

/*
 * Whether the level is complete: level 1 on the 1zoom pad once the panel
 * took its Cogs and five door panels are open; level 2 on a floor of
 * texture port03 once the altar took its Voidguides; level 3 on the l2a46
 * object's floor of texture black; levels 4 and 5 on an object with
 * property 103 (the exit gates); level 6 the same once its three keys are
 * placed.
 */
bool bs_level_script_exit(const bs_level_script *s, const bs_exit_query *q);

/* The level number after n (0x26997): n + 1, skipping 7; 0 after the last (8). */
static inline int bs_next_level_number(int n)
{
    if (n >= 8 || n < 1)
        return 0;
    return n + 1 == 7 ? 8 : n + 1;
}

/* ---- sigils ---- */

typedef struct bs_sigil {
    uint32_t object;          /* level object */
    char letter;              /* 'a'..'z' */
    float pos[3];
    bool gone;                /* dispelled: moved out of the way (y = 10000 in the game) */
    uint32_t last_hurt;       /* the frame it last hurt the player (0x5B6AB) */
} bs_sigil;

/* Whether a model name is a sigil's, and its letter. */
bool bs_sigil_model(const char *model, char *letter);

/* Whether the player carries the Sigil Amulet of Entry for a letter (0x5BCB5). */
bool bs_items_have_sigil(const bs_items *it, char letter);

typedef enum bs_sigil_event { BS_SIGIL_NONE, BS_SIGIL_BLOCKED, BS_SIGIL_OPENED } bs_sigil_event;

/*
 * The player at feet position pos, in frame number frame (0x5B4B8): a sigil
 * within 300 units across and 160 up or down vanishes if the player has its
 * amulet (OPENED, *which set); otherwise within 120 across it blocks
 * (BLOCKED), and *hurt says whether it hurts this time (not again in the
 * next frame).
 */
bs_sigil_event bs_sigils_check(bs_sigil *sigils, uint32_t count, const bs_items *it, const float pos[3],
                               uint32_t frame, uint32_t *which, bool *hurt);

#define BS_SIGIL_DAMAGE 30          /* 0x5BC98 */

/* ---- gems ---- */

#define BS_GEM_RECHARGE_MS 60000    /* 0x4D7E9 */

/*
 * A gem used (0x4D64C): health (property 110) or spell points (111) change by
 * percent of their maximum, kept within 0..maximum. Returns the change.
 */
int32_t bs_gem_use(bs_character *c, bool spell_points, int32_t percent);

#endif
