#include "menus.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* RE references are GAME.EXE addresses; see docs/re/README.md, "The bar's screens". */

/* ---- help (0x20D33) ---- */

/* The game shows small in the pages' window (0x20A95: 160, 121 - 479, 360). */
static const int help_view[4] = {160, 121, 320, 240};
/* Clicks (0x20F3A): Yes, No, Done, Next, Previous; x0, y0, x1, y1, exclusive. */
static const int help_yes[4] = {275, 451, 295, 463};
static const int help_no[4] = {301, 451, 319, 463};
static const int help_done[4] = {567, 436, 635, 468};
static const int help_next[4] = {494, 438, 561, 468};
static const int help_previous[4] = {368, 438, 490, 470};

/* ---- the character sheet (0x558DC, drawn by 0x55B55) ---- */

/* Buttons (0xF0EB4: x0, y0, x1, y1 inclusive). */
static const int sheet_automap[4] = {263, 420, 388, 456};
static const int sheet_inventory[4] = {407, 420, 531, 456};
static const int sheet_exit[4] = {583, 425, 632, 475};
static const int sheet_help[4] = {616, 2, 637, 22};
/* The skills' scroll (0x16CFE: the text box 272, 128 - 496, 272, its arrows). */
static const int sheet_list[4] = {272, 128, 496, 272};
static const int sheet_up[4] = {522, 115, 543, 156};
static const int sheet_down[4] = {522, 247, 543, 288};
#define SHEET_DOLL_DY 20              /* the doll at (10, 30) where the inventory has it at (10, 10) */
#define SHEET_HELP 16
/* The level-up screen: the attributes' buttons (560, 31 + 50 n - 617, 70 + 50 n), the strips left and right
 * of the skills, 16-pixel rows from y 126 (0x56F5A, 0x57074), the points left centred at 416, 416. */
#define SHEET_ATTRIBUTE_X0 560
#define SHEET_ATTRIBUTE_X1 617
#define SHEET_ATTRIBUTE_Y 31
#define SHEET_ATTRIBUTE_H 39
#define SHEET_ATTRIBUTE_STEP 50
static const int sheet_skill_lower[4] = {239, 126, 254, 269};
static const int sheet_skill_raise[4] = {503, 126, 518, 269};
#define SHEET_SKILL_ROW_Y 126
#define SHEET_SKILL_ROW_H 16
#define SHEET_POINTS_X 416
#define SHEET_POINTS_Y 416
#define SHEET_SHIFT_STEPS 10
/* Skills the sheet leaves out unless they're the class's (0xEE5AC): Medical, Lockpicking, Running. */
static const int hidden_skills[3] = {0, 2, 7};

/* ---- the automap (0x57415, drawn by 0x577B7) ---- */

static const int automap_exit[4] = {590, 427, 634, 473};
#define AUTOMAP_CELL 16
#define MARKER_RED 0xFF0000FFu        /* 0x7C00 */
#define MARKER_GREEN 0x00F608FFu      /* 0x03C1, every other 8 ticks of the timer */
#define MARKER_BLINK_MS 440           /* 8 ticks of 18.2 a second (inferred) */

/* ---- the options bar (0x2EA54, drawn by 0x2EB70) ---- */

enum {
    OPT_FULLSCREEN, OPT_SCREEN_SIZE, OPT_CONTROLS, OPT_LOAD, OPT_SAVE, OPT_END_GAME, OPT_DOS, OPT_SOUND,
    OPT_MUSIC, OPT_EXIT, OPT_HELP, OPT_COUNT
};
/* 0xED8DC: x0, y0, x1, y1 inclusive. */
static const int options_buttons[OPT_COUNT][4] = {
    {52, 389, 183, 423},  {189, 391, 311, 424}, {286, 432, 408, 466}, {314, 393, 373, 423},
    {380, 393, 439, 423}, {446, 390, 568, 423}, {419, 432, 574, 465}, {120, 439, 253, 446},
    {120, 454, 253, 461}, {590, 427, 634, 473}, {9, 455, 32, 477},
};
#define OPTIONS_HELP 20
#define SLIDER_X 120                  /* a volume is (x - 120) x 127 / the bar's width (0x2F067) */
#define VOLUME_SHOWN 5                /* bars at or under this aren't drawn (0x2EBD8) */
/* "Are you sure?" (0x1FE2F): sure00.bsi, Yes and No; or the Y and N keys. */
static const int confirm_yes[4] = {209, 217, 278, 243};
static const int confirm_no[4] = {342, 217, 410, 243};

static bool in(const int r[4], int x, int y)
{
    return x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3];
}

/* The help clicks' areas are tested as open intervals (x0 < x < x1). */
static bool in_open(const int r[4], int x, int y)
{
    return x > r[0] - 1 && x < r[2] && y > r[1] - 1 && y < r[3];
}

void menus_init(menus *m)
{
    memset(m, 0, sizeof *m);
    m->auto_help = true;
    m->dragging = -1;
    m->sound_volume = m->music_volume = 127;
}

void menus_open(menus *m, menu_kind k)
{
    m->open = k;
    m->confirm = MENU_NOTHING;
    m->dragging = -1;
    if (k == MENU_SHEET) {
        m->skill_scroll = 0;
        m->level_up = false;
    }
}

void menus_level_up(menus *m, bs_character *c, int32_t points)
{
    menus_open(m, MENU_SHEET);
    m->level_up = true;
    bs_level_up_begin(&m->lu, c, points);
}

void menus_help(menus *m, int first, int count)
{
    m->under = m->open == MENU_HELP ? m->under : m->open;
    m->open = MENU_HELP;
    m->help_first = first;
    m->help_count = count > 0 ? count : 1;
    m->help_page = 0;
}

static menu_action close_menu(menus *m, const menu_env *env)
{
    if (m->open == MENU_HELP && m->under != MENU_NONE) {
        m->open = m->under;
        m->under = MENU_NONE;
        return MENU_NOTHING;
    }
    if (m->open == MENU_SHEET && m->level_up && env->character) {
        bs_level_up_end(&m->lu, env->character);
        m->level_up = false;
    }
    m->open = MENU_NONE;
    m->under = MENU_NONE;
    return MENU_CLOSED;
}

/* ---- the sheet's skill list ---- */

typedef struct skill_line {
    int skill;
    int font;                         /* 2 lower than its base, 4 higher, 3 the same (0x56057) */
} skill_line;

static bool hidden(int s)
{
    for (int i = 0; i < 3; i++)
        if (hidden_skills[i] == s)
            return true;
    return false;
}

/* The lines (0x55E04): the class's skills first, then the rest but Medical, Lockpicking and Running. The skills
 * have no value apart from their base here, so they show as unchanged; on the level-up screen those raised
 * show green. */
static int skill_lines(const menus *m, const menu_env *env, skill_line *out)
{
    int n = 0;
    bool listed[BS_SKILL_COUNT] = {false};
    const bs_character *c = env->character;
    for (int i = 0; c && i < (int)sizeof c->class_skills; i++) {
        int s = c->class_skills[i];
        if (s < BS_SKILL_COUNT && !listed[s]) {
            listed[s] = true;
            out[n++] = (skill_line){s, 3};
        }
    }
    for (int s = 0; s < BS_SKILL_COUNT; s++)
        if (!listed[s] && !hidden(s))
            out[n++] = (skill_line){s, 3};
    for (int i = 0; m->level_up && c && i < n; i++)
        if (c->skills[out[i].skill] != m->lu.skills_floor[out[i].skill])
            out[i].font = 4;
    return n;
}

static int list_rows(const menu_env *env)
{
    const bs_font *f = env->fonts[3];
    int lh = f ? bs_text_height(f) : 16;
    return lh > 0 ? (sheet_list[3] - sheet_list[1]) / lh : 1;
}

static void scroll_skills(menus *m, const menu_env *env, int by)
{
    skill_line lines[BS_SKILL_COUNT];
    int n = skill_lines(m, env, lines), rows = list_rows(env), most = n > rows ? n - rows : 0;
    m->skill_scroll += by;
    if (m->skill_scroll > most)
        m->skill_scroll = most;
    if (m->skill_scroll < 0)
        m->skill_scroll = 0;
}

/* ---- events ---- */

static int volume_at(const menu_env *env, int x, int slider)
{
    const bs_bsi_image *bar = bs_assets_image(env->assets, slider == 0 ? "optn07.bsi" : "optn08.bsi");
    int w = bar && bar->width ? (int)bar->width : 132;
    int v = (x - SLIDER_X) * 127 / w;
    return v < 0 ? 0 : v > 127 ? 127 : v;
}

static menu_action confirm_answer(menus *m, bool yes)
{
    menu_action a = m->confirm;
    m->confirm = MENU_NOTHING;
    if (!yes)
        return MENU_NOTHING;
    m->open = MENU_NONE;
    return a;
}

static menu_action options_click(menus *m, const menu_env *env, int x, int y)
{
    for (int b = 0; b < OPT_COUNT; b++) {
        if (!in(options_buttons[b], x, y))
            continue;
        switch (b) {
        case OPT_FULLSCREEN:
            return MENU_FULL_VIEW;
        case OPT_SOUND:
        case OPT_MUSIC:
            m->dragging = b == OPT_SOUND ? 0 : 1;
            *(b == OPT_SOUND ? &m->sound_volume : &m->music_volume) = volume_at(env, x, m->dragging);
            return MENU_VOLUME;
        case OPT_END_GAME:
            m->confirm = MENU_END_GAME;
            return MENU_NOTHING;
        case OPT_DOS:
            m->confirm = MENU_QUIT;
            return MENU_NOTHING;
        case OPT_LOAD:
        case OPT_SAVE:
            m->open = MENU_NONE;
            return b == OPT_LOAD ? MENU_LOAD : MENU_SAVE;
        case OPT_EXIT:
            return close_menu(m, env);
        case OPT_HELP:
            menus_help(m, OPTIONS_HELP, 1);
            return MENU_NOTHING;
        default:
            return MENU_NOT_THERE;
        }
    }
    return MENU_NOTHING;
}

/* ---- the level-up screen ---- */

static void say(const menu_env *env, const bs_level_up_result *r)
{
    if (!r->message || !env->message || !env->text)
        return;
    char s[160];
    if (r->message == BS_MSG_SKILL_ABOVE)
        snprintf(s, sizeof s, "%s %s.", bs_exe_message(env->text, r->message),
                 bs_exe_message(env->text, BS_MSG_ATTRIBUTES + (uint32_t)r->attribute));
    else
        snprintf(s, sizeof s, "%s", bs_exe_message(env->text, r->message));
    env->message(env->ctx, s);
}

/* A click on an attribute (0x562D0) or beside a skill (0x56F5A, 0x57074). Returns whether it was one. */
static bool level_up_click(menus *m, const menu_env *env, int x, int y)
{
    bs_character *c = env->character;
    if (!c)
        return false;
    int steps = (SDL_GetModState() & SDL_KMOD_SHIFT) ? SHEET_SHIFT_STEPS : 1;
    for (int a = 0; a < 8; a++) {
        int y0 = SHEET_ATTRIBUTE_Y + SHEET_ATTRIBUTE_STEP * a, y1 = y0 + SHEET_ATTRIBUTE_H;
        const int r[4] = {SHEET_ATTRIBUTE_X0, y0, SHEET_ATTRIBUTE_X1, y1};
        if (in(r, x, y)) {
            bs_level_up_result res = bs_level_up_attribute(&m->lu, c, a, (y0 + y1) / 2 > y, steps);
            say(env, &res);
            return true;
        }
    }
    bool raise = in(sheet_skill_raise, x, y);
    if (!raise && !in(sheet_skill_lower, x, y))
        return false;
    skill_line lines[BS_SKILL_COUNT];
    int n = skill_lines(m, env, lines), row = (y - SHEET_SKILL_ROW_Y) / SHEET_SKILL_ROW_H + m->skill_scroll;
    if (row >= 0 && row < n) {
        bs_level_up_result res = bs_level_up_skill(&m->lu, c, lines[row].skill, raise, steps);
        say(env, &res);
    }
    return true;
}

static menu_action click(menus *m, const menu_env *env, int x, int y)
{
    switch (m->open) {
    case MENU_HELP:
        if (in_open(help_yes, x, y))
            m->auto_help = true;
        else if (in_open(help_no, x, y))
            m->auto_help = false;
        else if (in_open(help_done, x, y))
            return close_menu(m, env);
        else if (in_open(help_next, x, y) && m->help_page < m->help_count - 1)
            m->help_page++;
        else if (in_open(help_previous, x, y) && m->help_page > 0)
            m->help_page--;
        return MENU_NOTHING;
    case MENU_SHEET:
        if (m->level_up && level_up_click(m, env, x, y))
            return MENU_NOTHING;
        if (in(sheet_automap, x, y) && !m->level_up) {
            menus_open(m, MENU_AUTOMAP);
            return MENU_NOTHING;
        }
        if (in(sheet_inventory, x, y) && !m->level_up) {
            m->open = MENU_NONE;
            return MENU_TO_INVENTORY;
        }
        if (in(sheet_exit, x, y))
            return close_menu(m, env);
        if (in(sheet_help, x, y)) {
            menus_help(m, SHEET_HELP, 1);
            return MENU_NOTHING;
        }
        if (in(sheet_up, x, y))
            scroll_skills(m, env, -1);
        else if (in(sheet_down, x, y))
            scroll_skills(m, env, 1);
        return MENU_NOTHING;
    case MENU_AUTOMAP:
        return in(automap_exit, x, y) ? close_menu(m, env) : MENU_NOTHING;
    case MENU_OPTIONS:
        if (m->confirm != MENU_NOTHING) {
            if (in(confirm_yes, x, y))
                return confirm_answer(m, true);
            if (in(confirm_no, x, y))
                return confirm_answer(m, false);
            return MENU_NOTHING;
        }
        return options_click(m, env, x, y);
    default:
        return MENU_NOTHING;
    }
}

menu_action menus_event(menus *m, const menu_env *env, const SDL_Event *e, int x, int y)
{
    if (m->open == MENU_NONE)
        return MENU_NOTHING;
    if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
        SDL_Keycode k = e->key.key;
        if (m->confirm != MENU_NOTHING) {
            if (k == SDLK_Y)
                return confirm_answer(m, true);
            if (k == SDLK_N || k == SDLK_ESCAPE)
                return confirm_answer(m, false);
            return MENU_NOTHING;
        }
        /* Esc closes any; F9 the sheet, F11 the automap (0x55DD5, 0x57A68). */
        if (k == SDLK_ESCAPE || (k == SDLK_F9 && m->open == MENU_SHEET && !m->level_up) ||
            (k == SDLK_F11 && m->open == MENU_AUTOMAP))
            return close_menu(m, env);
        if (m->open == MENU_SHEET && (k == SDLK_UP || k == SDLK_DOWN))
            scroll_skills(m, env, k == SDLK_UP ? -1 : 1);
        return MENU_NOTHING;
    }
    if (e->type == SDL_EVENT_MOUSE_WHEEL && m->open == MENU_SHEET) {
        scroll_skills(m, env, e->wheel.y > 0 ? -1 : 1);
        return MENU_NOTHING;
    }
    if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN && e->button.button == SDL_BUTTON_LEFT)
        return click(m, env, x, y);
    if (e->type == SDL_EVENT_MOUSE_BUTTON_UP && e->button.button == SDL_BUTTON_LEFT)
        m->dragging = -1;
    if (e->type == SDL_EVENT_MOUSE_MOTION && m->dragging >= 0 && m->open == MENU_OPTIONS) {
        *(m->dragging == 0 ? &m->sound_volume : &m->music_volume) = volume_at(env, x, m->dragging);
        return MENU_VOLUME;
    }
    return MENU_NOTHING;
}

/* ---- drawing ---- */

static void text(bs_overlay *o, const bs_font *f, int x, int y, const char *s)
{
    if (f)
        bs_overlay_text(o, f, x, y, s);
}

static void draw_help(const menus *m, const menu_env *env, bs_overlay *o)
{
    char name[16];
    snprintf(name, sizeof name, "help%02d.bsi", m->help_first + m->help_page);
    bs_overlay_image(o, bs_assets_image(env->assets, name), 0, 0);
    /* The game shows through the window. */
    bs_overlay_fill(o, help_view[0], help_view[1], help_view[2], help_view[3], 0);
    bs_overlay_image(o, bs_assets_image(env->assets, m->auto_help ? "hyes00.bsi" : "hno000.bsi"), 0, 0);
}

static void draw_sheet(const menus *m, const menu_env *env, bs_overlay *o)
{
    const bs_character *c = env->character;
    bs_overlay_image(o, bs_assets_image(env->assets, m->level_up ? "cscr01.bsi" : "cscr00.bsi"), 0, 0);
    for (int i = 0; i < env->doll_count; i++)
        bs_overlay_image(o, env->doll[i], 0, SHEET_DOLL_DY);
    text(o, env->fonts[1], 256, 34, env->name);
    /* The attributes, white, red when lowered, green when raised; what follows keeps the last one's font. */
    char s[48];
    const bs_font *f = env->fonts[1];
    for (int i = 0; i < 8; i++) {
        int32_t now = bs_character_attribute(c, i), base = c->attributes[i];
        f = env->fonts[now < base ? 2 : now > base ? 4 : 1];
        snprintf(s, sizeof s, "%d", (int)now);
        text(o, f, 576, 50 + 50 * i, s);
    }
    snprintf(s, sizeof s, "%d/%d", (int)c->health, (int)c->health_max);
    text(o, f, 368, 354, s);
    snprintf(s, sizeof s, "%d/%d", (int)c->spell_points, (int)c->spell_points_max);
    text(o, f, 424, 322, s);
    snprintf(s, sizeof s, "%d.%d / %d", (int)(env->carried / 2), (env->carried & 1) ? 5 : 0,
             (int)bs_character_attribute(c, 0) * 2);
    text(o, f, 424, 386, s);
    if (m->level_up && env->fonts[1]) {
        snprintf(s, sizeof s, "%d", (int)m->lu.points);
        text(o, env->fonts[1], SHEET_POINTS_X - bs_text_width(env->fonts[1], s) / 2, SHEET_POINTS_Y, s);
    }
    /* The skills, "Name [ATT]: n". */
    skill_line lines[BS_SKILL_COUNT];
    int n = skill_lines(m, env, lines), rows = list_rows(env);
    int lh = env->fonts[3] ? bs_text_height(env->fonts[3]) : 16;
    bs_overlay_clip(o, sheet_list[0], sheet_list[1], sheet_list[2] - sheet_list[0], sheet_list[3] - sheet_list[1]);
    for (int r = 0; r < rows && m->skill_scroll + r < n; r++) {
        const skill_line *l = &lines[m->skill_scroll + r];
        const char *nm = env->skill_names[l->skill] ? env->skill_names[l->skill] : "";
        const char *at = env->attribute_names[bs_skill_attribute[l->skill]] ? env->attribute_names[bs_skill_attribute[l->skill]] : "";
        snprintf(s, sizeof s, "%s [%s]: %d", nm, at, (int)c->skills[l->skill]);
        text(o, env->fonts[l->font], sheet_list[0], sheet_list[1] + r * lh, s);
    }
    bs_overlay_clip(o, 0, 0, 0, 0);
}

/* The game's sine and cosine x len (0x1B975: tables of sin x 2^28). */
static void vector(int32_t angle, int32_t len, int32_t *x, int32_t *z)
{
    double a = (double)(angle & 0x7FF) * (2.0 * 3.14159265358979323846 / 2048.0);
    *x = (int32_t)(((int64_t)len * llround(sin(a) * 268435456.0)) >> 28);
    *z = (int32_t)(((int64_t)len * llround(cos(a) * 268435456.0)) >> 28);
}

/* The player's marker (0x5752A): a cross and a 16-pixel line the way they face, blinking. The game
 * writes only the colour's low byte along the line, which mixes it with the map; here it's drawn whole.
 * Its line steps by the larger of the signed differences, so facing between north and west there's no
 * line, as in the game. */
static void draw_marker(bs_overlay *o, int x, int y, int32_t yaw, uint32_t rgba)
{
    x = x > 635 ? 635 : x < 5 ? 5 : x;
    y = y > 475 ? 475 : y < 5 ? 5 : y;
    for (int i = -3; i <= 3; i++)
        bs_overlay_fill(o, x + i, y, 1, 1, rgba);
    for (int j = -4; j <= 4; j++)
        bs_overlay_fill(o, x, y + j, 1, 1, rgba);
    int32_t dx, dz;
    vector(yaw, 16, &dx, &dz);
    float fx = (float)dx, fy = (float)-dz;
    int steps = (int)(fx > fy ? fx : fy);
    if (steps <= 0)
        return;
    float sx = fx / (float)steps, sy = fy / (float)steps, px = (float)x, py = (float)y;
    for (int k = 0; k < steps; k++) {
        bs_overlay_fill(o, (int)px, (int)py, 1, 1, rgba);
        px += sx;
        py += sy;
    }
}

static void draw_automap(const menu_env *env, bs_overlay *o)
{
    bs_overlay_image(o, bs_assets_image(env->assets, "map030.bsi"), 0, 0);
    if (!env->map)
        return;
    const bs_bsi_image *map = bs_assets_image(env->assets, env->map->image);
    /* The map where it has been seen, in runs of cells. */
    for (int r = 0; map && env->revealed && r < AUTOMAP_ROWS; r++)
        for (int c = 0; c < AUTOMAP_COLS;) {
            if (!env->revealed[r * AUTOMAP_COLS + c]) {
                c++;
                continue;
            }
            int run = c;
            while (run < AUTOMAP_COLS && env->revealed[r * AUTOMAP_COLS + run])
                run++;
            bs_overlay_clip(o, c * AUTOMAP_CELL, r * AUTOMAP_CELL, (run - c) * AUTOMAP_CELL, AUTOMAP_CELL);
            bs_overlay_image(o, map, 0, 0);
            c = run;
        }
    bs_overlay_clip(o, 0, 0, 0, 0);
    int x, y;
    bs_automap_point(env->map, env->player_x, env->player_z, &x, &y);
    draw_marker(o, x, y, env->player_yaw, (env->now_ms / MARKER_BLINK_MS) & 1 ? MARKER_RED : MARKER_GREEN);
}

static void draw_options(const menus *m, const menu_env *env, bs_overlay *o)
{
    bs_overlay_image(o, bs_assets_image(env->assets, "options.bsi"), 0, 0);
    if (env->full_view)
        bs_overlay_image(o, bs_assets_image(env->assets, "optnon00.bsi"), 0, 0);
    /* The volumes as bars from the left, the part beyond them cut off. */
    static const char *const bars[2] = {"optn07.bsi", "optn08.bsi"};
    const int volumes[2] = {m->sound_volume, m->music_volume};
    for (int k = 0; k < 2; k++) {
        const bs_bsi_image *bar = bs_assets_image(env->assets, bars[k]);
        if (!bar || volumes[k] <= VOLUME_SHOWN)
            continue;
        int w = (int)bar->width * volumes[k] / 127;
        bs_overlay_clip(o, bar->x, bar->y, w, (int)bar->height);
        bs_overlay_image(o, bar, w - (int)bar->width, 0);
        bs_overlay_clip(o, 0, 0, 0, 0);
    }
    if (m->confirm != MENU_NOTHING)
        bs_overlay_image(o, bs_assets_image(env->assets, "sure00.bsi"), 0, 0);
}

void menus_draw(const menus *m, const menu_env *env, bs_overlay *o)
{
    switch (m->open) {
    case MENU_HELP:
        draw_help(m, env, o);
        break;
    case MENU_SHEET:
        draw_sheet(m, env, o);
        break;
    case MENU_AUTOMAP:
        draw_automap(env, o);
        break;
    case MENU_OPTIONS:
        draw_options(m, env, o);
        break;
    default:
        break;
    }
}

int menus_view(const menus *m, int r[4])
{
    if (m->open == MENU_HELP) {
        memcpy(r, help_view, sizeof help_view);
        return 2;
    }
    return m->open == MENU_OPTIONS || m->open == MENU_NONE ? 1 : 0;
}

void automap_reveal(uint8_t revealed[AUTOMAP_COLS * AUTOMAP_ROWS], const bs_automap_entry *map, float x, float z)
{
    if (!map)
        return;
    int sx, sy;
    bs_automap_point(map, x, z, &sx, &sy);
    int cx = sx / AUTOMAP_CELL, cy = sy / AUTOMAP_CELL;
    cx = cx >= AUTOMAP_COLS ? AUTOMAP_COLS - 1 : cx < 1 ? 1 : cx;
    cy = cy >= AUTOMAP_ROWS ? AUTOMAP_ROWS - 1 : cy < 1 ? 1 : cy;
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++)
            if (cx + i < AUTOMAP_COLS && cy + j < AUTOMAP_ROWS)
                revealed[(cy + j) * AUTOMAP_COLS + cx + i] = 1;
}
