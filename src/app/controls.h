#ifndef OPENBATTLESPIRE_CONTROLS_H
#define OPENBATTLESPIRE_CONTROLS_H

#include <stdbool.h>

#include <SDL3/SDL.h>

/*
 * Key and mouse bindings. Two layouts:
 * - original: the game's defaults (UESP, "Battlespire:Controls"): arrows move,
 *   Alt jumps, Delete/End turn, Insert/Home look, Page Up/Down move up and
 *   down, S sneaks, Space with the mouse pulled back ducks, the left button
 *   uses.
 * - modern: W A S D and mouse look, Space jumps, C ducks, Ctrl sneaks, E or
 *   the left button uses.
 * Both keep the mouse looking around (the original's "View" mode).
 */

typedef enum action {
    ACT_FORWARD, ACT_BACK, ACT_LEFT, ACT_RIGHT,
    ACT_TURN_LEFT, ACT_TURN_RIGHT, ACT_LOOK_UP, ACT_LOOK_DOWN,
    ACT_JUMP, ACT_MOVE_UP, ACT_MOVE_DOWN,
    ACT_SPECIAL,   /* original: held with mouse movement: back ducks (forward shoves, not implemented) */
    ACT_DUCK,      /* modern: held */
    ACT_SNEAK,     /* toggles */
    ACT_USE,
    ACT_FLY,       /* not in the original: free flight */
    ACT_COUNT
} action;

#define CONTROLS_KEYS 3

typedef struct controls {
    SDL_Scancode keys[ACT_COUNT][CONTROLS_KEYS];  /* SDL_SCANCODE_UNKNOWN = unused */
    SDL_MouseButtonFlags mouse[ACT_COUNT];        /* mouse buttons, 0 = none */
    bool original;
} controls;

void controls_set(controls *c, bool original);

/* Whether an action's key or button is held now. */
bool controls_held(const controls *c, action a);

/* Whether an event presses an action's key or button. */
bool controls_pressed(const controls *c, action a, const SDL_Event *e);

#endif
