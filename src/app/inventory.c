#include "inventory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RE references are GAME.EXE addresses; see docs/formats/items.md, "The inventory screen". */

/* The screen's buttons in 640x480 pixels (table at 0xEFB5C: x0, y0, x1, y1). */
static const int list_x[2][2] = {{320, 416}, {464, 560}};  /* the carried list, the other list */
#define BOX_Y 32           /* each list's open container */
#define ROW_Y 112          /* four item slots, 80 apart, 64 high */
#define ROW_STEP 80
#define ROW_H 64
#define ROWS 4
static const int scroll_rect[2][4] = {{430, 102, 451, 431}, {575, 102, 596, 431}};
static const int arrow_x[2][2] = {{430, 429}, {575, 575}};  /* where the dimmed arrows go, up and down */
#define ARROW_UP_Y 102
#define ARROW_DOWN_Y 390
#define SCROLL_SPLIT 240   /* clicks above scroll up, below down (0x4FC5A) */
static const int exit_rect[4] = {582, 424, 630, 474};
static const int filter_x[6] = {272, 329, 385, 441, 497, 553};
#define FILTER_W 55
#define FILTER_Y0 8
#define FILTER_Y1 26
#define INFO_X 325         /* the selected item's two lines, FONT0008 (0x4F286) */
#define INFO_Y 450
#define DRAG_START 8       /* pixels the mouse moves before an item is dragged (0x4FE50) */
/* The paper doll (0x56510): items dropped in it are equipped (0x4E802); below it the secondary
 * weapon and the arrows ready (0x4F8FA), the six armour values and hand-to-hand damage (0x542B8). */
static const int doll_rect[4] = {1, 1, 299, 389};
static const int secondary_rect[4] = {16, 400, 112, 464};
static const int arrows_rect[4] = {128, 400, 224, 464};
#define ARMOUR_X 285
#define ARMOUR_Y 405
#define ARMOUR_STEP 11
#define HAND_X 283
#define HAND_Y 388
#define DOLL_LAYERS 32
/* The book screen (0x54078, buttons at 0xEFB17): previous and next page, close; text in FONT0003
 * from x 80 (0x3CAA7). */
static const int book_prev[4] = {598, 79, 620, 119};
static const int book_next[4] = {598, 362, 620, 401};
static const int book_close[4] = {582, 424, 630, 474};
#define BOOK_X 80
#define FRAME_COLOR 0x00C600FFu  /* the selected item's frame: RGB555 0x300 */
/* The slot camera (0x51030): the model at 4 x its radius ahead, a quarter
 * radius down, projected through a 200 x 180 view plane (0x6EB78), so x is
 * scaled by 360 / 200 and y by 360 / 180 (inferred from the 3D view's
 * 53.13 degree field of view with its 240 x 180 plane). */
#define SLOT_SX 1.8f
#define SLOT_SY 2.0f

enum { FILTER_WEAPONS = 1, FILTER_MAGIC = 2, FILTER_MISC = 4, FILTER_CLOTHING = 8, FILTER_POTIONS = 16,
       FILTER_AMULETS = 32, FILTER_ALL = 63 };

static bool in_rect(int x, int y, int x0, int y0, int x1, int y1)
{
    return x >= x0 && x <= x1 && y >= y0 && y <= y1;
}

uint32_t inventory_models(const bs_item_tables *t, char names[INVENTORY_MODELS][16])
{
    for (uint32_t i = 0; i < BS_ITEM_MODEL_COUNT; i++)
        memcpy(names[i], t->models[i], 16);
    for (uint32_t i = 0; i < 26; i++)
        snprintf(names[BS_ITEM_MODEL_COUNT + i], 16, "sigil%c.3d", 'a' + (char)i);
    return INVENTORY_MODELS;
}

uint32_t inventory_item_object(const inventory_env *env, uint32_t item)
{
    if (item >= env->items->count || env->model_first == UINT32_MAX)
        return UINT32_MAX;
    const bs_item *it = &env->items->items[item].item;
    char name[16];
    bs_item_model_name(env->db->tables, it->base, it->magic ? it->magic : it->number, name);
    if (strncmp(name, "sigil", 5) == 0 && name[5] >= 'a' && name[5] <= 'z')
        return env->model_first + BS_ITEM_MODEL_COUNT + (uint32_t)(name[5] - 'a');
    uint16_t image = env->db->tables->bases[it->base].image;
    return name[0] && image < BS_ITEM_MODEL_COUNT ? env->model_first + image : UINT32_MAX;
}

static void clamp_scroll(inventory *inv, const inventory_env *env);

void inventory_init(inventory *inv)
{
    memset(inv, 0, sizeof *inv);
    inv->filters = FILTER_ALL;
    inv->selected = inv->held = UINT32_MAX;
}

void inventory_open(inventory *inv, const inventory_env *env, bs_item_place other)
{
    uint8_t filters = inv->filters;
    inventory_init(inv);
    inv->filters = filters;
    inv->open = true;
    inv->stack[0][0] = (bs_item_place){BS_ITEM_CARRIED, 0};
    inv->stack[1][0] = other;
    inv->depth[0] = inv->depth[1] = 1;
    clamp_scroll(inv, env);
}

static void close_book(inventory *inv);

void inventory_close(inventory *inv)
{
    close_book(inv);
    inv->open = false;
    inv->held = inv->selected = UINT32_MAX;
    inv->pressed = false;
}

static bs_item_place top(const inventory *inv, int list)
{
    return inv->stack[list][inv->depth[list] - 1];
}

/* The filter test (0x54BC9): filters only apply to carried items; containers always show. */
static bool shown(const inventory *inv, const inventory_env *env, uint32_t i)
{
    const bs_world_item *w = &env->items->items[i];
    const bs_item *it = &w->item;
    if (!bs_items_place_carried(env->items, w->place) || env->db->tables->bases[it->base].container)
        return true;
    uint8_t f = inv->filters;
    if (it->base == BS_ITEM_POTION)
        return f & FILTER_POTIONS;
    if (it->base == BS_ITEM_SIGIL_AMULET)
        return f & FILTER_AMULETS;
    if (it->magic)
        return f & FILTER_MAGIC;
    if (bs_item_is_weapon(it->base) || it->base == BS_ITEM_ARROW)
        return f & FILTER_WEAPONS;
    if (it->base == BS_ITEM_CLOTHES)
        return f & FILTER_CLOTHING;
    return f & FILTER_MISC;
}

#define LIST_MAX 512

/* The items a list shows, filtered, without the one being dragged. */
static uint32_t visible(const inventory *inv, const inventory_env *env, int list, uint32_t *out)
{
    static uint32_t all[LIST_MAX];
    uint32_t n = bs_items_list(env->items, top(inv, list), all, LIST_MAX), k = 0;
    for (uint32_t i = 0; i < n && i < LIST_MAX; i++) {
        const bs_world_item *w = &env->items->items[all[i]];
        /* Worn items and the secondary weapon are on the doll instead (0x4ED43). */
        if (all[i] != inv->held && !w->equipped && !w->secondary && shown(inv, env, all[i]))
            out[k++] = all[i];
    }
    return k;
}

static void clamp_scroll(inventory *inv, const inventory_env *env)
{
    static uint32_t v[LIST_MAX];
    for (int l = 0; l < 2; l++) {
        int n = (int)visible(inv, env, l, v);
        int max = n > ROWS ? n - ROWS : 0;
        if (inv->scroll[l] > max)
            inv->scroll[l] = max;
        if (inv->scroll[l] < 0)
            inv->scroll[l] = 0;
    }
}

/* Closes containers that moved away from where their list opened them. */
static void check_stacks(inventory *inv, const inventory_env *env)
{
    for (int l = 0; l < 2; l++)
        for (int d = 1; d < inv->depth[l]; d++) {
            bs_item_place p = inv->stack[l][d];
            const bs_world_item *w = p.ref < env->items->count ? &env->items->items[p.ref] : NULL;
            bs_item_place parent = inv->stack[l][d - 1];
            if (!w || w->place.where != parent.where || (parent.where != BS_ITEM_CARRIED && w->place.ref != parent.ref)) {
                inv->depth[l] = d;
                break;
            }
        }
}

/* Which list and row a point is over: row 0 the box, 1-4 the slots, -1 between. Returns the list or -1. */
static int slot_at(int x, int y, int *row)
{
    for (int l = 0; l < 2; l++) {
        if (x < list_x[l][0] || x > list_x[l][1])
            continue;
        *row = -1;
        if (y >= BOX_Y && y <= BOX_Y + ROW_H)
            *row = 0;
        for (int r = 0; r < ROWS; r++)
            if (y >= ROW_Y + r * ROW_STEP && y <= ROW_Y + r * ROW_STEP + ROW_H)
                *row = r + 1;
        if (y >= BOX_Y && y <= ROW_Y + (ROWS - 1) * ROW_STEP + ROW_H)
            return l;
    }
    return -1;
}

/* The item in a list's row (1-4), or UINT32_MAX. Row 0: the open container, if it is an item. */
static uint32_t item_at(const inventory *inv, const inventory_env *env, int list, int row)
{
    if (row == 0) {
        bs_item_place p = top(inv, list);
        return p.where == BS_ITEM_IN_ITEM ? p.ref : UINT32_MAX;
    }
    static uint32_t v[LIST_MAX];
    uint32_t n = visible(inv, env, list, v);
    uint32_t k = (uint32_t)(inv->scroll[list] + row - 1);
    return row >= 1 && k < n ? v[k] : UINT32_MAX;
}

static void say(const inventory_env *env, const char *text)
{
    if (env->message && text && text[0])
        env->message(env->ctx, text);
}

/* ---- the paper doll ---- */

typedef struct doll_layer {
    const bs_bsi_image *img;
    uint32_t item;           /* UINT32_MAX: the body, face or frame */
} doll_layer;

static int picture_order(const char *picture)
{
    return picture[0] ? atoi(picture + 5) : 0;
}

/* The doll's images, bottom first (0x56510): body, eyes, mouth, hair (not under a helmet), the
 * worn items by the number in their picture's name, the frame. */
static int doll_layers(const inventory *inv, const inventory_env *env, doll_layer *out)
{
    int n = 0;
    if (!env->image)
        return 0;
    char sex = env->female ? 'f' : 'm', name[32];
    bool helmet = bs_items_worn(env->items, BS_ITEM_HELMET) != UINT32_MAX &&
                  bs_items_worn(env->items, BS_ITEM_HELMET) != inv->held;
    snprintf(name, sizeof name, "%c%sbd001.bsi", sex, env->race);
    out[n++] = (doll_layer){env->image(env->ctx, name), UINT32_MAX};
    snprintf(name, sizeof name, "%c%sey%03d.bsi", sex, env->race, env->eyes);
    out[n++] = (doll_layer){env->image(env->ctx, name), UINT32_MAX};
    snprintf(name, sizeof name, "%c%smt%03d.bsi", sex, env->race, env->mouth);
    out[n++] = (doll_layer){env->image(env->ctx, name), UINT32_MAX};
    if (!helmet) {
        snprintf(name, sizeof name, "%c%shr%03d.bsi", sex, env->race, env->hair);
        out[n++] = (doll_layer){env->image(env->ctx, name), UINT32_MAX};
    }
    int first = n;
    for (uint32_t i = 0; i < env->items->count && n < DOLL_LAYERS - 1; i++) {
        const bs_world_item *w = &env->items->items[i];
        if (!w->equipped || i == inv->held || !w->item.picture[0] || w->place.where == BS_ITEM_GONE ||
            !bs_items_place_carried(env->items, w->place))
            continue;
        snprintf(name, sizeof name, "%s.bsi", w->item.picture);
        doll_layer l = {env->image(env->ctx, name), i};
        /* Insertion sort by the picture's number, as the game's bubble sort (0x56999). */
        int k = n++;
        while (k > first && picture_order(env->items->items[out[k - 1].item].item.picture) >
                                picture_order(w->item.picture)) {
            out[k] = out[k - 1];
            k--;
        }
        out[k] = l;
    }
    out[n++] = (doll_layer){env->image(env->ctx, "fram00.bsi"), UINT32_MAX};
    return n;
}

/* The worn item drawn at a doll point: the topmost whose image isn't transparent there (0x56CFA). */
static uint32_t doll_item_at(const inventory *inv, const inventory_env *env, int x, int y)
{
    doll_layer l[DOLL_LAYERS];
    int n = doll_layers(inv, env, l);
    for (int k = n - 1; k >= 0; k--) {
        const bs_bsi_image *img = l[k].img;
        if (l[k].item == UINT32_MAX || !img || !img->pixels)
            continue;
        int ix = x - img->x, iy = y - img->y;
        if (ix < 0 || iy < 0 || ix >= img->width || iy >= img->height)
            continue;
        if (!img->transparent || img->pixels[(size_t)iy * img->width + (size_t)ix] != 0)
            return l[k].item;
    }
    return UINT32_MAX;
}

/* What lies under a point of the left side: the secondary weapon, the arrows, or a worn item. */
static uint32_t worn_at(const inventory *inv, const inventory_env *env, int x, int y)
{
    if (in_rect(x, y, secondary_rect[0], secondary_rect[1], secondary_rect[2], secondary_rect[3]))
        return bs_items_secondary(env->items);
    if (in_rect(x, y, arrows_rect[0], arrows_rect[1], arrows_rect[2], arrows_rect[3]))
        return bs_items_arrows(env->items);
    if (in_rect(x, y, doll_rect[0], doll_rect[1], doll_rect[2], doll_rect[3]))
        return doll_item_at(inv, env, x, y);
    return UINT32_MAX;
}

static void report(inventory *inv, const inventory_env *env, bs_equip e)
{
    switch (e) {
    case BS_EQUIP_OK:
        inv->changed = true;
        break;
    case BS_EQUIP_NOT_ALLOWED:
        say(env, bs_exe_message(env->text, BS_MSG_NOT_ALLOWED));
        break;
    case BS_EQUIP_TOO_HEAVY:
        say(env, bs_exe_message(env->text, BS_MSG_CANNOT_CARRY));
        break;
    case BS_EQUIP_NO_ARROWS:
        say(env, bs_exe_message(env->text, BS_MSG_NO_ARROWS));
        break;
    case BS_EQUIP_INVALID:
        break;
    }
}

/* Drops the dragged item on the doll or its slots (0x4E802, 0x507DD, 0x5087A); false if not there. */
static bool drop_on_doll(inventory *inv, const inventory_env *env, uint32_t held)
{
    int x = inv->mouse_x, y = inv->mouse_y;
    const bs_item *it = &env->items->items[held].item;
    if (in_rect(x, y, secondary_rect[0], secondary_rect[1], secondary_rect[2], secondary_rect[3])) {
        if (bs_item_is_weapon(it->base))
            report(inv, env, bs_items_set_secondary(env->items, env->db, held, env->rules, env->strength));
        return true;
    }
    if (in_rect(x, y, arrows_rect[0], arrows_rect[1], arrows_rect[2], arrows_rect[3])) {
        if (it->base == BS_ITEM_ARROW)
            report(inv, env, bs_items_equip(env->items, env->db, held, env->rules, env->strength));
        return true;
    }
    if (in_rect(x, y, doll_rect[0], doll_rect[1], doll_rect[2], doll_rect[3])) {
        report(inv, env, bs_items_equip(env->items, env->db, held, env->rules, env->strength));
        return true;
    }
    return false;
}

/* Drops the dragged item where the mouse is (0x50763). */
static void drop(inventory *inv, const inventory_env *env)
{
    uint32_t held = inv->held;
    inv->held = UINT32_MAX;
    if (drop_on_doll(inv, env, held)) {
        if (bs_items_check_bow(env->items))
            inv->changed = true;
        clamp_scroll(inv, env);
        return;
    }
    int row;
    int list = slot_at(inv->mouse_x, inv->mouse_y, &row);
    if (list < 0)
        return; /* back where it was */
    bs_item_place to = top(inv, list);
    uint32_t onto = row >= 1 ? item_at(inv, env, list, row) : UINT32_MAX;
    if (onto != UINT32_MAX && onto != held && bs_items_is_container(env->db, env->items, onto))
        to = (bs_item_place){BS_ITEM_IN_ITEM, onto};
    switch (bs_items_move(env->items, env->db, held, to, env->drop_pos, env->strength)) {
    case BS_MOVE_OK:
        inv->changed = true;
        /* Dropped on the floor, it lies at the player's feet; the list shows that cell (0x50AC9). */
        if (to.where == BS_ITEM_FLOOR) {
            inv->stack[list][0] = env->items->items[held].place;
            inv->depth[list] = 1;
        }
        break;
    case BS_MOVE_JOINED:
        inv->changed = true;
        if (inv->selected == held)
            inv->selected = UINT32_MAX;
        say(env, bs_exe_message(env->text, BS_MSG_COMBINE));
        break;
    case BS_MOVE_TOO_HEAVY:
        say(env, bs_exe_message(env->text, BS_MSG_CANNOT_CARRY));
        break;
    default:
        break;
    }
    /* Without arrows ready a bow is put away (0x4E7A4). */
    if (bs_items_check_bow(env->items))
        inv->changed = true;
    check_stacks(inv, env);
    clamp_scroll(inv, env);
}

/* A game message with its "%d"s filled in, in order. */
static void format_message(char *out, size_t cap, const char *msg, const int32_t args[3])
{
    size_t n = 0;
    int k = 0;
    for (const char *p = msg; *p && n + 1 < cap; p++) {
        if (p[0] == '%' && p[1] == 'd' && k < 3) {
            int w = snprintf(out + n, cap - n, "%d", args[k++]);
            n = w > 0 && n + (size_t)w < cap ? n + (size_t)w : cap - 1;
            p++;
        } else {
            out[n++] = *p;
        }
    }
    out[n] = '\0';
}

static int book_advance(void *ctx, uint8_t c)
{
    return bs_text_advance(ctx, c);
}

/* Opens the book screen with a book's text (0x54078). */
static void open_book(inventory *inv, const inventory_env *env, int32_t number)
{
    size_t size = 0;
    const uint8_t *text = env->book_text ? env->book_text(env->ctx, number, &size) : NULL;
    if (!text || !env->font_book)
        return;
    if (inv->reading)
        bs_arena_free(&inv->book_arena);
    bs_arena_init(&inv->book_arena, 0);
    if (bs_book_layout(text, size, book_advance, (void *)env->font_book, bs_text_height(env->font_book),
                       &inv->book_arena, &inv->book) != BS_OK) {
        bs_arena_free(&inv->book_arena);
        inv->reading = false;
        return;
    }
    inv->reading = true;
    inv->page = 0;
}

static void close_book(inventory *inv)
{
    if (inv->reading)
        bs_arena_free(&inv->book_arena);
    inv->reading = false;
}

/* Uses an item (0x501B4). */
static void use_item(inventory *inv, const inventory_env *env, uint32_t i)
{
    if (!env->character)
        return;
    bs_use_result u = bs_items_use(env->items, env->db, i, env->character, env->spells, env->level_number,
                                   env->now_ms, env->rng);
    for (uint32_t k = 0; k < u.message_count; k++) {
        char text[160];
        format_message(text, sizeof text, bs_exe_message(env->text, u.messages[k].id), u.messages[k].args);
        say(env, text);
    }
    if (u.kind == BS_USE_READ)
        open_book(inv, env, u.book);
    if (u.spell && env->drink)
        env->drink(env->ctx, u.spell);
    if (u.kind == BS_USE_DONE) {
        inv->changed = true;
        if (inv->selected < env->items->count && env->items->items[inv->selected].place.where == BS_ITEM_GONE)
            inv->selected = UINT32_MAX;
        clamp_scroll(inv, env);
    }
}

/* Double-click (0x50036): the box goes back out, a container opens, anything else is used. */
static void open_or_back(inventory *inv, const inventory_env *env, int list, int row)
{
    if (row == 0) {
        if (inv->depth[list] > 1)
            inv->depth[list]--;
    } else {
        uint32_t i = item_at(inv, env, list, row);
        if (i == UINT32_MAX)
            return;
        if (!bs_items_is_container(env->db, env->items, i)) {
            use_item(inv, env, i);
            return;
        }
        if (inv->depth[list] >= INVENTORY_DEPTH)
            return;
        inv->stack[list][inv->depth[list]++] = (bs_item_place){BS_ITEM_IN_ITEM, i};
    }
    inv->scroll[list] = 0;
    inv->selected = UINT32_MAX;
}

/* The 640x480 screen's placement in the window, as bs_screen letterboxes it. */
static void placement(int ww, int wh, float *scale, int *ox, int *oy)
{
    float sx = (float)ww / 640.0f, sy = (float)wh / 480.0f;
    *scale = sx < sy ? sx : sy;
    *ox = (int)(((float)ww - 640.0f * *scale) / 2.0f);
    *oy = (int)(((float)wh - 480.0f * *scale) / 2.0f);
}

static void to_screen(int ww, int wh, float wx, float wy, int *x, int *y)
{
    float s;
    int ox, oy;
    placement(ww, wh, &s, &ox, &oy);
    *x = (int)((wx - (float)ox) / s);
    *y = (int)((wy - (float)oy) / s);
}

/* The book screen's events; it closes back to the inventory. */
static void book_event(inventory *inv, const SDL_Event *e, int ww, int wh)
{
    if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN && e->button.button == SDL_BUTTON_LEFT) {
        int x, y;
        to_screen(ww, wh, e->button.x, e->button.y, &x, &y);
        if (in_rect(x, y, book_prev[0], book_prev[1], book_prev[2], book_prev[3]) && inv->page > 0)
            inv->page--;
        else if (in_rect(x, y, book_next[0], book_next[1], book_next[2], book_next[3]) &&
                 inv->page + 1 < inv->book.page_count)
            inv->page++;
        else if (in_rect(x, y, book_close[0], book_close[1], book_close[2], book_close[3]))
            close_book(inv);
    } else if (e->type == SDL_EVENT_KEY_DOWN) {
        SDL_Keycode k = e->key.key;
        if (k == SDLK_ESCAPE)
            close_book(inv);
        else if ((k == SDLK_PAGEUP || k == SDLK_LEFT || k == SDLK_UP) && inv->page > 0)
            inv->page--;
        else if ((k == SDLK_PAGEDOWN || k == SDLK_RIGHT || k == SDLK_DOWN || k == SDLK_SPACE) &&
                 inv->page + 1 < inv->book.page_count)
            inv->page++;
    }
}

bool inventory_event(inventory *inv, const inventory_env *env, const SDL_Event *e, int ww, int wh)
{
    if (!inv->open)
        return false;
    if (inv->reading) {
        book_event(inv, e, ww, wh);
        return true;
    }
    if (e->type == SDL_EVENT_MOUSE_MOTION) {
        to_screen(ww, wh, e->motion.x, e->motion.y, &inv->mouse_x, &inv->mouse_y);
        int dx = inv->mouse_x - inv->press_x, dy = inv->mouse_y - inv->press_y;
        if (inv->pressed && inv->held == UINT32_MAX && inv->selected != UINT32_MAX &&
            dx * dx + dy * dy > DRAG_START * DRAG_START) {
            inv->held = inv->selected;
            clamp_scroll(inv, env);
        }
    } else if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN && e->button.button == SDL_BUTTON_LEFT) {
        int x, y, row;
        to_screen(ww, wh, e->button.x, e->button.y, &x, &y);
        inv->mouse_x = inv->press_x = x;
        inv->mouse_y = inv->press_y = y;
        int list = slot_at(x, y, &row);
        if (list >= 0 && row >= 0) {
            if (e->button.clicks >= 2) {
                open_or_back(inv, env, list, row);
            } else {
                /* Pressing picks the item (0x4FFF8); moving drags it. */
                uint32_t i = item_at(inv, env, list, row);
                if (i != UINT32_MAX && row > 0)
                    inv->selected = i;
                inv->pressed = i != UINT32_MAX && row > 0;
            }
        } else if (worn_at(inv, env, x, y) != UINT32_MAX && e->button.clicks < 2) {
            /* The doll and its slots: pick a worn item, to drag it off (0x4FA17). */
            inv->selected = worn_at(inv, env, x, y);
            inv->pressed = true;
        } else if (in_rect(x, y, exit_rect[0], exit_rect[1], exit_rect[2], exit_rect[3])) {
            inventory_close(inv);
            return false;
        } else {
            for (int l = 0; l < 2; l++)
                if (in_rect(x, y, scroll_rect[l][0], scroll_rect[l][1], scroll_rect[l][2], scroll_rect[l][3]))
                    inv->scroll[l] += y < SCROLL_SPLIT ? -1 : 1;
            for (int f = 0; f < 6; f++)
                if (in_rect(x, y, filter_x[f], FILTER_Y0, filter_x[f] + FILTER_W, FILTER_Y1)) {
                    /* A click toggles a filter; with Ctrl or Shift only it shows (0x54CC5). */
                    if (SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT))
                        inv->filters = (uint8_t)(1u << f);
                    else
                        inv->filters ^= (uint8_t)(1u << f);
                }
            clamp_scroll(inv, env);
        }
    } else if (e->type == SDL_EVENT_MOUSE_BUTTON_UP && e->button.button == SDL_BUTTON_LEFT) {
        to_screen(ww, wh, e->button.x, e->button.y, &inv->mouse_x, &inv->mouse_y);
        inv->pressed = false;
        if (inv->held != UINT32_MAX)
            drop(inv, env);
    } else if (e->type == SDL_EVENT_MOUSE_WHEEL) {
        int row;
        int list = slot_at(inv->mouse_x, inv->mouse_y, &row);
        if (list < 0)
            list = inv->mouse_x > (scroll_rect[0][2] + list_x[1][0]) / 2 ? 1 : 0;
        inv->scroll[list] -= (int)e->wheel.y;
        clamp_scroll(inv, env);
    } else if (e->type == SDL_EVENT_KEY_DOWN && e->key.key == SDLK_ESCAPE) {
        inventory_close(inv);
        return false;
    } else if (e->type == SDL_EVENT_KEY_DOWN) {
        /* The carried list scrolls by four with Page Up/Down, by one with the arrows (0x4E720). */
        SDL_Keycode k = e->key.key;
        inv->scroll[0] += k == SDLK_PAGEUP ? -4 : k == SDLK_PAGEDOWN ? 4 : k == SDLK_UP ? -1 : k == SDLK_DOWN ? 1 : 0;
        clamp_scroll(inv, env);
    }
    return inv->open;
}

/* ---- drawing ---- */

static int32_t display_weight(const bs_item_db *db, const bs_item *it)
{
    int32_t w = bs_item_weight(db, it);
    if (it->base == BS_ITEM_POTION && it->quantity > 1)
        w = db->tables->bases[it->base].weight * it->quantity; /* 0x546F3 */
    return w;
}

/* A message with one "%d" in it, filled in. */
static void with_number(char *out, size_t cap, const char *msg, int32_t n)
{
    const char *d = strstr(msg, "%d");
    if (!d) {
        snprintf(out, cap, "%s", msg);
        return;
    }
    snprintf(out, cap, "%.*s%d%s", (int)(d - msg), msg, n, d + 2);
}

/* The selected item's two lines (0x4F37D). */
static void info_lines(const inventory_env *env, const bs_item *it, char *l1, char *l2, size_t cap)
{
    const bs_exe_text *t = env->text;
    char name[96], weight[48];
    bs_item_name(env->db, it, name, sizeof name);
    int32_t w = display_weight(env->db, it);
    /* Weights are in halves: "Weight: 2.5" (0x54678). */
    snprintf(weight, sizeof weight, "%s %d.%d", bs_exe_message(t, BS_MSG_WEIGHT), w / 2, w & 1 ? 5 : 0);
    int cond = it->condition_max > 0 ? it->condition * 100 / it->condition_max : 100;
    snprintf(l1, cap, "%s", name);
    if (bs_item_is_weapon(it->base)) {
        const bs_weapon_stats *ws = &env->db->tables->weapons[it->base];
        snprintf(l2, cap, "%s %d - %d  %s %d%%  %s", bs_exe_message(t, BS_MSG_DAMAGE),
                 it->material + ws->damage_min - 1, it->material + ws->damage_max - 1,
                 bs_exe_message(t, BS_MSG_CONDITION), cond, weight);
    } else if (it->base == BS_ITEM_ARROW) {
        snprintf(l1, cap, "%s", name);
        char *paren = strrchr(l1, '(');
        if (paren && paren > l1)
            paren[-1] = '\0'; /* the count goes on the second line */
        snprintf(l2, cap, "%s %d  %s %d%%  %s", bs_exe_message(t, BS_MSG_NUMBER), it->quantity,
                 bs_exe_message(t, BS_MSG_CONDITION), cond, weight);
    } else if (it->base == BS_ITEM_POTION) {
        int32_t n = it->quantity > 0 ? it->quantity : 1;
        if (it->number == 40 || it->number == 39)
            with_number(l1, cap, bs_exe_message(t, it->number == 40 ? BS_MSG_POTION_RESTORATION
                                                                   : BS_MSG_POTION_WATER_BREATHING), n);
        else if (env->spells && it->number >= 1 && it->number <= BS_SPELL_COUNT) {
            /* "%s of %s (%d)" (message 242) with the spell's name */
            const char *fmt = bs_exe_message(t, BS_MSG_POTION_OF);
            char a[96], b[96];
            snprintf(a, sizeof a, "%s", name);
            snprintf(b, sizeof b, "%s", env->spells[it->number - 1].name);
            const char *s1 = strstr(fmt, "%s"), *s2 = s1 ? strstr(s1 + 2, "%s") : NULL, *d = s2 ? strstr(s2 + 2, "%d") : NULL;
            if (d)
                snprintf(l1, cap, "%.*s%s%.*s%s%.*s%d%s", (int)(s1 - fmt), fmt, a, (int)(s2 - s1 - 2), s1 + 2, b,
                         (int)(d - s2 - 2), s2 + 2, n, d + 2);
            else
                snprintf(l1, cap, "%s (%d)", name, n);
        } else
            snprintf(l1, cap, "%s (%d)", name, n);
        snprintf(l2, cap, "%s", weight);
    } else {
        snprintf(l2, cap, "%s %d%%  %s", bs_exe_message(t, BS_MSG_CONDITION), cond, weight);
    }
}

/* Windows-1252 to the fonts' code page 437, where there is an equivalent. */
static void cp437(const char *in, char *out, size_t cap)
{
    size_t n = 0;
    for (const uint8_t *p = (const uint8_t *)in; *p && n + 1 < cap; p++) {
        uint8_t c = *p;
        if (c == 0x91 || c == 0x92)
            c = '\'';
        else if (c == 0x93 || c == 0x94)
            c = '"';
        else if (c >= 0x80)
            c = c == 0xE9 ? 130 : '?';
        out[n++] = (char)c;
    }
    out[n] = '\0';
}

/* An item's model spinning in a 640x480 rectangle (0x51030). */
static void draw_item(const inventory_env *env, uint32_t item, int x0, int y0, int x1, int y1, int ww, int wh,
                      uint64_t ms)
{
    uint32_t ob = inventory_item_object(env, item);
    if (ob == UINT32_MAX)
        return;
    const bs_item *it = &env->items->items[item].item;
    float r = env->model_radius[ob - env->model_first];
    if (r <= 0.0f)
        return;
    /* Every item turns about y; weapons, armour but helmets, and the Voidguide also about x; weapons about z. */
    int32_t t2 = (int32_t)((ms / 2) & 0x7FF), t1 = (int32_t)(ms & 0x7FF);
    bool spin_x = (bs_item_is_weapon(it->base) || bs_item_is_armour(it->base) || it->base == 30) &&
                  it->base != BS_ITEM_HELMET;
    int32_t angles[3] = {spin_x ? t2 : 0, t2, bs_item_is_weapon(it->base) ? t1 : 0};
    float offset[3] = {0.0f, r * 0.25f, r * 4.0f};
    float s;
    int ox, oy;
    placement(ww, wh, &s, &ox, &oy);
    int x = ox + (int)((float)x0 * s), y = oy + (int)((float)y0 * s);
    int w = (int)((float)(x1 - x0) * s), h = (int)((float)(y1 - y0) * s);
    bs_level_renderer_draw_object(env->renderer, ob, angles, offset, SLOT_SX, SLOT_SY, x, y, w, h, wh);
}

static void draw_book(inventory *inv, const inventory_env *env)
{
    bs_overlay *o = env->overlay;
    bs_overlay_clear(o);
    const bs_bsi_image *bg = env->image ? env->image(env->ctx, "textbk.bsi") : NULL;
    if (bg)
        bs_overlay_image(o, bg, -bg->x, -bg->y);
    uint32_t first, n = bs_book_page(&inv->book, inv->page, &first);
    int lh = bs_text_height(env->font_book);
    const char *first_ap = env->female ? "Josian" : "Vatasha", *second_ap = env->female ? "Kaid" : "Trenelle";
    for (uint32_t k = 0; k < n; k++) {
        char line[512], shown[512];
        bs_book_line(inv->book.lines[first + k], env->name ? env->name : "", first_ap, second_ap, line, sizeof line);
        cp437(line, shown, sizeof shown);
        bs_overlay_text(o, env->font_book, BOOK_X, BS_BOOK_TOP + (int)k * lh, shown);
    }
}

void inventory_draw(inventory *inv, const inventory_env *env, int ww, int wh, uint64_t ms)
{
    if (!inv->open)
        return;
    if (inv->reading) {
        bs_screen_draw(env->screen, ww, wh); /* clears; the book's image covers it */
        draw_book(inv, env);
        return;
    }
    bs_screen_draw(env->screen, ww, wh);
    bs_overlay *o = env->overlay;
    bs_overlay_clear(o);
    static uint32_t v[LIST_MAX];
    /* The paper doll, and below it the secondary weapon and the arrows ready. */
    doll_layer layers[DOLL_LAYERS];
    int nl = doll_layers(inv, env, layers);
    for (int k = 0; k < nl; k++)
        if (layers[k].img)
            bs_overlay_image(o, layers[k].img, 0, 0);
    uint32_t sec = bs_items_secondary(env->items), arrows = bs_items_arrows(env->items);
    const int *rects[2] = {secondary_rect, arrows_rect};
    uint32_t slot_items[2] = {sec, arrows};
    for (int k = 0; k < 2; k++) {
        if (slot_items[k] == UINT32_MAX || slot_items[k] == inv->held)
            continue;
        draw_item(env, slot_items[k], rects[k][0], rects[k][1], rects[k][2], rects[k][3], ww, wh, ms);
        if (slot_items[k] == inv->selected)
            bs_overlay_frame(o, rects[k][0], rects[k][1], rects[k][2] - rects[k][0] + 1, rects[k][3] - rects[k][1] + 1,
                             FRAME_COLOR);
    }
    if (env->font_info) {
        int32_t av[BS_ARMOUR_PLACES];
        bs_items_armour_values(env->items, av);
        char num[32];
        for (int k = 0; k < BS_ARMOUR_PLACES; k++) {
            snprintf(num, sizeof num, "%d", av[k]);
            bs_overlay_text(o, env->font_info, ARMOUR_X, ARMOUR_Y + k * ARMOUR_STEP, num);
        }
        snprintf(num, sizeof num, "%d-%d", env->hand_to_hand / 7 + 2, env->hand_to_hand / 7 + 4);
        bs_overlay_text(o, env->font_info, HAND_X, HAND_Y, num);
    }
    for (int l = 0; l < 2; l++) {
        uint32_t box = item_at(inv, env, l, 0);
        if (box != UINT32_MAX)
            draw_item(env, box, list_x[l][0], BOX_Y, list_x[l][1], BOX_Y + ROW_H, ww, wh, ms);
        uint32_t n = visible(inv, env, l, v);
        for (int r = 0; r < ROWS; r++) {
            uint32_t k = (uint32_t)(inv->scroll[l] + r);
            if (k >= n)
                break;
            int y0 = ROW_Y + r * ROW_STEP;
            draw_item(env, v[k], list_x[l][0], y0, list_x[l][1], y0 + ROW_H, ww, wh, ms);
            if (v[k] == inv->selected)
                bs_overlay_frame(o, list_x[l][0], y0, list_x[l][1] - list_x[l][0] + 1, ROW_H + 1, FRAME_COLOR);
        }
        /* The arrows dim when the list can't scroll that way (0x4FB7E). */
        if (inv->scroll[l] == 0 && env->arrow_off[0])
            bs_overlay_image(o, env->arrow_off[0], arrow_x[l][0] - env->arrow_off[0]->x,
                             ARROW_UP_Y - env->arrow_off[0]->y);
        if ((int)n - ROWS <= inv->scroll[l] && env->arrow_off[1])
            bs_overlay_image(o, env->arrow_off[1], arrow_x[l][1] - env->arrow_off[1]->x,
                             ARROW_DOWN_Y - env->arrow_off[1]->y);
    }
    for (int f = 0; f < 6; f++)
        if ((inv->filters & (1u << f)) && env->filter_on[f])
            bs_overlay_image(o, env->filter_on[f], 0, 0);
    if (inv->selected != UINT32_MAX && inv->selected < env->items->count && env->font_info) {
        char l1[128], l2[128], s1[128], s2[128];
        info_lines(env, &env->items->items[inv->selected].item, l1, l2, sizeof l1);
        cp437(l1, s1, sizeof s1);
        cp437(l2, s2, sizeof s2);
        bs_overlay_text(o, env->font_info, INFO_X, INFO_Y, s1);
        bs_overlay_text(o, env->font_info, INFO_X, INFO_Y + 1 + bs_text_height(env->font_info), s2);
    }
    /* The dragged item follows the mouse in a slot-sized box, kept on the screen (0x4F0D8). */
    if (inv->held != UINT32_MAX) {
        int x0 = inv->mouse_x - 48, y0 = inv->mouse_y - 32;
        x0 = x0 < 0 ? 0 : x0 > 640 - 96 ? 640 - 96 : x0;
        y0 = y0 < 0 ? 0 : y0 > 480 - 64 ? 480 - 64 : y0;
        draw_item(env, inv->held, x0, y0, x0 + 96, y0 + 64, ww, wh, ms);
    }
}
