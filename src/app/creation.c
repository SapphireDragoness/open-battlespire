#include "creation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/core/fs.h"
#include "bs/formats/save.h"
#include "bs/formats/scl.h"
#include "text.h"

/* RE references are GAME.EXE addresses; see docs/re/README.md, "Character creation". Rectangles are
 * x0, y0, x1, y1 inclusive, from the screens' button tables. */

static const int back_button[4] = {576, 416, 639, 479};
static const int help_button[4] = {616, 2, 637, 22};

/* ---- Select Scenario (0x31B50: mult02.bsi, 0xEDC0D) ---- */

static const int scenario_load[4] = {195, 258, 426, 286};
static const int scenario_new[4] = {195, 295, 426, 323};

/* ---- the name (0x33502) ---- */

#define NAME_X 170
#define NAME_Y 242
#define NAME_MAX 31

/* ---- male or female (gender.bsi, 0xEDDC2) ---- */

static const int gender_male[4] = {224, 207, 393, 234};
static const int gender_female[4] = {224, 247, 394, 275};

/* ---- the race (0x33F4B: char13.bsi, 0xEDE07) ---- */

static const int race_buttons[6][4] = {
    {16, 16, 208, 176}, {224, 16, 416, 176}, {432, 16, 624, 176},
    {16, 192, 208, 352}, {224, 192, 416, 352}, {432, 192, 624, 352},
};
static const SDL_Keycode race_keys[6] = {SDLK_R, SDLK_B, SDLK_N, SDLK_H, SDLK_D, SDLK_W};
static const int race_done[4] = {9, 425, 53, 472};
static const char *const race_codes[6] = {"rd", "br", "no", "hi", "dk", "wd"};
#define RACE_TEXT 1076                   /* TEXT.RSC: the races' descriptions */
#define RACE_TEXT_X 357                  /* centred on x, from y, wrapped at the width (0x5A9C8) */
#define RACE_TEXT_Y 400
#define RACE_TEXT_W 383
#define RACE_SOUND 209                   /* + the race, on a double click (0x33FF7) */
#define FRAME_DX 12                      /* brdr00.bsi goes round the picture (0x34093) */
#define FRAME_DY (-4)
#define FRAME_MS 60                      /* its animation (a guess) */
#define SLIDE_MS 200                     /* moving to another race (0x2093A; a guess) */
#define DOUBLE_CLICK_MS 400

/* ---- Load Game (load00.bsi, buttons 0xEDC52, drawn by 0x325B9) ---- */

#define LOAD_X 48                        /* slot n's picture: x 48 + 112 (n mod 5), y 192 or 304 */
#define LOAD_DX 112
#define LOAD_Y0 192
#define LOAD_Y1 304
#define LOAD_NAME_DY 64                  /* its name below, in FONT0008 (y 256, 368) */
#define LOAD_FRAME_D 4                   /* brdr01.bsi goes 4 up and left of the picture */
#define LOAD_PAGE_Y 384                  /* ours or the original game's (an addition) */
static const int load_button[4] = {199, 416, 294, 455};   /* Enter */
static const int load_back[4] = {327, 418, 422, 455};     /* Esc */

/* ---- the face (0x32B6C: face00.bsi, 0xEDED6) ---- */

static const int face_buttons[6][4] = {
    {309, 133, 331, 174}, {309, 214, 330, 254},   /* hair: next, previous */
    {453, 133, 476, 174}, {453, 214, 476, 253},   /* eyes */
    {598, 133, 620, 174}, {598, 214, 620, 254},   /* mouth */
};
static const SDL_Keycode face_keys[3] = {SDLK_H, SDLK_E, SDLK_M};
static const int face_random[4] = {280, 295, 562, 332};
static const int face_done[4] = {512, 416, 575, 479};
static const char *const part_codes[3] = {"hr", "ey", "mt"};
static const int part_x[3] = {258, 401, 547};   /* each part centred here */
#define PART_Y 192
#define DOLL_DX (-5)                     /* the doll at (5, 70), where the inventory has it at (10, 10) */
#define DOLL_DY 60

/* ---- the class (0x33AEC: clas00.bsi, 0xEDD66; the list 0x3363F: clas01.bsi, 0xEDFBC) ---- */

static const int class_existing[4] = {224, 207, 393, 234};
static const int class_new[4] = {224, 247, 394, 275};
static const int class_list[4] = {219, 147, 396, 329};
static const int class_up[4] = {423, 159, 444, 200};
static const int class_down[4] = {423, 268, 444, 299};
static const int class_done[4] = {528, 416, 575, 479};

/* ---- the CharacterMaker (0x372DE: char00.bsi, 0xEE5EB) ---- */

enum {
    ED_NAME, ED_WOUNDS_UP, ED_WOUNDS_DOWN, ED_PRIMARY, ED_MAJOR, ED_MINOR, ED_ATTRIBUTE, /* .. + 7 */
    ED_SPELLS = 15, ED_EQUIPMENT, ED_MAGIC, ED_ADVANTAGES, ED_SAVE, ED_CANCEL, ED_DONE, ED_HELP = 24,
    ED_COUNT
};
static const int editor_buttons[ED_COUNT][4] = {
    {35, 11, 286, 34},   {315, 37, 329, 54},  {315, 60, 329, 75},   {80, 120, 329, 171},  {80, 216, 329, 268},
    {80, 354, 329, 447}, {30, 83, 76, 123},   {30, 133, 76, 171},   {30, 182, 76, 220},   {30, 232, 76, 270},
    {30, 282, 76, 320},  {30, 332, 76, 370},  {30, 381, 76, 420},   {30, 430, 76, 470},   {-1, -1, -1, -1},
    {382, 185, 592, 222}, {382, 234, 592, 269}, {382, 281, 592, 317}, {382, 329, 592, 366}, {433, 432, 550, 467},
    {590, 427, 634, 473}, {347, 427, 391, 475}, {-1, -1, -1, -1},   {-1, -1, -1, -1},     {616, 2, 637, 22},
};
/* An attribute's button raises it above y and lowers it below (0x389FD ..). */
static const int attribute_raise[8] = {100, 149, 198, 248, 298, 348, 397, 446};
static const int attribute_lower[8] = {107, 156, 205, 255, 305, 355, 404, 453};
/* The skill lists: their first line's y, and the click's (0x38172 ..). */
static const int list_text_y[3] = {122, 218, 356};
static const int list_click_y[3] = {120, 216, 354};
static const int list_first[3] = {0, 3, 6}, list_slots[3] = {3, 3, 6};
#define SKILL_TEXT_X 110
#define SKILL_ARROW 16                   /* the list's ends lower and raise its skills */
#define STEP_MS 300                      /* steps at most this often (0x387A1) */
#define STEPS_SHIFT 10
/* The skill list (plst00.bsi at 143, 117; 0xEEC63). */
#define PICKER_X 143
#define PICKER_Y 117
static const int picker_list[4] = {172, 143, 406, 270};
static const int picker_up[4] = {436, 131, 456, 170};
static const int picker_down[4] = {436, 249, 455, 287};
static const int picker_ok[4] = {172, 293, 266, 331};
static const int picker_cancel[4] = {301, 293, 396, 331};
/* "Are you sure?" (sure00.bsi). */
static const int confirm_yes[4] = {209, 217, 278, 243};
static const int confirm_no[4] = {342, 217, 410, 243};
static const int page_exit[4] = {590, 427, 634, 473};

/* Spells (char02.bsi, 0xEE858; char03.bsi, 0xEE8B4). */
static const int spells_list[4] = {388, 121, 583, 289};
static const int spells_up[4] = {599, 108, 620, 149};
static const int spells_down[4] = {599, 259, 620, 300};
static const int spells_add[4] = {373, 343, 469, 379};
static const int spells_delete[4] = {499, 343, 597, 378};
static const int offered_list[4] = {382, 109, 584, 200};
static const int offered_up[4] = {599, 108, 620, 149};
static const int offered_down[4] = {600, 149, 621, 190};
static const int offered_ok[4] = {373, 375, 470, 415};
static const int offered_cancel[4] = {504, 375, 596, 411};
#define SPELL_TEXT 200                   /* TEXT.RSC: + the spell's number */
#define SPELL_TEXT_Y 287
static const char school_letters[] = "ARDMTIP";

/* The shops (char05.bsi, 0xEE8F9; char07.bsi, 0xEE983). */
static const int shop_select[4] = {373, 386, 470, 421};
static const int shop_deselect[4] = {500, 386, 598, 421};
static const int shop_previous[4] = {612, 107, 634, 147};
static const int shop_next[4] = {612, 227, 639, 267};
static const int shop_model[4] = {384, 128, 592, 240};
#define SHOP_TEXT_X 400
#define SHOP_LINE_Y 130
#define SHOP_COST_Y 150
#define SHOP_DESC_X 484                  /* centred, from y, wrapped at the width */
#define SHOP_DESC_Y 307
#define SHOP_DESC_W 201
#define EQUIPMENT_TEXT 100               /* TEXT.RSC: + the base */
#define POTION_TEXT 300                  /* + the potion's type */
#define ENCHANTED_TEXT 401               /* + the enchanted item's place in the shop's list (0x3B9D5) */
#define MAGIC_TEXT 200
static const int32_t shop_enchanted[12] = {9242, 9265, 9248, 9259, 9204, 9245, 9251, 9254, 9214, 9221, 3014, 1012};

/* Advantages and disadvantages (char08.bsi, 0xEEA0D; ticks from char09, greyed out from char10). */
enum {
    AD_RESIST, AD_IMMUNE, AD_FLAG, /* .. + 6 */
    AD_MAGERY_DOWN = 9, AD_MAGERY_UP, AD_LOW, AD_CRITICAL, AD_NO_REGEN, AD_NO_SHIELD, AD_ARMOUR_DOWN, AD_ARMOUR_UP,
    AD_MATERIAL_DOWN, AD_MATERIAL_UP, AD_WEAPON, /* .. + 4 */
    AD_EXIT = 24, AD_HELP, AD_COUNT
};
static const int advantage_buttons[AD_COUNT][4] = {
    {64, 304, 288, 336},  {64, 384, 288, 416},  {64, 128, 256, 143},  {64, 144, 256, 159},  {64, 160, 256, 175},
    {64, 176, 256, 191},  {64, 192, 256, 207},  {64, 208, 256, 223},  {64, 224, 256, 239},  {214, 240, 270, 256},
    {271, 240, 325, 256}, {352, 304, 576, 336}, {352, 384, 576, 416}, {352, 80, 560, 95},   {352, 96, 560, 111},
    {352, 127, 368, 143}, {560, 127, 576, 143}, {352, 159, 368, 175}, {560, 159, 576, 175}, {352, 224, 384, 256},
    {400, 224, 432, 256}, {448, 224, 480, 256}, {496, 224, 528, 256}, {544, 224, 576, 256}, {590, 427, 634, 473},
    {616, 2, 637, 22},
};
#define ELEMENT_STEP 48                  /* the element boxes: 32 wide, 48 apart */
#define ELEMENT_BOX 33
#define FLAG_ROW 16
#define POINTS_X 81
#define POINTS_Y 49
#define POINTS_X_ADVANTAGES 176
#define POINTS_Y_ADVANTAGES 65
static const char *const magery_names[BS_CREATION_MAGERY_STEPS] = {"1 x INT", "1.5 x INT", "2 x INT", "3 x INT"};

/* Help pages (0x20C5D). */
enum {
    HELP_NAME = 0, HELP_CLASS = 1, HELP_CLASS_LIST = 2, HELP_RACE = 3, HELP_FACE = 4, HELP_MAGIC = 6,
    HELP_ADVANTAGES = 7, HELP_EQUIPMENT = 8, HELP_SPELLS = 9, HELP_EDITOR = 10
};

/* Messages (GAME.EXE's table). */
enum {
    MSG_NEGATIVE = 131, MSG_NO_NAME = 132, MSG_SKILLS = 133, MSG_NAME_CLASS = 139, MSG_NO_POINTS = 431,
    MSG_COST = 513, MSG_SKILL_LIMIT = 525, MSG_ATTRIBUTE = 527, MSG_DEPENDENT = 565
};


static bool in(const int r[4], int x, int y)
{
    return x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3];
}

static void say(const creation_env *env, const char *text)
{
    if (env->message && text && text[0])
        env->message(env->ctx, text);
}

static void say_message(const creation_env *env, uint32_t id)
{
    say(env, env->text ? bs_exe_message(env->text, id) : "");
}

static void auto_help(const creation_env *env, int page)
{
    if (env->help && env->help->auto_help)
        menus_help(env->help, page, 1);
}

static void show_help(const creation_env *env, int page)
{
    if (env->help)
        menus_help(env->help, page, 1);
}

const char *creation_race_code(int race)
{
    return race >= 0 && race < 6 ? race_codes[race] : "br";
}

bool creation_typing(const creation *cr)
{
    return cr->screen == CREATION_NAME || (cr->screen == CREATION_EDITOR && cr->naming_class);
}

/* ---- text ---- */

static void text(bs_overlay *o, const bs_font *f, int x, int y, const char *s)
{
    if (f)
        bs_overlay_text(o, f, x, y, s);
}

/* ---- lists ---- */

typedef enum list_kind { LIST_CLASSES, LIST_SKILLS, LIST_KNOWN, LIST_OFFERED } list_kind;

static const int *list_rect(list_kind k)
{
    switch (k) {
    case LIST_CLASSES: return class_list;
    case LIST_SKILLS: return picker_list;
    case LIST_KNOWN: return spells_list;
    default: return offered_list;
    }
}

static int list_count(const creation *cr, list_kind k)
{
    switch (k) {
    case LIST_CLASSES: return cr->class_count;
    case LIST_SKILLS: return cr->skill_choice_count;
    case LIST_KNOWN: return (int)cr->c.cls.spell_count;
    default: return cr->offered_count;
    }
}

static int list_rows(const creation_env *env, list_kind k)
{
    const bs_font *f = env->fonts[3];
    int lh = f ? bs_text_height(f) : 10, h = list_rect(k)[3] - list_rect(k)[1];
    return lh > 0 && h / lh > 0 ? h / lh : 1;
}

static void list_line(const creation *cr, const creation_env *env, list_kind k, int i, char *out, size_t cap)
{
    switch (k) {
    case LIST_CLASSES:
        snprintf(out, cap, "%s", cr->classes[i].line);
        break;
    case LIST_SKILLS: {
        int s = cr->skill_choices[i];
        snprintf(out, cap, "%s [%s]", env->skill_names[s] ? env->skill_names[s] : "",
                 env->attribute_names[bs_skill_attribute[s]] ? env->attribute_names[bs_skill_attribute[s]] : "");
        break;
    }
    case LIST_KNOWN: {
        const bs_spell *s = &cr->c.cls.spells[i];
        snprintf(out, cap, "%d - %s", (int)bs_creation_spell_cost(s), s->name);
        break;
    }
    default: {
        const bs_spell *s = &env->spells[cr->offered[i]];
        char school = s->school >= 0 && s->school < (int32_t)sizeof school_letters - 1 ? school_letters[s->school]
                                                                                      : '?';
        snprintf(out, cap, "%d - %s [%c]", (int)bs_creation_spell_cost(s), s->name, school);
        break;
    }
    }
}

static int *list_selected_of(creation *cr, list_kind k)
{
    return k == LIST_CLASSES ? &cr->class_selected : &cr->list_selected;
}

static int *list_scroll_of(creation *cr, list_kind k)
{
    return k == LIST_CLASSES ? &cr->class_scroll : &cr->list_scroll;
}

static void list_scroll_by(creation *cr, const creation_env *env, list_kind k, int by)
{
    int n = list_count(cr, k), rows = list_rows(env, k), most = n > rows ? n - rows : 0;
    int *s = list_scroll_of(cr, k);
    *s += by;
    *s = *s > most ? most : *s < 0 ? 0 : *s;
}

/* Selects the line under a click; returns whether it was the one selected already. */
static bool list_click(creation *cr, const creation_env *env, list_kind k, int y)
{
    const bs_font *f = env->fonts[3];
    int lh = f ? bs_text_height(f) : 10;
    int i = *list_scroll_of(cr, k) + (lh > 0 ? (y - list_rect(k)[1]) / lh : 0);
    if (i < 0 || i >= list_count(cr, k))
        return false;
    int *sel = list_selected_of(cr, k);
    bool again = *sel == i;
    *sel = i;
    return again;
}

/* Keeps the selection in the list and in view. */
static void list_fix(creation *cr, const creation_env *env, list_kind k)
{
    int n = list_count(cr, k), *sel = list_selected_of(cr, k), *scroll = list_scroll_of(cr, k);
    int rows = list_rows(env, k);
    if (*sel >= n)
        *sel = n - 1;
    if (*sel < 0)
        *sel = n > 0 ? 0 : -1;
    if (*sel >= 0 && *sel < *scroll)
        *scroll = *sel;
    if (*sel >= *scroll + rows)
        *scroll = *sel - rows + 1;
    list_scroll_by(cr, env, k, 0);
}

static void draw_list(const creation *cr, const creation_env *env, list_kind k, bs_overlay *o)
{
    const bs_font *f = env->fonts[3];
    if (!f)
        return;
    const int *r = list_rect(k);
    int lh = bs_text_height(f), rows = list_rows(env, k), n = list_count(cr, k);
    int scroll = k == LIST_CLASSES ? cr->class_scroll : cr->list_scroll;
    int sel = k == LIST_CLASSES ? cr->class_selected : cr->list_selected;
    bs_overlay_clip(o, r[0], r[1], r[2] - r[0] + 1, r[3] - r[1] + 1);
    for (int i = 0; i < rows && scroll + i < n; i++) {
        char line[96], shown[96];
        list_line(cr, env, k, scroll + i, line, sizeof line);
        ui_cp437(line, shown, sizeof shown);
        const bs_font *lf = scroll + i == sel && env->fonts[4] ? env->fonts[4] : f;
        bs_overlay_text(o, lf, r[0], r[1] + i * lh, shown);
    }
    bs_overlay_clip(o, 0, 0, 0, 0);
}

/* ---- the screens ---- */

static void go(creation *cr, const creation_env *env, creation_screen s);

/* The face's pictures for the sex and race (0x32B6C): BSI names "<sex><race><part>NNN" in the archive's
 * order; the first is where each starts. */
static void find_parts(creation *cr, const creation_env *env)
{
    for (int p = 0; p < 3; p++) {
        char prefix[8];
        snprintf(prefix, sizeof prefix, "%c%s%s", cr->female ? 'f' : 'm', creation_race_code(cr->race), part_codes[p]);
        cr->part_count[p] = 0;
        cr->part_at[p] = 0;
        for (uint32_t i = 0; env->pictures && i < env->picture_count && cr->part_count[p] < CREATION_FACE_PARTS; i++)
            if (bs_strncasecmp(env->pictures[i], prefix, 5) == 0)
                cr->parts[p][cr->part_count[p]++] = atoi(env->pictures[i] + 5);
    }
}

static int part_number(const creation *cr, int p)
{
    return cr->part_count[p] > 0 ? cr->parts[p][cr->part_at[p]] : p == 0 ? 0 : 1;
}

/* The existing classes (0x337EE): CLASSnnn.SCL, "Name (level)". */
static void find_classes(creation *cr, const creation_env *env)
{
    cr->class_count = 0;
    cr->class_selected = 0;
    cr->class_scroll = 0;
    if (!env->vfs)
        return;
    bs_arena a;
    bs_arena_init(&a, 0);
    static bs_class k;
    for (int i = 0; i < 1000 && cr->class_count < CREATION_CLASSES; i++) {
        creation_class_file *f = &cr->classes[cr->class_count];
        snprintf(f->file, sizeof f->file, "GAMEDATA/CLASS%03d.SCL", i);
        if (bs_vfs_exists(env->vfs, f->file) != BS_OK)
            continue;
        uint8_t *data;
        size_t n;
        bs_arena_mark mark = bs_arena_get_mark(&a);
        if (bs_vfs_read(env->vfs, f->file, &a, &data, &n) == BS_OK && bs_class_parse(data, n, &k) == BS_OK) {
            snprintf(f->line, sizeof f->line, "%s (%d)", k.class_name, k.level);
            cr->class_count++;
        }
        bs_arena_rewind(&a, mark);
    }
    bs_arena_free(&a);
}

/* The CharacterMaker on a class (0x372DE): the shops made, the class's items in them. */
static void open_editor(creation *cr, const creation_env *env, const bs_class *from)
{
    if (from)
        bs_creation_from_class(&cr->c, from);
    else
        bs_creation_new(&cr->c);
    /* The shops' pictures are for the sex just chosen (0x51A0A reads +0x226). */
    bs_item_db db = env->db ? *env->db : (bs_item_db){0};
    db.female = cr->female;
    bs_creation_stock(env->db ? &db : NULL, env->rng, &cr->shops[0], &cr->shops[1]);
    cr->other_count = from ? bs_creation_class_items(from, cr->female, &cr->shops[0], &cr->shops[1], cr->others,
                                                     BS_SCL_MAX_ITEMS)
                           : 0;
    cr->shop_at[0] = cr->shop_at[1] = 0;
    cr->page = PAGE_MAIN;
    cr->skill_slot = -1;
    cr->naming_class = false;
    cr->confirm = false;
    cr->held = -1;
    go(cr, env, CREATION_EDITOR);
}

/* Each slot's name and picture (0x31104): ours, or the original game's. */
static void find_saves(creation *cr, const creation_env *env)
{
    save_slots_read(cr->load_originals ? env->save_root : env->saves_dir, !cr->load_originals, cr->saves);
}

/* A slot chosen: the frame slides there (0x32868). */
static void pick_save(creation *cr, const creation_env *env, int slot)
{
    if (slot < 0 || slot >= CREATION_SAVES || slot == cr->load_slot)
        return;
    cr->load_from = cr->load_slot;
    cr->load_slide_ms = env->now_ms;
    cr->load_slot = slot;
}

static void load_slot_xy(int slot, int *x, int *y)
{
    *x = LOAD_X + (slot % 5) * LOAD_DX;
    *y = slot < 5 ? LOAD_Y0 : LOAD_Y1;
}

static void go(creation *cr, const creation_env *env, creation_screen s)
{
    cr->screen = s;
    cr->click_ms = 0;
    cr->click_at = -1;
    switch (s) {
    case CREATION_NAME:
        auto_help(env, HELP_NAME);
        break;
    case CREATION_RACE:
        cr->slide_ms = 0;
        cr->slide_from = cr->race;
        auto_help(env, HELP_RACE);
        break;
    case CREATION_FACE:
        find_parts(cr, env);
        auto_help(env, HELP_FACE);
        break;
    case CREATION_CLASS:
        auto_help(env, HELP_CLASS);
        break;
    case CREATION_CLASS_LIST:
        find_classes(cr, env);
        auto_help(env, HELP_CLASS_LIST);
        break;
    case CREATION_EDITOR:
        auto_help(env, HELP_EDITOR);
        break;
    case CREATION_LOAD_GAME:
        cr->load_originals = !env->saves_dir; /* ours first */
        find_saves(cr, env);
        cr->load_slot = cr->load_from = 0; /* (0x31C2B) */
        cr->load_slide_ms = 0;
        break;
    default:
        break;
    }
}

void creation_start(creation *cr, const creation_env *env)
{
    memset(cr, 0, sizeof *cr);
    cr->skill_slot = -1;
    cr->held = -1;
    cr->click_at = -1;
    go(cr, env, CREATION_SCENARIO);
}

static bool double_click(creation *cr, const creation_env *env, int target)
{
    bool d = cr->click_at == target && env->now_ms - cr->click_ms < DOUBLE_CLICK_MS;
    cr->click_at = target;
    cr->click_ms = env->now_ms;
    return d;
}

static void pick_race(creation *cr, const creation_env *env, int race)
{
    if (race == cr->race)
        return;
    cr->slide_from = cr->race;
    cr->slide_ms = env->now_ms;
    cr->race = race;
}

/* ---- the CharacterMaker's steps ---- */

static void report(const creation_env *env, bs_creation_result r)
{
    if (r == BS_CREATE_NO_POINTS)
        say_message(env, MSG_NO_POINTS);
}

/* A skill can't pass its attribute: "Skill cannot be increased higher than your agility." */
static void say_skill_limit(const creation_env *env, int skill)
{
    if (!env->text)
        return;
    char s[160];
    snprintf(s, sizeof s, "%s %s.", bs_exe_message(env->text, MSG_SKILL_LIMIT),
             bs_exe_message(env->text, MSG_ATTRIBUTE + (uint32_t)bs_skill_attribute[skill]));
    say(env, s);
}

/* One press of a step button: attributes, wounds, a skill's ends (x, y the click). */
static void step(creation *cr, const creation_env *env, int b, int x, int y, bool shift)
{
    int times = shift ? STEPS_SHIFT : 1;
    for (int t = 0; t < times; t++) {
        bs_creation_result r = BS_CREATE_LIMIT;
        if (b == ED_WOUNDS_UP || b == ED_WOUNDS_DOWN) {
            r = bs_creation_wounds(&cr->c, b == ED_WOUNDS_UP);
        } else if (b >= ED_ATTRIBUTE && b < ED_ATTRIBUTE + 8) {
            int a = b - ED_ATTRIBUTE;
            if (y <= attribute_raise[a]) {
                r = bs_creation_attribute(&cr->c, a, true);
                if (r == BS_CREATE_NO_POINTS)
                    r = BS_CREATE_LIMIT; /* the game says nothing here */
            } else if (y >= attribute_lower[a]) {
                r = bs_creation_attribute(&cr->c, a, false);
                if (r == BS_CREATE_SKILL_BELOW)
                    say_message(env, MSG_DEPENDENT);
            }
        } else if (b >= ED_PRIMARY && b <= ED_MINOR) {
            int list = b - ED_PRIMARY;
            const bs_font *f = env->fonts[3];
            int lh = f ? bs_text_height(f) : 10;
            int row = lh > 0 ? (y - list_click_y[list]) / lh : 0;
            row = row < 0 ? 0 : row >= list_slots[list] ? list_slots[list] - 1 : row;
            int slot = list_first[list] + row;
            bool up = x > editor_buttons[b][2] - SKILL_ARROW;
            r = bs_creation_skill(&cr->c, slot, up);
            if (r == BS_CREATE_ATTRIBUTE)
                say_skill_limit(env, cr->c.cls.class_skills[slot]);
        }
        report(env, r);
        if (r != BS_CREATE_OK)
            break;
    }
}

/* Whether a click on an editor button is a step that repeats while held. */
static bool is_step(int b, int x)
{
    if (b == ED_WOUNDS_UP || b == ED_WOUNDS_DOWN || (b >= ED_ATTRIBUTE && b < ED_ATTRIBUTE + 8))
        return true;
    return b >= ED_PRIMARY && b <= ED_MINOR &&
           (x < editor_buttons[b][0] + SKILL_ARROW || x > editor_buttons[b][2] - SKILL_ARROW);
}

static void open_picker(creation *cr, const creation_env *env, int b, int y)
{
    int list = b - ED_PRIMARY;
    const bs_font *f = env->fonts[3];
    int lh = f ? bs_text_height(f) : 10;
    int row = lh > 0 ? (y - list_click_y[list]) / lh : 0;
    row = row < 0 ? 0 : row >= list_slots[list] ? list_slots[list] - 1 : row;
    cr->skill_slot = list_first[list] + row;
    cr->skill_choice_count = bs_creation_skill_choices(&cr->c, cr->skill_choices);
    cr->list_selected = 0;
    cr->list_scroll = 0;
}

static void update_offered(creation *cr, const creation_env *env)
{
    cr->offered_count = 0;
    for (int i = 0; env->spells && i < BS_SPELL_COUNT; i++)
        if (bs_creation_spell_offered(&cr->c, &env->spells[i]))
            cr->offered[cr->offered_count++] = i;
    list_fix(cr, env, LIST_OFFERED);
}

static void open_page(creation *cr, const creation_env *env, creation_page p)
{
    cr->page = p;
    cr->list_selected = 0;
    cr->list_scroll = 0;
    switch (p) {
    case PAGE_SPELLS:
        list_fix(cr, env, LIST_KNOWN);
        auto_help(env, HELP_SPELLS);
        break;
    case PAGE_SPELL_PICK:
        cr->added_now = 0;
        cr->added_cost = 0;
        update_offered(cr, env);
        break;
    case PAGE_EQUIPMENT:
        auto_help(env, HELP_EQUIPMENT);
        break;
    case PAGE_MAGIC:
        auto_help(env, HELP_MAGIC);
        break;
    case PAGE_ADVANTAGES:
        auto_help(env, HELP_ADVANTAGES);
        break;
    default:
        break;
    }
}

static creation_action editor_done(creation *cr, const creation_env *env)
{
    switch (bs_creation_ready(&cr->c)) {
    case BS_CHECK_POINTS:
        say_message(env, MSG_NEGATIVE);
        return CREATION_NOTHING;
    case BS_CHECK_NO_NAME:
        say_message(env, MSG_NO_NAME);
        return CREATION_NOTHING;
    case BS_CHECK_SKILLS:
        say_message(env, MSG_SKILLS);
        return CREATION_NOTHING;
    default:
        return CREATION_PLAY;
    }
}

static creation_action editor_click(creation *cr, const creation_env *env, int x, int y, bool shift)
{
    cr->naming_class = false;
    for (int b = 0; b < ED_COUNT; b++) {
        if (!in(editor_buttons[b], x, y))
            continue;
        if (is_step(b, x)) {
            if (env->now_ms - cr->step_ms >= STEP_MS || cr->step_ms == 0) {
                cr->step_ms = env->now_ms;
                step(cr, env, b, x, y, shift);
            }
            cr->held = b;
            cr->held_x = x;
            cr->held_y = y;
            cr->held_shift = shift;
            return CREATION_NOTHING;
        }
        switch (b) {
        case ED_NAME:
            cr->naming_class = true;
            say_message(env, MSG_NAME_CLASS);
            break;
        case ED_PRIMARY:
        case ED_MAJOR:
        case ED_MINOR:
            open_picker(cr, env, b, y);
            break;
        case ED_SPELLS:
            open_page(cr, env, PAGE_SPELLS);
            break;
        case ED_EQUIPMENT:
            if (env->db && env->db->tables)
                open_page(cr, env, PAGE_EQUIPMENT);
            break;
        case ED_MAGIC:
            if (env->db && env->db->tables)
                open_page(cr, env, PAGE_MAGIC);
            break;
        case ED_ADVANTAGES:
            open_page(cr, env, PAGE_ADVANTAGES);
            break;
        case ED_SAVE:
            say(env, "Saving classes is not available yet.");
            break;
        case ED_CANCEL:
            cr->confirm = true;
            break;
        case ED_DONE:
            return editor_done(cr, env);
        case ED_HELP:
            show_help(env, HELP_EDITOR);
            break;
        default:
            break;
        }
        return CREATION_NOTHING;
    }
    return CREATION_NOTHING;
}

static void picker_click(creation *cr, const creation_env *env, int x, int y)
{
    if (in(picker_list, x, y)) {
        if (list_click(cr, env, LIST_SKILLS, y))
            goto ok;
    } else if (in(picker_up, x, y)) {
        list_scroll_by(cr, env, LIST_SKILLS, -1);
    } else if (in(picker_down, x, y)) {
        list_scroll_by(cr, env, LIST_SKILLS, 1);
    } else if (in(picker_ok, x, y)) {
        goto ok;
    } else if (in(picker_cancel, x, y)) {
        cr->skill_slot = -1;
    }
    return;
ok:
    if (cr->list_selected >= 0 && cr->list_selected < cr->skill_choice_count)
        bs_creation_set_skill(&cr->c, cr->skill_slot, cr->skill_choices[cr->list_selected]);
    cr->skill_slot = -1;
}

static void add_offered(creation *cr, const creation_env *env)
{
    if (cr->list_selected < 0 || cr->list_selected >= cr->offered_count)
        return;
    const bs_spell *s = &env->spells[cr->offered[cr->list_selected]];
    bs_creation_result r = bs_creation_add_spell(&cr->c, s);
    report(env, r);
    if (r == BS_CREATE_OK) {
        cr->added_now++;
        cr->added_cost += bs_creation_spell_cost(s);
        update_offered(cr, env);
    }
}

static void spells_click(creation *cr, const creation_env *env, int x, int y)
{
    if (cr->page == PAGE_SPELLS) {
        if (in(spells_list, x, y))
            list_click(cr, env, LIST_KNOWN, y);
        else if (in(spells_up, x, y))
            list_scroll_by(cr, env, LIST_KNOWN, -1);
        else if (in(spells_down, x, y))
            list_scroll_by(cr, env, LIST_KNOWN, 1);
        else if (in(spells_add, x, y))
            open_page(cr, env, PAGE_SPELL_PICK);
        else if (in(spells_delete, x, y) && cr->list_selected >= 0) {
            bs_creation_remove_spell(&cr->c, (uint32_t)cr->list_selected);
            list_fix(cr, env, LIST_KNOWN);
        } else if (in(page_exit, x, y))
            cr->page = PAGE_MAIN;
        else if (in(help_button, x, y))
            show_help(env, HELP_SPELLS);
        return;
    }
    /* Adding (0x39457): OK takes the spell, Cancel gives back what this visit took (0x39809). */
    if (in(offered_list, x, y)) {
        if (list_click(cr, env, LIST_OFFERED, y))
            add_offered(cr, env);
    } else if (in(offered_up, x, y)) {
        list_scroll_by(cr, env, LIST_OFFERED, -1);
    } else if (in(offered_down, x, y)) {
        list_scroll_by(cr, env, LIST_OFFERED, 1);
    } else if (in(offered_ok, x, y)) {
        add_offered(cr, env);
    } else if (in(offered_cancel, x, y)) {
        while (cr->added_now > 0 && cr->c.cls.spell_count > 0) {
            bs_creation_remove_spell(&cr->c, cr->c.cls.spell_count - 1);
            cr->added_now--;
        }
        open_page(cr, env, PAGE_SPELLS);
    } else if (in(page_exit, x, y)) {
        open_page(cr, env, PAGE_SPELLS);
    }
}

static void shop_click(creation *cr, const creation_env *env, int x, int y)
{
    int s = cr->page == PAGE_MAGIC;
    bs_creation_shop *shop = &cr->shops[s];
    uint32_t *at = &cr->shop_at[s];
    if (in(shop_select, x, y))
        report(env, bs_creation_buy(&cr->c, shop, *at));
    else if (in(shop_deselect, x, y))
        bs_creation_sell(&cr->c, shop, *at);
    else if (in(shop_previous, x, y) && *at > 0)
        (*at)--;
    else if (in(shop_next, x, y) && *at + 1 < shop->count)
        (*at)++;
    else if (in(page_exit, x, y))
        cr->page = PAGE_MAIN;
    else if (in(help_button, x, y))
        show_help(env, s ? HELP_MAGIC : HELP_EQUIPMENT);
}

/* An element box under x in a row of five from x0, or -1. */
static int element_at(int x, int x0)
{
    int d = x - x0;
    if (d < 0 || d % ELEMENT_STEP >= ELEMENT_BOX || d / ELEMENT_STEP >= 5)
        return -1;
    return d / ELEMENT_STEP;
}

static void advantages_click(creation *cr, const creation_env *env, int x, int y)
{
    bs_creation *c = &cr->c;
    for (int b = 0; b < AD_COUNT; b++) {
        if (!in(advantage_buttons[b], x, y))
            continue;
        bs_creation_result r = BS_CREATE_OK;
        if (b == AD_RESIST || b == AD_IMMUNE || b == AD_LOW || b == AD_CRITICAL) {
            int e = element_at(x, b == AD_RESIST || b == AD_IMMUNE ? 64 : 352);
            if (e < 0)
                return;
            bs_tolerance_kind t = b == AD_RESIST ? BS_TOL_RESISTANCE : b == AD_IMMUNE ? BS_TOL_IMMUNITY
                                  : b == AD_LOW    ? BS_TOL_LOW
                                                   : BS_TOL_CRITICAL;
            r = bs_creation_tolerance(c, t, 1 << e);
        } else if (b >= AD_FLAG && b < AD_FLAG + 7) {
            r = bs_creation_toggle(c, (bs_creation_flag)(BS_ADV_ACUTE_HEARING + (b - AD_FLAG)));
        } else if (b == AD_MAGERY_DOWN || b == AD_MAGERY_UP) {
            r = bs_creation_magery(c, b == AD_MAGERY_UP);
        } else if (b == AD_NO_REGEN) {
            r = bs_creation_toggle(c, BS_DIS_NO_SP_REGEN);
        } else if (b == AD_NO_SHIELD) {
            r = bs_creation_toggle(c, BS_DIS_NO_SHIELD);
        } else if (b == AD_ARMOUR_DOWN || b == AD_ARMOUR_UP) {
            r = bs_creation_armour(c, b == AD_ARMOUR_UP);
        } else if (b == AD_MATERIAL_DOWN || b == AD_MATERIAL_UP) {
            r = bs_creation_material(c, b == AD_MATERIAL_UP);
        } else if (b >= AD_WEAPON && b < AD_WEAPON + 5) {
            r = bs_creation_forbid_weapon(c, (uint8_t)(1 << (b - AD_WEAPON)));
        } else if (b == AD_EXIT) {
            cr->page = PAGE_MAIN;
        } else if (b == AD_HELP) {
            show_help(env, HELP_ADVANTAGES);
        }
        report(env, r);
        return;
    }
}

/* ---- events ---- */

static creation_action screen_click(creation *cr, const creation_env *env, int x, int y, bool shift)
{
    switch (cr->screen) {
    case CREATION_SCENARIO:
        if (in(scenario_load, x, y))
            go(cr, env, CREATION_LOAD_GAME);
        else if (in(scenario_new, x, y))
            go(cr, env, CREATION_NAME);
        else if (in(back_button, x, y))
            return CREATION_TO_TITLE;
        break;
    case CREATION_LOAD_GAME:
        for (int n = 0; n < CREATION_SAVES; n++) {
            int sx, sy;
            load_slot_xy(n, &sx, &sy);
            const int r[4] = {sx, sy, sx + BS_SAVE_IMAGE_WIDTH, sy + BS_SAVE_IMAGE_HEIGHT - 1};
            if (in(r, x, y)) {
                pick_save(cr, env, n);
                if (double_click(cr, env, n) && cr->saves[n].present)
                    return CREATION_LOAD;
                return CREATION_NOTHING;
            }
        }
        if (in(load_button, x, y) && cr->saves[cr->load_slot].present)
            return CREATION_LOAD;
        if (in(load_back, x, y))
            go(cr, env, CREATION_SCENARIO);
        break;
    case CREATION_GENDER:
        if (in(gender_male, x, y) || in(gender_female, x, y)) {
            cr->female = in(gender_female, x, y);
            go(cr, env, CREATION_RACE);
        } else if (in(back_button, x, y)) {
            go(cr, env, CREATION_NAME);
        }
        break;
    case CREATION_RACE:
        for (int r = 0; r < 6; r++)
            if (in(race_buttons[r], x, y)) {
                pick_race(cr, env, r);
                if (double_click(cr, env, r)) {
                    if (env->sound)
                        env->sound(env->ctx, RACE_SOUND + r);
                    go(cr, env, CREATION_FACE);
                }
                return CREATION_NOTHING;
            }
        if (in(race_done, x, y))
            go(cr, env, CREATION_FACE);
        else if (in(back_button, x, y))
            go(cr, env, CREATION_GENDER);
        else if (in(help_button, x, y))
            show_help(env, HELP_RACE);
        break;
    case CREATION_FACE:
        for (int b = 0; b < 6; b++)
            if (in(face_buttons[b], x, y)) {
                int p = b / 2, n = cr->part_count[p];
                if (n > 0)
                    cr->part_at[p] = (cr->part_at[p] + (b % 2 ? n - 1 : 1)) % n;
                return CREATION_NOTHING;
            }
        if (in(face_random, x, y)) {
            for (int p = 0; p < 3; p++)
                if (cr->part_count[p] > 0)
                    cr->part_at[p] = bs_rng_range(env->rng, 0, cr->part_count[p] - 1);
        } else if (in(face_done, x, y)) {
            go(cr, env, CREATION_CLASS);
        } else if (in(back_button, x, y)) {
            go(cr, env, CREATION_RACE);
        } else if (in(help_button, x, y)) {
            show_help(env, HELP_FACE);
        }
        break;
    case CREATION_CLASS:
        if (in(class_existing, x, y))
            go(cr, env, CREATION_CLASS_LIST);
        else if (in(class_new, x, y))
            open_editor(cr, env, NULL);
        else if (in(back_button, x, y))
            go(cr, env, CREATION_FACE);
        else if (in(help_button, x, y))
            show_help(env, HELP_CLASS);
        break;
    case CREATION_CLASS_LIST: {
        bool take = false;
        if (in(class_list, x, y))
            take = list_click(cr, env, LIST_CLASSES, y);
        else if (in(class_up, x, y))
            list_scroll_by(cr, env, LIST_CLASSES, -1);
        else if (in(class_down, x, y))
            list_scroll_by(cr, env, LIST_CLASSES, 1);
        else if (in(class_done, x, y))
            take = true;
        else if (in(back_button, x, y))
            go(cr, env, CREATION_CLASS);
        else if (in(help_button, x, y))
            show_help(env, HELP_CLASS_LIST);
        if (take && cr->class_selected >= 0 && cr->class_selected < cr->class_count) {
            static bs_class k;
            bs_arena a;
            bs_arena_init(&a, 0);
            uint8_t *data;
            size_t n;
            if (bs_vfs_read(env->vfs, cr->classes[cr->class_selected].file, &a, &data, &n) == BS_OK &&
                bs_class_parse(data, n, &k) == BS_OK)
                open_editor(cr, env, &k);
            bs_arena_free(&a);
        }
        break;
    }
    case CREATION_EDITOR:
        if (cr->confirm) {
            if (in(confirm_yes, x, y)) {
                cr->confirm = false;
                go(cr, env, CREATION_CLASS);
            } else if (in(confirm_no, x, y)) {
                cr->confirm = false;
            }
            break;
        }
        if (cr->skill_slot >= 0) {
            picker_click(cr, env, x, y);
            break;
        }
        switch (cr->page) {
        case PAGE_MAIN:
            return editor_click(cr, env, x, y, shift);
        case PAGE_SPELLS:
        case PAGE_SPELL_PICK:
            spells_click(cr, env, x, y);
            break;
        case PAGE_EQUIPMENT:
        case PAGE_MAGIC:
            shop_click(cr, env, x, y);
            break;
        case PAGE_ADVANTAGES:
            advantages_click(cr, env, x, y);
            break;
        }
        break;
    default:
        break;
    }
    return CREATION_NOTHING;
}

/* Typing a name: printable characters, Backspace; returns true on Enter. */
static bool type_into(char *buf, size_t cap, const SDL_Event *e)
{
    if (e->type == SDL_EVENT_TEXT_INPUT) {
        size_t n = strlen(buf);
        for (const char *p = e->text.text; *p && n + 1 < cap; p++)
            if (*p >= ' ' && *p < 127)
                buf[n++] = *p;
        buf[n] = '\0';
        return false;
    }
    if (e->type == SDL_EVENT_KEY_DOWN) {
        if (e->key.key == SDLK_BACKSPACE && buf[0])
            buf[strlen(buf) - 1] = '\0';
        return e->key.key == SDLK_RETURN || e->key.key == SDLK_KP_ENTER;
    }
    return false;
}

static creation_action key(creation *cr, const creation_env *env, SDL_Keycode k, bool shift)
{
    bool enter = k == SDLK_RETURN || k == SDLK_KP_ENTER, esc = k == SDLK_ESCAPE;
    switch (cr->screen) {
    case CREATION_SCENARIO:
        if (k == SDLK_L)
            go(cr, env, CREATION_LOAD_GAME);
        else if (k == SDLK_N || enter)
            go(cr, env, CREATION_NAME);
        else if (esc)
            return CREATION_TO_TITLE;
        break;
    case CREATION_LOAD_GAME:
        /* Right or Tab, Left or Backspace, Up and Down move the frame (0x325B9). */
        if ((k == SDLK_RIGHT || (k == SDLK_TAB && !shift)) && cr->load_slot < CREATION_SAVES - 1)
            pick_save(cr, env, cr->load_slot + 1);
        else if ((k == SDLK_LEFT || k == SDLK_BACKSPACE || (k == SDLK_TAB && shift)) && cr->load_slot > 0)
            pick_save(cr, env, cr->load_slot - 1);
        else if (k == SDLK_UP && cr->load_slot >= 5)
            pick_save(cr, env, cr->load_slot - 5);
        else if (k == SDLK_DOWN && cr->load_slot < 5)
            pick_save(cr, env, cr->load_slot + 5);
        else if ((k == SDLK_PAGEDOWN || k == SDLK_PAGEUP) && env->saves_dir && env->save_root) {
            cr->load_originals = k == SDLK_PAGEDOWN;
            find_saves(cr, env);
        } else if (enter && cr->saves[cr->load_slot].present)
            return CREATION_LOAD;
        else if (esc)
            go(cr, env, CREATION_SCENARIO);
        break;
    case CREATION_GENDER:
        if (k == SDLK_M || k == SDLK_F) {
            cr->female = k == SDLK_F;
            go(cr, env, CREATION_RACE);
        } else if (esc) {
            go(cr, env, CREATION_NAME);
        }
        break;
    case CREATION_RACE: {
        /* The arrows move the frame (0x34093). */
        int r = cr->race;
        for (int i = 0; i < 6; i++)
            if (k == race_keys[i])
                r = i;
        if ((k == SDLK_RIGHT || k == SDLK_TAB) && r < 5)
            r++;
        else if ((k == SDLK_LEFT || k == SDLK_BACKSPACE) && r > 0)
            r--;
        else if (k == SDLK_UP && r >= 3)
            r -= 3;
        else if (k == SDLK_DOWN && r < 3)
            r += 3;
        pick_race(cr, env, r);
        if (enter)
            go(cr, env, CREATION_FACE);
        else if (esc)
            go(cr, env, CREATION_GENDER);
        break;
    }
    case CREATION_FACE:
        for (int p = 0; p < 3; p++)
            if (k == face_keys[p] && cr->part_count[p] > 0)
                cr->part_at[p] = (cr->part_at[p] + 1) % cr->part_count[p];
        if (k == SDLK_R)
            screen_click(cr, env, face_random[0], face_random[1], false);
        else if (enter)
            go(cr, env, CREATION_CLASS);
        else if (esc)
            go(cr, env, CREATION_RACE);
        break;
    case CREATION_CLASS:
        if (k == SDLK_E)
            go(cr, env, CREATION_CLASS_LIST);
        else if (k == SDLK_N)
            open_editor(cr, env, NULL);
        else if (esc)
            go(cr, env, CREATION_FACE);
        break;
    case CREATION_CLASS_LIST:
        if (k == SDLK_UP || k == SDLK_DOWN) {
            cr->class_selected += k == SDLK_UP ? -1 : 1;
            if (cr->class_selected < 0)
                cr->class_selected = 0;
            list_fix(cr, env, LIST_CLASSES);
        } else if (enter) {
            screen_click(cr, env, class_done[0], class_done[1], false);
        } else if (esc) {
            go(cr, env, CREATION_CLASS);
        }
        break;
    case CREATION_EDITOR:
        if (cr->confirm) {
            if (k == SDLK_Y)
                screen_click(cr, env, confirm_yes[0], confirm_yes[1], false);
            else if (k == SDLK_N || esc)
                cr->confirm = false;
        } else if (cr->skill_slot >= 0) {
            if (k == SDLK_UP || k == SDLK_DOWN) {
                cr->list_selected += k == SDLK_UP ? -1 : 1;
                if (cr->list_selected < 0)
                    cr->list_selected = 0;
                list_fix(cr, env, LIST_SKILLS);
            } else if (enter) {
                picker_click(cr, env, picker_ok[0], picker_ok[1]);
            } else if (esc) {
                cr->skill_slot = -1;
            }
        } else if (cr->page == PAGE_EQUIPMENT || cr->page == PAGE_MAGIC) {
            const int *r = enter ? shop_select : k == SDLK_DELETE ? shop_deselect : k == SDLK_UP ? shop_previous
                           : k == SDLK_DOWN ? shop_next : esc ? page_exit : NULL;
            if (r)
                shop_click(cr, env, r[0], r[1]);
        } else if (cr->page == PAGE_SPELLS || cr->page == PAGE_SPELL_PICK) {
            list_kind lk = cr->page == PAGE_SPELLS ? LIST_KNOWN : LIST_OFFERED;
            if (k == SDLK_UP || k == SDLK_DOWN) {
                cr->list_selected += k == SDLK_UP ? -1 : 1;
                if (cr->list_selected < 0)
                    cr->list_selected = 0;
                list_fix(cr, env, lk);
            } else if (enter) {
                const int *r = cr->page == PAGE_SPELLS ? spells_add : offered_ok;
                spells_click(cr, env, r[0], r[1]);
            } else if (k == SDLK_DELETE && cr->page == PAGE_SPELLS) {
                spells_click(cr, env, spells_delete[0], spells_delete[1]);
            } else if (esc) {
                cr->page == PAGE_SPELLS ? (void)(cr->page = PAGE_MAIN) : open_page(cr, env, PAGE_SPELLS);
            }
        } else if (cr->page == PAGE_ADVANTAGES) {
            if (esc)
                cr->page = PAGE_MAIN;
        } else if (esc) {
            cr->confirm = true;
        } else if (enter) {
            (void)shift;
            return editor_done(cr, env);
        }
        break;
    default:
        break;
    }
    return CREATION_NOTHING;
}

creation_action creation_event(creation *cr, const creation_env *env, const SDL_Event *e, int x, int y)
{
    if (env->help && env->help->open == MENU_HELP) {
        menu_env me;
        memset(&me, 0, sizeof me);
        me.assets = env->assets;
        memcpy(me.fonts, env->fonts, sizeof me.fonts);
        menus_event(env->help, &me, e, x, y);
        return CREATION_NOTHING;
    }
    bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    if (cr->screen == CREATION_NAME) {
        if (e->type == SDL_EVENT_KEY_DOWN && e->key.key == SDLK_ESCAPE) {
            go(cr, env, CREATION_SCENARIO);
        } else if (type_into(cr->name, NAME_MAX + 1, e)) {
            if (cr->name[0])
                go(cr, env, CREATION_GENDER);
        } else if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN && e->button.button == SDL_BUTTON_LEFT &&
                   in(help_button, x, y)) {
            show_help(env, HELP_NAME);
        }
        return CREATION_NOTHING;
    }
    if (cr->screen == CREATION_EDITOR && cr->naming_class) {
        if (e->type == SDL_EVENT_KEY_DOWN && e->key.key == SDLK_ESCAPE) {
            cr->naming_class = false;
            return CREATION_NOTHING;
        }
        if (type_into(cr->c.cls.class_name, sizeof cr->c.cls.class_name, e)) {
            cr->naming_class = false;
            return CREATION_NOTHING;
        }
        if (e->type == SDL_EVENT_TEXT_INPUT || e->type == SDL_EVENT_KEY_DOWN)
            return CREATION_NOTHING;
    }
    if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat)
        return key(cr, env, e->key.key, shift);
    if (e->type == SDL_EVENT_MOUSE_WHEEL) {
        int by = e->wheel.y > 0 ? -1 : 1;
        if (cr->screen == CREATION_CLASS_LIST)
            list_scroll_by(cr, env, LIST_CLASSES, by);
        else if (cr->screen == CREATION_EDITOR && cr->skill_slot >= 0)
            list_scroll_by(cr, env, LIST_SKILLS, by);
        else if (cr->screen == CREATION_EDITOR && (cr->page == PAGE_SPELLS || cr->page == PAGE_SPELL_PICK))
            list_scroll_by(cr, env, cr->page == PAGE_SPELLS ? LIST_KNOWN : LIST_OFFERED, by);
        return CREATION_NOTHING;
    }
    if (e->type == SDL_EVENT_MOUSE_BUTTON_UP && e->button.button == SDL_BUTTON_LEFT)
        cr->held = -1;
    if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN && e->button.button == SDL_BUTTON_LEFT)
        return screen_click(cr, env, x, y, shift);
    return CREATION_NOTHING;
}

void creation_update(creation *cr, const creation_env *env)
{
    if (cr->held < 0 || cr->screen != CREATION_EDITOR || cr->page != PAGE_MAIN || cr->skill_slot >= 0 ||
        (env->help && env->help->open == MENU_HELP))
        return;
    if (env->now_ms - cr->step_ms >= STEP_MS) {
        cr->step_ms = env->now_ms;
        step(cr, env, cr->held, cr->held_x, cr->held_y, cr->held_shift);
    }
}

/* ---- drawing ---- */

const char *creation_background(const creation *cr)
{
    static char name[16];
    switch (cr->screen) {
    case CREATION_SCENARIO: return "mult02.bsi";
    case CREATION_NAME: return "name00.bsi";
    case CREATION_GENDER: return "gender.bsi";
    case CREATION_RACE: return "char13.bsi";
    case CREATION_FACE: return "face00.bsi";
    case CREATION_CLASS: return "clas00.bsi";
    case CREATION_CLASS_LIST: return "clas01.bsi";
    case CREATION_EDITOR:
        snprintf(name, sizeof name, "char%02d.bsi", (int)cr->page);
        return name;
    case CREATION_LOAD_GAME: return "load00.bsi";
    default: return NULL;
    }
}

const bs_item *creation_shown_item(const creation *cr, int r[4])
{
    if (cr->screen != CREATION_EDITOR || (cr->page != PAGE_EQUIPMENT && cr->page != PAGE_MAGIC) || cr->confirm)
        return NULL;
    int s = cr->page == PAGE_MAGIC;
    if (cr->shop_at[s] >= cr->shops[s].count)
        return NULL;
    memcpy(r, shop_model, sizeof shop_model);
    return &cr->shops[s].items[cr->shop_at[s]];
}

/* Draws a BSI image with its top left at (x, y). */
static void image_at(bs_overlay *o, const bs_bsi_image *img, uint32_t frame, int x, int y)
{
    if (img)
        bs_overlay_image_frame(o, img, img->frames ? frame % img->frames : 0, x - img->x, y - img->y);
}

/* Draws a BSI image centred on (cx, cy). */
static void image_centred(bs_overlay *o, const bs_bsi_image *img, int cx, int cy)
{
    if (img)
        bs_overlay_image(o, img, cx - img->width / 2 - img->x, cy - img->height / 2 - img->y);
}

/* A face picture: "<sex><race><part>NNN.bsi". */
static const bs_bsi_image *part_image(const creation_env *env, const creation *cr, int p)
{
    char name[32];
    snprintf(name, sizeof name, "%c%s%s%03d.bsi", cr->female ? 'f' : 'm', creation_race_code(cr->race), part_codes[p],
             part_number(cr, p));
    return bs_assets_image(env->assets, name);
}

static void draw_name(const creation *cr, const creation_env *env, bs_overlay *o)
{
    const bs_font *f = env->fonts[1];
    if (!f)
        return;
    char shown[64];
    ui_cp437(cr->name, shown, sizeof shown);
    int w = bs_overlay_text(o, f, NAME_X, NAME_Y, shown);
    if ((env->now_ms / 400) & 1)
        bs_overlay_fill(o, NAME_X + w + 1, NAME_Y, 2, bs_text_height(f), 0xFFFFFFFFu);
}

static void draw_race(const creation *cr, const creation_env *env, bs_overlay *o)
{
    const bs_bsi_image *frame = bs_assets_image(env->assets, "brdr00.bsi");
    int fx = race_buttons[cr->race][0] + FRAME_DX, fy = race_buttons[cr->race][1] + FRAME_DY;
    uint64_t since = env->now_ms - cr->slide_ms;
    if (cr->slide_ms && since < SLIDE_MS) {
        int sx = race_buttons[cr->slide_from][0] + FRAME_DX, sy = race_buttons[cr->slide_from][1] + FRAME_DY;
        fx = sx + (int)((int64_t)(fx - sx) * (int64_t)since / SLIDE_MS);
        fy = sy + (int)((int64_t)(fy - sy) * (int64_t)since / SLIDE_MS);
    }
    image_at(o, frame, (uint32_t)(env->now_ms / FRAME_MS), fx, fy);
    char s[2048];
    ui_rsc_text(env->rsc, RACE_TEXT + cr->race, s, sizeof s);
    ui_text_box(o, env->fonts[10], s, RACE_TEXT_X, RACE_TEXT_Y, RACE_TEXT_W);
}

/* The slots' pictures and names, the frame on the one chosen (0x325B9). */
static void draw_load(const creation *cr, const creation_env *env, bs_overlay *o)
{
    for (int n = 0; n < CREATION_SAVES; n++) {
        int x, y;
        load_slot_xy(n, &x, &y);
        if (cr->saves[n].have_picture)
            bs_overlay_rgb(o, cr->saves[n].rgb, BS_SAVE_IMAGE_WIDTH, BS_SAVE_IMAGE_HEIGHT, x, y);
        text(o, env->fonts[8], x, y + LOAD_NAME_DY, cr->saves[n].name);
    }
    int fx, fy;
    load_slot_xy(cr->load_slot, &fx, &fy);
    uint64_t since = env->now_ms - cr->load_slide_ms;
    if (cr->load_slide_ms && since < SLIDE_MS) {
        int sx, sy;
        load_slot_xy(cr->load_from, &sx, &sy);
        fx = sx + (int)((int64_t)(fx - sx) * (int64_t)since / SLIDE_MS);
        fy = sy + (int)((int64_t)(fy - sy) * (int64_t)since / SLIDE_MS);
    }
    image_at(o, bs_assets_image(env->assets, "brdr01.bsi"), (uint32_t)(env->now_ms / FRAME_MS), fx - LOAD_FRAME_D,
             fy - LOAD_FRAME_D);
    if (env->saves_dir && env->save_root && env->fonts[8]) {
        const char *page = cr->load_originals ? "The original game's saves  (Page Up: yours)"
                                              : "Your saves  (Page Down: the original game's)";
        text(o, env->fonts[8], 320 - bs_text_width(env->fonts[8], page) / 2, LOAD_PAGE_Y, page);
    }
}

static void draw_face(const creation *cr, const creation_env *env, bs_overlay *o)
{
    char name[32];
    snprintf(name, sizeof name, "%c%sbd001.bsi", cr->female ? 'f' : 'm', creation_race_code(cr->race));
    const bs_bsi_image *body = bs_assets_image(env->assets, name);
    if (body)
        bs_overlay_image(o, body, DOLL_DX, DOLL_DY);
    static const int order[3] = {1, 2, 0}; /* eyes, mouth, hair, as the doll's layers */
    for (int k = 0; k < 3; k++) {
        const bs_bsi_image *img = part_image(env, cr, order[k]);
        if (img)
            bs_overlay_image(o, img, DOLL_DX, DOLL_DY);
    }
    for (int p = 0; p < 3; p++)
        image_centred(o, part_image(env, cr, p), part_x[p], PART_Y);
}

static void draw_left(const creation *cr, const creation_env *env, bs_overlay *o)
{
    const bs_class *k = &cr->c.cls;
    const bs_font *f = env->fonts[1];
    char s[96], shown[96];
    snprintf(s, sizeof s, "%d", (int)k->wounds);
    text(o, f, 241, POINTS_Y, s);
    ui_cp437(k->class_name, shown, sizeof shown);
    int w = f ? bs_overlay_text(o, f, 36, 20, shown) : 0;
    if (cr->naming_class && f && (env->now_ms / 400) & 1)
        bs_overlay_fill(o, 36 + w + 1, 20, 2, bs_text_height(f), 0xFFFFFFFFu);
    /* The skills, "Name [ATT] : n%". */
    const bs_font *f3 = env->fonts[3];
    int lh = f3 ? bs_text_height(f3) : 10;
    for (int list = 0; list < 3; list++)
        for (int r = 0; r < list_slots[list]; r++) {
            int sk = k->class_skills[list_first[list] + r];
            if (sk >= BS_SCL_SKILLS)
                continue;
            snprintf(s, sizeof s, "%s [%s] : %d%%", env->skill_names[sk] ? env->skill_names[sk] : "",
                     env->attribute_names[bs_skill_attribute[sk]] ? env->attribute_names[bs_skill_attribute[sk]] : "",
                     (int)k->skills[sk]);
            ui_cp437(s, shown, sizeof shown);
            text(o, f3, SKILL_TEXT_X, list_text_y[list] + lh * r, shown);
        }
    for (int i = 0; i < 8; i++) {
        snprintf(s, sizeof s, "%d", (int)k->attributes[i]);
        text(o, f, 17, 105 + 50 * i, s);
    }
}

static void draw_spells(const creation *cr, const creation_env *env, bs_overlay *o)
{
    if (cr->page == PAGE_SPELLS) {
        draw_list(cr, env, LIST_KNOWN, o);
        return;
    }
    draw_list(cr, env, LIST_OFFERED, o);
    if (cr->list_selected >= 0 && cr->list_selected < cr->offered_count) {
        char s[1024];
        ui_rsc_text(env->rsc, SPELL_TEXT + env->spells[cr->offered[cr->list_selected]].effect, s, sizeof s);
        ui_text_box(o, env->fonts[10], s, SHOP_DESC_X, SPELL_TEXT_Y, SHOP_DESC_W);
    }
}

/* The description of a magic item (0x3B9D5): a potion's by its type, the enchanted ones by their place. */
static int32_t magic_text(const bs_item *it)
{
    if (it->base == BS_ITEM_POTION)
        return POTION_TEXT + it->number;
    for (int i = 0; i < 12; i++)
        if (shop_enchanted[i] == it->magic)
            return ENCHANTED_TEXT + i;
    return MAGIC_TEXT;
}

static void draw_shop(const creation *cr, const creation_env *env, bs_overlay *o)
{
    int s = cr->page == PAGE_MAGIC;
    const bs_creation_shop *shop = &cr->shops[s];
    uint32_t at = cr->shop_at[s];
    if (at >= shop->count)
        return;
    const bs_item *it = &shop->items[at];
    if (!shop->bought[at]) {
        bs_overlay_image(o, bs_assets_image(env->assets, "slct00.bsi"), 0, 0);
    } else {
        bs_overlay_image(o, bs_assets_image(env->assets, "dlct00.bsi"), 0, 0);
        bs_overlay_image(o, bs_assets_image(env->assets, "brdr02.bsi"), 0, 0);
    }
    const bs_font *f = env->fonts[8];
    char l1[128], l2[128], shown[128];
    l1[0] = l2[0] = '\0';
    if (env->item_lines)
        env->item_lines(env->ctx, it, l1, l2, sizeof l1);
    int lh = f ? bs_text_height(f) : 10;
    ui_cp437(l1, shown, sizeof shown);
    text(o, f, SHOP_TEXT_X, SHOP_LINE_Y, shown);
    ui_cp437(l2, shown, sizeof shown);
    text(o, f, SHOP_TEXT_X, SHOP_LINE_Y + 1 + lh, shown);
    char cost[64];
    snprintf(cost, sizeof cost, "%s: %d", env->text ? bs_exe_message(env->text, MSG_COST) : "Cost",
             (int)bs_creation_shop_cost(shop, at));
    text(o, f, SHOP_TEXT_X, SHOP_COST_Y, cost);
    char d[1024];
    ui_rsc_text(env->rsc, s ? magic_text(it) : EQUIPMENT_TEXT + it->base, d, sizeof d);
    ui_text_box(o, env->fonts[10], d, SHOP_DESC_X, SHOP_DESC_Y, SHOP_DESC_W);
}

/* Copies a part of another full-screen image over the page. */
static void patch(bs_overlay *o, const bs_bsi_image *img, int x, int y, int w, int h)
{
    if (!img)
        return;
    bs_overlay_clip(o, x, y, w, h);
    bs_overlay_image(o, img, 0, 0);
    bs_overlay_clip(o, 0, 0, 0, 0);
}

/* What one of the class's choices shows (0x3A930): pass `on` from the class, or what's excluded. */
static void draw_choices(bs_overlay *o, const bs_bsi_image *img, const bs_class *k, const bool flags[7],
                         bool no_regen, bool no_shield, uint8_t weapons)
{
    const uint8_t rows[4] = {k->resists, k->immune, k->low_tolerance, k->critical_weakness};
    static const int row_x[4] = {64, 64, 352, 352}, row_y[4] = {304, 384, 304, 384};
    for (int r = 0; r < 4; r++)
        for (int e = 0; e < 5; e++)
            if (rows[r] & (1 << e))
                patch(o, img, row_x[r] + ELEMENT_STEP * e, row_y[r], 32, 32);
    for (int i = 0; i < 7; i++)
        if (flags[i])
            patch(o, img, 64, 128 + FLAG_ROW * i, 193, FLAG_ROW);
    if (no_regen)
        patch(o, img, 352, 80, 209, FLAG_ROW);
    if (no_shield)
        patch(o, img, 352, 96, 209, FLAG_ROW);
    for (int e = 0; e < 5; e++)
        if (weapons & (1 << e))
            patch(o, img, 352 + ELEMENT_STEP * e, 224, 33, 32);
}

static void draw_advantages(const creation *cr, const creation_env *env, bs_overlay *o)
{
    const bs_class *k = &cr->c.cls;
    const bool on[7] = {k->acute_hearing, k->spell_absorption, k->rapid_healing, k->regenerate_health,
                        k->regenerate_sp, k->athleticism, k->adrenaline_rush};
    /* What's excluded (0x3AEE1): the other tolerances to an element, and No Regeneration with Regenerate
     * SPs. */
    bs_class out;
    memset(&out, 0, sizeof out);
    for (int e = 0; e < 5; e++) {
        int bit = 1 << e;
        out.resists |= (uint8_t)(bs_creation_excluded_tolerance(&cr->c, BS_TOL_RESISTANCE, bit) ? bit : 0);
        out.immune |= (uint8_t)(bs_creation_excluded_tolerance(&cr->c, BS_TOL_IMMUNITY, bit) ? bit : 0);
        out.low_tolerance |= (uint8_t)(bs_creation_excluded_tolerance(&cr->c, BS_TOL_LOW, bit) ? bit : 0);
        out.critical_weakness |= (uint8_t)(bs_creation_excluded_tolerance(&cr->c, BS_TOL_CRITICAL, bit) ? bit : 0);
    }
    const bool off[7] = {false, false, false, false, bs_creation_excluded_flag(&cr->c, BS_ADV_REGENERATE_SP), false,
                         false};
    draw_choices(o, bs_assets_image(env->assets, "char09.bsi"), k, on, k->no_sp_regen, k->forbidden_shield,
                 k->forbidden_weapons);
    draw_choices(o, bs_assets_image(env->assets, "char10.bsi"), &out, off,
                 bs_creation_excluded_flag(&cr->c, BS_DIS_NO_SP_REGEN), false, 0);
    const bs_font *f = env->fonts[1];
    if (!f)
        return;
    static const int32_t magery_steps[BS_CREATION_MAGERY_STEPS] = {0x100, 0x180, 0x200, 0x300};
    int m = 0;
    while (m < BS_CREATION_MAGERY_STEPS - 1 && k->magery > magery_steps[m])
        m++;
    const char *labels[3] = {magery_names[m], k->max_armour < 4 ? env->armour_names[k->max_armour] : NULL,
                             env->db && env->db->tables && k->max_material < BS_ITEM_MATERIAL_COUNT
                                 ? env->db->tables->materials[k->max_material]
                                 : NULL};
    static const int label_x[3] = {248, 464, 464}, label_y[3] = {242, 129, 161};
    for (int i = 0; i < 3; i++)
        if (labels[i])
            bs_overlay_text(o, f, label_x[i] - bs_text_width(f, labels[i]) / 2, label_y[i], labels[i]);
}

void creation_draw(const creation *cr, const creation_env *env, bs_overlay *o)
{
    switch (cr->screen) {
    case CREATION_NAME:
        draw_name(cr, env, o);
        break;
    case CREATION_RACE:
        draw_race(cr, env, o);
        break;
    case CREATION_LOAD_GAME:
        draw_load(cr, env, o);
        break;
    case CREATION_FACE:
        draw_face(cr, env, o);
        break;
    case CREATION_CLASS_LIST:
        draw_list(cr, env, LIST_CLASSES, o);
        break;
    case CREATION_EDITOR: {
        char s[32];
        snprintf(s, sizeof s, "%d", (int)cr->c.points);
        if (cr->page == PAGE_ADVANTAGES) {
            text(o, env->fonts[1], POINTS_X_ADVANTAGES, POINTS_Y_ADVANTAGES, s);
            draw_advantages(cr, env, o);
        } else {
            text(o, env->fonts[1], POINTS_X, POINTS_Y, s);
            draw_left(cr, env, o);
        }
        if (cr->page == PAGE_SPELLS || cr->page == PAGE_SPELL_PICK)
            draw_spells(cr, env, o);
        else if (cr->page == PAGE_EQUIPMENT || cr->page == PAGE_MAGIC)
            draw_shop(cr, env, o);
        if (cr->skill_slot >= 0) {
            image_at(o, bs_assets_image(env->assets, "plst00.bsi"), 0, PICKER_X, PICKER_Y);
            draw_list(cr, env, LIST_SKILLS, o);
        }
        if (cr->confirm)
            bs_overlay_image(o, bs_assets_image(env->assets, "sure00.bsi"), 0, 0);
        break;
    }
    default:
        break;
    }
    if (env->help && env->help->open == MENU_HELP) {
        menu_env me;
        memset(&me, 0, sizeof me);
        me.assets = env->assets;
        memcpy(me.fonts, env->fonts, sizeof me.fonts);
        menus_draw(env->help, &me, o);
    }
}
