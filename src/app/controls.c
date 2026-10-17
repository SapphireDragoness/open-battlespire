#include "controls.h"

#include <string.h>

static void bind(controls *c, action a, SDL_Scancode k0, SDL_Scancode k1, SDL_Scancode k2)
{
    c->keys[a][0] = k0;
    c->keys[a][1] = k1;
    c->keys[a][2] = k2;
}

void controls_set(controls *c, bool original)
{
    memset(c, 0, sizeof *c);
    c->original = original;
    const SDL_Scancode none = SDL_SCANCODE_UNKNOWN;
    if (original) {
        bind(c, ACT_FORWARD, SDL_SCANCODE_UP, none, none);
        bind(c, ACT_BACK, SDL_SCANCODE_DOWN, none, none);
        bind(c, ACT_LEFT, SDL_SCANCODE_LEFT, none, none);
        bind(c, ACT_RIGHT, SDL_SCANCODE_RIGHT, none, none);
        bind(c, ACT_TURN_LEFT, SDL_SCANCODE_DELETE, none, none);
        bind(c, ACT_TURN_RIGHT, SDL_SCANCODE_END, none, none);
        bind(c, ACT_LOOK_UP, SDL_SCANCODE_INSERT, none, none);
        bind(c, ACT_LOOK_DOWN, SDL_SCANCODE_HOME, none, none);
        bind(c, ACT_JUMP, SDL_SCANCODE_LALT, SDL_SCANCODE_RALT, none);
        bind(c, ACT_MOVE_UP, SDL_SCANCODE_PAGEUP, none, none);
        bind(c, ACT_MOVE_DOWN, SDL_SCANCODE_PAGEDOWN, none, none);
        bind(c, ACT_SPECIAL, SDL_SCANCODE_SPACE, none, none);
        bind(c, ACT_SNEAK, SDL_SCANCODE_S, none, none);
        bind(c, ACT_INVENTORY, SDL_SCANCODE_F10, none, none);
        bind(c, ACT_FLY, SDL_SCANCODE_F, none, none);
        bind(c, ACT_SWITCH_WEAPON, SDL_SCANCODE_A, none, none);
        bind(c, ACT_LEAVE, SDL_SCANCODE_D, none, none);
    } else {
        bind(c, ACT_FORWARD, SDL_SCANCODE_W, SDL_SCANCODE_UP, none);
        bind(c, ACT_BACK, SDL_SCANCODE_S, SDL_SCANCODE_DOWN, none);
        bind(c, ACT_LEFT, SDL_SCANCODE_A, SDL_SCANCODE_LEFT, none);
        bind(c, ACT_RIGHT, SDL_SCANCODE_D, SDL_SCANCODE_RIGHT, none);
        bind(c, ACT_JUMP, SDL_SCANCODE_SPACE, none, none);
        bind(c, ACT_MOVE_UP, SDL_SCANCODE_PAGEUP, SDL_SCANCODE_R, none);
        bind(c, ACT_MOVE_DOWN, SDL_SCANCODE_PAGEDOWN, SDL_SCANCODE_V, none);
        bind(c, ACT_DUCK, SDL_SCANCODE_C, none, none);
        bind(c, ACT_SNEAK, SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL, none);
        bind(c, ACT_USE, SDL_SCANCODE_E, none, none);
        bind(c, ACT_INVENTORY, SDL_SCANCODE_I, SDL_SCANCODE_F10, none);
        bind(c, ACT_FLY, SDL_SCANCODE_F, none, none);
        bind(c, ACT_SWITCH_WEAPON, SDL_SCANCODE_Q, none, none);
        bind(c, ACT_LEAVE, SDL_SCANCODE_X, none, none);
    }
    bind(c, ACT_SHEET, SDL_SCANCODE_F9, none, none);
    bind(c, ACT_AUTOMAP, SDL_SCANCODE_F11, none, none);
    c->mouse[ACT_USE] = SDL_BUTTON_LMASK;
    c->mouse[ACT_ATTACK] = SDL_BUTTON_RMASK;
}

bool controls_held(const controls *c, action a)
{
    const bool *k = SDL_GetKeyboardState(NULL);
    for (int i = 0; i < CONTROLS_KEYS; i++)
        if (c->keys[a][i] != SDL_SCANCODE_UNKNOWN && k[c->keys[a][i]])
            return true;
    return c->mouse[a] && (SDL_GetMouseState(NULL, NULL) & c->mouse[a]);
}

bool controls_pressed(const controls *c, action a, const SDL_Event *e)
{
    if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
        for (int i = 0; i < CONTROLS_KEYS; i++)
            if (c->keys[a][i] != SDL_SCANCODE_UNKNOWN && e->key.scancode == c->keys[a][i])
                return true;
    }
    if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN && c->mouse[a])
        return (SDL_BUTTON_MASK(e->button.button) & c->mouse[a]) != 0;
    return false;
}
