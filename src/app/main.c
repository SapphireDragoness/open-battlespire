/*
 * openbattlespire: the game.
 *
 * So far: the start-up sequence (the intro video, the title menu with its
 * music), and Single Player entering the first level, which can be explored
 * with its music and ambient sounds. The level's moving parts work: using
 * objects, stepping onto them, doors, lifts and teleports (docs/re/README.md,
 * "Level logic"). The player walks with the original's collision, speeds,
 * gravity and jumping ("Player movement and collision"); F toggles flying.
 * Items are placed and filled into containers as the game does
 * (docs/formats/items.md); using an item or a container opens the inventory
 * screen with it (inventory.c), as F10 (or I) does with what lies around, and
 * messages show at the top of the screen. Monsters spawn, hunt the player and
 * fight in melee (fight.c, docs/formats/creatures.md). Spells are cast from
 * the spell book (F12, spellbook.c) or its hot keys F1-F8, by monsters and
 * from potions (docs/formats/spells.md).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "bs/assets/assets.h"
#include "bs/audio/audio.h"
#include "bs/audio/level_audio.h"
#include "bs/audio/video.h"
#include "bs/core/fs.h"
#include "bs/core/install.h"
#include "bs/core/log.h"
#include "bs/core/png.h"
#include "bs/formats/bsa.h"
#include "bs/formats/exe_text.h"
#include "bs/formats/font.h"
#include "bs/formats/items.h"
#include "bs/formats/le.h"
#include "bs/formats/levels.h"
#include "bs/formats/rsc.h"
#include "bs/formats/spells.h"
#include "bs/game/collision.h"
#include "bs/game/items.h"
#include "bs/game/level_logic.h"
#include "bs/game/level_script.h"
#include "bs/game/player.h"
#include "bs/game/use.h"
#include "bs/render/gl.h"
#include "bs/render/level_renderer.h"
#include "bs/render/level_view.h"
#include "bs/render/overlay.h"
#include "bs/render/screen.h"
#include "controls.h"
#include "fight.h"
#include "inventory.h"
#include "spellbook.h"
#include "statusbar.h"

#define MOUSE_SENSITIVITY 0.0025f
/* Keyboard turning and looking (the original's turn rate is an option; these are guesses). */
#define TURN_RATE 3.0f       /* radians per second */
#define LOOK_RATE 1.5f
#define FLY_SPEED 400.0f     /* level units per second, flying */
#define SOUND_JUMP 401       /* 400 for a female character (0x40BAB) */
#define TITLE_TRACK 6        /* RE: 0x31B50 */
#define TITLE_IMAGE "mult01.bsi"
#define SCREENSHOT_MS 3000
/* How far away an object or item can be used (0x4D0E0, 0x4D16C). */
#define USE_REACH 192.0f
#define NO_OBJECT UINT32_MAX
#define SCENE_MODELS (INVENTORY_MODELS + BS_ITEM_LAST_WEAPON + 1) /* the item models, then the weapons in hand */
/* On-screen messages (0x1D211, drawn by 0x1D3A8): up to 16 lines of 80
 * characters, each for 10 s, centred from y = 5, white (FONT0001) over a
 * shadow (FONT0003) two pixels down and right. */
#define MESSAGE_SLOTS 16
#define MESSAGE_LEN 80
#define MESSAGE_MS 10000
#define MESSAGE_TOP 5
#define MESSAGE_WRAP 600     /* TEXT.RSC messages are wrapped to this width (0x4DBF2) */
#define PROP_MESSAGE 114     /* BS6: a TEXT.RSC message shown on use (0x24C26) */
#define PROP_DOOR 100        /* BS6: a door to another level file or interior (0x24817) */
#define PROP_ARRIVAL 101     /* BS6: an arrival point's number on a flat (0x248E6) */
/* The Teleport spell's screen (0x2B22F): teleport.bsi with two buttons, x0, y0, x1, y1. */
static const int anchor_button[4] = {166, 182, 300, 213};
static const int teleport_button[4] = {313, 182, 447, 213};

/* Title menu buttons, in the image's 640x480 pixels (RE: table at 0xEDBC8). */
typedef enum title_button { BUTTON_NONE = -1, BUTTON_SINGLE, BUTTON_MULTI, BUTTON_EXIT } title_button;
static const int title_buttons[3][4] = {
    {195, 258, 426, 286}, /* Single Player */
    {195, 295, 426, 323}, /* Multi Player */
    {576, 416, 639, 479}, /* Exit */
};

typedef enum state { STATE_INTRO, STATE_TITLE, STATE_LEVEL, STATE_QUIT } state;

typedef struct options {
    const char *data_dir;
    const char *level;   /* start in this level, skipping intro and title */
    bool no_intro;
    bool mute;
    bool fixed_ambience;
    bool original_keys;
    int width, height;
    bool female;             /* the character, until there is character creation */
    const char *race;
    const char *name;
    const char *screenshot;  /* write the screen after SCREENSHOT_MS and quit (for testing) */
    bool inventory;          /* open the inventory screen at the start (for testing) */
    bool no_monsters;
    const char *spells;      /* spells known at the start: numbers, or "all" (for testing) */
} options;

typedef struct door_name_t {
    char s[16];
} door_name_t;

typedef struct game {
    bs_install inst;
    bs_vfs vfs;
    bs_cdimage *cd;
    bs_assets *assets;
    bs_arena arena;          /* lives as long as the game: level table, title music */
    bs_level_table levels;
    SDL_Window *window;
    bs_audio *audio;         /* NULL: silent */

    state state;
    bs_screen *screen;
    bs_video *video;
    bs_arena video_arena;    /* the video file while it plays */
    uint8_t *title_music;
    size_t title_music_size;

    bs_level_view level;
    bs_level_logic logic;
    uint64_t level_start_ns;     /* the logic clock's zero */
    bs_collision_world collision;
    bs_player player;
    bs_player_stats stats;
    bool flying;
    controls keys;
    bool sneaking;       /* Sneak Mode, toggled */
    bool ducking;        /* original layout: Special held and the mouse pulled back */
    bs_level_audio level_audio;
    bs_camera cam;
    bs_render_settings settings;

    /* Text and items, read from GAME.EXE, TXT.BSA, TEXT.RSC; any may be missing. */
    bs_exe_text text;
    bs_rsc rsc;
    bs_item_tables item_tables;
    bs_magic_table magic[2];
    bs_item_db items;            /* items.tables NULL: no items */
    bs_items world_items;        /* the level's and the player's */
    int32_t *object_message;     /* per level object: a TEXT.RSC id, or -1 */
    uint32_t *item_objects;      /* level objects that show placed items */
    uint32_t item_object_count;
    bs_rng rng;
    char model_names[SCENE_MODELS][16];
    float model_radius[SCENE_MODELS];
    bs_level_instance *instances;    /* the dropped items' copies, then the weapon in hand */
    uint32_t item_instances, instance_cap;
    inventory inv;
    const bs_bsi_image *inv_background, *inv_filters[6], *inv_arrows[2];
    bs_font info_font;           /* FONT0008 */
    bool had_mouse;              /* the mouse was captured when the inventory opened */
    bs_character character;
    bs_level_script script;      /* keys (0x3E578) */
    bs_sigil *sigils;
    uint32_t sigil_count;
    uint32_t frame;              /* frames since the level started, for the sigils (0x16BF84) */
    int32_t *gem_percent;        /* per level object: gem property 110/111 value, or 0 */
    bool *gem_spell_points;      /* per level object: 111 rather than 110 */
    bool *locked;                /* per level object: property 103 (+0x13 bit 8) */
    uint32_t *gem_until;         /* per level object: hidden until (logic clock ms) */
    /* Level transitions. */
    char after_video[32];        /* the level a video leads to ("" for the title) */
    const bs_level_info *main_info;  /* the campaign level; interiors keep its number, script and music */
    door_name_t *door_name;      /* per level object: property 100's destination file ("" if no door) */
    int32_t *door_arrival;       /* per level object: its arrival point number */
    int32_t *arrival;            /* per flat: property 101's arrival point number, or -1 */
    struct {
        char file[32];
        bs_items items;
    } level_cache[16];           /* levels left through a door, with their items */
    uint32_t level_cache_count;
    enum { GO_NONE, GO_NEXT_LEVEL, GO_DOOR } go;   /* a transition to make between frames */
    char go_file[32];
    int32_t go_arrival;
    bs_spell spells[BS_SPELL_COUNT];
    bool have_spells;
    const char *name;
    bs_bsa *txt;                 /* TXT.BSA, for books */
    uint8_t *book;               /* the last book read */
    bs_arena book_arena;
    const char **pictures;       /* BSI names worn items can show */
    uint32_t picture_count;
    bool female;
    const char *race;
    bs_equip_rules rules;
    fight fight;                 /* the monsters and the fighting */
    bool monsters_on;
    spellbook spellbook;         /* F12 */
    statusbar bar;               /* the interface bar; Tab for the full view */
    const bs_bsi *spell_panel;   /* spmenu.bsi */
    const bs_bsi_image *deliveries[BS_DELIVERY_COUNT], *elements[BS_ELEMENT_COUNT];
    const bs_bsi_image *teleport_image;
    bool teleporting;            /* the Teleport spell's screen is up */
    bool hud_moving;             /* the overlay's spell pictures move: draw it again */

    bs_overlay *overlay;
    bs_font font, shadow_font;   /* FONT0001, FONT0003 */
    bool have_fonts;
    bool overlay_dirty;
    struct {
        char text[MESSAGE_LEN];
        uint64_t at_ms;
    } messages[MESSAGE_SLOTS];
} game;

static void usage(FILE *out)
{
    fprintf(out,
            "usage: openbattlespire [-d install-dir] [options]\n"
            "  --no-intro          skip the intro video\n"
            "  --level NAME        go straight into a level, e.g. l1.bs6\n"
            "  --mute              no sound\n"
            "  --fixed-ambience    ambient sounds from a random nearby node (the original picks a\n"
            "                      fixed node, usually out of earshot)\n"
            "  --size WxH          window size (default 1280x960)\n"
            "  --screenshot FILE   save the screen as PNG after 3 s and quit (for testing)\n"
            "  --inventory         open the inventory screen at the start (for testing)\n"
            "  --no-monsters       no monsters (their loot lies where they would have been)\n"
            "  --female            play a female character (default male)\n"
            "  --name NAME         the character's name (default Adventurer)\n"
            "  --race RACE         the character's race: rd br no hi dk wd (Redguard, Breton,\n"
            "                      Nord, High Elf, Dark Elf, Wood Elf; default br)\n"
            "  --keys LAYOUT       modern (default) or original (the game's own defaults)\n"
            "\n"
            "Any key skips the intro; on the title, Enter = Single Player, Esc = Exit.\n"
            "In a level, click to capture the mouse; the mouse looks around and the left button\n"
            "uses what's under the crosshair; hold the right button and move the mouse to swing\n"
            "your weapon (down chops, across slashes, up thrusts). Hold the jump key to charge\n"
            "a jump (the pointer shows where you'll land), release to jump. Sneak toggles slow\n"
            "movement.\n"
            "F12 opens the spell book: pick a spell, click its icon, delivery, element and\n"
            "detonation to change them, press F1-F8 to give it a hot key, Cast to cast it.\n"
            "In play F1-F8 cast; a touch spell goes with the next use of a monster, a ranged one\n"
            "with the right button.\n"
            "Using an item or container opens the inventory: drag items between the lists,\n"
            "double-click a container to open it (its box to go back), drop items on the\n"
            "character to equip them, drag them off to take them off, double-click a book to\n"
            "read it or a potion to drink it; Esc or Exit to close.\n"
            "The bar at the bottom shows health (blue), spell points (yellow) and the compass;\n"
            "with the mouse free its buttons open the spell book and the inventory. Tab hides it.\n"
            "  modern:   W A S D move, Space jump, C duck, Ctrl sneak, E use, I or F10 inventory,\n"
            "            PgUp/PgDn (R/V) up/down\n"
            "  original: arrows move, Alt jump, Delete/End turn, Insert/Home look, S sneak,\n"
            "            Space + mouse back duck, PgUp/PgDn up/down, F10 inventory\n"
            "  both:     F fly (Shift faster), G classic/enhanced rendering, M sound, Esc title\n");
}

static bool parse_args(int argc, char **argv, options *o, bool *help)
{
    memset(o, 0, sizeof *o);
    *help = false;
    o->width = 1280;
    o->height = 960;
    o->data_dir = getenv("BS_DATA_DIR");
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        bool has = i + 1 < argc;
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0)
            *help = true;
        else if ((strcmp(a, "-d") == 0 || strcmp(a, "--data") == 0) && has)
            o->data_dir = argv[++i];
        else if (strcmp(a, "--level") == 0 && has)
            o->level = argv[++i];
        else if (strcmp(a, "--no-intro") == 0)
            o->no_intro = true;
        else if (strcmp(a, "--mute") == 0)
            o->mute = true;
        else if (strcmp(a, "--fixed-ambience") == 0)
            o->fixed_ambience = true;
        else if (strcmp(a, "--screenshot") == 0 && has)
            o->screenshot = argv[++i];
        else if (strcmp(a, "--inventory") == 0)
            o->inventory = true;
        else if (strcmp(a, "--no-monsters") == 0)
            o->no_monsters = true;
        else if (strcmp(a, "--spells") == 0 && has)
            o->spells = argv[++i];
        else if (strcmp(a, "--female") == 0)
            o->female = true;
        else if (strcmp(a, "--name") == 0 && has)
            o->name = argv[++i];
        else if (strcmp(a, "--race") == 0 && has) {
            static const char *const races[6] = {"rd", "br", "no", "hi", "dk", "wd"};
            const char *rc = argv[++i];
            bool ok = false;
            for (int k = 0; k < 6; k++)
                ok = ok || strcmp(rc, races[k]) == 0;
            if (!ok)
                return false;
            o->race = rc;
        }
        else if (strcmp(a, "--keys") == 0 && has) {
            const char *k = argv[++i];
            if (strcmp(k, "original") == 0)
                o->original_keys = true;
            else if (strcmp(k, "modern") != 0)
                return false;
        }
        else if (strcmp(a, "--size") == 0 && has) {
            if (sscanf(argv[++i], "%dx%d", &o->width, &o->height) != 2 || o->width < 64 || o->height < 64)
                return false;
        } else
            return false;
    }
    return true;
}

/* ---- states ---- */

static void leave_state(game *g)
{
    switch (g->state) {
    case STATE_INTRO:
        bs_video_close(g->video);
        g->video = NULL;
        bs_arena_free(&g->video_arena);
        break;
    case STATE_TITLE:
        if (g->audio)
            bs_audio_stop_music(g->audio);
        break;
    case STATE_LEVEL:
        inventory_close(&g->inv);
        g->spellbook.open = false;
        g->teleporting = false;
        bs_level_audio_stop(&g->level_audio);
        bs_level_view_unload(&g->level);
        SDL_SetWindowRelativeMouseMode(g->window, false);
        break;
    case STATE_QUIT:
        break;
    }
}

static void enter_title(game *g);
static void update_item_world(game *g);

static bool enter_level(game *g, const char *file);
static fight_env fight_env_of(game *g);
static void show_instances(game *g);

/* After a video: the level it leads to, or the title. */
static void after_video(game *g)
{
    char next[32];
    snprintf(next, sizeof next, "%s", g->after_video);
    g->after_video[0] = '\0';
    if (next[0])
        enter_level(g, next);
    else
        enter_title(g);
}

/* Plays a CD video, then goes on to `next` (a level file, or "" for the title). */
static void play_video(game *g, const char *path, const char *next)
{
    leave_state(g);
    g->state = STATE_INTRO;
    snprintf(g->after_video, sizeof g->after_video, "%s", next ? next : "");
    uint8_t *data;
    size_t size;
    bs_arena_init(&g->video_arena, 0);
    bs_result r = bs_vfs_read(&g->vfs, path, &g->video_arena, &data, &size);
    if (r == BS_OK)
        r = bs_video_open(g->audio, data, size, &g->video);
    if (r != BS_OK) {
        BS_WARN("%s: %s", path, bs_result_str(r));
        after_video(g);
    }
}

/* The intro plays STORY.SMK from the CD; without it, the title comes straight up. */
static void enter_intro(game *g)
{
    play_video(g, "VIDEOS/STORY.SMK", "");
}

static void enter_title(game *g)
{
    leave_state(g);
    g->state = STATE_TITLE;
    const bs_bsi_image *img = bs_assets_image(g->assets, TITLE_IMAGE);
    if (!img || bs_screen_set_bsi(g->screen, img) != BS_OK)
        BS_WARN("cannot show %s", TITLE_IMAGE);
    if (g->audio && g->title_music &&
        bs_audio_play_music(g->audio, g->title_music, g->title_music_size, true, g->level_audio.music_gain) != BS_OK)
        BS_WARN("cannot play the title music");
}

/* The feet position of arrival point n (a flat with property 101 = n). */
static bool arrival_point(const game *g, int32_t n, float out[3])
{
    for (uint32_t i = 0; g->arrival && i < g->level.level.flat_count; i++)
        if (g->arrival[i] == n) {
            for (int c = 0; c < 3; c++)
                out[c] = (float)g->level.level.flats[i].pos[c];
            return true;
        }
    return false;
}

/* Keeps the current level's items aside while the player is in another (an interior). */
static void cache_level(game *g)
{
    if (!g->level.loaded || !g->level.file[0])
        return;
    uint32_t k = 0;
    while (k < g->level_cache_count && bs_strcasecmp(g->level_cache[k].file, g->level.file) != 0)
        k++;
    if (k == g->level_cache_count) {
        if (g->level_cache_count == sizeof g->level_cache / sizeof g->level_cache[0]) {
            BS_WARN("too many levels to keep: %s's items will be made again", g->level.file);
            bs_items_keep_carried(&g->world_items);
            return;
        }
        g->level_cache_count++;
    } else {
        bs_items_free(&g->level_cache[k].items);
    }
    snprintf(g->level_cache[k].file, sizeof g->level_cache[k].file, "%s", g->level.file);
    if (bs_items_take_level(&g->world_items, &g->level_cache[k].items) != BS_OK) {
        BS_WARN("cannot keep %s's items", g->level.file);
        bs_items_keep_carried(&g->world_items);
    }
}

static void clear_level_cache(game *g)
{
    for (uint32_t k = 0; k < g->level_cache_count; k++)
        bs_items_free(&g->level_cache[k].items);
    g->level_cache_count = 0;
}

/*
 * Enters a level file: a campaign level from the start, or (through_door)
 * a level or interior at arrival point `arrival` (BS6 property 101), keeping
 * the level left (its items) and the campaign level's number and script.
 */
static bool enter_level_ex(game *g, const char *file, int32_t arrival, bool through_door)
{
    if (through_door)
        cache_level(g);
    else
        clear_level_cache(g);
    leave_state(g);
    g->state = STATE_QUIT; /* nothing to leave if loading fails */
    const bs_level_info *info = NULL;
    for (size_t i = 0; i < g->levels.count && !info; i++)
        if (bs_strcasecmp(g->levels.levels[i].file, file) == 0)
            info = &g->levels.levels[i];
    if (!through_door || !g->main_info)
        g->main_info = info;
    /* The item models are hidden objects of the level's scene, for the inventory and dropped items; then
     * the weapons in hand. */
    const char *models[SCENE_MODELS];
    for (uint32_t i = 0; i < SCENE_MODELS; i++)
        models[i] = g->model_names[i];
    bs_result r = bs_level_view_load_ex(&g->level, g->assets, &g->vfs, file, info, 0.0f, &g->settings, models,
                                        g->items.tables ? SCENE_MODELS : 0);
    if (r != BS_OK) {
        fprintf(stderr, "openbattlespire: cannot load %s: %s\n", file, bs_result_str(r));
        enter_title(g);
        return false;
    }
    r = bs_level_logic_build(&g->level.level, &g->level.arena, &g->logic);
    if (r != BS_OK) {
        fprintf(stderr, "openbattlespire: %s: level logic: %s\n", file, bs_result_str(r));
        bs_level_view_unload(&g->level);
        enter_title(g);
        return false;
    }
    r = bs_collision_build(&g->level.scene, &g->level.level, &g->level.arena, &g->collision);
    if (r != BS_OK) {
        fprintf(stderr, "openbattlespire: %s: collision: %s\n", file, bs_result_str(r));
        bs_level_view_unload(&g->level);
        enter_title(g);
        return false;
    }
    /* The items, and the messages objects show when used. An interior counts as its campaign level. */
    const bs_level_info *campaign = g->main_info ? g->main_info : info;
    int number = campaign ? bs_level_number(campaign) : 1;
    bool multiplayer = campaign && bs_level_game_type(campaign) != BS_GAME_CAMPAIGN;
    /* What the player carries goes on; a level visited before has its items as they were left. */
    bs_items_keep_carried(&g->world_items);
    uint32_t cached = 0;
    while (cached < g->level_cache_count && bs_strcasecmp(g->level_cache[cached].file, file) != 0)
        cached++;
    if (cached < g->level_cache_count) {
        if (bs_items_put_level(&g->world_items, &g->level_cache[cached].items) != BS_OK)
            BS_WARN("%s: cannot restore the level's items", file);
        bs_items_free(&g->level_cache[cached].items);
        g->level_cache[cached] = g->level_cache[--g->level_cache_count];
    } else if (g->items.tables &&
               bs_items_add_level(&g->world_items, &g->level.level, &g->items, number, multiplayer,
                                  campaign && campaign == info ? info->grid_cell : 2048, &g->rng) != BS_OK) {
        BS_WARN("%s: cannot make the level's items", file);
    }
    /* The objects that show placed items. */
    g->item_objects = BS_ARENA_ARRAY(&g->level.arena, uint32_t, g->world_items.count + 1);
    g->item_object_count = 0;
    for (uint32_t i = 0; g->item_objects && i < g->world_items.count; i++)
        if (g->world_items.items[i].place.where == BS_ITEM_FLOOR && g->world_items.items[i].object >= 0)
            g->item_objects[g->item_object_count++] = (uint32_t)g->world_items.items[i].object;
    inventory_init(&g->inv);
    /* Keys, sigils and gems; the script goes on through doors. */
    if (!through_door)
        bs_level_script_init(&g->script, number);
    uint32_t nobj = g->level.level.object_count;
    g->sigils = BS_ARENA_ARRAY(&g->level.arena, bs_sigil, nobj + 1);
    g->gem_percent = BS_ARENA_ARRAY(&g->level.arena, int32_t, nobj + 1);
    g->gem_spell_points = BS_ARENA_ARRAY(&g->level.arena, bool, nobj + 1);
    g->gem_until = BS_ARENA_ARRAY(&g->level.arena, uint32_t, nobj + 1);
    g->locked = BS_ARENA_ARRAY(&g->level.arena, bool, nobj + 1);
    g->sigil_count = 0;
    g->frame = 0;
    for (uint32_t i = 0; g->sigils && g->gem_percent && g->gem_spell_points && g->gem_until && g->locked && i < nobj;
         i++) {
        const bs_bs6_object *lo = &g->level.level.objects[i];
        const char *model = lo->model < g->level.level.model_count ? g->level.level.model_names[lo->model] : "";
        char letter;
        if (bs_sigil_model(model, &letter))
            g->sigils[g->sigil_count++] =
                (bs_sigil){.object = i, .letter = letter, .pos = {(float)lo->pos[0], (float)lo->pos[1], (float)lo->pos[2]}};
        bs_bs6_logic lg;
        bs_arena_mark mark = bs_arena_get_mark(&g->level.arena);
        if (bs_bs6_entity_logic(&lo->chunk, &g->level.arena, &lg) == BS_OK) {
            const bs_bs6_record *gem = bs_bs6_logic_prop(&lg, 110), *sp = bs_bs6_logic_prop(&lg, 111);
            g->gem_percent[i] = gem ? bs_bs6_record_i32(gem, 0) : sp ? bs_bs6_record_i32(sp, 0) : 0;
            g->gem_spell_points[i] = !gem && sp;
            g->locked[i] = bs_bs6_logic_prop(&lg, 103) != NULL;
        }
        bs_arena_rewind(&g->level.arena, mark);
    }
    g->object_message = BS_ARENA_ARRAY(&g->level.arena, int32_t, g->level.level.object_count + 1);
    for (uint32_t i = 0; g->object_message && i < g->level.level.object_count; i++) {
        bs_bs6_logic lg;
        const bs_bs6_record *rec = NULL;
        bs_arena_mark mark = bs_arena_get_mark(&g->level.arena);
        if (bs_bs6_entity_logic(&g->level.level.objects[i].chunk, &g->level.arena, &lg) == BS_OK)
            rec = bs_bs6_logic_prop(&lg, PROP_MESSAGE);
        g->object_message[i] = rec ? bs_bs6_record_i32(rec, 0) : -1;
        bs_arena_rewind(&g->level.arena, mark);
    }
    /* Doors (property 100: a 14-byte destination name, then its arrival point number, 0x24817) and
     * arrival points (property 101 on flats, 0x248E6). */
    g->door_name = BS_ARENA_ARRAY(&g->level.arena, door_name_t, nobj + 1);
    g->door_arrival = BS_ARENA_ARRAY(&g->level.arena, int32_t, nobj + 1);
    g->arrival = BS_ARENA_ARRAY(&g->level.arena, int32_t, g->level.level.flat_count + 1);
    for (uint32_t i = 0; g->door_name && g->door_arrival && i < nobj; i++) {
        bs_bs6_logic lg;
        bs_arena_mark mark = bs_arena_get_mark(&g->level.arena);
        const bs_bs6_record *rec = NULL;
        if (bs_bs6_entity_logic(&g->level.level.objects[i].chunk, &g->level.arena, &lg) == BS_OK)
            rec = bs_bs6_logic_prop(&lg, PROP_DOOR);
        g->door_name[i].s[0] = '\0';
        if (rec && rec->size >= 18) {
            size_t n = 0;
            while (n < 14 && n < sizeof g->door_name[i].s - 1 && rec->data[n])
                n++;
            memcpy(g->door_name[i].s, rec->data, n);
            g->door_name[i].s[n] = '\0';
            g->door_arrival[i] = (int32_t)((uint32_t)rec->data[14] | (uint32_t)rec->data[15] << 8 |
                                           (uint32_t)rec->data[16] << 16 | (uint32_t)rec->data[17] << 24);
        }
        bs_arena_rewind(&g->level.arena, mark);
    }
    for (uint32_t i = 0; g->arrival && i < g->level.level.flat_count; i++) {
        bs_bs6_logic lg;
        bs_arena_mark mark = bs_arena_get_mark(&g->level.arena);
        const bs_bs6_record *rec = NULL;
        if (bs_bs6_entity_logic(&g->level.level.flats[i].chunk, &g->level.arena, &lg) == BS_OK)
            rec = bs_bs6_logic_prop(&lg, PROP_ARRIVAL);
        g->arrival[i] = rec ? bs_bs6_record_i32(rec, 0) : -1;
        bs_arena_rewind(&g->level.arena, mark);
    }
    memset(g->messages, 0, sizeof g->messages);
    g->overlay_dirty = true;

    g->level_start_ns = SDL_GetTicksNS();
    g->state = STATE_LEVEL;
    /* The monsters. */
    fight_env fe = fight_env_of(g);
    fight_enter_level(&g->fight, &fe, info, &g->level.arena);
    /* The player starts at the START marker, or the arrival point, and drops to the floor. */
    bs_level_view_place_camera(&g->level, -1, &g->cam);
    if (arrival >= 0) {
        float at[3];
        if (arrival_point(g, arrival, at))
            memcpy(g->cam.pos, at, sizeof at);
        else
            BS_WARN("%s: no arrival point %d", file, (int)arrival);
    }
    bs_player_init(&g->player, g->cam.pos, g->cam.yaw);
    g->stats = (bs_player_stats){g->character.attributes[6], g->character.attributes[0],
                                 g->character.skills[BS_SKILL_JUMPING], multiplayer};
    bs_player_eye(&g->player, g->cam.pos);
    update_item_world(g);
    bs_level_audio_start(&g->level_audio, campaign);
    char title[128];
    snprintf(title, sizeof title, "OpenBattlespire - %s", campaign ? campaign->name : file);
    SDL_SetWindowTitle(g->window, title);
    return true;
}

static bool enter_level(game *g, const char *file)
{
    return enter_level_ex(g, file, -1, false);
}

/* The first campaign level: the one whose level number is 1. */
static const char *first_level(const game *g)
{
    for (size_t i = 0; i < g->levels.count; i++)
        if (bs_level_number(&g->levels.levels[i]) == 1 &&
            bs_level_game_type(&g->levels.levels[i]) == BS_GAME_CAMPAIGN)
            return g->levels.levels[i].file;
    return "l1.bs6";
}

static title_button button_at(const game *g, float x, float y)
{
    int w, h, ix, iy;
    SDL_GetWindowSize(g->window, &w, &h);
    if (!bs_screen_to_image(g->screen, w, h, x, y, &ix, &iy))
        return BUTTON_NONE;
    for (int b = 0; b < 3; b++) {
        const int *r = title_buttons[b];
        if (ix >= r[0] && ix <= r[2] && iy >= r[1] && iy <= r[3])
            return (title_button)b;
    }
    return BUTTON_NONE;
}

static void press(game *g, title_button b)
{
    switch (b) {
    case BUTTON_SINGLE:
        bs_items_free(&g->world_items); /* a new game: nothing carried */
        bs_character_default(&g->character);
        enter_level(g, first_level(g));
        break;
    case BUTTON_MULTI:
        BS_INFO("multiplayer is not implemented");
        break;
    case BUTTON_EXIT:
        leave_state(g);
        g->state = STATE_QUIT;
        break;
    case BUTTON_NONE:
        break;
    }
}

/* ---- messages ---- */

/* The fonts are code page 437 but the text is Windows-1252: map what has an equivalent. */
static void to_cp437(const char *in, char *out, size_t cap)
{
    size_t n = 0;
    for (const uint8_t *p = (const uint8_t *)in; *p && n + 1 < cap; p++) {
        uint8_t c = *p;
        switch (c) {
        case 0x91: case 0x92: c = '\''; break;
        case 0x93: case 0x94: c = '"'; break;
        case 0x96: case 0x97: c = '-'; break;
        case 0xE9: c = 130; break; /* é */
        case 0xE8: c = 138; break; /* è */
        case 0xE0: c = 133; break; /* à */
        default: if (c >= 0x80) c = '?';
        }
        out[n++] = (char)c;
    }
    out[n] = '\0';
}

/* Queues a message as the game does: skipped if already shown, dropped if all slots are in use. */
static void message(game *g, const char *text)
{
    char t[MESSAGE_LEN];
    to_cp437(text, t, sizeof t);
    for (int i = 0; i < MESSAGE_SLOTS; i++)
        if (g->messages[i].text[0] && strcmp(g->messages[i].text, t) == 0)
            return;
    for (int i = 0; i < MESSAGE_SLOTS; i++)
        if (!g->messages[i].text[0]) {
            memcpy(g->messages[i].text, t, sizeof t);
            g->messages[i].at_ms = SDL_GetTicks();
            g->overlay_dirty = true;
            return;
        }
}

static void fight_message(void *ctx, const char *text)
{
    message(ctx, text);
}

/* A TEXT.RSC record as messages, wrapped to MESSAGE_WRAP pixels. */
static void message_rsc(game *g, int32_t id)
{
    const bs_rsc_record *rec = id >= 0 && id < 0xFFFF ? bs_rsc_find(&g->rsc, (uint16_t)id) : NULL;
    if (!rec || !g->have_fonts)
        return;
    char line[MESSAGE_LEN];
    size_t ln = 0, i = 0;
    while (i <= rec->size) {
        /* The next word, and whether a line break follows it. */
        char word[MESSAGE_LEN];
        size_t wn = 0;
        bool brk = false;
        for (; i < rec->size; i++) {
            uint8_t c = rec->data[i];
            if (c == BS_RSC_CODE) {
                i++; /* and its argument */
            } else if (c == BS_RSC_NEWLINE || c == BS_RSC_NEWLINE_C) {
                brk = true;
                i++;
                break;
            } else if (c == ' ') {
                i++;
                break;
            } else if (c > ' ' && wn + 1 < sizeof word) {
                word[wn++] = (char)c;
            }
        }
        if (i >= rec->size) {
            brk = true;
            i = rec->size + 1;
        }
        if (wn) {
            char test[2 * MESSAGE_LEN], shown[2 * MESSAGE_LEN];
            size_t tn = ln;
            memcpy(test, line, ln);
            if (ln)
                test[tn++] = ' ';
            memcpy(test + tn, word, wn);
            tn += wn;
            test[tn] = '\0';
            to_cp437(test, shown, sizeof shown);
            if (ln && (tn >= MESSAGE_LEN || bs_text_width(&g->font, shown) > MESSAGE_WRAP)) {
                line[ln] = '\0';
                message(g, line);
                memcpy(line, word, wn);
                ln = wn;
            } else {
                memcpy(line, test, tn);
                ln = tn;
            }
        }
        if (brk && ln) {
            line[ln] = '\0';
            message(g, line);
            ln = 0;
        }
    }
}

/* Draws the messages into the overlay. */
static void draw_messages(game *g)
{
    if (!g->have_fonts)
        return;
    int lh = bs_text_height(&g->font), y = MESSAGE_TOP;
    for (int i = 0; i < MESSAGE_SLOTS; i++) {
        if (!g->messages[i].text[0])
            continue;
        int w = bs_text_width(&g->font, g->messages[i].text), x = BS_OVERLAY_WIDTH / 2 - w / 2;
        bs_overlay_fill(g->overlay, x - 4, y - 1, w + 8, lh + 2, 0x00000080);
        bs_overlay_text(g->overlay, &g->shadow_font, x + 2, y + 2, g->messages[i].text);
        bs_overlay_text(g->overlay, &g->font, x, y, g->messages[i].text);
        y += lh + 2;
    }
}

static void update_messages(game *g)
{
    uint64_t now = SDL_GetTicks();
    for (int i = 0; i < MESSAGE_SLOTS; i++)
        if (g->messages[i].text[0] && now - g->messages[i].at_ms > MESSAGE_MS) {
            g->messages[i].text[0] = '\0';
            g->overlay_dirty = true;
        }
    /* Close the gaps, keeping the order. */
    int k = 0;
    for (int i = 0; i < MESSAGE_SLOTS; i++)
        if (g->messages[i].text[0]) {
            if (i != k)
                g->messages[k] = g->messages[i];
            k++;
        }
    for (; k < MESSAGE_SLOTS; k++)
        g->messages[k].text[0] = '\0';
}

/* ---- level logic ---- */

static uint32_t logic_now(const game *g)
{
    return (uint32_t)((SDL_GetTicksNS() - g->level_start_ns) / 1000000);
}

static void fight_message(void *ctx, const char *text);

/* What the monsters and the fighting work on. */
static fight_env fight_env_of(game *g)
{
    const bs_level_info *campaign = g->main_info ? g->main_info : g->level.info;
    return (fight_env){
        .assets = g->assets,
        .vfs = &g->vfs,
        .audio = &g->level_audio,
        .text = &g->text,
        .items = &g->world_items,
        .db = g->items.tables ? &g->items : NULL,
        .character = &g->character,
        .player = &g->player,
        .listener = g->cam.pos,
        .listener_yaw = g->cam.yaw,
        .aim_yaw = g->cam.yaw,
        .aim_pitch = g->cam.pitch,
        .spells = g->have_spells ? g->spells : NULL,
        .collision = &g->collision,
        .level = &g->level.level,
        .rng = &g->rng,
        .now_ms = logic_now(g),
        .level_number = g->script.level_number,
        .multiplayer = campaign && bs_level_game_type(campaign) != BS_GAME_CAMPAIGN,
        .active_distance = campaign ? campaign->active_distance : 1500,
        .grid_cell = g->world_items.grid_cell,
        .message = fight_message,
        .ctx = g,
    };
}

static void apply_logic(game *g, const bs_logic_output *out)
{
    for (uint32_t i = 0; i < out->sound_count; i++)
        bs_level_audio_play_at(&g->level_audio, out->sounds[i].id, g->logic.objects[out->sounds[i].object].pos,
                               g->cam.pos, g->cam.yaw);
    if (out->player_moved) {
        /* Teleported: the logic moved the player's feet. */
        memcpy(g->player.pos, g->logic.player, sizeof g->player.pos);
        g->player.vy = 0;
        bs_player_eye(&g->player, g->cam.pos);
    }
}

static void activate(game *g, int32_t level_object, uint32_t event)
{
    uint32_t o = level_object >= 0 ? bs_level_logic_find(&g->logic, BS_LOGIC_OBJECT, (uint32_t)level_object)
                                   : NO_OBJECT;
    if (o == NO_OBJECT)
        return;
    bs_logic_output out;
    memcpy(g->logic.player, g->player.pos, sizeof g->logic.player);
    bs_level_logic_activate(&g->logic, o, event, logic_now(g), &out);
    apply_logic(g, &out);
}

/* ---- items ---- */

static void inventory_message(void *ctx, const char *text)
{
    message(ctx, text);
}

static const bs_bsi_image *inventory_image(void *ctx, const char *name)
{
    return bs_assets_image(((game *)ctx)->assets, name);
}

/* A book's text: TXT.BSA "bk??%03d.txt" (0x3C8BA), e.g. bk1_993.txt. */
static const uint8_t *inventory_book(void *ctx, int32_t number, size_t *size)
{
    game *g = ctx;
    if (!g->txt || number < 0 || number > 999)
        return NULL;
    char suffix[16];
    snprintf(suffix, sizeof suffix, "%03d.TXT", (int)number);
    for (uint32_t i = 0; i < g->txt->count; i++) {
        const char *n = g->txt->records[i].name;
        if (strlen(n) == 11 && (n[0] == 'b' || n[0] == 'B') && (n[1] == 'k' || n[1] == 'K') &&
            bs_strcasecmp(n + 4, suffix) == 0) {
            bs_arena_free(&g->book_arena);
            bs_arena_init(&g->book_arena, 0);
            if (bs_bsa_read(g->txt, &g->txt->records[i], &g->book_arena, &g->book, size) == BS_OK)
                return g->book;
            return NULL;
        }
    }
    BS_WARN("no book %d", (int)number);
    return NULL;
}

/* A potion's spell goes off on the drinker. */
static void inventory_drink(void *ctx, int32_t spell)
{
    game *g = ctx;
    fight_env fe = fight_env_of(g);
    fight_drink(&g->fight, &fe, spell);
}

/* What the inventory screen works on. */
static inventory_env inventory_env_of(game *g)
{
    inventory_env e;
    memset(&e, 0, sizeof e);
    e.items = &g->world_items;
    e.db = &g->items;
    e.strength = g->stats.strength;
    /* Dropped items land on the floor under the player (0x2BA21), 4 units up as monsters' loot does (0x52731). */
    float from[3] = {g->player.pos[0], g->player.pos[1] - 32.0f, g->player.pos[2]};
    float to[3] = {g->player.pos[0], g->player.pos[1] + 4000.0f, g->player.pos[2]}, t;
    int32_t ob;
    float floor_y = bs_collision_segment(&g->collision, from, to, &t, &ob) ? from[1] + t * (to[1] - from[1])
                                                                          : g->player.pos[1];
    e.drop_pos[0] = g->player.pos[0];
    e.drop_pos[1] = floor_y - 4.0f;
    e.drop_pos[2] = g->player.pos[2];
    e.renderer = g->level.renderer;
    e.model_first = g->level.extra_first;
    e.model_radius = g->model_radius;
    e.overlay = g->overlay;
    e.screen = g->screen;
    e.font_info = g->have_fonts ? &g->info_font : NULL;
    e.text = &g->text;
    memcpy(e.filter_on, g->inv_filters, sizeof e.filter_on);
    memcpy(e.arrow_off, g->inv_arrows, sizeof e.arrow_off);
    e.message = inventory_message;
    e.ctx = g;
    e.rules = &g->rules;
    e.female = g->female;
    e.race = g->race;
    /* The face until there is character creation: the first of each. */
    e.hair = 0;
    e.eyes = 1;
    e.mouth = 1;
    e.hand_to_hand = g->character.skills[BS_SKILL_HAND_TO_HAND];
    e.image = inventory_image;
    e.character = &g->character;
    e.spells = g->have_spells ? g->spells : NULL;
    e.level_number = g->level.info ? bs_level_number(g->level.info) : 1;
    e.now_ms = logic_now(g);
    e.rng = &g->rng;
    e.name = g->name;
    e.font_book = g->have_fonts ? &g->shadow_font : NULL; /* FONT0003 */
    e.book_text = inventory_book;
    e.drink = inventory_drink;
    return e;
}

/* Shows the items lying in the level: placed ones by their objects, dropped ones as copies of their models. */
static void update_item_world(game *g)
{
    for (uint32_t k = 0; k < g->item_object_count; k++) {
        uint32_t ob = g->item_objects[k];
        if (bs_items_on_object(&g->world_items, ob) == UINT32_MAX) {
            bs_level_renderer_show(g->level.renderer, ob, false);
            bs_collision_remove(&g->collision, ob);
        }
    }
    inventory_env e = inventory_env_of(g);
    uint32_t n = 0;
    for (uint32_t i = 0; i < g->world_items.count; i++)
        n += g->world_items.items[i].place.where == BS_ITEM_FLOOR && g->world_items.items[i].object < 0;
    if (n + 1 > g->instance_cap) {
        bs_level_instance *more = realloc(g->instances, sizeof *more * (n + 1));
        if (!more)
            return;
        g->instances = more;
        g->instance_cap = n + 1;
    }
    bs_level_instance *inst = g->instances;
    uint32_t k = 0;
    for (uint32_t i = 0; i < g->world_items.count; i++) {
        const bs_world_item *w = &g->world_items.items[i];
        uint32_t ob = inventory_item_object(&e, i);
        if (w->place.where != BS_ITEM_FLOOR || w->object >= 0 || ob == UINT32_MAX)
            continue;
        static const int32_t flat[3] = {0, 0, 0};
        inst[k].object = ob;
        inst[k].xform = bs_scene_object_xform(flat, w->pos);
        k++;
    }
    g->item_instances = k;
    show_instances(g);
}

/* The dropped items and the weapon in hand (0x48B38) go to the renderer. */
static void show_instances(game *g)
{
    uint32_t n = g->item_instances;
    if (n + 1 > g->instance_cap) {
        bs_level_instance *more = realloc(g->instances, sizeof *more * (n + 1));
        if (!more)
            return;
        g->instances = more;
        g->instance_cap = n + 1;
    }
    fight_env fe = fight_env_of(g);
    bs_scene_xform x;
    int weapon;
    if (g->level.renderer && g->level.extra_first != UINT32_MAX &&
        fight_hand(&g->fight, &fe, &x, &weapon) && g->model_names[INVENTORY_MODELS + weapon][0])
        g->instances[n++] = (bs_level_instance){g->level.extra_first + INVENTORY_MODELS + (uint32_t)weapon, x};
    if (bs_level_renderer_set_instances(g->level.renderer, g->instances, n) != BS_OK)
        BS_WARN("cannot show the dropped items");
}

static void open_inventory(game *g, bs_item_place other)
{
    if (!g->items.tables || !g->inv_background || g->inv.open)
        return;
    inventory_env e = inventory_env_of(g);
    inventory_open(&g->inv, &e, other);
    bs_screen_set_bsi(g->screen, g->inv_background);
    g->had_mouse = SDL_GetWindowRelativeMouseMode(g->window);
    SDL_SetWindowRelativeMouseMode(g->window, false);
}

static void close_inventory(game *g)
{
    inventory_close(&g->inv);
    if (g->had_mouse)
        SDL_SetWindowRelativeMouseMode(g->window, true);
    g->overlay_dirty = true;
    update_item_world(g);
}

/* The dropped item a ray from the eye passes within reach of (its model's radius), or UINT32_MAX. */
static uint32_t dropped_along(game *g, const float dir[3], float reach)
{
    inventory_env e = inventory_env_of(g);
    uint32_t best = UINT32_MAX;
    float best_t = reach;
    for (uint32_t i = 0; i < g->world_items.count; i++) {
        const bs_world_item *w = &g->world_items.items[i];
        uint32_t ob = inventory_item_object(&e, i);
        if (w->place.where != BS_ITEM_FLOOR || w->object >= 0 || ob == UINT32_MAX)
            continue;
        float r = fmaxf(g->model_radius[ob - g->level.extra_first], 16.0f);
        float d[3] = {w->pos[0] - g->cam.pos[0], w->pos[1] - g->cam.pos[1], w->pos[2] - g->cam.pos[2]};
        float t = d[0] * dir[0] + d[1] * dir[1] + d[2] * dir[2];
        float dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2] - t * t;
        if (t > 0 && t < best_t && dd <= r * r) {
            best_t = t;
            best = i;
        }
    }
    return best;
}

/* Uses a door (0x4D287): to its arrival point here if there is one, else into the level file it names. */
static void use_door(game *g, uint32_t ob)
{
    int32_t n = g->door_arrival[ob];
    float at[3];
    if (arrival_point(g, n, at)) {
        memcpy(g->player.pos, at, sizeof at);
        g->player.vy = 0;
        bs_player_eye(&g->player, g->cam.pos);
        return;
    }
    g->go = GO_DOOR;
    snprintf(g->go_file, sizeof g->go_file, "%s.bs6", g->door_name[ob].s);
    g->go_arrival = n;
}

/* Level 1's door panels whose doors have opened (0x3EBB9 with 0x4C5C0): a reversible move or rotation that
 * ran forward, on the panel or what it links to. */
static uint32_t doorpans_open(const game *g)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < g->logic.object_count; i++) {
        const bs_logic_object *o = &g->logic.objects[i];
        if (o->kind != BS_LOGIC_OBJECT)
            continue;
        const bs_bs6_object *lo = &g->level.level.objects[o->index];
        if (lo->model >= g->level.level.model_count ||
            bs_strncasecmp(g->level.level.model_names[lo->model], "1doorpan", 8) != 0)
            continue;
        bool open = false;
        for (uint32_t c = o->first_ctrl; c < o->first_ctrl + o->ctrl_count && !open; c++)
            open = (g->logic.ctrls[c].type == BS_LOGIC_CTRL_MOVE || g->logic.ctrls[c].type == BS_LOGIC_CTRL_ROTATE) &&
                   g->logic.ctrls[c].reversed;
        for (uint32_t l = o->first_link; l < o->first_link + o->link_count && !open; l++) {
            const bs_logic_object *t = &g->logic.objects[g->logic.links[l]];
            for (uint32_t c = t->first_ctrl; c < t->first_ctrl + t->ctrl_count && !open; c++)
                open = (g->logic.ctrls[c].type == BS_LOGIC_CTRL_MOVE ||
                        g->logic.ctrls[c].type == BS_LOGIC_CTRL_ROTATE) &&
                       g->logic.ctrls[c].reversed;
        }
        n += open;
    }
    return n;
}

/* The level's exit (0x40511 → 0x3E578): checked while the player stands on an object. */
static void check_exit(game *g)
{
    const bs_player *p = &g->player;
    if (!p->on_ground || p->floor_object < 0 || (uint32_t)p->floor_object >= g->level.level.object_count)
        return;
    const bs_bs6_object *lo = &g->level.level.objects[p->floor_object];
    float from[3] = {p->pos[0], p->pos[1] - 32.0f, p->pos[2]}, to[3] = {p->pos[0], p->pos[1] + 200.0f, p->pos[2]}, t;
    int32_t ob;
    uint32_t mat = UINT32_MAX;
    bs_collision_segment_material(&g->collision, from, to, &t, &ob, &mat);
    bs_exit_query q = {
        .floor_model = lo->model < g->level.level.model_count ? g->level.level.model_names[lo->model] : "",
        .floor_locked = g->locked && g->locked[p->floor_object],
        .floor_texture = mat < g->level.scene.material_count ? g->level.scene.materials[mat].name : "",
        .doorpans_open = g->script.level_number == 1 ? doorpans_open(g) : 0,
    };
    if (bs_level_script_exit(&g->script, &q))
        g->go = GO_NEXT_LEVEL;
}

/* Completes the level (0x268A1): JUMP.SMK, then the next campaign level by number (7 is skipped). After the
 * last, the title for now. */
static void complete_level(game *g)
{
    int next = bs_next_level_number(g->script.level_number);
    const char *file = "";
    for (size_t i = 0; next && i < g->levels.count && !file[0]; i++)
        if (bs_level_game_type(&g->levels.levels[i]) == BS_GAME_CAMPAIGN &&
            bs_level_number(&g->levels.levels[i]) == next)
            file = g->levels.levels[i].file;
    play_video(g, "VIDEOS/JUMP.SMK", file);
}

/* A transition asked for during the frame. */
static void make_transition(game *g)
{
    int go = g->go;
    g->go = GO_NONE;
    if (go == GO_NEXT_LEVEL)
        complete_level(g);
    else if (go == GO_DOOR)
        enter_level_ex(g, g->go_file, g->go_arrival, true);
}

/* Whether a level object with a model name has moved (the level 3 gate's buttons, 0x3EBB9). */
static bool script_moved(void *ctx, const char *model)
{
    game *g = ctx;
    for (uint32_t i = 0; i < g->logic.object_count; i++) {
        const bs_logic_object *o = &g->logic.objects[i];
        if (o->kind != BS_LOGIC_OBJECT || !o->moved)
            continue;
        const bs_bs6_object *lo = &g->level.level.objects[o->index];
        if (lo->model < g->level.level.model_count &&
            bs_strncasecmp(g->level.level.model_names[lo->model], model, strlen(model)) == 0)
            return true;
    }
    return false;
}

/* The counter (level 1's cogs, level 2's voidguides) shows image n of its sequence (0x3E448). */
static void show_counter(game *g, int n)
{
    const char *const *names;
    uint32_t count = bs_level_script_counter(g->script.level_number, &names);
    if (n < 0 || (uint32_t)n >= count)
        return;
    char image[32];
    snprintf(image, sizeof image, "%s.bsi", names[n]);
    if (!bs_level_renderer_set_texture(g->level.renderer, names[0], bs_assets_image(g->assets, image)))
        BS_WARN("no texture %s to show %s", names[0], image);
}

/*
 * Uses what the view points at within reach (0x4CF0A): its level logic, its
 * message, and an item or a container opens the inventory screen with it:
 * a container's contents, or every item in the item's cell of the level grid.
 */
static void use_object(game *g)
{
    float cp = cosf(g->cam.pitch);
    float fwd[3] = {sinf(g->cam.yaw) * cp, -sinf(g->cam.pitch), cosf(g->cam.yaw) * cp};
    float to[3] = {g->cam.pos[0] + fwd[0] * USE_REACH, g->cam.pos[1] + fwd[1] * USE_REACH,
                   g->cam.pos[2] + fwd[2] * USE_REACH};
    float t;
    int32_t ob = -1;
    bool hit = bs_collision_segment(&g->collision, g->cam.pos, to, &t, &ob);
    uint32_t dropped = dropped_along(g, fwd, hit ? t * USE_REACH : USE_REACH);
    if (dropped != UINT32_MAX) {
        open_inventory(g, g->world_items.items[dropped].place);
        return;
    }
    if (!hit || ob < 0 || (uint32_t)ob >= g->level.level.object_count)
        return;
    /* The level's script first (keys, 0x4D1BB), then the object's own use event unless the script locks it
     * (0x4D1D2). */
    const bs_bs6_object *lo = &g->level.level.objects[ob];
    const char *model = lo->model < g->level.level.model_count ? g->level.level.model_names[lo->model] : "";
    bs_script_result sr = bs_level_script_use(&g->script, &g->world_items, model, g->locked && g->locked[ob],
                                              script_moved, g);
    for (uint32_t k = 0; k < sr.message_count; k++)
        message(g, bs_exe_message(&g->text, sr.messages[k]));
    if (sr.counter >= 0)
        show_counter(g, sr.counter);
    if (sr.activate || !sr.locked)
        activate(g, ob, BS_LOGIC_EVENT_USE);
    /* A gem gives health or spell points, then is gone for a minute (0x4D64C). */
    if (g->gem_percent && g->gem_percent[ob] && logic_now(g) >= g->gem_until[ob]) {
        bs_gem_use(&g->character, g->gem_spell_points[ob], g->gem_percent[ob]);
        g->gem_until[ob] = logic_now(g) + BS_GEM_RECHARGE_MS;
        bs_level_renderer_show(g->level.renderer, (uint32_t)ob, false);
    }
    if (g->object_message && g->object_message[ob] >= 0)
        message_rsc(g, g->object_message[ob]);
    if (!sr.locked && g->door_name && g->door_name[ob].s[0]) {
        use_door(g, (uint32_t)ob);
        return;
    }
    if (bs_items_is_container_object(&g->world_items, (uint32_t)ob)) {
        open_inventory(g, (bs_item_place){BS_ITEM_IN_OBJECT, (uint32_t)ob});
    } else {
        uint32_t i = bs_items_on_object(&g->world_items, (uint32_t)ob);
        if (i != UINT32_MAX)
            open_inventory(g, g->world_items.items[i].place);
    }
}

static void fly(game *g, float dt)
{
    const bool *k = SDL_GetKeyboardState(NULL);
    const controls *c = &g->keys;
    float speed = FLY_SPEED * (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT] ? 4.0f : 1.0f);
    float f = (float)(controls_held(c, ACT_FORWARD) - controls_held(c, ACT_BACK));
    float s = (float)(controls_held(c, ACT_RIGHT) - controls_held(c, ACT_LEFT));
    float u = (float)(controls_held(c, ACT_MOVE_UP) - controls_held(c, ACT_MOVE_DOWN));
    bs_camera_fly(&g->cam, f, s, u, speed * dt);
    /* The feet follow, so walking resumes from here. */
    memcpy(g->player.pos, g->cam.pos, sizeof g->player.pos);
    g->player.pos[1] += g->player.eye;
    g->player.vy = 0;
}

static void walk(game *g, uint32_t dt_ms)
{
    const controls *c = &g->keys;
    if (g->keys.original && !controls_held(c, ACT_SPECIAL))
        g->ducking = false; /* ducking lasts while Special is held */
    bs_player_input in = {
        .forward = (float)(controls_held(c, ACT_FORWARD) - controls_held(c, ACT_BACK)),
        .strafe = (float)(controls_held(c, ACT_RIGHT) - controls_held(c, ACT_LEFT)),
        .jump = controls_held(c, ACT_JUMP),
        .duck = g->ducking || controls_held(c, ACT_DUCK),
        .sneak = g->sneaking,
        /* Spells' effects. */
        .run = (g->character.flags & BS_FLAG_RUNNING) != 0,
        .jumping = (g->character.flags & BS_FLAG_JUMPING) != 0,
        .slow_fall = (g->character.flags & BS_FLAG_SLOW_FALL) != 0,
    };
    /* Haste, Slow and Strength change the attributes as they last. */
    g->stats.speed = bs_character_attribute(&g->character, 6);
    g->stats.strength = bs_character_attribute(&g->character, 0);
    bs_player *p = &g->player;
    p->yaw = g->cam.yaw;
    p->pitch = g->cam.pitch;
    /* A moving floor carries whoever stands on it. */
    uint32_t fo = p->floor_object >= 0 ? bs_level_logic_find(&g->logic, BS_LOGIC_OBJECT, (uint32_t)p->floor_object)
                                       : NO_OBJECT;
    if (fo != NO_OBJECT)
        for (int a = 0; a < 3; a++)
            p->pos[a] += g->logic.objects[fo].delta[a];
    bs_player_events ev;
    float before[3];
    memcpy(before, p->pos, sizeof before);
    bs_player_move(p, &g->stats, &g->collision, &in, dt_ms, &ev);
    /* Sigils (0x5B4B8): the right amulet makes one vanish; otherwise it blocks, and hurts. */
    uint32_t which;
    bool hurt;
    /* Measured from the eye: sigils hang about 190 above the floor, which the 160-unit window only reaches
     * from eye height (inferred: the player object's height is its eye's). */
    float eye[3];
    bs_player_eye(p, eye);
    bs_sigil_event se = bs_sigils_check(g->sigils, g->sigil_count, &g->world_items, eye, ++g->frame, &which, &hurt);
    if (se == BS_SIGIL_OPENED) {
        bs_level_renderer_show(g->level.renderer, g->sigils[which].object, false);
        bs_collision_remove(&g->collision, g->sigils[which].object);
    } else if (se == BS_SIGIL_BLOCKED) {
        p->pos[0] = before[0];
        p->pos[2] = before[2];
        if (hurt)
            g->character.health = g->character.health > BS_SIGIL_DAMAGE ? g->character.health - BS_SIGIL_DAMAGE : 0;
    }
    bs_player_eye(p, g->cam.pos);
    if (ev.jumped)
        bs_level_audio_play_at(&g->level_audio, SOUND_JUMP, g->cam.pos, g->cam.pos, g->cam.yaw);
    /* While a jump charges, the pointer spins where it will land (0x4163D). */
    if (g->level.jump_pointer != UINT32_MAX) {
        float target[3];
        bool charging = bs_player_jump_target(p, target);
        if (charging) {
            int32_t angles[3] = {0, (int32_t)((logic_now(g) / 2) & 0x7FF), 0};
            bs_level_renderer_place(g->level.renderer, g->level.jump_pointer, target, angles);
        }
        bs_level_renderer_show(g->level.renderer, g->level.jump_pointer, charging);
    }
    /* The game's step and bump events: the object stood on or touched changed. */
    if (ev.floor_changed && p->floor_object >= 0)
        activate(g, p->floor_object, BS_LOGIC_EVENT_STEP);
    if (ev.touch_changed && p->touch_object >= 0)
        activate(g, p->touch_object, BS_LOGIC_EVENT_BUMP);
    check_exit(g);
}

static void run_level(game *g, float dt)
{
    uint32_t dt_ms = (uint32_t)(fminf(dt, 0.1f) * 1000.0f);
    /* Keyboard turning and looking. */
    float secs = (float)dt_ms / 1000.0f;
    g->cam.yaw += (float)(controls_held(&g->keys, ACT_TURN_RIGHT) - controls_held(&g->keys, ACT_TURN_LEFT)) *
                  TURN_RATE * secs;
    g->cam.pitch += (float)(controls_held(&g->keys, ACT_LOOK_UP) - controls_held(&g->keys, ACT_LOOK_DOWN)) *
                    LOOK_RATE * secs;
    g->cam.pitch = fmaxf(fminf(g->cam.pitch, 1.55f), -1.55f);
    if (g->fight.player_died) {
        /* Dead: down to the floor (0x1E64F), the ending after 2 s. */
        g->player.eye = fmaxf(16.0f, g->player.eye - 120.0f * secs);
        bs_player_eye(&g->player, g->cam.pos);
    } else if (g->flying) {
        fly(g, (float)dt_ms / 1000.0f);
    } else {
        walk(g, dt_ms);
    }
    /* Used gems come back after a minute (0x2CDA9). */
    for (uint32_t i = 0; g->gem_until && i < g->level.level.object_count; i++)
        if (g->gem_until[i] && logic_now(g) >= g->gem_until[i]) {
            g->gem_until[i] = 0;
            bs_level_renderer_show(g->level.renderer, i, true);
        }
    bs_logic_output out;
    memcpy(g->logic.player, g->player.pos, sizeof g->logic.player);
    bs_level_logic_update(&g->logic, logic_now(g), &out);
    apply_logic(g, &out);
    for (uint32_t i = 0; i < g->logic.object_count; i++) {
        const bs_logic_object *o = &g->logic.objects[i];
        if (o->kind == BS_LOGIC_OBJECT && o->moved) {
            bs_level_renderer_place(g->level.renderer, o->index, o->pos, o->angles);
            bs_collision_place(&g->collision, o->index, o->pos, o->angles);
        }
    }
    /* The monsters and the fighting. */
    fight_env fe = fight_env_of(g);
    bool attack = SDL_GetWindowRelativeMouseMode(g->window) && controls_held(&g->keys, ACT_ATTACK);
    if (fight_update(&g->fight, &fe, attack, dt_ms))
        update_item_world(g);
    if (fight_game_over(&g->fight, logic_now(g))) {
        /* The captivity ending (0x13621): CHOP2, then the title. */
        SDL_SetWindowTitle(g->window, "OpenBattlespire");
        play_video(g, "VIDEOS/CHOP2.SMK", "");
    }
}

/* ---- spells ---- */

/* Whether the overlay shows spell pictures: the casting hand, the effects' icons. */
static bool hud_active(const game *g)
{
    if (g->fight.punching || g->fight.magic.hand_playing || g->fight.magic.ready || g->character.shield > 0)
        return true;
    for (uint32_t i = 0; i < BS_EFFECTS; i++)
        if (g->character.effects[i].spell)
            return true;
    return false;
}

static void book_cast(void *ctx, const bs_spell *sp)
{
    game *g = ctx;
    fight_env fe = fight_env_of(g);
    fight_cast(&g->fight, &fe, sp);
}

static spellbook_env spellbook_env_of(game *g)
{
    spellbook_env e;
    memset(&e, 0, sizeof e);
    e.character = &g->character;
    e.level_number = g->script.level_number;
    e.overlay = g->overlay;
    e.font = g->have_fonts ? &g->font : NULL;
    e.list_font = g->have_fonts ? &g->shadow_font : NULL; /* FONT0003 (0x2992D) */
    e.text = &g->text;
    e.panel = g->spell_panel;
    memcpy(e.icons, g->fight.icons, sizeof e.icons);
    memcpy(e.deliveries, g->deliveries, sizeof e.deliveries);
    memcpy(e.elements, g->elements, sizeof e.elements);
    e.message = fight_message;
    e.cast = book_cast;
    e.ctx = g;
    return e;
}

static void open_spellbook(game *g)
{
    spellbook_env be = spellbook_env_of(g);
    if (g->spellbook.open || !g->spell_panel || !spellbook_open(&g->spellbook, &be))
        return;
    g->had_mouse = SDL_GetWindowRelativeMouseMode(g->window);
    SDL_SetWindowRelativeMouseMode(g->window, false);
    g->overlay_dirty = true;
}

static void close_spellbook(game *g)
{
    g->spellbook.open = false;
    if (g->had_mouse)
        SDL_SetWindowRelativeMouseMode(g->window, true);
    g->overlay_dirty = true;
}

/* The Teleport spell's screen (0x2B1FC): Anchor drops the anchor where the player stands, Teleport goes
 * back to it. */
static void teleport_click(game *g, int x, int y)
{
    bool drop = x >= anchor_button[0] && x <= anchor_button[2] && y >= anchor_button[1] && y <= anchor_button[3];
    bool go = x >= teleport_button[0] && x <= teleport_button[2] && y >= teleport_button[1] &&
              y <= teleport_button[3];
    if (!drop && !go)
        return;
    uint32_t msg = bs_magic_anchor(&g->character, &g->player, drop);
    if (msg)
        message(g, bs_exe_message(&g->text, msg));
    bs_player_eye(&g->player, g->cam.pos);
    g->teleporting = false;
    if (g->had_mouse)
        SDL_SetWindowRelativeMouseMode(g->window, true);
    g->overlay_dirty = true;
}

/* A window point (pixels) in the 640x480 screen, as bs_screen letterboxes it. */
static void window_to_screen(game *g, float wx, float wy, int *x, int *y)
{
    int ww, wh, lw, lh;
    SDL_GetWindowSizeInPixels(g->window, &ww, &wh);
    SDL_GetWindowSize(g->window, &lw, &lh);
    wx *= lw > 0 ? (float)ww / (float)lw : 1.0f;
    wy *= lh > 0 ? (float)wh / (float)lh : 1.0f;
    float sx = (float)ww / 640.0f, sy = (float)wh / 480.0f, sc = sx < sy ? sx : sy;
    *x = (int)((wx - ((float)ww - 640.0f * sc) / 2.0f) / sc);
    *y = (int)((wy - ((float)wh - 480.0f * sc) / 2.0f) / sc);
}

/* A button of the interface bar (0xED838). */
static void bar_click(game *g, statusbar_button b)
{
    switch (b) {
    case STATUSBAR_SPELLBOOK:
        open_spellbook(g);
        break;
    case STATUSBAR_INVENTORY:
        open_inventory(g, (bs_item_place){BS_ITEM_FLOOR, bs_items_cell(g->world_items.grid_cell, g->player.pos)});
        break;
    default:
        break; /* the character sheet, automap, options, hot key list and help aren't there yet */
    }
}

/* ---- events ---- */

static void handle_event(game *g, const SDL_Event *e)
{
    if (e->type == SDL_EVENT_QUIT) {
        leave_state(g);
        g->state = STATE_QUIT;
        return;
    }
    switch (g->state) {
    case STATE_INTRO:
        /* A key press skips the video, as in the original. */
        if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat)
            after_video(g);
        break;
    case STATE_TITLE:
        if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
            if (e->key.key == SDLK_RETURN || e->key.key == SDLK_KP_ENTER)
                press(g, BUTTON_SINGLE);
            else if (e->key.key == SDLK_ESCAPE)
                press(g, BUTTON_EXIT);
        } else if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN && e->button.button == SDL_BUTTON_LEFT) {
            press(g, button_at(g, e->button.x, e->button.y));
        }
        break;
    case STATE_LEVEL:
        if (g->inv.open) {
            int ww, wh;
            SDL_GetWindowSizeInPixels(g->window, &ww, &wh);
            /* Mouse coordinates are in window units; scale them to pixels. */
            SDL_Event pe = *e;
            int lw, lh;
            SDL_GetWindowSize(g->window, &lw, &lh);
            float fx = lw > 0 ? (float)ww / (float)lw : 1.0f, fy = lh > 0 ? (float)wh / (float)lh : 1.0f;
            if (pe.type == SDL_EVENT_MOUSE_MOTION) {
                pe.motion.x *= fx;
                pe.motion.y *= fy;
            } else if (pe.type == SDL_EVENT_MOUSE_BUTTON_DOWN || pe.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                pe.button.x *= fx;
                pe.button.y *= fy;
            }
            inventory_env env = inventory_env_of(g);
            if (controls_pressed(&g->keys, ACT_INVENTORY, e) || !inventory_event(&g->inv, &env, &pe, ww, wh))
                close_inventory(g);
            else if (g->inv.changed) {
                g->inv.changed = false;
                update_item_world(g);
            }
            break;
        }
        if (g->teleporting) {
            if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN && e->button.button == SDL_BUTTON_LEFT) {
                int x, y;
                window_to_screen(g, e->button.x, e->button.y, &x, &y);
                teleport_click(g, x, y);
            } else if (e->type == SDL_EVENT_KEY_DOWN && e->key.key == SDLK_ESCAPE) {
                g->teleporting = false;
                if (g->had_mouse)
                    SDL_SetWindowRelativeMouseMode(g->window, true);
                g->overlay_dirty = true;
            }
            break;
        }
        if (g->spellbook.open) {
            int ww, wh;
            SDL_GetWindowSizeInPixels(g->window, &ww, &wh);
            SDL_Event pe = *e;
            int lw, lh;
            SDL_GetWindowSize(g->window, &lw, &lh);
            float fx = lw > 0 ? (float)ww / (float)lw : 1.0f, fy = lh > 0 ? (float)wh / (float)lh : 1.0f;
            if (pe.type == SDL_EVENT_MOUSE_BUTTON_DOWN || pe.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                pe.button.x *= fx;
                pe.button.y *= fy;
            }
            bool book_event = pe.type == SDL_EVENT_KEY_DOWN || pe.type == SDL_EVENT_MOUSE_WHEEL ||
                              pe.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            if (book_event) {
                spellbook_env be = spellbook_env_of(g);
                if (!spellbook_event(&g->spellbook, &be, &pe, ww, wh))
                    close_spellbook(g);
                g->overlay_dirty = true;
                if (pe.type != SDL_EVENT_KEY_DOWN || pe.key.key == SDLK_ESCAPE || pe.key.key == SDLK_F12 ||
                    (pe.key.key >= SDLK_F1 && pe.key.key <= SDLK_F8))
                    break;
            }
        }
        if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat && e->key.key == SDLK_F12) {
            open_spellbook(g);
            break;
        }
        if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat && e->key.key >= SDLK_F1 && e->key.key <= SDLK_F8) {
            fight_env fe = fight_env_of(g);
            fight_hotkey(&g->fight, &fe, (int)(e->key.key - SDLK_F1));
            break;
        }
        if (controls_pressed(&g->keys, ACT_USE, e) &&
            (e->type != SDL_EVENT_MOUSE_BUTTON_DOWN || SDL_GetWindowRelativeMouseMode(g->window))) {
            fight_env fe = fight_env_of(g);
            if (!fight_use(&g->fight, &fe, USE_REACH))
                use_object(g);
            break;
        }
        if (controls_pressed(&g->keys, ACT_ATTACK, e) && SDL_GetWindowRelativeMouseMode(g->window)) {
            fight_env fe = fight_env_of(g);
            fight_attack_pressed(&g->fight, &fe);
            break;
        }
        if (controls_pressed(&g->keys, ACT_SNEAK, e)) {
            g->sneaking = !g->sneaking;
            break;
        }
        if (controls_pressed(&g->keys, ACT_INVENTORY, e)) {
            /* What lies around the player (0x4E4C1 with no container: the player's cell). */
            open_inventory(g, (bs_item_place){BS_ITEM_FLOOR, bs_items_cell(g->world_items.grid_cell, g->player.pos)});
            break;
        }
        if (controls_pressed(&g->keys, ACT_FLY, e)) {
            g->flying = !g->flying;
            break;
        }
        if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
            if (e->key.key == SDLK_ESCAPE) {
                if (SDL_GetWindowRelativeMouseMode(g->window))
                    SDL_SetWindowRelativeMouseMode(g->window, false);
                else {
                    SDL_SetWindowTitle(g->window, "OpenBattlespire");
                    enter_title(g);
                }
            } else if (e->key.key == SDLK_G) {
                g->settings.classic = !g->settings.classic;
            } else if (e->key.key == SDLK_TAB) {
                g->bar.full_view = !g->bar.full_view;
                g->overlay_dirty = true;
            } else if (e->key.key == SDLK_M && g->audio) {
                static bool paused;
                paused = !paused;
                bs_audio_pause(g->audio, paused);
            }
        } else if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN && !g->spellbook.open) {
            /* A click on the bar presses its button; elsewhere the first click captures the mouse (later
             * clicks use, above). */
            int x, y;
            window_to_screen(g, e->button.x, e->button.y, &x, &y);
            if (statusbar_covers(&g->bar, x, y)) {
                if (e->button.button == SDL_BUTTON_LEFT)
                    bar_click(g, statusbar_at(&g->bar, x, y));
                break;
            }
            SDL_SetWindowRelativeMouseMode(g->window, true);
        } else if (e->type == SDL_EVENT_MOUSE_MOTION && SDL_GetWindowRelativeMouseMode(g->window)) {
            if (g->keys.original && controls_held(&g->keys, ACT_SPECIAL)) {
                /* Special moves: pulling the mouse back ducks (pushing it shoves, not implemented). */
                if (e->motion.yrel > 0)
                    g->ducking = true;
                break;
            }
            if (controls_held(&g->keys, ACT_ATTACK)) {
                /* Swinging: the motion picks the blow, the view holds still (0x41296). */
                fight_mouse(&g->fight, e->motion.xrel, e->motion.yrel);
                break;
            }
            g->cam.yaw += e->motion.xrel * MOUSE_SENSITIVITY;
            g->cam.pitch = fmaxf(fminf(g->cam.pitch - e->motion.yrel * MOUSE_SENSITIVITY, 1.55f), -1.55f);
        }
        break;
    case STATE_QUIT:
        break;
    }
}

/* ---- set-up ---- */

/* The game's messages and item tables from GAME.EXE, the enchantments, TEXT.RSC and the fonts. */
static void load_text_and_items(game *g)
{
    uint8_t *data;
    size_t size;
    bs_le_image img;
    if (bs_vfs_read(&g->vfs, "GAME.EXE", &g->arena, &data, &size) != BS_OK) {
        BS_WARN("cannot read GAME.EXE: no items or messages");
        return;
    }
    const uint8_t *data_exe = data;
    size_t size_exe = size;
    bool loaded = bs_le_load(data, size, &g->arena, &img) == BS_OK;
    if (!loaded || bs_exe_text_read(&img, &g->arena, &g->text) != BS_OK)
        BS_WARN("no messages in GAME.EXE");
    if (bs_item_tables_read(data, size, &g->item_tables) == BS_OK && loaded &&
        bs_item_models_read(&img, &g->item_tables) == BS_OK) {
        static const char *const files[2] = {"MG0_GEN.TXT", "MG2_SPC.TXT"};
        for (int i = 0; i < 2; i++)
            if (bs_assets_read(g->assets, files[i], &g->arena, &data, &size) != BS_OK ||
                bs_magic_parse(data, size, &g->arena, &g->magic[i]) != BS_OK)
                BS_WARN("cannot read %s", files[i]);
        g->items = (bs_item_db){.tables = &g->item_tables, .magic = {&g->magic[0], &g->magic[1]}};
        /* The images worn items show on the character: every BSI name. */
        bs_bsa *bsi;
        if (bs_bsa_open_vfs(&g->vfs, "GAMEDATA/BSI.BSA", &bsi) == BS_OK) {
            g->pictures = BS_ARENA_ARRAY(&g->arena, const char *, bsi->count + 1);
            for (uint32_t i = 0; g->pictures && i < bsi->count; i++) {
                char name[16];
                snprintf(name, sizeof name, "%s", bsi->records[i].name);
                char *dot = strrchr(name, '.');
                if (dot)
                    *dot = '\0';
                g->pictures[g->picture_count++] = bs_arena_strdup(&g->arena, name);
            }
            bs_bsa_close(bsi);
        }
        g->items.pictures = g->pictures;
        g->items.picture_count = g->picture_count;
        g->items.female = g->female;
        g->rules = bs_equip_rules_any();
        g->have_spells = bs_spells_read(data_exe, size_exe, g->spells) == BS_OK;
        fight_init(&g->fight, data_exe, size_exe, g->monsters_on);
        if (!g->have_spells)
            BS_WARN("no spell table in GAME.EXE");
        if (bs_bsa_open_vfs(&g->vfs, "GAMEDATA/TXT.BSA", &g->txt) != BS_OK)
            BS_WARN("cannot open TXT.BSA: no books");
        /* The models items are shown with, and their sizes for the inventory's camera. */
        inventory_models(&g->item_tables, g->model_names);
        if (loaded && bs_item_hand_models_read(&img, &g->item_tables) == BS_OK)
            for (uint32_t i = 0; i <= BS_ITEM_LAST_WEAPON; i++)
                memcpy(g->model_names[INVENTORY_MODELS + i], g->item_tables.hand_models[i], 16);
        else
            BS_WARN("no weapon models in GAME.EXE: no weapon in hand");
        for (uint32_t i = 0; i < SCENE_MODELS; i++) {
            const bs_model *m = bs_assets_model(g->assets, g->model_names[i]);
            g->model_radius[i] = m ? (float)m->radius / 256.0f : 0.0f; /* model units are 1/256 */
        }
        /* The inventory screen's images. */
        g->inv_background = bs_assets_image(g->assets, "char12.bsi");
        for (int i = 0; i < 6; i++) {
            char name[16];
            snprintf(name, sizeof name, "filt%02d.bsi", i);
            g->inv_filters[i] = bs_assets_image(g->assets, name);
        }
        /* The spell book's and the Teleport spell's pictures. */
        g->spell_panel = bs_assets_bsi(g->assets, "spmenu.bsi");
        for (int i = 0; i < BS_DELIVERY_COUNT; i++) {
            char name[16];
            snprintf(name, sizeof name, "dlvr%02d.bsi", i);
            g->deliveries[i] = bs_assets_image(g->assets, name);
            snprintf(name, sizeof name, "elmt%02d.bsi", i);
            g->elements[i] = bs_assets_image(g->assets, name);
        }
        g->teleport_image = bs_assets_image(g->assets, "teleport.bsi");
        /* The interface bar, with the face the inventory shows until there is character creation. */
        statusbar_load(&g->bar, g->assets);
        statusbar_set_face(&g->bar, g->assets, g->female ? 'f' : 'm', g->race, 0, 1, 1);
        if (!g->spell_panel)
            BS_WARN("no spmenu.bsi: no spell book");
        g->inv_arrows[0] = bs_assets_image(g->assets, "point0.bsi");
        g->inv_arrows[1] = bs_assets_image(g->assets, "point1.bsi");
        if (!g->inv_background)
            BS_WARN("no char12.bsi: no inventory screen");
    } else {
        BS_WARN("no item tables in GAME.EXE: no items");
    }
    if (bs_vfs_read(&g->vfs, "GAMEDATA/TEXT.RSC", &g->arena, &data, &size) != BS_OK ||
        bs_rsc_parse(data, size, &g->arena, &g->rsc) != BS_OK)
        BS_WARN("cannot read TEXT.RSC");
    uint8_t *f1, *f3, *f8;
    size_t n1, n3, n8;
    g->have_fonts = bs_vfs_read(&g->vfs, "GAMEDATA/FONT0001.FNT", &g->arena, &f1, &n1) == BS_OK &&
                    bs_vfs_read(&g->vfs, "GAMEDATA/FONT0003.FNT", &g->arena, &f3, &n3) == BS_OK &&
                    bs_vfs_read(&g->vfs, "GAMEDATA/FONT0008.FNT", &g->arena, &f8, &n8) == BS_OK &&
                    bs_font_parse(f1, n1, &g->arena, &g->font) == BS_OK &&
                    bs_font_parse(f3, n3, &g->arena, &g->shadow_font) == BS_OK &&
                    bs_font_parse(f8, n8, &g->arena, &g->info_font) == BS_OK;
    if (!g->have_fonts)
        BS_WARN("cannot read the fonts: no messages");
    g->rng.state = (uint32_t)SDL_GetTicksNS() | 1u;
}

static bool open_data(game *g, const char *dir)
{
    if (bs_install_detect(dir, &g->inst) != BS_OK) {
        fprintf(stderr, "openbattlespire: '%s' is not a Battlespire installation\n", dir);
        return false;
    }
    bs_vfs_init(&g->vfs);
    if (bs_install_mount(&g->inst, &g->vfs, &g->cd) != BS_OK || bs_assets_open(&g->vfs, &g->assets) != BS_OK)
        return false;
    uint8_t *text;
    size_t size;
    if (bs_vfs_read(&g->vfs, "GAMEDATA/LEVELS.TXT", &g->arena, &text, &size) != BS_OK ||
        bs_levels_parse(text, size, &g->arena, &g->levels) != BS_OK)
        BS_WARN("cannot read LEVELS.TXT");
    load_text_and_items(g);
    char path[32];
    snprintf(path, sizeof path, "MUSIC/Track%02d.ogg", TITLE_TRACK);
    if (bs_vfs_read(&g->vfs, path, &g->arena, &g->title_music, &g->title_music_size) != BS_OK) {
        BS_WARN("no title music (%s)", path);
        g->title_music = NULL;
    }
    return true;
}

static void save_screenshot(const char *path, int w, int h)
{
    uint8_t *px = malloc((size_t)w * (size_t)h * 3), *flipped = malloc((size_t)w * (size_t)h * 3);
    if (px && flipped) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
        size_t row = (size_t)w * 3;
        for (int y = 0; y < h; y++)
            memcpy(flipped + (size_t)y * row, px + (size_t)(h - 1 - y) * row, row);
        if (bs_png_write_rgb(path, (uint32_t)w, (uint32_t)h, flipped) != BS_OK)
            fprintf(stderr, "openbattlespire: cannot write %s\n", path);
    }
    free(px);
    free(flipped);
}

static SDL_FunctionPointer gl_proc_any;
static bs_gl_proc gl_loader(const char *name)
{
    gl_proc_any = SDL_GL_GetProcAddress(name);
    return (bs_gl_proc)gl_proc_any;
}

int main(int argc, char **argv)
{
    options o;
    bool help;
    bool parsed = parse_args(argc, argv, &o, &help);
    if (help || !parsed || !o.data_dir) {
        if (!help && !o.data_dir)
            fprintf(stderr, "openbattlespire: no install directory; use -d <dir> or set BS_DATA_DIR\n\n");
        usage(help ? stdout : stderr);
        return help ? 0 : 2;
    }

    static game g;
    bs_arena_init(&g.arena, 0);
    g.female = o.female;
    g.race = o.race ? o.race : "br";
    g.name = o.name ? o.name : "Adventurer";
    bs_character_default(&g.character);
    bs_arena_init(&g.book_arena, 0);
    g.monsters_on = !o.no_monsters;
    if (!open_data(&g, o.data_dir))
        return 1;
    /* Spells known from the start (until there is character creation). */
    for (const char *p = o.spells; p && *p && g.have_spells;) {
        if (strncmp(p, "all", 3) == 0) {
            for (int i = 0; i < BS_SPELL_COUNT - 1; i++) /* not Dispel Sigil: multiplayer's */
                bs_character_learn(&g.character, &g.spells[i]);
            break;
        }
        int n = atoi(p);
        if (n >= 1 && n <= BS_SPELL_COUNT)
            bs_character_learn(&g.character, &g.spells[n - 1]);
        p = strchr(p, ',');
        p = p ? p + 1 : NULL;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "openbattlespire: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (!o.mute && (!SDL_InitSubSystem(SDL_INIT_AUDIO) || bs_audio_open(&g.audio) != BS_OK)) {
        BS_WARN("no audio device: running silent");
        g.audio = NULL;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    g.window = SDL_CreateWindow("OpenBattlespire", o.width, o.height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext ctx = g.window ? SDL_GL_CreateContext(g.window) : NULL;
    const char *missing;
    if (!ctx || !bs_gl_load(gl_loader, &missing) || bs_screen_create(&g.screen) != BS_OK) {
        fprintf(stderr, "openbattlespire: cannot create an OpenGL 3.3 context: %s\n",
                ctx ? missing : SDL_GetError());
        return 1;
    }
    if (bs_overlay_create(&g.overlay) != BS_OK)
        BS_WARN("no overlay: messages won't show");
    SDL_GL_SetSwapInterval(1);

    g.settings = bs_render_settings_default();
    g.cam.fov_y = 53.13f * SDL_PI_F / 180.0f; /* the game's at 640x480 (RE: 0x10F5C) */
    controls_set(&g.keys, o.original_keys);
    bs_level_audio_open(&g.level_audio, g.audio, &g.vfs);
    g.level_audio.fixed_ambience = o.fixed_ambience;

    g.state = STATE_QUIT; /* nothing to leave yet */
    if (o.level)
        enter_level(&g, o.level);
    else if (o.no_intro)
        enter_title(&g);
    else
        enter_intro(&g);

    if (o.inventory && g.state == STATE_LEVEL)
        open_inventory(&g, (bs_item_place){BS_ITEM_FLOOR, bs_items_cell(g.world_items.grid_cell, g.player.pos)});
    uint64_t start = SDL_GetTicksNS(), last = start;
    while (g.state != STATE_QUIT) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            handle_event(&g, &e);
        if (g.state == STATE_LEVEL && g.go != GO_NONE)
            make_transition(&g);
        uint64_t now = SDL_GetTicksNS();
        float dt = (float)(now - last) / 1e9f;
        last = now;
        int w, h;
        SDL_GetWindowSizeInPixels(g.window, &w, &h);

        switch (g.state) {
        case STATE_INTRO: {
            bool done;
            const uint8_t *frame = bs_video_update(g.video, &done);
            if (frame)
                bs_screen_set(g.screen, frame, bs_video_width(g.video), bs_video_height(g.video));
            if (done) {
                if (bs_video_error(g.video) != BS_OK)
                    BS_WARN("video: %s", bs_result_str(bs_video_error(g.video)));
                after_video(&g);
            }
            bs_screen_draw(g.screen, w, h);
            break;
        }
        case STATE_TITLE:
            bs_screen_draw(g.screen, w, h);
            break;
        case STATE_LEVEL:
            bs_level_audio_update(&g.level_audio, g.cam.pos, g.cam.yaw, dt);
            if (g.inv.open && g.overlay) {
                /* The world waits while the inventory is open. */
                inventory_env env = inventory_env_of(&g);
                inventory_draw(&g.inv, &env, w, h, SDL_GetTicks());
                update_messages(&g);
                draw_messages(&g);
                bs_overlay_draw(g.overlay, w, h);
                break;
            }
            run_level(&g, dt);
            if (g.state != STATE_LEVEL)
                break; /* the player died and the ending began */
            if (g.fight.magic.teleport_screen) {
                /* Teleport was cast: its screen, with the cursor (0x2B1FC). */
                g.fight.magic.teleport_screen = false;
                if (g.teleport_image && !g.teleporting) {
                    g.teleporting = true;
                    g.had_mouse = SDL_GetWindowRelativeMouseMode(g.window);
                    SDL_SetWindowRelativeMouseMode(g.window, false);
                }
            }
            fight_draw(&g.fight, g.cam.pos, logic_now(&g), g.level.renderer);
            show_instances(&g);
            bs_level_renderer_draw(g.level.renderer, &g.cam, &g.settings, w, h, (double)(now - start) / 1e9);
            if (g.overlay) {
                update_messages(&g);
                if (g.overlay_dirty || g.hud_moving || hud_active(&g) || g.spellbook.open || g.teleporting ||
                    g.bar.panel) {
                    bs_overlay_clear(g.overlay);
                    fight_env fe = fight_env_of(&g);
                    g.hud_moving = fight_draw_hud(&g.fight, &fe, g.overlay);
                    int32_t yaw = (int32_t)lroundf(g.player.yaw * 2048.0f / (2.0f * 3.14159265f));
                    statusbar_draw(&g.bar, g.overlay, &g.character, yaw);
                    if (g.spellbook.open) {
                        spellbook_env be = spellbook_env_of(&g);
                        spellbook_draw(&g.spellbook, &be);
                    }
                    if (g.teleporting)
                        bs_overlay_image(g.overlay, g.teleport_image, 0, 0);
                    draw_messages(&g);
                    g.overlay_dirty = false;
                }
                bs_overlay_draw(g.overlay, w, h);
            }
            break;
        case STATE_QUIT:
            break;
        }
        if (o.screenshot && now - start > (uint64_t)SCREENSHOT_MS * 1000000) {
            save_screenshot(o.screenshot, w, h);
            leave_state(&g);
            g.state = STATE_QUIT;
        }
        SDL_GL_SwapWindow(g.window);
    }

    bs_level_audio_close(&g.level_audio);
    fight_free(&g.fight);
    free(g.instances);
    bs_bsa_close(g.txt);
    bs_arena_free(&g.book_arena);
    bs_overlay_destroy(g.overlay);
    bs_screen_destroy(g.screen);
    bs_audio_close(g.audio);
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(g.window);
    SDL_Quit();
    bs_assets_close(g.assets);
    bs_cdimage_close(g.cd);
    bs_arena_free(&g.arena);
    return 0;
}
