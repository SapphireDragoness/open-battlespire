#include "loadsave.h"

#include <stdio.h>
#include <string.h>

#include "bs/core/fs.h"
#include "bs/core/ini.h"

/* RE references are GAME.EXE addresses; see docs/formats/game_save.md. */

static const int ok_button[4] = {252, 434, 375, 467};     /* Enter */
static const int exit_button[4] = {590, 427, 634, 473};   /* Esc */
#define SLOT_W 84                     /* a slot's button (0xEDA93) */
#define SLOT_H 53
#define FRAME_D 4                     /* brdr01.bsi from 4 up and left (0x31776) */
#define NAME_Y 397                    /* the chosen slot's name, centred (0x1D5BB) ... */
#define NAME_X 141                    /* ... and where one is typed (0x31714) */
#define PAGE_Y 160                    /* which slots these are (ours) */
#define SLIDE_MS 200
#define FRAME_MS 60
#define DOUBLE_CLICK_MS 400
#define MSG_ENTER_NAME "Enter name of save game."

static bool in(const int r[4], int x, int y)
{
    return x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3];
}

/* ---- the slots ---- */

void save_slot_dir(const char *dir, int slot, char *out, size_t cap)
{
    snprintf(out, cap, "%s/SAVE%d", dir, slot);
}

void save_slot_xy(int slot, int *x, int *y)
{
    *x = 48 + (slot % 5) * 112;
    *y = slot < 5 ? 192 : 304;
}

static bool read_file(const char *dir, const char *name, bs_arena *a, uint8_t **data, size_t *size)
{
    char path[1100], lower[32];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (bs_fs_read_file(path, a, data, size) == BS_OK)
        return true;
    size_t i = 0;
    for (; name[i] && i + 1 < sizeof lower; i++)
        lower[i] = (char)(name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 32 : name[i]);
    lower[i] = '\0';
    snprintf(path, sizeof path, "%s/%s", dir, lower);
    return bs_fs_read_file(path, a, data, size) == BS_OK;
}

void save_slots_read(const char *dir, bool ours, save_slot slots[SAVE_SLOTS])
{
    bs_arena a;
    bs_arena_init(&a, 0);
    for (int n = 0; n < SAVE_SLOTS; n++) {
        save_slot *s = &slots[n];
        s->present = s->have_picture = false;
        s->name[0] = '\0';
        if (!dir)
            continue;
        char slot[1024];
        save_slot_dir(dir, n, slot, sizeof slot);
        uint8_t *data;
        size_t size;
        if (ours && read_file(slot, SAVE_FILE, &a, &data, &size)) {
            /* The name is [save]'s. */
            bs_ini ini;
            if (bs_ini_parse((const char *)data, size, &a, &ini, NULL) == BS_OK) {
                for (uint32_t k = 0; k < ini.count; k++)
                    if (!strcmp(ini.sections[k].name, "save")) {
                        const char *name = bs_ini_get(&ini.sections[k], "name");
                        snprintf(s->name, sizeof s->name, "%s", name ? name : "");
                        s->present = true;
                    }
            }
        } else if (!ours && read_file(slot, "SAVENAME.DAT", &a, &data, &size)) {
            s->present = true; /* (0x31468) */
            size_t k = 0;
            for (; k < size && k < 24 && k + 1 < sizeof s->name && data[k]; k++)
                s->name[k] = (char)data[k];
            s->name[k] = '\0';
        }
        if (read_file(slot, "IMAGE.RAW", &a, &data, &size) && size >= BS_SAVE_IMAGE_SIZE) {
            bs_save_image_rgb(data, s->rgb);
            s->have_picture = true;
        }
        bs_arena_reset(&a);
    }
    bs_arena_free(&a);
}

/* ---- the screen ---- */

static void read_page(loadsave *ls, const loadsave_env *env)
{
    save_slots_read(ls->originals ? env->original_dir : env->saves_dir, !ls->originals, ls->slots);
}

void loadsave_open(loadsave *ls, const loadsave_env *env, loadsave_mode mode)
{
    memset(ls, 0, sizeof *ls);
    ls->open = true;
    ls->mode = mode;
    ls->click_at = -1;
    read_page(ls, env);
}

static void pick(loadsave *ls, const loadsave_env *env, int slot)
{
    if (slot < 0 || slot >= SAVE_SLOTS || slot == ls->slot)
        return;
    ls->from = ls->slot;
    ls->slide_ms = env->now_ms;
    ls->slot = slot;
}

static void say(const loadsave_env *env, const char *text)
{
    if (env->message)
        env->message(env->ctx, text);
}

/* OK, Enter or a double click (0x3163E): load the slot, or start naming the save. */
static loadsave_result go(loadsave *ls, const loadsave_env *env)
{
    if (ls->mode == LOADSAVE_LOAD) {
        if (!ls->slots[ls->slot].present)
            return LOADSAVE_NOTHING;
        ls->open = false;
        return LOADSAVE_DO_LOAD;
    }
    ls->naming = true;
    snprintf(ls->name, sizeof ls->name, "%.23s", ls->slots[ls->slot].present ? ls->slots[ls->slot].name : "");
    say(env, MSG_ENTER_NAME);
    return LOADSAVE_NOTHING;
}

loadsave_result loadsave_event(loadsave *ls, const loadsave_env *env, const SDL_Event *e, int x, int y)
{
    if (!ls->open)
        return LOADSAVE_NOTHING;
    if (ls->naming) {
        if (e->type == SDL_EVENT_TEXT_INPUT) {
            for (const char *p = e->text.text; *p; p++) {
                size_t n = strlen(ls->name);
                if ((unsigned char)*p >= ' ' && (unsigned char)*p < 0x7F && n < SAVE_NAME_MAX) {
                    ls->name[n] = *p;
                    ls->name[n + 1] = '\0';
                }
            }
        } else if (e->type == SDL_EVENT_KEY_DOWN) {
            SDL_Keycode k = e->key.key;
            if (k == SDLK_BACKSPACE && ls->name[0])
                ls->name[strlen(ls->name) - 1] = '\0';
            else if (k == SDLK_ESCAPE)
                ls->naming = false;
            else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && ls->name[0]) {
                ls->naming = false;
                ls->open = false;
                return LOADSAVE_DO_SAVE;
            }
        }
        return LOADSAVE_NOTHING;
    }
    if (e->type == SDL_EVENT_KEY_DOWN) {
        SDL_Keycode k = e->key.key;
        bool shift = (e->key.mod & SDL_KMOD_SHIFT) != 0;
        if ((k == SDLK_RIGHT || (k == SDLK_TAB && !shift)) && ls->slot < SAVE_SLOTS - 1)
            pick(ls, env, ls->slot + 1);
        else if ((k == SDLK_LEFT || (k == SDLK_TAB && shift)) && ls->slot > 0)
            pick(ls, env, ls->slot - 1);
        else if (k == SDLK_UP && ls->slot >= 5)
            pick(ls, env, ls->slot - 5);
        else if (k == SDLK_DOWN && ls->slot < 5)
            pick(ls, env, ls->slot + 5);
        else if ((k == SDLK_PAGEDOWN || k == SDLK_PAGEUP) && ls->mode == LOADSAVE_LOAD && env->original_dir) {
            ls->originals = k == SDLK_PAGEDOWN;
            read_page(ls, env);
        } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER)
            return go(ls, env);
        else if (k == SDLK_ESCAPE) {
            ls->open = false;
            return LOADSAVE_CLOSED;
        }
        return LOADSAVE_NOTHING;
    }
    if (e->type != SDL_EVENT_MOUSE_BUTTON_DOWN || e->button.button != SDL_BUTTON_LEFT)
        return LOADSAVE_NOTHING;
    for (int n = 0; n < SAVE_SLOTS; n++) {
        int sx, sy;
        save_slot_xy(n, &sx, &sy);
        const int r[4] = {sx, sy, sx + SLOT_W, sy + SLOT_H - 1};
        if (!in(r, x, y))
            continue;
        pick(ls, env, n);
        bool twice = ls->click_at == n && env->now_ms - ls->click_ms < DOUBLE_CLICK_MS;
        ls->click_at = n;
        ls->click_ms = env->now_ms;
        return twice ? go(ls, env) : LOADSAVE_NOTHING; /* (0x31539) */
    }
    if (in(ok_button, x, y))
        return go(ls, env);
    if (in(exit_button, x, y)) {
        ls->open = false;
        return LOADSAVE_CLOSED;
    }
    return LOADSAVE_NOTHING;
}

static void text_centred(bs_overlay *o, const bs_font *f, int cx, int y, const char *s)
{
    if (f)
        bs_overlay_text(o, f, cx - bs_text_width(f, s) / 2, y, s);
}

void loadsave_draw(const loadsave *ls, const loadsave_env *env, bs_overlay *o)
{
    if (!ls->open)
        return;
    bs_overlay_image(o, bs_assets_image(env->assets, "loadsave.bsi"), 0, 0);
    bs_overlay_image(o, bs_assets_image(env->assets, ls->mode == LOADSAVE_LOAD ? "load.bsi" : "save.bsi"), 0, 0);
    for (int n = 0; n < SAVE_SLOTS; n++) {
        int x, y;
        save_slot_xy(n, &x, &y);
        if (ls->slots[n].have_picture)
            bs_overlay_rgb(o, ls->slots[n].rgb, BS_SAVE_IMAGE_WIDTH, BS_SAVE_IMAGE_HEIGHT, x, y);
    }
    int fx, fy;
    save_slot_xy(ls->slot, &fx, &fy);
    uint64_t since = env->now_ms - ls->slide_ms;
    if (ls->slide_ms && since < SLIDE_MS) {
        int sx, sy;
        save_slot_xy(ls->from, &sx, &sy);
        fx = sx + (int)((int64_t)(fx - sx) * (int64_t)since / SLIDE_MS);
        fy = sy + (int)((int64_t)(fy - sy) * (int64_t)since / SLIDE_MS);
    }
    const bs_bsi_image *frame = bs_assets_image(env->assets, "brdr01.bsi");
    if (frame)
        bs_overlay_image_frame(o, frame, (uint32_t)(env->now_ms / FRAME_MS) % (frame->frames ? frame->frames : 1),
                               fx - FRAME_D - frame->x, fy - FRAME_D - frame->y);
    if (ls->mode == LOADSAVE_LOAD && env->original_dir)
        text_centred(o, env->fonts[8], 320, PAGE_Y,
                     ls->originals ? "The original game's saves  (Page Up: yours)"
                                   : "Your saves  (Page Down: the original game's)");
    if (ls->naming) {
        char s[SAVE_NAME_MAX + 2];
        snprintf(s, sizeof s, "%s%s", ls->name, (env->now_ms / 400) % 2 ? "_" : "");
        if (env->fonts[1])
            bs_overlay_text(o, env->fonts[1], NAME_X, NAME_Y, s);
    } else if (ls->slots[ls->slot].present) {
        text_centred(o, env->fonts[1], 320, NAME_Y, ls->slots[ls->slot].name);
    }
}
