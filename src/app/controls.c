#include "controls.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Names for the actions in the settings file. */
static const char *const action_keys[ACT_COUNT] = {
    "inventory", "automap", "char_sheet", "chat_bar", "chat_room", "fullscreen", "exit_vehicle",
    "switch_weapon", "special_moves", "swing_weapon", "sneak", "spellbook", "forward", "backward", "left",
    "right", "jump", "turn_right", "turn_left", "look_up", "look_down", "move_up", "move_down", "cursor",
    "select", "lantern", "duck", "fly",
};

/* PC scan codes (set 1, what the game's key names go by) for SDL's scancodes. The grey keys share the
 * keypad's codes, as the game sees them. */
static const struct {
    SDL_Scancode sdl;
    uint8_t pc;
} pc_codes[] = {
    {SDL_SCANCODE_ESCAPE, 0x01}, {SDL_SCANCODE_1, 0x02}, {SDL_SCANCODE_2, 0x03}, {SDL_SCANCODE_3, 0x04},
    {SDL_SCANCODE_4, 0x05}, {SDL_SCANCODE_5, 0x06}, {SDL_SCANCODE_6, 0x07}, {SDL_SCANCODE_7, 0x08},
    {SDL_SCANCODE_8, 0x09}, {SDL_SCANCODE_9, 0x0A}, {SDL_SCANCODE_0, 0x0B}, {SDL_SCANCODE_MINUS, 0x0C},
    {SDL_SCANCODE_EQUALS, 0x0D}, {SDL_SCANCODE_BACKSPACE, 0x0E}, {SDL_SCANCODE_TAB, 0x0F},
    {SDL_SCANCODE_Q, 0x10}, {SDL_SCANCODE_W, 0x11}, {SDL_SCANCODE_E, 0x12}, {SDL_SCANCODE_R, 0x13},
    {SDL_SCANCODE_T, 0x14}, {SDL_SCANCODE_Y, 0x15}, {SDL_SCANCODE_U, 0x16}, {SDL_SCANCODE_I, 0x17},
    {SDL_SCANCODE_O, 0x18}, {SDL_SCANCODE_P, 0x19}, {SDL_SCANCODE_LEFTBRACKET, 0x1A},
    {SDL_SCANCODE_RIGHTBRACKET, 0x1B}, {SDL_SCANCODE_RETURN, 0x1C}, {SDL_SCANCODE_KP_ENTER, 0x1C},
    {SDL_SCANCODE_LCTRL, 0x1D}, {SDL_SCANCODE_RCTRL, 0x1D}, {SDL_SCANCODE_A, 0x1E}, {SDL_SCANCODE_S, 0x1F},
    {SDL_SCANCODE_D, 0x20}, {SDL_SCANCODE_F, 0x21}, {SDL_SCANCODE_G, 0x22}, {SDL_SCANCODE_H, 0x23},
    {SDL_SCANCODE_J, 0x24}, {SDL_SCANCODE_K, 0x25}, {SDL_SCANCODE_L, 0x26}, {SDL_SCANCODE_SEMICOLON, 0x27},
    {SDL_SCANCODE_APOSTROPHE, 0x28}, {SDL_SCANCODE_GRAVE, 0x29}, {SDL_SCANCODE_LSHIFT, 0x2A},
    {SDL_SCANCODE_BACKSLASH, 0x2B}, {SDL_SCANCODE_Z, 0x2C}, {SDL_SCANCODE_X, 0x2D}, {SDL_SCANCODE_C, 0x2E},
    {SDL_SCANCODE_V, 0x2F}, {SDL_SCANCODE_B, 0x30}, {SDL_SCANCODE_N, 0x31}, {SDL_SCANCODE_M, 0x32},
    {SDL_SCANCODE_COMMA, 0x33}, {SDL_SCANCODE_PERIOD, 0x34}, {SDL_SCANCODE_SLASH, 0x35},
    {SDL_SCANCODE_KP_DIVIDE, 0x35}, {SDL_SCANCODE_RSHIFT, 0x36}, {SDL_SCANCODE_KP_MULTIPLY, 0x37},
    {SDL_SCANCODE_LALT, 0x38}, {SDL_SCANCODE_RALT, 0x38}, {SDL_SCANCODE_SPACE, 0x39},
    {SDL_SCANCODE_CAPSLOCK, 0x3A}, {SDL_SCANCODE_F1, 0x3B}, {SDL_SCANCODE_F2, 0x3C}, {SDL_SCANCODE_F3, 0x3D},
    {SDL_SCANCODE_F4, 0x3E}, {SDL_SCANCODE_F5, 0x3F}, {SDL_SCANCODE_F6, 0x40}, {SDL_SCANCODE_F7, 0x41},
    {SDL_SCANCODE_F8, 0x42}, {SDL_SCANCODE_F9, 0x43}, {SDL_SCANCODE_F10, 0x44},
    {SDL_SCANCODE_NUMLOCKCLEAR, 0x45}, {SDL_SCANCODE_SCROLLLOCK, 0x46}, {SDL_SCANCODE_HOME, 0x47},
    {SDL_SCANCODE_KP_7, 0x47}, {SDL_SCANCODE_UP, 0x48}, {SDL_SCANCODE_KP_8, 0x48},
    {SDL_SCANCODE_PAGEUP, 0x49}, {SDL_SCANCODE_KP_9, 0x49}, {SDL_SCANCODE_KP_MINUS, 0x4A},
    {SDL_SCANCODE_LEFT, 0x4B}, {SDL_SCANCODE_KP_4, 0x4B}, {SDL_SCANCODE_KP_5, 0x4C},
    {SDL_SCANCODE_RIGHT, 0x4D}, {SDL_SCANCODE_KP_6, 0x4D}, {SDL_SCANCODE_KP_PLUS, 0x4E},
    {SDL_SCANCODE_END, 0x4F}, {SDL_SCANCODE_KP_1, 0x4F}, {SDL_SCANCODE_DOWN, 0x50}, {SDL_SCANCODE_KP_2, 0x50},
    {SDL_SCANCODE_PAGEDOWN, 0x51}, {SDL_SCANCODE_KP_3, 0x51}, {SDL_SCANCODE_INSERT, 0x52},
    {SDL_SCANCODE_KP_0, 0x52}, {SDL_SCANCODE_DELETE, 0x53}, {SDL_SCANCODE_KP_PERIOD, 0x53},
    {SDL_SCANCODE_F11, 0x57}, {SDL_SCANCODE_F12, 0x58},
};

static void bind(controls *c, action a, int32_t k0, int32_t k1, int32_t k2)
{
    c->bind[a][0] = k0;
    c->bind[a][1] = k1;
    c->bind[a][2] = k2;
}

static void layout(controls *c)
{
    memset(c->bind, 0, sizeof c->bind);
    bind(c, ACT_SHEET, SDL_SCANCODE_F9, 0, 0);
    bind(c, ACT_AUTOMAP, SDL_SCANCODE_F11, 0, 0);
    bind(c, ACT_SPELLBOOK, SDL_SCANCODE_F12, 0, 0);
    bind(c, ACT_CHAT_BAR, SDL_SCANCODE_T, 0, 0);
    bind(c, ACT_FULL_VIEW, SDL_SCANCODE_TAB, 0, 0);
    bind(c, ACT_ATTACK, CONTROLS_MOUSE_RIGHT, 0, 0);
    bind(c, ACT_CURSOR, SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER, 0);
    bind(c, ACT_LANTERN, SDL_SCANCODE_L, 0, 0);
    bind(c, ACT_MOVE_UP, SDL_SCANCODE_PAGEUP, 0, 0);
    bind(c, ACT_MOVE_DOWN, SDL_SCANCODE_PAGEDOWN, 0, 0);
    bind(c, ACT_FLY, SDL_SCANCODE_F, 0, 0);
    if (c->original) {
        /* 0xE8229 */
        bind(c, ACT_INVENTORY, SDL_SCANCODE_F10, 0, 0);
        bind(c, ACT_CHAT_ROOM, SDL_SCANCODE_C, 0, 0);
        bind(c, ACT_LEAVE, SDL_SCANCODE_D, 0, 0);
        bind(c, ACT_SWITCH_WEAPON, SDL_SCANCODE_A, 0, 0);
        bind(c, ACT_SPECIAL, SDL_SCANCODE_SPACE, 0, 0);
        bind(c, ACT_SNEAK, SDL_SCANCODE_S, 0, 0);
        bind(c, ACT_FORWARD, SDL_SCANCODE_UP, 0, 0);
        bind(c, ACT_BACK, SDL_SCANCODE_DOWN, 0, 0);
        bind(c, ACT_LEFT, SDL_SCANCODE_LEFT, 0, 0);
        bind(c, ACT_RIGHT, SDL_SCANCODE_RIGHT, 0, 0);
        bind(c, ACT_JUMP, SDL_SCANCODE_LALT, SDL_SCANCODE_RALT, 0);
        bind(c, ACT_TURN_RIGHT, SDL_SCANCODE_END, 0, 0);
        bind(c, ACT_TURN_LEFT, SDL_SCANCODE_DELETE, 0, 0);
        bind(c, ACT_LOOK_UP, SDL_SCANCODE_INSERT, 0, 0);
        bind(c, ACT_LOOK_DOWN, SDL_SCANCODE_HOME, 0, 0);
        bind(c, ACT_USE, CONTROLS_MOUSE_LEFT, 0, 0);
    } else {
        bind(c, ACT_INVENTORY, SDL_SCANCODE_I, SDL_SCANCODE_F10, 0);
        bind(c, ACT_LEAVE, SDL_SCANCODE_X, 0, 0);
        bind(c, ACT_SWITCH_WEAPON, SDL_SCANCODE_Q, 0, 0);
        bind(c, ACT_SNEAK, SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL, 0);
        bind(c, ACT_FORWARD, SDL_SCANCODE_W, SDL_SCANCODE_UP, 0);
        bind(c, ACT_BACK, SDL_SCANCODE_S, SDL_SCANCODE_DOWN, 0);
        bind(c, ACT_LEFT, SDL_SCANCODE_A, SDL_SCANCODE_LEFT, 0);
        bind(c, ACT_RIGHT, SDL_SCANCODE_D, SDL_SCANCODE_RIGHT, 0);
        bind(c, ACT_JUMP, SDL_SCANCODE_SPACE, 0, 0);
        bind(c, ACT_MOVE_UP, SDL_SCANCODE_PAGEUP, SDL_SCANCODE_R, 0);
        bind(c, ACT_MOVE_DOWN, SDL_SCANCODE_PAGEDOWN, SDL_SCANCODE_V, 0);
        bind(c, ACT_DUCK, SDL_SCANCODE_C, 0, 0);
        bind(c, ACT_USE, SDL_SCANCODE_E, CONTROLS_MOUSE_LEFT, 0);
    }
}

void controls_default(controls *c)
{
    layout(c);
    c->sensitivity_x = c->sensitivity_y = 1.0f;
    c->reverse = false;
    c->double_click = CONTROLS_DOUBLE_CLICK_MIN;
}

void controls_set(controls *c, bool original)
{
    memset(c, 0, sizeof *c);
    c->original = original;
    controls_default(c);
    c->turn_rate = CONTROLS_TURN_RATE_START;
}

static SDL_MouseButtonFlags mouse_mask(int32_t code)
{
    switch (code) {
    case CONTROLS_MOUSE_LEFT: return SDL_BUTTON_LMASK;
    case CONTROLS_MOUSE_RIGHT: return SDL_BUTTON_RMASK;
    case CONTROLS_MOUSE_MIDDLE: return SDL_BUTTON_MMASK;
    default: return 0;
    }
}

bool controls_held(const controls *c, action a)
{
    const bool *k = SDL_GetKeyboardState(NULL);
    SDL_MouseButtonFlags buttons = SDL_GetMouseState(NULL, NULL);
    for (int i = 0; i < CONTROLS_KEYS; i++) {
        int32_t b = c->bind[a][i];
        if (b > 0 && b < SDL_SCANCODE_COUNT && k[b])
            return true;
        if (b < 0 && (buttons & mouse_mask(b)))
            return true;
    }
    return false;
}

int32_t controls_code(const SDL_Event *e)
{
    if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat)
        return (int32_t)e->key.scancode;
    if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        switch (e->button.button) {
        case SDL_BUTTON_LEFT: return CONTROLS_MOUSE_LEFT;
        case SDL_BUTTON_RIGHT: return CONTROLS_MOUSE_RIGHT;
        case SDL_BUTTON_MIDDLE: return CONTROLS_MOUSE_MIDDLE;
        default: return 0;
        }
    }
    return 0;
}

bool controls_pressed(const controls *c, action a, const SDL_Event *e)
{
    int32_t code = controls_code(e);
    for (int i = 0; code && i < CONTROLS_KEYS; i++)
        if (c->bind[a][i] == code)
            return true;
    return false;
}

bool controls_reserved(int32_t code)
{
    return code == SDL_SCANCODE_ESCAPE || (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F8);
}

bool controls_duplicate(const controls *c, action a)
{
    int32_t b = c->bind[a][0];
    for (int i = 0; b && i < ACT_GAME_COUNT; i++)
        if (i != (int)a && c->bind[i][0] == b)
            return true;
    return false;
}

bool controls_any_duplicate(const controls *c)
{
    for (int i = 0; i < ACT_GAME_COUNT; i++)
        if (controls_duplicate(c, (action)i))
            return true;
    return false;
}

const char *controls_name(int32_t code, const bs_exe_text *keys, char *buf, size_t cap)
{
    if (code < 0) {
        snprintf(buf, cap, "M%d", -code);
        return buf;
    }
    for (size_t i = 0; i < sizeof pc_codes / sizeof *pc_codes; i++) {
        const char *s = pc_codes[i].sdl == (SDL_Scancode)code ? bs_exe_message(keys, pc_codes[i].pc) : NULL;
        if (s && *s) {
            snprintf(buf, cap, "%s", s);
            return buf;
        }
    }
    snprintf(buf, cap, "%s", code > 0 ? SDL_GetScancodeName((SDL_Scancode)code) : "");
    for (char *p = buf; *p; p++)
        *p = (char)toupper((unsigned char)*p);
    return buf;
}

uint32_t controls_double_click_ms(const controls *c)
{
    return (uint32_t)(c->double_click * 10000 / 182);
}

float controls_turn_speed(const controls *c)
{
    return (float)c->turn_rate * 6.0f * 2.0f * SDL_PI_F / 2048.0f;
}

/* ---- the settings file ---- */

static const char *code_text(int32_t code, char *buf, size_t cap)
{
    if (code < 0)
        snprintf(buf, cap, "mouse%d", -code);
    else
        snprintf(buf, cap, "%s", SDL_GetScancodeName((SDL_Scancode)code));
    return buf;
}

static int32_t code_from(const char *s)
{
    if (!strncmp(s, "mouse", 5)) {
        int n = atoi(s + 5);
        return n >= 1 && n <= 3 ? -n : 0;
    }
    SDL_Scancode k = SDL_GetScancodeFromName(s);
    return k == SDL_SCANCODE_UNKNOWN ? 0 : (int32_t)k;
}

void controls_write(const controls *c, bs_ini_writer *w)
{
    bs_ini_section_begin(w, "controls");
    bs_ini_put(w, "layout", NULL, "%s", c->original ? "original" : "modern");
    /* Each action's keys, the first the Controls screen's, separated by " | ". */
    for (int a = 0; a < ACT_COUNT; a++) {
        char v[128] = "", one[48];
        for (int i = 0; i < CONTROLS_KEYS; i++)
            if (c->bind[a][i]) {
                size_t n = strlen(v);
                snprintf(v + n, sizeof v - n, "%s%s", n ? " | " : "", code_text(c->bind[a][i], one, sizeof one));
            }
        bs_ini_put_str(w, action_keys[a], v);
    }
    bs_ini_put(w, "sensitivity_x", NULL, "%.2f", (double)c->sensitivity_x);
    bs_ini_put(w, "sensitivity_y", NULL, "%.2f", (double)c->sensitivity_y);
    bs_ini_put(w, "mouse_reverse", NULL, "%s", c->reverse ? "yes" : "no");
    bs_ini_put(w, "double_click", "ticks of 18.2 a second, 8 .. 18", "%d", c->double_click);
    bs_ini_put(w, "turn_rate", "x 6 angle units a second; 0 is 128", "%d", c->turn_rate);
    bs_ini_put(w, "movement", NULL, "%s", c->cursor_movement ? "cursor" : "view");
    bs_ini_put(w, "weapon_swing", NULL, "%s", c->click_swing ? "click" : "classic");
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void controls_read(controls *c, const bs_ini_section *s)
{
    const char *v = bs_ini_get(s, "layout");
    if (v && (!strcmp(v, "original")) != c->original) {
        c->original = !c->original;
        layout(c);
    }
    for (int a = 0; a < ACT_COUNT; a++) {
        if (!(v = bs_ini_get(s, action_keys[a])))
            continue;
        int32_t codes[CONTROLS_KEYS] = {0};
        int n = 0;
        char buf[128];
        snprintf(buf, sizeof buf, "%s", v);
        for (char *p = buf; p && n < CONTROLS_KEYS;) {
            char *bar = strstr(p, " | ");
            if (bar)
                *bar = '\0';
            int32_t code = code_from(p);
            if (code)
                codes[n++] = code;
            p = bar ? bar + 3 : NULL;
        }
        memcpy(c->bind[a], codes, sizeof codes);
    }
    float smax = CONTROLS_SENSITIVITY_MIN + CONTROLS_SENSITIVITY_SPAN;
    if ((v = bs_ini_get(s, "sensitivity_x")))
        c->sensitivity_x = clampf(strtof(v, NULL), CONTROLS_SENSITIVITY_MIN, smax);
    if ((v = bs_ini_get(s, "sensitivity_y")))
        c->sensitivity_y = clampf(strtof(v, NULL), CONTROLS_SENSITIVITY_MIN, smax);
    if ((v = bs_ini_get(s, "mouse_reverse")))
        c->reverse = !strcmp(v, "yes");
    if ((v = bs_ini_get(s, "double_click"))) {
        int d = atoi(v);
        int most = CONTROLS_DOUBLE_CLICK_MIN + CONTROLS_DOUBLE_CLICK_SPAN;
        c->double_click = d < CONTROLS_DOUBLE_CLICK_MIN ? CONTROLS_DOUBLE_CLICK_MIN : d > most ? most : d;
    }
    if ((v = bs_ini_get(s, "turn_rate")))
        c->turn_rate = (uint8_t)atoi(v);
    if ((v = bs_ini_get(s, "movement")))
        c->cursor_movement = !strcmp(v, "cursor");
    if ((v = bs_ini_get(s, "weapon_swing")))
        c->click_swing = !strcmp(v, "click");
}
