#include "spellbook.h"

#include <stdio.h>
#include <string.h>

/* RE references are GAME.EXE addresses; see docs/formats/spells.md, "The spell book". */

/* The buttons (0xED5B2: x0, y0, x1, y1) in the 640x480 screen. */
static const int icon_rect[4] = {256, 396, 287, 425};
static const int delivery_rect[4] = {311, 395, 344, 427};
static const int element_rect[4] = {367, 397, 396, 423};
static const int hotkey_rect[4] = {412, 389, 554, 411};
static const int detonation_rect[4] = {411, 415, 576, 438};
static const int cast_rect[4] = {563, 389, 631, 414};
static const int exit_rect[4] = {590, 427, 634, 473};
/* The list (0x29950): its rows, the scroll bar's arrows. */
static const int list_rect[4] = {20, 396, 197, 468};
static const int up_rect[4] = {218, 381, 238, 406};
static const int down_rect[4] = {218, 460, 238, 479};
#define PANEL_TOP 380
/* Where the choices and values go (0x29C92). */
#define ICON_X 252
#define DELIVERY_X 307
#define ELEMENT_X 363
#define CHOICE_Y 391
#define COST_X 318
#define DURATION_X 375
#define MAGNITUDE_X 463
#define VALUES_Y 450
#define DETONATION_X 452
#define DETONATION_Y 420
#define HOTKEY_X 498
#define HOTKEY_Y 395
#define ICONS 27
#define SELECTED_BAND 0x00000050u  /* the selected row's shade (not the game's: its list isn't decoded) */

enum {
    MSG_DETONATION = 58, MSG_NO_SPELLS = 81, MSG_NA = 82, MSG_NONE = 83, MSG_HOTKEY_HELP = 430,
};

static bool in_rect(int x, int y, const int r[4])
{
    return x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3];
}

/* The known spells, by number. */
static int known(const bs_character *c, int out[BS_SPELL_COUNT])
{
    int n = 0;
    for (int s = 1; s <= BS_SPELL_COUNT; s++)
        if (c->spells_known & (1u << (s - 1)))
            out[n++] = s;
    return n;
}

static int row_height(const spellbook_env *env)
{
    return env->list_font ? bs_text_height(env->list_font) + 1 : 10;
}

static int rows(const spellbook_env *env)
{
    int h = row_height(env);
    return h > 0 ? (list_rect[3] - list_rect[1]) / h : 1;
}

static void clamp_scroll(spellbook *b, const spellbook_env *env)
{
    int list[BS_SPELL_COUNT], n = known(env->character, list), max = n - rows(env);
    if (b->scroll > max)
        b->scroll = max;
    if (b->scroll < 0)
        b->scroll = 0;
}

bool spellbook_open(spellbook *b, const spellbook_env *env)
{
    int list[BS_SPELL_COUNT], n = known(env->character, list);
    if (!n) {
        if (env->message)
            env->message(env->ctx, bs_exe_message(env->text, MSG_NO_SPELLS));
        return false;
    }
    if (b->selected < 1 || b->selected > BS_SPELL_COUNT || !(env->character->spells_known & (1u << (b->selected - 1))))
        b->selected = list[0];
    b->open = true;
    clamp_scroll(b, env);
    return true;
}

/* The 640x480 screen's placement in the window, as bs_screen letterboxes it. */
static void to_screen(int ww, int wh, float wx, float wy, int *x, int *y)
{
    float sx = (float)ww / 640.0f, sy = (float)wh / 480.0f, s = sx < sy ? sx : sy;
    float ox = ((float)ww - 640.0f * s) / 2.0f, oy = ((float)wh - 480.0f * s) / 2.0f;
    *x = (int)((wx - ox) / s);
    *y = (int)((wy - oy) / s);
}

bool spellbook_covers(int window_w, int window_h, float x, float y)
{
    int sx, sy;
    to_screen(window_w, window_h, x, y, &sx, &sy);
    return sy >= PANEL_TOP;
}

/* F1-F8 while the book is open give the selected spell that key, and take it from any other (0x29FF5). */
static void set_hotkey(bs_character *c, int key, int spell)
{
    c->hotkeys[key] = (uint8_t)spell;
    for (int k = 0; k < BS_HOTKEYS; k++)
        if (k != key && c->hotkeys[k] == spell)
            c->hotkeys[k] = 0;
}

bool spellbook_event(spellbook *b, const spellbook_env *env, const SDL_Event *e, int window_w, int window_h)
{
    bs_character *c = env->character;
    bs_spell *s = b->selected >= 1 && b->selected <= BS_SPELL_COUNT ? &c->book[b->selected - 1] : NULL;
    if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
        SDL_Keycode k = e->key.key;
        if (k == SDLK_ESCAPE || k == SDLK_F12) {
            b->open = false;
            return false;
        }
        if (k >= SDLK_F1 && k <= SDLK_F8 && s)
            set_hotkey(c, (int)(k - SDLK_F1), b->selected);
        return true;
    }
    if (e->type == SDL_EVENT_MOUSE_WHEEL) {
        b->scroll -= e->wheel.y > 0 ? 1 : e->wheel.y < 0 ? -1 : 0;
        clamp_scroll(b, env);
        return true;
    }
    if (e->type != SDL_EVENT_MOUSE_BUTTON_DOWN || e->button.button != SDL_BUTTON_LEFT)
        return true;
    int x, y;
    to_screen(window_w, window_h, e->button.x, e->button.y, &x, &y);
    if (in_rect(x, y, list_rect)) {
        int list[BS_SPELL_COUNT], n = known(c, list);
        int i = b->scroll + (y - list_rect[1]) / row_height(env);
        if (i >= 0 && i < n)
            b->selected = list[i];
    } else if (in_rect(x, y, up_rect)) {
        b->scroll--;
        clamp_scroll(b, env);
    } else if (in_rect(x, y, down_rect)) {
        b->scroll++;
        clamp_scroll(b, env);
    } else if (in_rect(x, y, exit_rect)) {
        b->open = false;
        return false;
    } else if (!s) {
        return true;
    } else if (in_rect(x, y, icon_rect)) {
        s->icon = (uint8_t)((s->icon + 1) % ICONS);
    } else if (in_rect(x, y, delivery_rect)) {
        s->delivery = bs_spell_next_delivery(s, s->delivery);
    } else if (in_rect(x, y, element_rect)) {
        s->element = (uint8_t)((s->element + 1) % BS_ELEMENT_COUNT);
    } else if (in_rect(x, y, detonation_rect)) {
        s->detonation = (s->detonation + 1) % BS_DETONATION_COUNT;
    } else if (in_rect(x, y, hotkey_rect)) {
        if (env->message)
            env->message(env->ctx, bs_exe_message(env->text, MSG_HOTKEY_HELP));
    } else if (in_rect(x, y, cast_rect)) {
        b->open = false;
        if (env->cast)
            env->cast(env->ctx, s);
        return false;
    }
    return true;
}

static void text(const spellbook_env *env, const bs_font *f, int x, int y, const char *t)
{
    if (f)
        bs_overlay_text(env->overlay, f, x, y, t);
}

void spellbook_draw(const spellbook *b, const spellbook_env *env)
{
    if (!b->open || !env->overlay)
        return;
    /* The panel: its background, then the frames and buttons over it. */
    if (env->panel && env->panel->count) {
        bs_overlay_image(env->overlay, &env->panel->images[env->panel->count - 1], 0, 0);
        for (size_t i = 0; i + 1 < env->panel->count; i++)
            bs_overlay_image(env->overlay, &env->panel->images[i], 0, 0);
    }
    const bs_character *c = env->character;
    int list[BS_SPELL_COUNT], n = known(c, list), h = row_height(env);
    for (int r = 0; r < rows(env) && b->scroll + r < n; r++) {
        int spell = list[b->scroll + r];
        const bs_spell *s = &c->book[spell - 1];
        int skill = s->skill >= 0 && s->skill < BS_SKILL_COUNT ? c->skills[s->skill] : 0;
        char line[64];
        snprintf(line, sizeof line, "%d - %s", (int)bs_spell_cost(s, skill), s->name);
        /* Cut to the list's width. */
        size_t len = strlen(line);
        while (len > 0 && env->list_font && bs_text_width(env->list_font, line) > list_rect[2] - list_rect[0])
            line[--len] = '\0';
        int y = list_rect[1] + r * h;
        if (spell == b->selected)
            bs_overlay_fill(env->overlay, list_rect[0] - 2, y - 1, list_rect[2] - list_rect[0] + 4, h, SELECTED_BAND);
        text(env, env->list_font, list_rect[0], y, line);
    }
    if (b->selected < 1 || b->selected > BS_SPELL_COUNT)
        return;
    const bs_spell *s = &c->book[b->selected - 1];
    if (s->icon < ICONS && env->icons[s->icon])
        bs_overlay_image(env->overlay, env->icons[s->icon], ICON_X, CHOICE_Y);
    if (s->delivery < BS_DELIVERY_COUNT && env->deliveries[s->delivery])
        bs_overlay_image(env->overlay, env->deliveries[s->delivery], DELIVERY_X, CHOICE_Y);
    if (s->element < BS_ELEMENT_COUNT && env->elements[s->element])
        bs_overlay_image(env->overlay, env->elements[s->element], ELEMENT_X, CHOICE_Y);
    char v[64];
    int skill = s->skill >= 0 && s->skill < BS_SKILL_COUNT ? c->skills[s->skill] : 0;
    snprintf(v, sizeof v, "%d", (int)bs_spell_cost(s, skill));
    text(env, env->font, COST_X, VALUES_Y, v);
    /* The duration in its units of 5 s, and the magnitude's two ranges. */
    if (s->duration_base) {
        int32_t step = s->duration_step > 0 ? s->duration_step : 1;
        snprintf(v, sizeof v, "%d", (int)(c->level / step * s->duration_increase + s->duration_base));
    } else {
        snprintf(v, sizeof v, "%s", bs_exe_message(env->text, MSG_NA));
    }
    text(env, env->font, DURATION_X, VALUES_Y, v);
    if (s->magnitude_step) {
        int32_t k = env->level_number / s->magnitude_step;
        snprintf(v, sizeof v, "%d-%d + %d-%d", (int)s->magnitude_min, (int)s->magnitude_max,
                 (int)(k * s->increase_min), (int)(k * s->increase_max));
    } else {
        snprintf(v, sizeof v, "%s", bs_exe_message(env->text, MSG_NA));
    }
    text(env, env->font, MAGNITUDE_X, VALUES_Y, v);
    if (s->detonation >= 0 && s->detonation < BS_DETONATION_COUNT)
        text(env, env->font, DETONATION_X, DETONATION_Y, bs_exe_message(env->text, MSG_DETONATION + (uint32_t)s->detonation));
    int key = 0;
    for (int k = 0; k < BS_HOTKEYS; k++)
        if (c->hotkeys[k] == b->selected)
            key = k + 1;
    if (key)
        snprintf(v, sizeof v, "F%d", key);
    else
        snprintf(v, sizeof v, "%s", bs_exe_message(env->text, MSG_NONE));
    text(env, env->font, HOTKEY_X, HOTKEY_Y, v);
}
