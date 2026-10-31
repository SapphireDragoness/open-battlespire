#ifndef OPENBATTLESPIRE_CONTROLS_H
#define OPENBATTLESPIRE_CONTROLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#include "bs/core/ini.h"
#include "bs/formats/exe_text.h"

/*
 * Key and mouse bindings, and the Controls screen's other settings (docs/re/README.md, "The Controls
 * screen").
 *
 * The actions are the game's 26, in its key table's order (0x16C2B8), then two of the engine's own. Each
 * has up to three bindings: an SDL scancode, or a mouse button as minus its number (CONTROLS_MOUSE_*).
 * The first is the one the Controls screen shows and sets. Two layouts:
 * - original: the game's defaults (0xE8229): arrows move, Alt jumps, Delete/End turn, Insert/Home look,
 *   Page Up/Down move up and down, S sneaks, Space with the mouse pulled back ducks, the left button
 *   selects (uses), the right swings the weapon, F9-F12 open the screens, D leaves a boat or the
 *   balloon, L the lantern.
 * - modern: W A S D and mouse look, Space jumps, C ducks, Ctrl sneaks, E or the left button uses, I or
 *   F10 opens the inventory; the arrows also move.
 * Both start with the mouse looking around (the original's "View" movement); the Controls screen's
 * "Cursor" leaves the mouse free and steers by the pointer instead (bs_cursor_zone).
 */

typedef enum action {
    ACT_INVENTORY, ACT_AUTOMAP, ACT_SHEET, ACT_CHAT_BAR, ACT_CHAT_ROOM, ACT_FULL_VIEW, ACT_LEAVE,
    ACT_SWITCH_WEAPON,
    ACT_SPECIAL,   /* held with mouse movement: back ducks (forward shoves, not implemented) */
    ACT_ATTACK,    /* held: moving the mouse swings the weapon */
    ACT_SNEAK,     /* toggles */
    ACT_SPELLBOOK,
    ACT_FORWARD, ACT_BACK, ACT_LEFT, ACT_RIGHT, ACT_JUMP,
    ACT_TURN_RIGHT, ACT_TURN_LEFT, ACT_LOOK_UP, ACT_LOOK_DOWN, ACT_MOVE_UP, ACT_MOVE_DOWN,
    ACT_CURSOR,    /* held: the cursor shows, to click the bar */
    ACT_USE,       /* the game's "Select" */
    ACT_LANTERN,   /* the player's light on or off (+0x227 bit 4, 0x41189) */
    ACT_GAME_COUNT,
    ACT_DUCK = ACT_GAME_COUNT,  /* modern: held */
    ACT_FLY,                    /* not in the original: free flight */
    ACT_COUNT
} action;

#define CONTROLS_KEYS 3
#define CONTROLS_MOUSE_LEFT (-1)       /* the game's M1 */
#define CONTROLS_MOUSE_RIGHT (-2)      /* M2 */
#define CONTROLS_MOUSE_MIDDLE (-3)     /* M3 */

/* The Controls screen's ranges (0x1F54C .. 0x1F7D5). */
#define CONTROLS_SENSITIVITY_MIN 0.01f
#define CONTROLS_SENSITIVITY_SPAN 3.0f
#define CONTROLS_DOUBLE_CLICK_MIN 8    /* ticks of 18.2 a second */
#define CONTROLS_DOUBLE_CLICK_SPAN 10
#define CONTROLS_TURN_RATE_MIN 10
#define CONTROLS_TURN_RATE_MAX 256
#define CONTROLS_TURN_RATE_START 128   /* a turn rate of 0 becomes this when a level starts (0x12BDE) */

typedef struct controls {
    int32_t bind[ACT_COUNT][CONTROLS_KEYS];  /* scancode > 0, mouse button < 0, 0 none */
    bool original;
    float sensitivity_x, sensitivity_y;      /* mouse look's speed: 0.01 .. 3.01 (0x16C320, 0x16C324) */
    bool reverse;                            /* Mouse Reverse: pulling the mouse back looks up (0x16C328) */
    int double_click;                        /* the double-click time in ticks, 8 .. 18 (0x16C329) */
    uint8_t turn_rate;                       /* keyboard turning: 6 x it angle units (2048 a turn) a second;
                                                the player's +0x2E0, a byte, so the slider's 256 is 0 */
    bool cursor_movement;                    /* Movement "Cursor" rather than "View" (+0x226 bit 0x80) */
    bool click_swing;                        /* Weapon Swing "Click": a press swings a weapon in a random
                                                direction, instead of the mouse's drag ("Classic"); an
                                                addition, not in the game */
} controls;

/* A layout's bindings and the default settings. */
void controls_set(controls *c, bool original);

/* The Controls screen's Default (0x1F703): the layout's bindings, sensitivity 1, Mouse Reverse off, the
 * double-click time 8. The turn rate, the movement and the weapon swing stay. */
void controls_default(controls *c);

/* Whether an action's key or button is held now. */
bool controls_held(const controls *c, action a);

/* Whether an event presses an action's key or button. */
bool controls_pressed(const controls *c, action a, const SDL_Event *e);

/* A key or mouse button press as a binding (0 if it is neither). */
int32_t controls_code(const SDL_Event *e);

/* Whether the game keeps a key for itself: Esc and F1-F8 (0xE8291). */
bool controls_reserved(int32_t code);

/* Whether an action's first binding is also another of the game's actions' (0x200AE). */
bool controls_duplicate(const controls *c, action a);
bool controls_any_duplicate(const controls *c);

/* The name the Controls screen shows: the game's name for the key's scan code (keys: bs_exe_key_names),
 * else SDL's; M1-M3 for the mouse. */
const char *controls_name(int32_t code, const bs_exe_text *keys, char *buf, size_t cap);

/* The double-click time in ms. */
uint32_t controls_double_click_ms(const controls *c);

/* Keyboard turning and looking in radians a second (0x3F19F): the turn rate x 6 angle units. */
float controls_turn_speed(const controls *c);

/* The bindings and settings as a settings file's [controls] section, and back. Reading keeps what a
 * section leaves out. */
void controls_write(const controls *c, bs_ini_writer *w);
void controls_read(controls *c, const bs_ini_section *s);

#endif
