/*
 * The startup screen: where Battlespire is, then Play or Quit (launcher.h).
 *
 * It draws with an SDL software renderer into a 640x480 surface, using SDL's built-in debug font, and shows
 * that with bs_screen like the game's own menu screens. Browse opens the system's folder dialog, which
 * answers through an event of ours (it may call back from another thread).
 */

#include "launcher.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bs/core/fs.h"
#include "bs/core/install.h"

#define CANVAS_W 640
#define CANVAS_H 480
#define GLYPH SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE
#define FIELD_GLYPHS 56 /* the path's characters the field shows */

/* Colours, 0xRRGGBB. */
#define C_BACK 0x120A1F
#define C_PANEL 0x1E1433
#define C_GOLD 0xD9B45A
#define C_TEXT 0xE8E0D0
#define C_DIM 0x8A8098
#define C_GOOD 0x7FD47F
#define C_BAD 0xE06A5A
#define C_BUTTON 0x3A2A5C
#define C_HOVER 0x55407F
#define C_OFF 0x2A2433

typedef enum button { BUTTON_BROWSE, BUTTON_PLAY, BUTTON_QUIT, BUTTON_COUNT, BUTTON_NONE = -1 } button;

static const SDL_FRect button_rect[BUTTON_COUNT] = {
    [BUTTON_BROWSE] = {510, 190, 90, 26},
    [BUTTON_PLAY] = {190, 370, 120, 40},
    [BUTTON_QUIT] = {330, 370, 120, 40},
};
static const char *const button_label[BUTTON_COUNT] = {"Browse...", "Play", "Quit"};
static const SDL_FRect field_rect = {40, 190, 460, 26};

typedef enum folder_state { FOLDER_NONE, FOLDER_OK, FOLDER_MISSING, FOLDER_NOT_GAME } folder_state;

typedef struct launcher {
    SDL_Window *window;
    bs_screen *screen;
    SDL_Surface *canvas;
    SDL_Renderer *r;
    char *dir;
    size_t cap;
    folder_state state;
    bs_install inst;
    button hover, pressed;
    bool dialog_open;
    Uint32 dialog_event;
    char note[160]; /* why the folder dialog didn't open */
} launcher;

static void set_color(SDL_Renderer *r, uint32_t rgb)
{
    SDL_SetRenderDrawColor(r, (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb, 255);
}

static void fill(SDL_Renderer *r, SDL_FRect rect, uint32_t rgb)
{
    set_color(r, rgb);
    SDL_RenderFillRect(r, &rect);
}

static void frame(SDL_Renderer *r, SDL_FRect rect, uint32_t rgb)
{
    set_color(r, rgb);
    SDL_RenderRect(r, &rect);
}

static float text_width(const char *s, float scale)
{
    return (float)SDL_utf8strlen(s) * GLYPH * scale;
}

static void text(SDL_Renderer *r, float x, float y, float scale, uint32_t rgb, const char *s)
{
    set_color(r, rgb);
    SDL_SetRenderScale(r, scale, scale);
    SDL_RenderDebugText(r, x / scale, y / scale, s);
    SDL_SetRenderScale(r, 1, 1);
}

static void text_centered(SDL_Renderer *r, float y, float scale, uint32_t rgb, const char *s)
{
    text(r, ((float)CANVAS_W - text_width(s, scale)) / 2, y, scale, rgb, s);
}

/* ---- the folder ---- */

static void check_folder(launcher *l)
{
    if (!l->dir[0])
        l->state = FOLDER_NONE;
    else if (!bs_fs_is_dir(l->dir))
        l->state = FOLDER_MISSING;
    else
        l->state = bs_install_detect(l->dir, &l->inst) == BS_OK ? FOLDER_OK : FOLDER_NOT_GAME;
}

/* Sets the folder; a file (GAME.EXE dropped on the window, say) gives the folder it is in. Surrounding blanks
 * and quotes (Windows' "Copy as path") are left out. */
static void set_folder(launcher *l, const char *path)
{
    size_t n = strlen(path);
    while (n && (path[n - 1] == ' ' || path[n - 1] == '\t' || path[n - 1] == '\n' || path[n - 1] == '\r'))
        n--;
    while (n && (*path == ' ' || *path == '\t')) {
        path++;
        n--;
    }
    if (n >= 2 && path[0] == '"' && path[n - 1] == '"') {
        path++;
        n -= 2;
    }
    if (n >= l->cap)
        return;
    memcpy(l->dir, path, n);
    l->dir[n] = '\0';
    if (bs_fs_is_file(l->dir)) {
        char *slash = strrchr(l->dir, '/');
        char *back = strrchr(l->dir, '\\');
        if (back > slash)
            slash = back;
        if (slash)
            slash[slash == l->dir] = '\0'; /* keep "/" itself */
    }
    l->note[0] = '\0';
    check_folder(l);
}

static void append_text(launcher *l, const char *s)
{
    size_t have = strlen(l->dir), add = strlen(s);
    if (have + add >= l->cap)
        return;
    memcpy(l->dir + have, s, add + 1);
    l->note[0] = '\0';
    check_folder(l);
}

static void delete_char(launcher *l)
{
    size_t n = strlen(l->dir);
    while (n && ((unsigned char)l->dir[n - 1] & 0xC0) == 0x80) /* UTF-8 continuation bytes */
        n--;
    if (n)
        n--;
    l->dir[n] = '\0';
    check_folder(l);
}

static void SDLCALL folder_chosen(void *user, const char *const *files, int filter)
{
    (void)filter;
    SDL_Event e;
    SDL_zero(e);
    e.type = (Uint32)(uintptr_t)user;
    if (!files) {
        e.user.code = 1;
        e.user.data1 = SDL_strdup(SDL_GetError());
    } else if (files[0]) {
        e.user.data1 = SDL_strdup(files[0]);
    } else {
        e.user.code = 2; /* cancelled */
    }
    SDL_PushEvent(&e);
}

static void browse(launcher *l)
{
    if (l->dialog_open)
        return;
    l->dialog_open = true;
    l->note[0] = '\0';
    SDL_ShowOpenFolderDialog(folder_chosen, (void *)(uintptr_t)l->dialog_event, l->window,
                             bs_fs_is_dir(l->dir) ? l->dir : NULL, false);
}

static void dialog_answered(launcher *l, const SDL_UserEvent *u)
{
    l->dialog_open = false;
    if (u->code == 0 && u->data1)
        set_folder(l, u->data1);
    else if (u->code == 1)
        snprintf(l->note, sizeof l->note, "No folder dialog (%s): type the folder instead.",
                 u->data1 ? (const char *)u->data1 : "?");
    SDL_free(u->data1);
}

/* ---- drawing ---- */

static void draw_button(launcher *l, button b, bool enabled)
{
    SDL_FRect rect = button_rect[b];
    float scale = b == BUTTON_BROWSE ? 1.0f : 2.0f;
    fill(l->r, rect, !enabled ? C_OFF : l->hover == b ? C_HOVER : C_BUTTON);
    frame(l->r, rect, enabled ? C_GOLD : C_DIM);
    const char *s = button_label[b];
    text(l->r, rect.x + (rect.w - text_width(s, scale)) / 2, rect.y + (rect.h - GLYPH * scale) / 2, scale,
         enabled ? C_TEXT : C_DIM, s);
}

static void draw_field(launcher *l, uint64_t now_ms)
{
    fill(l->r, field_rect, C_PANEL);
    frame(l->r, field_rect, l->state == FOLDER_OK ? C_GOLD : C_DIM);
    /* A long path shows its end. */
    const char *s = l->dir;
    char shown[FIELD_GLYPHS * 4 + 8];
    size_t glyphs = SDL_utf8strlen(s);
    if (glyphs > FIELD_GLYPHS) {
        for (size_t skip = glyphs - (FIELD_GLYPHS - 3); skip; skip--) {
            s++;
            while (((unsigned char)*s & 0xC0) == 0x80)
                s++;
        }
        snprintf(shown, sizeof shown, "...%s", s);
        s = shown;
    }
    float x = field_rect.x + 6, y = field_rect.y + (field_rect.h - GLYPH) / 2;
    text(l->r, x, y, 1, C_TEXT, s);
    if (!l->dialog_open && now_ms / 500 % 2 == 0)
        fill(l->r, (SDL_FRect){x + text_width(s, 1) + 1, y - 1, 2, GLYPH + 2}, C_GOLD);
}

static void draw(launcher *l, uint64_t now_ms)
{
    SDL_Renderer *r = l->r;
    set_color(r, C_BACK);
    SDL_RenderClear(r);
    frame(r, (SDL_FRect){8, 8, CANVAS_W - 16, CANVAS_H - 16}, C_GOLD);
    frame(r, (SDL_FRect){11, 11, CANVAS_W - 22, CANVAS_H - 22}, C_BUTTON);

    text_centered(r, 60, 4, C_GOLD, "OpenBattlespire");
    text_centered(r, 104, 1, C_DIM, "An Elder Scrolls Legend: Battlespire, reimplemented");

    text(r, field_rect.x, field_rect.y - 14, 1, C_GOLD, "Battlespire folder");
    draw_field(l, now_ms);

    char status[160];
    uint32_t color = C_DIM;
    switch (l->state) {
    case FOLDER_NONE:
        snprintf(status, sizeof status, "Choose the folder Battlespire is installed in.");
        break;
    case FOLDER_OK:
        if (l->inst.version != BS_VERSION_UNKNOWN)
            snprintf(status, sizeof status, "Battlespire found (%s).", bs_game_version_str(l->inst.version));
        else
            snprintf(status, sizeof status, "Battlespire found.");
        color = C_GOOD;
        break;
    case FOLDER_MISSING:
        snprintf(status, sizeof status, "There is no such folder.");
        color = C_BAD;
        break;
    case FOLDER_NOT_GAME:
        snprintf(status, sizeof status, "Not a Battlespire folder: GAME.EXE and GAMEDATA aren't in it.");
        color = C_BAD;
        break;
    }
    if (l->note[0]) {
        snprintf(status, sizeof status, "%s", l->note);
        color = C_BAD;
    }
    text(r, field_rect.x, field_rect.y + field_rect.h + 12, 1, color, status);
    text(r, field_rect.x, field_rect.y + field_rect.h + 28, 1, C_DIM,
         "Browse, or type, paste or drop the folder here.");

    draw_button(l, BUTTON_BROWSE, !l->dialog_open);
    draw_button(l, BUTTON_PLAY, l->state == FOLDER_OK && !l->dialog_open);
    draw_button(l, BUTTON_QUIT, true);

    text_centered(r, 448, 1, C_DIM, "OpenBattlespire needs the files of your own copy of Battlespire.");
    SDL_FlushRenderer(r);
}

static void show(launcher *l)
{
    bs_screen_set(l->screen, l->canvas->pixels, CANVAS_W, CANVAS_H);
    int w, h;
    SDL_GetWindowSizeInPixels(l->window, &w, &h);
    bs_screen_draw(l->screen, w, h);
}

/* ---- input ---- */

static button button_at(launcher *l, float wx, float wy)
{
    int w, h, x, y;
    SDL_GetWindowSize(l->window, &w, &h);
    if (!bs_screen_to_image(l->screen, w, h, wx, wy, &x, &y))
        return BUTTON_NONE;
    for (int b = 0; b < BUTTON_COUNT; b++) {
        SDL_FRect rect = button_rect[b];
        if ((float)x >= rect.x && (float)x < rect.x + rect.w && (float)y >= rect.y && (float)y < rect.y + rect.h)
            return (button)b;
    }
    return BUTTON_NONE;
}

typedef enum outcome { GOING_ON, PLAY, QUIT } outcome;

static outcome press(launcher *l, button b)
{
    if (l->dialog_open && b != BUTTON_QUIT)
        return GOING_ON;
    switch (b) {
    case BUTTON_BROWSE:
        browse(l);
        break;
    case BUTTON_PLAY:
        if (l->state == FOLDER_OK)
            return PLAY;
        break;
    case BUTTON_QUIT:
        return QUIT;
    default:
        break;
    }
    return GOING_ON;
}

static outcome handle(launcher *l, const SDL_Event *e)
{
    if (e->type == l->dialog_event) {
        dialog_answered(l, &e->user);
        return GOING_ON;
    }
    switch (e->type) {
    case SDL_EVENT_QUIT:
        return QUIT;
    case SDL_EVENT_MOUSE_MOTION:
        l->hover = button_at(l, e->motion.x, e->motion.y);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e->button.button == SDL_BUTTON_LEFT)
            l->pressed = button_at(l, e->button.x, e->button.y);
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e->button.button == SDL_BUTTON_LEFT) {
            button b = button_at(l, e->button.x, e->button.y);
            bool same = b == l->pressed;
            l->pressed = BUTTON_NONE;
            if (same && b != BUTTON_NONE)
                return press(l, b);
        }
        break;
    case SDL_EVENT_DROP_FILE:
        if (e->drop.data)
            set_folder(l, e->drop.data);
        break;
    case SDL_EVENT_TEXT_INPUT:
        if (!l->dialog_open)
            append_text(l, e->text.text);
        break;
    case SDL_EVENT_KEY_DOWN: {
        bool ctrl = (e->key.mod & SDL_KMOD_CTRL) != 0;
        SDL_Keycode k = e->key.key;
        if (k == SDLK_ESCAPE)
            return QUIT;
        if (l->dialog_open)
            break;
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER)
            return press(l, BUTTON_PLAY);
        if (k == SDLK_BACKSPACE) {
            if (ctrl) {
                l->dir[0] = '\0';
                check_folder(l);
            } else {
                delete_char(l);
            }
        } else if ((ctrl && k == SDLK_V) || ((e->key.mod & SDL_KMOD_SHIFT) && k == SDLK_INSERT)) {
            char *clip = SDL_GetClipboardText();
            if (clip && clip[0]) {
                char joined[BS_PATH_MAX * 2];
                snprintf(joined, sizeof joined, "%s%s", l->dir, clip);
                set_folder(l, joined);
            }
            SDL_free(clip);
        }
        break;
    }
    default:
        break;
    }
    return GOING_ON;
}

bool launcher_run(SDL_Window *window, bs_screen *screen, char *dir, size_t cap, const launcher_testing *testing)
{
    launcher l = {.window = window, .screen = screen, .dir = dir, .cap = cap, .hover = BUTTON_NONE,
                  .pressed = BUTTON_NONE};
    l.canvas = SDL_CreateSurface(CANVAS_W, CANVAS_H, SDL_PIXELFORMAT_RGBA32);
    l.r = l.canvas ? SDL_CreateSoftwareRenderer(l.canvas) : NULL;
    l.dialog_event = SDL_RegisterEvents(1);
    if (!l.r || l.canvas->pitch != CANVAS_W * 4 || l.dialog_event == 0) {
        fprintf(stderr, "openbattlespire: startup screen: %s\n", SDL_GetError());
        SDL_DestroySurface(l.canvas);
        return false;
    }
    check_folder(&l);
    SDL_StartTextInput(window);

    outcome out = GOING_ON;
    uint64_t start = SDL_GetTicks();
    while (out == GOING_ON) {
        SDL_Event e;
        /* Nothing moves but the caret: wait for events, waking up for its blinking. */
        if (SDL_WaitEventTimeout(&e, 250)) {
            do
                out = handle(&l, &e);
            while (out == GOING_ON && SDL_PollEvent(&e));
        }
        uint64_t now = SDL_GetTicks();
        draw(&l, now);
        show(&l);
        if (testing && testing->screenshot && now - start >= testing->after_ms) {
            int w, h;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            testing->save(testing->screenshot, w, h);
            out = QUIT;
        }
        SDL_GL_SwapWindow(window);
    }

    SDL_StopTextInput(window);
    SDL_DestroyRenderer(l.r);
    SDL_DestroySurface(l.canvas);
    return out == PLAY;
}
