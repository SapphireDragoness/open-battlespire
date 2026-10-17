/*
 * openbattlespire: the game.
 *
 * So far: the start-up sequence (the intro video, the title menu with its
 * music), Single Player making the character (creation.c) and entering the
 * first level, which can be explored
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
 * from potions (docs/formats/spells.md). Load Saved Game (or --load) carries on
 * from one of the original game's saves (docs/formats/save.md, load_game).
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
#include "bs/formats/save.h"
#include "bs/formats/spells.h"
#include "bs/formats/wav.h"
#include "bs/game/collision.h"
#include "bs/game/dialogue.h"
#include "bs/game/game_save.h"
#include "bs/game/items.h"
#include "bs/game/level_logic.h"
#include "bs/game/level_script.h"
#include "bs/game/levelling.h"
#include "bs/game/player.h"
#include "bs/game/use.h"
#include "bs/render/gl.h"
#include "bs/render/level_renderer.h"
#include "bs/render/level_view.h"
#include "bs/render/overlay.h"
#include "bs/render/screen.h"
#include "controls.h"
#include "creation.h"
#include "fight.h"
#include "inventory.h"
#include "loadsave.h"
#include "spellbook.h"
#include "talk.h"
#include "menus.h"
#include "statusbar.h"

#define MOUSE_SENSITIVITY 0.0025f
/* Keyboard turning and looking (the original's turn rate is an option; these are guesses). */
#define TURN_RATE 3.0f       /* radians per second */
#define LOOK_RATE 1.5f
#define FLY_SPEED 400.0f     /* level units per second, flying */
#define SOUND_JUMP 401       /* 400 for a female character (0x40BAB) */
#define SOUND_SPLASH 86      /* falling into water (0x3FC5C) */
#define BOAT_TURN_RATE (TURN_RATE / 2)  /* the strafe keys turn a boat at half the turning keys' rate (0x3F209) */
#define MSG_SLOW_FALL 13     /* "Slowfalling active." */
#define MSG_TOO_HEAVY 14     /* "You carry too much stuff to stay afloat!" */
/* The balloon (0x21A1x): 20 s after it was left, or stood on, it rises at 150 units/s to 3000 above the player,
 * comes back above where it began and sinks there. */
#define BALLOON_RETURN_MS 20000
#define BALLOON_RETURN_SPEED 150.0f
#define BALLOON_RETURN_HEIGHT 3000.0f
#define BOAT_ROCK_MS 2000    /* boats rock 10 either way (0x4DAA1) */
/* Under water (0x110A8): haze.666 from 8 to 1024 in place of the level's, scaled as "C" levels' (level_view.c). */
#define UNDERWATER_FOG_START 24.0f
#define UNDERWATER_FOG_END 3072.0f
#define TITLE_TRACK 6        /* RE: 0x31B50 */
#define TITLE_IMAGE "mult01.bsi"
#define SCREENSHOT_MS 3000
/* How far away an object or item can be used (0x4D0E0, 0x4D16C). */
#define USE_REACH 192.0f
#define NO_OBJECT UINT32_MAX
/* Talking (0x4D005, 0x4D5A4): monsters within 384; NPC flats within 192 (the game's flat test isn't
 * decoded). */
#define TALK_REACH_MONSTER 384.0f
#define TALK_REACH_FLAT 192.0f
/* The item models, then the weapons in hand and the arrow in flight (FIGHT_MODEL_ARROW). */
#define SCENE_MODELS (INVENTORY_MODELS + FIGHT_MODEL_ARROW + 1)
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

typedef enum state { STATE_INTRO, STATE_TITLE, STATE_CREATION, STATE_LEVEL, STATE_QUIT } state;

typedef struct options {
    const char *data_dir;
    const char *level;   /* start in this level, skipping intro and title */
    bool no_intro;
    bool mute;
    bool fixed_ambience;
    bool original_keys;
    int width, height;
    bool female;             /* the character for --level (Single Player makes one) */
    const char *race;
    const char *name;
    const char *screenshot;  /* write the screen after SCREENSHOT_MS and quit (for testing) */
    bool inventory;          /* open the inventory screen at the start (for testing) */
    int level_up;            /* open the level-up screen with that level's points at the start (testing) */
    bool no_monsters;
    const char *spells;      /* spells known at the start: numbers, or "all" (for testing) */
    const char *give;        /* items carried at the start: base item numbers (for testing) */
    const char *load;        /* a saved game to start from: an original's slot 0-9, a save folder or file */
    const char *saves;       /* our saves' folder (default: the user's data folder) */
} options;

typedef struct door_name_t {
    char s[16];
} door_name_t;

/* A level object's or flat's dialogue: its code (STRU 108) and its dialogue link (STRU 104, -1 none). */
typedef struct talk_info {
    char code[4];
    int32_t link;
} talk_info;

/* Who talks: a monster, a level object or a flat. */
typedef enum speaker_kind { SPEAKER_MONSTER, SPEAKER_OBJECT, SPEAKER_FLAT } speaker_kind;

#define LEAVING_MAX 8                 /* speakers leaving after a while (TELEPORT n, 0x5A41C) */

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
    uint64_t level_start_ns;     /* the logic clock's zero ... */
    uint32_t clock_base;         /* ... where it starts: 0, or a loaded game's clock */
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
    bs_level_instance *instances;    /* the dropped items' copies, then the weapon in hand and arrows */
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
    bool *lockable;              /* per level object: had property 103 when the level was made */
    uint32_t *gem_until;         /* per level object: hidden until (logic clock ms) */
    /* Level transitions. */
    char after_video[32];        /* the level a video leads to ("" for the title) */
    int32_t level_up_points;     /* the level done: the level-up screen comes up with these in the next */
    float moved_from[3];         /* where the player last moved 128 units from (0x3F7BD0) */
    bool moved;                  /* since the last 5 s tick (0x3F7C28) */
    uint32_t tick_clock;         /* the clock at the last frame, for the 5 s tick */
    /* Boats and the balloon (0x16C064, 0x16C214). */
    struct ride {
        uint32_t object;
        bs_player_ride kind;
        float pos[3];
        int32_t angles[3];
        float yaw;               /* a boat's yaw in game units, kept fractional while it is steered */
        float home[3];           /* the balloon goes back here (0x16C290) */
        uint32_t left_at;        /* when the balloon was last left or stood on (+0x2BC) */
    } *rides;
    uint32_t ride_count;
    int32_t riding;              /* index into rides, or -1 */
    int32_t breath;              /* the breath bar's height (0x1E5E8), 0 none */
    bool underwater;             /* the view is under the water's surface: its haze */
    float above_fog[5];          /* the fog's start, end and colour above water */
    float under_fog[3];          /* its colour under it */
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
    char name_buf[32];           /* the name typed in */
    int hair, eyes, mouth;       /* the face's pictures (player record +0x23E ..) */
    creation creation;           /* Single Player's screens */
    const char *creation_image;  /* the background shown */
    const char *armour_names[4];
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
    bs_font fonts[MENU_FONTS];   /* FONT0001 .. FONT0010 by number, for the screens */
    bool have_font[MENU_FONTS];
    bool cursor_held;            /* Enter held: the cursor shows to click the bar (the "+" cursor) */
    menus menus;                 /* the bar's screens: help, character sheet, automap, options */
    bs_automap_table maps;
    /* Conversations (talk.c). */
    talk talk;
    speaker_kind talk_kind;
    bs_dialogue_state dialogue;  /* the variables and the visits: the whole game's */
    bs_bsa *heads;               /* FLC.BSA: the talking heads */
    bs_bsa *waves;               /* WAVES.BSA (the CD): the voices; NULL none */
    bool waves_tried;
    bs_arena voice_arena;
    bs_voice voice;
    bool voice_on;
    talk_info *object_talk;      /* per level object */
    talk_info *flat_talk;        /* per flat */
    struct {
        speaker_kind kind;
        uint32_t index;
        uint32_t at;             /* logic clock */
    } leaving[LEAVING_MAX];
    uint32_t leaving_count;
    uint32_t captive_at;         /* the "ZZ" conversation's captivity, 3 s after (0x5817E); 0 none */
    /* Saved games (loadsave.c). */
    loadsave loadsave;
    char saves_dir[BS_PATH_MAX];  /* ours; "" until needed */
    uint8_t save_picture[BS_SAVE_IMAGE_SIZE];
    bool picture_pending;         /* Save was chosen: the next frame drawn is the save's picture */
    uint8_t seen[BS_AUTOMAP_MAPS][AUTOMAP_COLS * AUTOMAP_ROWS];   /* the automaps' cells seen */
    const char *skill_names[BS_SKILL_COUNT], *attribute_names[8];
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
            "  --level-up N        open the level-up screen with level N's points at the start (testing)\n"
            "  --no-monsters       no monsters (their loot lies where they would have been)\n"
            "  --female            with --level: a female character (default male)\n"
            "  --name NAME         with --level: the character's name (default Adventurer)\n"
            "  --race RACE         with --level: the character's race: rd br no hi dk wd (Redguard,\n"
            "                      Breton, Nord, High Elf, Dark Elf, Wood Elf; default br)\n"
            "  --keys LAYOUT       modern (default) or original (the game's own defaults)\n"
            "  --spells LIST       with --level: spells known at the start: numbers, or all\n"
            "  --load SAVE         start from a saved game: the original game's slot 0-9 (the install's\n"
            "                      SAVE0-SAVE9), an original save folder, or one of ours (GAME.BSS)\n"
            "  --saves DIR         where our saved games go (default: the user's data folder)\n"
            "  --give LIST         with --level: items carried, by base item number, e.g. 9,22,2,24\n"
            "                      for a long bow, arrows, a long sword and a helmet: the first weapon\n"
            "                      is wielded, the next is the secondary, the rest worn (for testing)\n"
            "\n"
            "Any key skips the intro; on the title, Enter = Single Player, Esc = Exit.\n"
            "Single Player makes the character: New Game, then the name, sex, race, face and\n"
            "class (an existing one or a new one), changed in the CharacterMaker; Done plays.\n"
            "In a level, click to capture the mouse; the mouse looks around and the left button\n"
            "uses what's under the crosshair; hold the right button and move the mouse to swing\n"
            "your weapon (down chops, across slashes, up thrusts); with a bow, press it to shoot.\n"
            "The switch key swaps the weapon in hand for the secondary one. Hold the jump key to\n"
            "charge a jump (the pointer shows where you'll land), release to jump. Sneak toggles\n"
            "slow movement.\n");
    fprintf(out,
            "F12 opens the spell book: pick a spell, click its icon, delivery, element and\n"
            "detonation to change them, press F1-F8 to give it a hot key, Cast to cast it.\n"
            "In play F1-F8 cast; a touch spell goes with the next use of a monster, a ranged one\n"
            "with the right button.\n"
            "Use a monster within reach, or an NPC, to talk: click a reply; Exit when it's over.\n"
            "Using an item or container opens the inventory: drag items between the lists,\n"
            "double-click a container to open it (its box to go back), drop items on the\n"
            "character to equip them, drag them off to take them off, double-click a book to\n"
            "read it or a potion to drink it; Esc or Exit to close.\n"
            "The bar at the bottom shows health (blue), spell points (yellow) and the compass.\n"
            "Hold Enter for the cursor and click it: the face opens the character sheet (F9),\n"
            "the books the spell book and the automap (F11), the chest the inventory, the gears\n"
            "the options (Esc), the compass lists the hot keys, ? shows help. Tab hides the bar.\n"
            "The options' Save picks a slot and takes a name; Load shows your saves, Page Down\n"
            "the original game's.\n"
            "In water, up/down rise and dive; the bar at the right is your breath. Use a boat or\n"
            "the balloon to get in; the sideways keys turn a boat, up/down move the balloon, and\n"
            "the leave key gets you out.\n"
            "  modern:   W A S D move, Space jump, C duck, Ctrl sneak, E use, I or F10 inventory,\n"
            "            Q switch weapon, PgUp/PgDn (R/V) up/down, X leave a boat\n"
            "  original: arrows move, Alt jump, Delete/End turn, Insert/Home look, S sneak,\n"
            "            Space + mouse back duck, A switch weapon, PgUp/PgDn up/down, F10 inventory,\n"
            "            D leave a boat\n"
            "  both:     F fly (Shift faster), G classic/enhanced rendering, M sound, Esc options\n"
            "            (End Game goes back to the title)\n");
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
        else if (strcmp(a, "--level-up") == 0 && has)
            o->level_up = atoi(argv[++i]);
        else if (strcmp(a, "--no-monsters") == 0)
            o->no_monsters = true;
        else if (strcmp(a, "--spells") == 0 && has)
            o->spells = argv[++i];
        else if (strcmp(a, "--give") == 0 && has)
            o->give = argv[++i];
        else if (strcmp(a, "--load") == 0 && has)
            o->load = argv[++i];
        else if (strcmp(a, "--saves") == 0 && has)
            o->saves = argv[++i];
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

static talk_env talk_env_of(game *g);
static void start_talk(game *g, speaker_kind kind, uint32_t i);
static void update_leaving(game *g);

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
    case STATE_CREATION:
        if (g->audio)
            bs_audio_stop_music(g->audio);
        SDL_StopTextInput(g->window);
        bs_level_view_unload(&g->level); /* the shops' item models */
        break;
    case STATE_LEVEL:
        if (g->talk.open) {
            talk_env env = talk_env_of(g);
            talk_close(&g->talk, &env);
        }
        inventory_close(&g->inv);
        g->loadsave.open = false;
        g->picture_pending = false;
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
static const bs_automap_entry *current_map(const game *g, int *index);
static fight_env fight_env_of(game *g);
static void show_instances(game *g);
static void open_menu(game *g, menu_kind k);
static void setup_rides(game *g);

/* After a video: the level it leads to, or the title. */
static void after_video(game *g)
{
    char next[32];
    snprintf(next, sizeof next, "%s", g->after_video);
    g->after_video[0] = '\0';
    int32_t points = g->level_up_points;
    g->level_up_points = 0;
    if (next[0]) {
        if (enter_level(g, next) && points) {
            open_menu(g, MENU_SHEET);
            menus_level_up(&g->menus, &g->character, points);
        }
    } else {
        enter_title(g);
    }
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
    if (through_door) {
        cache_level(g);
    } else {
        clear_level_cache(g);
        memset(g->seen, 0, sizeof g->seen); /* the automaps start blank */
    }
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
        bs_items_forget_held(&g->world_items); /* the monsters are made again */
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
    g->lockable = BS_ARENA_ARRAY(&g->level.arena, bool, nobj + 1);
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
            if (g->lockable)
                g->lockable[i] = g->locked[i];
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
    /* Who has a dialogue code (STRU 108, the conversation map 0xFE684) or a dialogue link (STRU 104,
     * 0x100BE4). Monsters have theirs from their spawn points. */
    g->object_talk = BS_ARENA_ARRAY(&g->level.arena, talk_info, nobj + 1);
    g->flat_talk = BS_ARENA_ARRAY(&g->level.arena, talk_info, g->level.level.flat_count + 1);
    for (int kind = 0; kind < 2 && g->object_talk && g->flat_talk; kind++) {
        uint32_t n = kind ? g->level.level.flat_count : nobj;
        for (uint32_t i = 0; i < n; i++) {
            talk_info *ti = kind ? &g->flat_talk[i] : &g->object_talk[i];
            ti->code[0] = '\0';
            ti->link = -1;
            bs_bs6_logic lg;
            bs_arena_mark mark = bs_arena_get_mark(&g->level.arena);
            const bs_bs6_chunk *ch = kind ? &g->level.level.flats[i].chunk : &g->level.level.objects[i].chunk;
            if (bs_bs6_entity_logic(ch, &g->level.arena, &lg) == BS_OK) {
                const bs_bs6_record *code = bs_bs6_logic_prop(&lg, 108), *link = bs_bs6_logic_prop(&lg, 104);
                if (code && code->size >= 3) {
                    memcpy(ti->code, code->data, 3);
                    ti->code[3] = '\0';
                }
                if (link)
                    ti->link = bs_bs6_record_i32(link, 0);
            }
            bs_arena_rewind(&g->level.arena, mark);
        }
    }
    g->leaving_count = 0;
    talk_close(&g->talk, NULL);
    memset(g->messages, 0, sizeof g->messages);
    g->overlay_dirty = true;

    g->level_start_ns = SDL_GetTicksNS();
    g->clock_base = 0;
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
    g->tick_clock = 0;
    setup_rides(g);
    g->stats = (bs_player_stats){.speed = g->character.attributes[6], .strength = g->character.attributes[0],
                                 .jump_skill = g->character.skills[BS_SKILL_JUMPING], .deathmatch = multiplayer};
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

static creation_env creation_env_of(game *g);

/* Single Player: the character is made first (creation.c); the title's music goes on. */
static void enter_creation(game *g)
{
    g->state = STATE_CREATION;
    g->creation_image = NULL;
    creation_env env = creation_env_of(g);
    creation_start(&g->creation, &env);
    SDL_StartTextInput(g->window);
}

static void press(game *g, title_button b)
{
    switch (b) {
    case BUTTON_SINGLE:
        enter_creation(g);
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
    return g->clock_base + (uint32_t)((SDL_GetTicksNS() - g->level_start_ns) / 1000000);
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
        .water = g->level.water,
        .water_y = g->level.water_y,
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

/* A flat's use event (0x4D5A4: an NPC is used before it talks). */
static void activate_flat(game *g, uint32_t flat)
{
    uint32_t o = bs_level_logic_find(&g->logic, BS_LOGIC_FLAT, flat);
    if (o == NO_OBJECT)
        return;
    bs_logic_output out;
    memcpy(g->logic.player, g->player.pos, sizeof g->logic.player);
    bs_level_logic_activate(&g->logic, o, BS_LOGIC_EVENT_USE, logic_now(g), &out);
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
    float floor_y = g->player.pos[1];
    if (g->state == STATE_LEVEL && bs_collision_segment(&g->collision, from, to, &t, &ob))
        floor_y = from[1] + t * (to[1] - from[1]); /* (no level while the character is made) */
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
    e.hair = g->hair;
    e.eyes = g->eyes;
    e.mouth = g->mouth;
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
        inst[k].front = false;
        k++;
    }
    g->item_instances = k;
    show_instances(g);
}

/* The dropped items, the weapon in hand (0x48B38) and the arrows go to the renderer. */
static void show_instances(game *g)
{
    uint32_t n = g->item_instances;
    if (n + FIGHT_MODELS > g->instance_cap) {
        bs_level_instance *more = realloc(g->instances, sizeof *more * (n + FIGHT_MODELS));
        if (!more)
            return;
        g->instances = more;
        g->instance_cap = n + FIGHT_MODELS;
    }
    fight_env fe = fight_env_of(g);
    bs_scene_xform x[FIGHT_MODELS];
    int model[FIGHT_MODELS];
    uint32_t in_hand = 0;
    uint32_t count = g->level.renderer && g->level.extra_first != UINT32_MAX
                         ? fight_models(&g->fight, &fe, x, model, &in_hand)
                         : 0;
    for (uint32_t i = 0; i < count; i++)
        if (g->model_names[INVENTORY_MODELS + model[i]][0])
            g->instances[n++] =
                (bs_level_instance){g->level.extra_first + INVENTORY_MODELS + (uint32_t)model[i], x[i], i < in_hand};
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
    g->had_mouse = SDL_GetWindowRelativeMouseMode(g->window) || g->cursor_held;
    g->cursor_held = false;
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

/* Completes the level (0x268A1): JUMP.SMK, then the next campaign level by number (7 is skipped) with the
 * level-up screen and the level's points. After the last, the title for now. */
static void complete_level(game *g)
{
    int next = bs_next_level_number(g->script.level_number);
    const char *file = "";
    for (size_t i = 0; next && i < g->levels.count && !file[0]; i++)
        if (bs_level_game_type(&g->levels.levels[i]) == BS_GAME_CAMPAIGN &&
            bs_level_number(&g->levels.levels[i]) == next)
            file = g->levels.levels[i].file;
    g->level_up_points = bs_level_up_points(g->script.level_number);
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

/* ---- boats and the balloon ---- */

/* What can be ridden (0x23DD5, 0x240CC): boat.3d and boat2.3d (in single player), and the balloon. */
static bs_player_ride ride_kind(const char *model)
{
    if (!bs_strcasecmp(model, "boat") || !bs_strcasecmp(model, "boat2"))
        return BS_RIDE_BOAT;
    if (!bs_strcasecmp(model, "l8balon1"))
        return BS_RIDE_BALLOON;
    return BS_RIDE_NONE;
}

/* A level begins: its boats and balloon where the level has them, the fog under water. */
static void setup_rides(game *g)
{
    const bs_bs6_level *l = &g->level.level;
    g->ride_count = 0;
    g->riding = -1;
    g->breath = 0;
    g->underwater = false;
    g->above_fog[0] = g->settings.fog_start;
    g->above_fog[1] = g->settings.fog_end;
    memcpy(g->above_fog + 2, g->settings.fog_color, sizeof g->settings.fog_color);
    if (!bs_level_view_haze_color(&g->level, &g->vfs, "666", g->under_fog)) {
        /* The fog colour the game sets under water, RGB555 0x109A. */
        g->under_fog[0] = g->under_fog[1] = 4.0f / 31.0f;
        g->under_fog[2] = 26.0f / 31.0f;
    }
    g->rides = BS_ARENA_ARRAY(&g->level.arena, struct ride, l->object_count + 1);
    for (uint32_t i = 0; g->rides && i < l->object_count; i++) {
        const bs_bs6_object *lo = &l->objects[i];
        bs_player_ride kind = ride_kind(lo->model < l->model_count ? l->model_names[lo->model] : "");
        if (kind == BS_RIDE_NONE)
            continue;
        struct ride *r = &g->rides[g->ride_count++];
        memset(r, 0, sizeof *r);
        r->object = i;
        r->kind = kind;
        for (int k = 0; k < 3; k++) {
            r->pos[k] = r->home[k] = (float)lo->pos[k];
            r->angles[k] = lo->angles[k];
        }
        r->yaw = (float)lo->angles[1];
    }
}

static int32_t ride_of(const game *g, uint32_t object)
{
    for (uint32_t k = 0; k < g->ride_count; k++)
        if (g->rides[k].object == object)
            return (int32_t)k;
    return -1;
}

/* Taking a boat or the balloon (0x4D84D, 0x4D984): the player goes where it is, facing a boat's way; it leaves
 * the level and goes with the player. */
static void board(game *g, int32_t k)
{
    struct ride *r = &g->rides[k];
    g->riding = k;
    memcpy(g->player.pos, r->pos, sizeof g->player.pos);
    if (r->kind == BS_RIDE_BOAT)
        g->cam.yaw = r->yaw * 2.0f * SDL_PI_F / 2048.0f;
    g->player.vy = 0;
    g->player.jumping = g->player.charging = g->player.swimming = false;
    bs_collision_remove(&g->collision, r->object);
    bs_player_eye(&g->player, g->cam.pos);
}

/* Leaving it (0x4D907, 0x4DA13): it stays where it is, and the balloon's time to go back starts. */
static void leave_ride(game *g)
{
    if (g->riding < 0)
        return;
    struct ride *r = &g->rides[g->riding];
    g->riding = -1;
    bs_collision_restore(&g->collision, r->object);
    bs_collision_place(&g->collision, r->object, r->pos, r->angles);
    if (r->kind == BS_RIDE_BALLOON)
        r->left_at = logic_now(g);
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
    /* An object with a dialogue code talks once used (0x4D153): level 2's gems. */
    if (g->object_talk && g->object_talk[ob].code[0])
        start_talk(g, SPEAKER_OBJECT, (uint32_t)ob);
    if (!sr.locked && g->door_name && g->door_name[ob].s[0]) {
        use_door(g, (uint32_t)ob);
        return;
    }
    /* A boat or the balloon is taken, or the one ridden left (0x4D1F9, 0x4D236). */
    int32_t rk = ride_of(g, (uint32_t)ob);
    if (rk >= 0) {
        if (g->riding >= 0)
            leave_ride(g);
        else
            board(g, rk);
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

/* Carrying more than 3 x Strength in half units: no staying afloat (0x5493B). */
static bool carrying_too_much(game *g)
{
    return g->items.tables &&
           bs_items_carried_weight(&g->items, &g->world_items) > 3 * bs_character_attribute(&g->character, 0);
}

static void walk(game *g, uint32_t dt_ms)
{
    const controls *c = &g->keys;
    if (g->keys.original && !controls_held(c, ACT_SPECIAL))
        g->ducking = false; /* ducking lasts while Special is held */
    struct ride *rd = g->riding >= 0 ? &g->rides[g->riding] : NULL;
    float strafe = (float)(controls_held(c, ACT_RIGHT) - controls_held(c, ACT_LEFT)), turn = 0;
    if (rd && rd->kind == BS_RIDE_BOAT && strafe != 0) {
        /* The strafe keys turn a boat, and the view with it (0x3F209). */
        turn = strafe * BOAT_TURN_RATE * (float)dt_ms / 1000.0f;
        g->cam.yaw += turn;
        rd->yaw -= turn * 2048.0f / (2.0f * SDL_PI_F);
    }
    bool heavy = g->player.swimming && carrying_too_much(g);
    bs_player_input in = {
        .forward = (float)(controls_held(c, ACT_FORWARD) - controls_held(c, ACT_BACK)),
        .strafe = strafe,
        .up = controls_held(c, ACT_MOVE_UP),
        .down = controls_held(c, ACT_MOVE_DOWN),
        .water = g->level.water,
        .water_y = g->level.water_y,
        .dive_max = g->script.level_number == 5 ? 256.0f : 0.0f, /* 0x3FF36 */
        .heavy = heavy,
        .ride = rd ? rd->kind : BS_RIDE_NONE,
        /* A boat goes along its model's heading, the mirror of its yaw (0x3F6A6). */
        .ride_heading = rd ? (2047.0f - rd->yaw) * 2.0f * SDL_PI_F / 2048.0f : 0.0f,
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
    g->stats.jump_skill = g->character.skills[BS_SKILL_JUMPING];
    g->stats.swim_skill = g->character.skills[4]; /* Swimming */
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
    if (ev.ride_blocked && turn != 0) {
        /* A boat that touched something doesn't turn either (0x40397). */
        g->cam.yaw -= turn;
        rd->yaw += turn * 2048.0f / (2.0f * SDL_PI_F);
    }
    if (ev.splashed) {
        bs_level_audio_play_at(&g->level_audio, SOUND_SPLASH, g->cam.pos, g->cam.pos, g->cam.yaw);
        if (carrying_too_much(g))
            message(g, bs_exe_message(&g->text, MSG_TOO_HEAVY));
    }
    if (ev.slow_fall)
        message(g, bs_exe_message(&g->text, MSG_SLOW_FALL));
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
    if (ev.jumped) {
        bs_level_audio_play_at(&g->level_audio, SOUND_JUMP, g->cam.pos, g->cam.pos, g->cam.yaw);
        bs_skill_used(&g->character, BS_SKILL_JUMPING, 1); /* (0x40B96) */
    }
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

/* Boats rock, and one ridden follows the player (0x4DAA1, 0x4DB75); so does the balloon ridden, which left
 * alone goes back to where it began (0x21A1x). */
static void update_rides(game *g, uint32_t dt_ms)
{
    uint32_t now = logic_now(g);
    int32_t t = (int32_t)(now % BOAT_ROCK_MS / 50);
    if (t > 20)
        t = 40 - t;
    int32_t rock = (t - 10) & 0x7FF;
    float dt = (float)dt_ms / 1000.0f;
    for (uint32_t k = 0; k < g->ride_count; k++) {
        struct ride *r = &g->rides[k];
        bool ridden = (int32_t)k == g->riding;
        if (ridden)
            memcpy(r->pos, g->player.pos, sizeof r->pos);
        if (r->kind == BS_RIDE_BOAT) {
            r->angles[0] = rock;
            r->angles[1] = (int32_t)lroundf(r->yaw) & 0x7FF;
        } else if (!ridden) {
            if (g->player.floor_object == (int32_t)r->object) {
                r->left_at = now;
            } else if ((int32_t)(now - r->left_at) >= BALLOON_RETURN_MS) {
                if (r->pos[0] == r->home[0] && r->pos[2] == r->home[2]) {
                    if (r->pos[1] - r->home[1] <= -30.0f)
                        r->pos[1] += BALLOON_RETURN_SPEED * dt;
                    else
                        memcpy(r->pos, r->home, sizeof r->pos);
                } else if (r->pos[1] - g->player.pos[1] >= -BALLOON_RETURN_HEIGHT) {
                    r->pos[1] -= BALLOON_RETURN_SPEED * dt;
                } else {
                    memcpy(r->pos, r->home, sizeof r->pos);
                    r->pos[1] -= BALLOON_RETURN_HEIGHT;
                }
            }
        }
        bs_level_renderer_place(g->level.renderer, r->object, r->pos, r->angles);
        if (!ridden)
            bs_collision_place(&g->collision, r->object, r->pos, r->angles);
    }
}

/* Breath (0x1E5E8): above the water it's kept full; under it, after Endurance seconds the player drowns. The
 * bar at the right shows it (green, red under 10), and while Water Breathing lasts (it keeps the breath full)
 * above the water too. Then the view under the water (0x110A8). */
static void breathe(game *g)
{
    int32_t bar = 0;
    if (g->level.water && !g->fight.player_died) {
        uint32_t now = logic_now(g), *since = &g->character.water_breathing_until;
        if (g->cam.pos[1] < g->level.water_y && *since <= now) {
            *since = now;
        } else {
            uint32_t elapsed = now > *since ? now - *since : 0;
            int64_t rem = ((int64_t)bs_character_attribute(&g->character, 4) * 1000 - elapsed) / 500;
            if (rem <= 0)
                g->fight.player_died = now ? now : 1; /* drowned (0x16C258) */
            else if (rem <= 500)
                bar = (int32_t)rem;
        }
    }
    if (bar != g->breath) {
        g->breath = bar;
        g->overlay_dirty = true;
    }
    bool under = g->level.water && g->cam.pos[1] > g->level.water_y;
    if (under != g->underwater) {
        g->underwater = under;
        g->settings.fog_start = under ? UNDERWATER_FOG_START : g->above_fog[0];
        g->settings.fog_end = under ? UNDERWATER_FOG_END : g->above_fog[1];
        memcpy(g->settings.fog_color, under ? g->under_fog : g->above_fog + 2, sizeof g->settings.fog_color);
    }
}

/* Skills rising with use (0x1E3DF), and every 5 s (0x40C9A) Stealth and Swimming counting and regeneration. The
 * 5 s tick comes when the clock enters the first second of each 5 (0x13213). */
#define MOVED_DISTANCE 128.0f        /* moving this far counts as moving (0x4174C) */
static void level_skills(game *g)
{
    uint32_t now = logic_now(g);
    if (!g->tick_clock) { /* a level or a save begins */
        g->tick_clock = now ? now : 1;
        memcpy(g->moved_from, g->player.pos, sizeof g->moved_from);
        g->moved = false;
        return;
    }
    if (now % 5000 < 1000 && g->tick_clock % 5000 >= 1000) {
        bs_character_five_seconds(&g->character, g->sneaking, g->player.swimming, g->moved);
        g->moved = false;
    }
    g->tick_clock = now;
    float dx = g->player.pos[0] - g->moved_from[0], dz = g->player.pos[2] - g->moved_from[2];
    if (dx * dx + dz * dz >= MOVED_DISTANCE * MOVED_DISTANCE) {
        g->moved = true;
        memcpy(g->moved_from, g->player.pos, sizeof g->moved_from);
    }
    int rose[BS_SKILL_COUNT];
    int n = bs_skills_rise(&g->character, rose);
    for (int i = 0; i < n; i++) {
        const char *fmt = bs_exe_message(&g->text, BS_MSG_SKILL_INCREASED), *p = strstr(fmt, "%s");
        const char *name = g->skill_names[rose[i]] ? g->skill_names[rose[i]] : "";
        char s[160];
        if (p)
            snprintf(s, sizeof s, "%.*s%s%s", (int)(p - fmt), fmt, name, p + 2);
        else
            snprintf(s, sizeof s, "%s", fmt);
        message(g, s);
    }
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
    level_skills(g);
    update_rides(g, dt_ms);
    breathe(g);
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
    /* The automap's cells around the player are seen (0x57AC2). */
    int map;
    const bs_automap_entry *m = current_map(g, &map);
    if (m)
        automap_reveal(g->seen[map], m, g->player.pos[0], g->player.pos[2]);
    /* The monsters and the fighting. */
    fight_env fe = fight_env_of(g);
    bool attack = SDL_GetWindowRelativeMouseMode(g->window) && controls_held(&g->keys, ACT_ATTACK);
    if (fight_update(&g->fight, &fe, attack, dt_ms))
        update_item_world(g);
    /* A greeting that starts a conversation; speakers leaving; captivity after "ZZ" or ENDFRAME. */
    if (g->fight.talk_to != UINT32_MAX) {
        uint32_t talker = g->fight.talk_to;
        g->fight.talk_to = UINT32_MAX;
        start_talk(g, SPEAKER_MONSTER, talker);
    }
    update_leaving(g);
    if (g->captive_at && logic_now(g) >= g->captive_at) {
        g->captive_at = 0;
        SDL_SetWindowTitle(g->window, "OpenBattlespire");
        play_video(g, "VIDEOS/CHOP2.SMK", "");
        return;
    }
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
    g->had_mouse = SDL_GetWindowRelativeMouseMode(g->window) || g->cursor_held;
    g->cursor_held = false;
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

/* The automap of where the player is (0x57ECF), or NULL. */
static const bs_automap_entry *current_map(const game *g, int *index)
{
    int i = g->maps.count ? bs_automap_find(&g->maps, g->level.file, g->player.pos[1]) : -1;
    if (index)
        *index = i;
    return i >= 0 ? &g->maps.maps[i] : NULL;
}

static menu_env menu_env_of(game *g, const bs_bsi_image **doll, int doll_cap)
{
    menu_env e;
    memset(&e, 0, sizeof e);
    e.assets = g->assets;
    for (int i = 1; i < MENU_FONTS; i++)
        e.fonts[i] = g->have_font[i] ? &g->fonts[i] : NULL;
    e.character = &g->character;
    e.name = g->name;
    inventory_env ie = inventory_env_of(g);
    e.doll = doll;
    e.doll_count = doll ? inventory_doll_images(&ie, doll, doll_cap) : 0;
    e.carried = g->items.tables ? bs_items_carried_weight(&g->items, &g->world_items) : 0;
    memcpy(e.skill_names, g->skill_names, sizeof e.skill_names);
    memcpy(e.attribute_names, g->attribute_names, sizeof e.attribute_names);
    int map;
    e.map = current_map(g, &map);
    e.revealed = map >= 0 ? g->seen[map] : NULL;
    e.player_x = g->player.pos[0];
    e.player_z = g->player.pos[2];
    e.player_yaw = (int32_t)lroundf(g->player.yaw * 2048.0f / (2.0f * 3.14159265f)) & 0x7FF;
    e.full_view = g->bar.full_view;
    e.now_ms = SDL_GetTicks();
    e.text = &g->text;
    e.message = fight_message;
    e.ctx = g;
    return e;
}

static void open_menu(game *g, menu_kind k)
{
    if (g->menus.open == MENU_NONE) {
        g->had_mouse = SDL_GetWindowRelativeMouseMode(g->window) || g->cursor_held;
        g->cursor_held = false;
        SDL_SetWindowRelativeMouseMode(g->window, false);
    }
    menus_open(&g->menus, k);
    g->overlay_dirty = true;
}

/* ---- conversations ---- */

/* Whether the player carries an item of a base with a number or enchantment (-1 any), in a container too. */
static bool talk_has_item(void *ctx, int32_t base, int32_t number)
{
    game *g = ctx;
    for (uint32_t i = 0; i < g->world_items.count; i++) {
        const bs_world_item *w = &g->world_items.items[i];
        if (w->place.where == BS_ITEM_GONE || !bs_items_place_carried(&g->world_items, w->place))
            continue;
        if (w->item.base == base && (number < 0 || w->item.number == number || w->item.magic == number))
            return true;
    }
    return false;
}

static void talk_voice_stop(void *ctx)
{
    game *g = ctx;
    if (g->audio && g->voice)
        bs_audio_stop(g->audio, g->voice);
    g->voice = 0;
}

/* A line's voice, "<say code>.WAV" in WAVES.BSA on the CD (0x1CE6A). */
static bool talk_voice(void *ctx, const char *code)
{
    game *g = ctx;
    talk_voice_stop(g);
    if (!g->audio)
        return false;
    if (!g->waves_tried) {
        g->waves_tried = true;
        if (bs_bsa_open_vfs(&g->vfs, "WAVES.BSA", &g->waves) != BS_OK)
            g->waves = NULL;
    }
    if (!g->waves)
        return false;
    char name[32];
    size_t n = 0;
    for (const char *p = code; *p > ' ' && n + 5 < sizeof name; p++)
        if (*p != '"')
            name[n++] = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
    memcpy(name + n, ".WAV", 5);
    const bs_bsa_record *rec = bs_bsa_find(g->waves, name);
    if (!rec)
        return false;
    bs_arena_free(&g->voice_arena);
    bs_arena_init(&g->voice_arena, 0);
    uint8_t *data;
    size_t size;
    bs_wav w;
    if (bs_bsa_read(g->waves, rec, &g->voice_arena, &data, &size) != BS_OK || bs_wav_parse(data, size, &w) != BS_OK)
        return false;
    bs_pcm_format f = {w.bits, w.channels, w.rate};
    g->voice = bs_audio_play(g->audio, w.samples, w.sample_bytes, f, g->level_audio.effects_gain, 0.0f, false);
    return g->voice != 0;
}

static bool talk_voice_playing(void *ctx)
{
    game *g = ctx;
    return g->audio && g->voice && bs_audio_playing(g->audio, g->voice);
}

static void talk_message(void *ctx, const char *text)
{
    message(ctx, text);
}

static talk_env talk_env_of(game *g)
{
    talk_env e;
    memset(&e, 0, sizeof e);
    e.overlay = g->overlay;
    e.screen = g->screen;
    for (int i = 1; i < MENU_FONTS; i++)
        e.fonts[i] = g->have_font[i] ? &g->fonts[i] : NULL;
    e.assets = g->assets;
    e.text = &g->text;
    e.txt = g->txt;
    e.heads = g->heads;
    e.now_ms = SDL_GetTicks();
    e.rng = &g->rng;
    e.ctx = g;
    e.message = talk_message;
    e.voice = talk_voice;
    e.voice_playing = talk_voice_playing;
    e.voice_stop = talk_voice_stop;
    return e;
}

/* Where a speaker stands. */
static void speaker_pos(const game *g, speaker_kind kind, uint32_t i, float out[3])
{
    if (kind == SPEAKER_MONSTER && i < g->fight.monsters.count) {
        memcpy(out, g->fight.monsters.list[i].body.pos, sizeof(float) * 3);
    } else if (kind == SPEAKER_FLAT && i < g->level.level.flat_count) {
        for (int k = 0; k < 3; k++)
            out[k] = (float)g->level.level.flats[i].pos[k];
    } else if (kind == SPEAKER_OBJECT && i < g->level.level.object_count) {
        for (int k = 0; k < 3; k++)
            out[k] = (float)g->level.level.objects[i].pos[k];
    } else {
        memcpy(out, g->player.pos, sizeof(float) * 3);
    }
}

/* A speaker leaves (flag 2 at +0x14: TELEPORT, KILL). */
static void speaker_leaves(game *g, speaker_kind kind, uint32_t i)
{
    if (kind == SPEAKER_MONSTER && i < g->fight.monsters.count)
        g->fight.monsters.list[i].gone = true;
    else if (kind == SPEAKER_FLAT && i < g->level.level.flat_count)
        bs_level_renderer_show_flat(g->level.renderer, i, false);
    else if (kind == SPEAKER_OBJECT && i < g->level.level.object_count)
        bs_level_renderer_show(g->level.renderer, i, false);
}

/* Starts a conversation with a speaker (0x584C6). */
static void start_talk(game *g, speaker_kind kind, uint32_t i)
{
    if (!g->txt || g->talk.open)
        return;
    const char *code = "";
    uint32_t id = 0;
    int32_t creature = -1;
    if (kind == SPEAKER_MONSTER) {
        if (i >= g->fight.monsters.count)
            return;
        const bs_monster *m = &g->fight.monsters.list[i];
        creature = (int32_t)m->type;
        if (m->spawn < g->fight.monsters.point_count) {
            const bs_spawn_point *pt = &g->fight.monsters.points[m->spawn];
            code = pt->code;
            id = pt->flat < g->level.level.flat_count ? g->level.level.flats[pt->flat].id : 0;
        } else {
            id = 100000 + i; /* a monster made in play: its own record */
        }
    } else if (kind == SPEAKER_FLAT) {
        if (!g->flat_talk || i >= g->level.level.flat_count)
            return;
        code = g->flat_talk[i].code;
        id = g->level.level.flats[i].id;
    } else {
        if (!g->object_talk || i >= g->level.level.object_count)
            return;
        code = g->object_talk[i].code;
        id = g->level.level.objects[i].id;
    }
    char stem[4];
    int number = g->main_info ? bs_level_number(g->main_info) : 1;
    if (!bs_dialogue_stem(code, creature, number, stem))
        return;
    const char *heads = creature >= 0 ? bs_dialogue_creature_code(creature) : stem;
    bs_dialogue_update_vars(&g->dialogue, g->female, talk_has_item, g);
    talk_env env = talk_env_of(g);
    char msg[512];
    bs_dialogue_opening o =
        talk_open(&g->talk, &env, &g->dialogue, stem, heads, g->female, id, creature, msg, sizeof msg);
    if (o == BS_DIALOGUE_MESSAGE) {
        message(g, msg);
        return;
    }
    if (o != BS_DIALOGUE_TALK)
        return;
    g->talk.speaker = i;
    g->talk_kind = kind;
    g->had_mouse = SDL_GetWindowRelativeMouseMode(g->window) || g->cursor_held;
    g->cursor_held = false;
    SDL_SetWindowRelativeMouseMode(g->window, false);
}

/* An item by base and number (0x5A791): a book or scroll's text, a Sigil Amulet's, plain (10000), or an
 * enchantment on the base. */
static bool make_talk_item(game *g, int32_t base, int32_t number, bs_item *out)
{
    if (!g->items.tables || base < 0 || base >= BS_ITEM_BASE_COUNT)
        return false;
    int number_level = g->main_info ? bs_level_number(g->main_info) : 1;
    const int32_t v[4] = {base, 0, 0, number};
    if (!bs_item_make(&g->items, BS_ITEM_SPEC_PLACED, v, number_level, false, &g->rng, out))
        return false;
    bs_item_pick_picture(&g->items, out, &g->rng);
    return true;
}

/* A reply's actions (0x59D62). */
static void talk_actions(game *g, const bs_dialogue_action *a, uint32_t n)
{
    speaker_kind kind = g->talk_kind;
    uint32_t who = g->talk.speaker;
    bool monster = kind == SPEAKER_MONSTER && who < g->fight.monsters.count;
    fight_env fe = fight_env_of(g);
    static const float nowhere[3] = {0, 0, 0};
    for (uint32_t k = 0; k < n; k++) {
        switch (a[k].kind) {
        case BS_DLG_DROP_WEAPON: {
            uint32_t w = bs_items_weapon(&g->world_items);
            if (w != UINT32_MAX) {
                bs_items_move(&g->world_items, &g->items, w,
                              (bs_item_place){BS_ITEM_FLOOR, bs_items_cell(g->world_items.grid_cell, g->player.pos)},
                              g->player.pos, INT32_MAX);
                message(g, bs_exe_message(&g->text, 508));
                update_item_world(g);
            }
            break;
        }
        case BS_DLG_STATE:
            if (monster)
                fight_monster_state(&g->fight, who, a[k].a);
            break;
        case BS_DLG_RALLY:
            if (monster)
                fight_summon_at(&g->fight, &fe, g->fight.monsters.list[who].type, g->fight.monsters.list[who].body.pos,
                                a[k].a);
            break;
        case BS_DLG_HEALTH_HALF:
            g->character.health >>= 1;
            break;
        case BS_DLG_KILL:
            break; /* when the conversation ends */
        case BS_DLG_PC_TEAM:
        case BS_DLG_PC_TEAM_ALL:
            if (monster)
                g->fight.monsters.list[who].ally = true;
            break;
        case BS_DLG_SUMMON: {
            float p[3];
            speaker_pos(g, kind, who, p);
            fight_summon_at(&g->fight, &fe, (uint32_t)a[k].a, p, a[k].b);
            break;
        }
        case BS_DLG_COMMAND_ALL:
            for (uint32_t m = 0; m < g->fight.monsters.count; m++)
                if ((int32_t)g->fight.monsters.list[m].type == a[k].a)
                    fight_monster_state(&g->fight, m, a[k].b);
            break;
        case BS_DLG_PUT: {
            bs_item it;
            if (make_talk_item(g, a[k].a, a[k].b, &it))
                bs_items_add(&g->world_items, &it, (bs_item_place){BS_ITEM_CARRIED, 0}, -1, nowhere);
            break;
        }
        case BS_DLG_DROP: {
            bs_item it;
            float p[3];
            speaker_pos(g, kind, who, p);
            if (make_talk_item(g, a[k].a, a[k].b, &it)) {
                bs_items_add(&g->world_items, &it,
                             (bs_item_place){BS_ITEM_FLOOR, bs_items_cell(g->world_items.grid_cell, p)}, -1, p);
                update_item_world(g);
            }
            break;
        }
        case BS_DLG_TAKE:
            for (uint32_t i = 0; i < g->world_items.count; i++) {
                bs_world_item *w = &g->world_items.items[i];
                if (w->place.where == BS_ITEM_GONE || !bs_items_place_carried(&g->world_items, w->place) ||
                    w->item.base != a[k].a ||
                    (a[k].b != 10000 && w->item.number != a[k].b && w->item.magic != a[k].b))
                    continue;
                bs_items_unequip(&g->world_items, i);
                w->place.where = BS_ITEM_GONE;
                break;
            }
            break;
        case BS_DLG_LINK:
            /* The objects and flats with this dialogue link are used (0x5A666). */
            for (uint32_t o = 0; o < g->logic.object_count; o++) {
                const bs_logic_object *lo = &g->logic.objects[o];
                const talk_info *ti = lo->kind == BS_LOGIC_OBJECT && g->object_talk ? &g->object_talk[lo->index]
                                      : lo->kind == BS_LOGIC_FLAT && g->flat_talk ? &g->flat_talk[lo->index]
                                                                                   : NULL;
                if (ti && ti->link == a[k].a) {
                    bs_logic_output out;
                    memcpy(g->logic.player, g->player.pos, sizeof g->logic.player);
                    bs_level_logic_activate(&g->logic, o, BS_LOGIC_EVENT_USE, logic_now(g), &out);
                    apply_logic(g, &out);
                }
            }
            break;
        case BS_DLG_END_FRAME:
            g->captive_at = logic_now(g) ? logic_now(g) : 1; /* CHOP2 at once (0x5A395) */
            break;
        case BS_DLG_TELEPORT:
            if (a[k].a <= 0) {
                speaker_leaves(g, kind, who);
            } else if (g->leaving_count < LEAVING_MAX) {
                g->leaving[g->leaving_count].kind = kind;
                g->leaving[g->leaving_count].index = who;
                g->leaving[g->leaving_count].at = logic_now(g) + (uint32_t)a[k].a * 1000;
                g->leaving_count++;
            }
            break;
        }
    }
}

/* The conversation ends (0x5817E): KILL's speaker goes; after "ZZ", captivity in 3 s. */
static void end_talk(game *g)
{
    talk_env env = talk_env_of(g);
    if (g->talk.d.kill)
        speaker_leaves(g, g->talk_kind, g->talk.speaker);
    if (bs_talk_prefix(g->talk.stem, "ZZ", 2) && !g->captive_at)
        g->captive_at = logic_now(g) + 3000;
    talk_close(&g->talk, &env);
    if (g->had_mouse)
        SDL_SetWindowRelativeMouseMode(g->window, true);
    g->overlay_dirty = true;
}

/* The NPC flat with a dialogue code nearest along the view, before any wall, or UINT32_MAX. */
static uint32_t flat_ahead(game *g, float max, float *distance)
{
    float cp = cosf(g->cam.pitch);
    float fwd[3] = {sinf(g->cam.yaw) * cp, -sinf(g->cam.pitch), cosf(g->cam.yaw) * cp};
    float to[3] = {g->cam.pos[0] + fwd[0] * max, g->cam.pos[1] + fwd[1] * max, g->cam.pos[2] + fwd[2] * max}, t;
    int32_t ob;
    float best_t = bs_collision_segment(&g->collision, g->cam.pos, to, &t, &ob) ? t * max : max;
    uint32_t best = UINT32_MAX;
    for (uint32_t i = 0; g->flat_talk && i < g->level.level.flat_count; i++) {
        if (!g->flat_talk[i].code[0])
            continue;
        const bs_bs6_flat *fl = &g->level.level.flats[i];
        if (strncmp(fl->name, "monster", 7) == 0)
            continue; /* spawn points: their monsters talk */
        /* A column 40 units round from its feet up to 128 above (the sprite's size, roughly). */
        float c[3] = {(float)fl->pos[0] - g->cam.pos[0], (float)fl->pos[1] - 64.0f - g->cam.pos[1],
                      (float)fl->pos[2] - g->cam.pos[2]};
        float along = c[0] * fwd[0] + c[1] * fwd[1] + c[2] * fwd[2];
        float d2 = c[0] * c[0] + c[2] * c[2] - (c[0] * fwd[0] + c[2] * fwd[2]) * (c[0] * fwd[0] + c[2] * fwd[2]);
        if (along > 0 && along < best_t && d2 <= 40.0f * 40.0f && fabsf(c[1] - along * fwd[1]) < 96.0f) {
            best_t = along;
            best = i;
        }
    }
    *distance = best_t;
    return best;
}

/* Using a monster or an NPC flat ahead: within reach they talk (0x4D005, 0x4D5A4), further off "You are too
 * far away." Returns whether one was there. */
static bool use_speaker(game *g)
{
    fight_env fe = fight_env_of(g);
    float dm, df;
    uint32_t m = fight_monster_ahead(&g->fight, &fe, 2048.0f, &dm);
    uint32_t f = flat_ahead(g, 2048.0f, &df);
    if (m != UINT32_MAX && (f == UINT32_MAX || dm <= df)) {
        if (dm <= TALK_REACH_MONSTER)
            start_talk(g, SPEAKER_MONSTER, m);
        else
            message(g, bs_exe_message(&g->text, BS_MSG_TOO_FAR));
        return true;
    }
    if (f != UINT32_MAX) {
        if (df <= TALK_REACH_FLAT) {
            activate_flat(g, f);
            start_talk(g, SPEAKER_FLAT, f);
        } else {
            message(g, bs_exe_message(&g->text, BS_MSG_TOO_FAR));
        }
        return true;
    }
    return false;
}

/* Speakers leaving after their time (0x5A41C's table). */
static void update_leaving(game *g)
{
    uint32_t now = logic_now(g);
    for (uint32_t k = 0; k < g->leaving_count;) {
        if (now >= g->leaving[k].at) {
            speaker_leaves(g, g->leaving[k].kind, g->leaving[k].index);
            g->leaving[k] = g->leaving[--g->leaving_count];
        } else {
            k++;
        }
    }
}

/* The hot key list (0x551C5): "F1 : spell" for each key, or "Nothing assigned." (514). */
static void hotkey_list(game *g)
{
    for (int k = 0; k < BS_HOTKEYS; k++) {
        int sp = g->character.hotkeys[k];
        const char *what = sp >= 1 && sp <= BS_SPELL_COUNT && g->have_spells ? g->character.book[sp - 1].name
                                                                              : bs_exe_message(&g->text, 514);
        char line[MESSAGE_LEN];
        snprintf(line, sizeof line, "F%d : %s", k + 1, what);
        message(g, line);
    }
}

/* A button of the interface bar (0xED838). */
static void bar_click(game *g, statusbar_button b)
{
    switch (b) {
    case STATUSBAR_SHEET:
        open_menu(g, MENU_SHEET);
        break;
    case STATUSBAR_SPELLBOOK:
        open_spellbook(g);
        break;
    case STATUSBAR_INVENTORY:
        open_inventory(g, (bs_item_place){BS_ITEM_FLOOR, bs_items_cell(g->world_items.grid_cell, g->player.pos)});
        break;
    case STATUSBAR_AUTOMAP:
        open_menu(g, MENU_AUTOMAP);
        break;
    case STATUSBAR_OPTIONS:
        open_menu(g, MENU_OPTIONS);
        break;
    case STATUSBAR_HOTKEYS:
        hotkey_list(g);
        break;
    case STATUSBAR_HELP:
        /* Five pages from help11 (0x2EA28). */
        if (g->menus.open == MENU_NONE) {
            g->had_mouse = SDL_GetWindowRelativeMouseMode(g->window) || g->cursor_held;
            g->cursor_held = false;
            SDL_SetWindowRelativeMouseMode(g->window, false);
        }
        menus_help(&g->menus, 11, 5);
        break;
    default:
        break;
    }
}

static void open_loadsave(game *g, loadsave_mode mode);
static const char *saves_dir(game *g);

/* What a screen's button did. */
static void menu_did(game *g, menu_action a)
{
    switch (a) {
    case MENU_CLOSED:
        if (g->had_mouse)
            SDL_SetWindowRelativeMouseMode(g->window, true);
        break;
    case MENU_TO_INVENTORY:
        open_inventory(g, (bs_item_place){BS_ITEM_FLOOR, bs_items_cell(g->world_items.grid_cell, g->player.pos)});
        break;
    case MENU_FULL_VIEW:
        g->bar.full_view = !g->bar.full_view;
        break;
    case MENU_VOLUME:
        g->level_audio.effects_gain = (float)g->menus.sound_volume / 127.0f;
        g->level_audio.music_gain = (float)g->menus.music_volume / 127.0f;
        bs_audio_set_music_gain(g->audio, g->level_audio.music_gain);
        break;
    case MENU_END_GAME:
        SDL_SetWindowTitle(g->window, "OpenBattlespire");
        enter_title(g);
        break;
    case MENU_QUIT:
        leave_state(g);
        g->state = STATE_QUIT;
        break;
    case MENU_LOAD:
        open_loadsave(g, LOADSAVE_LOAD);
        break;
    case MENU_SAVE:
        g->picture_pending = true; /* the save screen opens once the view is drawn without the bar */
        break;
    case MENU_NOT_THERE:
        message(g, "Not available yet.");
        break;
    default:
        break;
    }
    g->overlay_dirty = true;
}

/* ---- making the character ---- */

static void creation_message(void *ctx, const char *text)
{
    message(ctx, text);
}

static void creation_sound(void *ctx, int32_t id)
{
    game *g = ctx;
    static const float here[3] = {0, 0, 0};
    bs_level_audio_play_at(&g->level_audio, id, here, here, 0.0f);
}

static void creation_item_lines(void *ctx, const bs_item *it, char *l1, char *l2, size_t cap)
{
    inventory_env e = inventory_env_of(ctx);
    inventory_item_lines(&e, it, l1, l2, cap);
}

static creation_env creation_env_of(game *g)
{
    creation_env e;
    memset(&e, 0, sizeof e);
    e.assets = g->assets;
    e.vfs = &g->vfs;
    for (int i = 1; i < MENU_FONTS; i++)
        e.fonts[i] = g->have_font[i] ? &g->fonts[i] : NULL;
    e.rsc = &g->rsc;
    e.text = &g->text;
    memcpy(e.skill_names, g->skill_names, sizeof e.skill_names);
    memcpy(e.attribute_names, g->attribute_names, sizeof e.attribute_names);
    memcpy(e.armour_names, g->armour_names, sizeof e.armour_names);
    e.spells = g->have_spells ? g->spells : NULL;
    e.db = &g->items;
    e.pictures = g->pictures;
    e.picture_count = g->picture_count;
    e.rng = &g->rng;
    e.now_ms = SDL_GetTicks();
    e.help = &g->menus;
    e.save_root = g->inst.root;
    e.saves_dir = saves_dir(g);
    e.ctx = g;
    e.message = creation_message;
    e.sound = creation_sound;
    e.item_lines = g->items.tables ? creation_item_lines : NULL;
    return e;
}

/* The shops show their items turning: the item models live in a level's scene, so the first level is
 * loaded for them (it's next anyway). */
static void draw_shown_item(game *g, int w, int h)
{
    int r[4];
    const bs_item *it = creation_shown_item(&g->creation, r);
    if (!it || !g->items.tables)
        return;
    if (!g->level.loaded) {
        const char *file = first_level(g);
        const bs_level_info *info = NULL;
        for (size_t i = 0; i < g->levels.count && !info; i++)
            if (bs_strcasecmp(g->levels.levels[i].file, file) == 0)
                info = &g->levels.levels[i];
        const char *models[SCENE_MODELS];
        for (uint32_t i = 0; i < SCENE_MODELS; i++)
            models[i] = g->model_names[i];
        if (bs_level_view_load_ex(&g->level, g->assets, &g->vfs, file, info, 0.0f, &g->settings, models,
                                  SCENE_MODELS) != BS_OK) {
            BS_WARN("%s: cannot load the item models", file);
            return;
        }
    }
    inventory_env e = inventory_env_of(g);
    inventory_draw_model(&e, it, r[0], r[1], r[2], r[3], w, h, SDL_GetTicks());
}

/* The character made: name, sex, race and face; the class's attributes, skills with the race's, spells
 * and items (carried, none worn: 0x54219); then the first level. */
static void finish_creation(game *g)
{
    const creation *cr = &g->creation;
    snprintf(g->name_buf, sizeof g->name_buf, "%s", cr->name);
    g->name = g->name_buf;
    g->female = cr->female;
    g->items.female = cr->female;
    g->race = creation_race_code(cr->race);
    g->hair = cr->part_count[0] ? cr->parts[0][cr->part_at[0]] : 0;
    g->eyes = cr->part_count[1] ? cr->parts[1][cr->part_at[1]] : 1;
    g->mouth = cr->part_count[2] ? cr->parts[2][cr->part_at[2]] : 1;
    bs_creation_apply(&cr->c.cls, cr->race, &g->character, &g->rules);
    g->character.points = cr->c.points; /* the CharacterMaker's points left go to the first level-up (0x37387) */
    bs_dialogue_state_init(&g->dialogue); /* a new game's variables (0x5AE3D) */
    g->captive_at = 0;
    for (uint32_t i = 0; i < cr->c.cls.spell_count; i++) {
        bs_spell sp = cr->c.cls.spells[i];
        if (g->have_spells && sp.effect >= 1 && sp.effect <= BS_SPELL_COUNT)
            sp.skill = g->spells[sp.effect - 1].skill;
        bs_character_learn(&g->character, &sp);
    }
    bs_items_free(&g->world_items);
    static const float nowhere[3] = {0, 0, 0};
    for (int k = 0; k < 2; k++)
        for (uint32_t i = 0; i < cr->shops[k].count; i++)
            if (cr->shops[k].bought[i])
                bs_items_add(&g->world_items, &cr->shops[k].items[i], (bs_item_place){BS_ITEM_CARRIED, 0}, -1,
                             nowhere);
    for (uint32_t i = 0; i < cr->other_count; i++)
        bs_items_add(&g->world_items, &cr->others[i], (bs_item_place){BS_ITEM_CARRIED, 0}, -1, nowhere);
    enter_level(g, first_level(g));
}

/* ---- saved games: the original's and ours (docs/formats/save.md, docs/formats/game_save.md) ---- */

/* Reads a file of a save folder, its name upper or lower case. */
static bool read_save_file(const char *dir, const char *name, bs_arena *a, uint8_t **data, size_t *size)
{
    char path[BS_PATH_MAX + 32], lower[32];
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

/* The level object with a BS6 id, or -1. */
static int32_t object_by_id(const game *g, uint32_t id)
{
    for (uint32_t i = 0; i < g->level.level.object_count; i++)
        if (g->level.level.objects[i].id == id)
            return (int32_t)i;
    return -1;
}

/* An object, flat or light by kind and BS6 id: its logic object, or NO_OBJECT. */
static uint32_t logic_by_id(const game *g, bs_saved_kind kind, uint32_t id)
{
    const bs_bs6_level *l = &g->level.level;
    uint32_t n = kind == BS_SAVED_KIND_FLAT ? l->flat_count : kind == BS_SAVED_KIND_LIGHT ? l->light_count : l->object_count;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t eid = kind == BS_SAVED_KIND_FLAT ? l->flats[i].id : kind == BS_SAVED_KIND_LIGHT ? l->lights[i].id
                                                                                                  : l->objects[i].id;
        if (eid == id)
            return bs_level_logic_find(&g->logic,
                                       kind == BS_SAVED_KIND_FLAT    ? BS_LOGIC_FLAT
                                       : kind == BS_SAVED_KIND_LIGHT ? BS_LOGIC_LIGHT
                                                                     : BS_LOGIC_OBJECT,
                                       i);
    }
    return NO_OBJECT;
}

/* A monster's record id: its spawn point's flat's BS6 id, or for one made in play 100000 + its number (as the
 * dialogue's speakers have them). */
static uint32_t monster_id(const game *g, uint32_t i)
{
    const bs_monsters *ms = &g->fight.monsters;
    const bs_monster *m = &ms->list[i];
    if (m->spawn < ms->point_count && ms->points[m->spawn].flat < g->level.level.flat_count)
        return g->level.level.flats[ms->points[m->spawn].flat].id;
    return 100000 + i;
}

/* The speaker with a record id: a monster, a flat, a level object. */
static bool speaker_by_id(const game *g, uint32_t id, speaker_kind *kind, uint32_t *index)
{
    const bs_monsters *ms = &g->fight.monsters;
    for (uint32_t i = 0; i < ms->count; i++)
        if (!ms->list[i].gone && monster_id(g, i) == id) {
            *kind = SPEAKER_MONSTER;
            *index = i;
            return true;
        }
    for (uint32_t i = 0; i < g->level.level.flat_count; i++)
        if (g->level.level.flats[i].id == id) {
            *kind = SPEAKER_FLAT;
            *index = i;
            return true;
        }
    int32_t o = object_by_id(g, id);
    if (o >= 0) {
        *kind = SPEAKER_OBJECT;
        *index = (uint32_t)o;
        return true;
    }
    return false;
}

static uint32_t speaker_id(const game *g, speaker_kind kind, uint32_t i)
{
    if (kind == SPEAKER_MONSTER)
        return i < g->fight.monsters.count ? monster_id(g, i) : 0;
    if (kind == SPEAKER_FLAT)
        return i < g->level.level.flat_count ? g->level.level.flats[i].id : 0;
    return i < g->level.level.object_count ? g->level.level.objects[i].id : 0;
}

/* Another level's file read, for its objects' ids (the items kept there). */
static bool other_level(game *g, const char *file, bs_arena *a, bs_bs6_level *out)
{
    uint8_t *data;
    size_t size;
    return bs_assets_read(g->assets, file, a, &data, &size) == BS_OK && bs_bs6_parse(data, size, a, out) == BS_OK;
}

/* The level's placed models, flats and lights as the save left them: where they are, locked or not, a used
 * gem's return, whether their controllers run (ours). */
static void load_objects(game *g, const bs_saved_game *sg)
{
    g->logic.active_count = 0;
    for (uint32_t i = 0; i < g->logic.object_count; i++)
        g->logic.objects[i].active = false;
    for (uint32_t k = 0; k < sg->object_count; k++) {
        const bs_saved_object *so = &sg->objects[k];
        uint32_t li = logic_by_id(g, so->kind, so->id);
        int32_t i = so->kind == BS_SAVED_KIND_OBJECT ? object_by_id(g, so->id) : -1;
        if (li == NO_OBJECT && i < 0) {
            BS_WARN("load: no level %s %u", so->kind == BS_SAVED_KIND_FLAT ? "flat" : "object", (unsigned)so->id);
            continue;
        }
        if (li != NO_OBJECT) {
            bs_logic_object *o = &g->logic.objects[li];
            bool moved = false;
            for (int c = 0; c < 3; c++) {
                moved = moved || fabsf(so->pos[c] - o->pos[c]) > 0.5f || ((so->angles[c] - o->angles[c]) & 0x7FF) != 0;
                o->pos[c] = so->pos[c];
                o->angles[c] = so->angles[c];
            }
            o->moved = o->moved || moved;
            if (so->active && g->logic.active_count < g->logic.object_count) {
                o->active = true;
                g->logic.active[g->logic.active_count++] = li;
            }
        }
        if (i < 0)
            continue;
        int32_t rk = ride_of(g, (uint32_t)i);
        if (rk >= 0) {
            struct ride *r = &g->rides[rk];
            memcpy(r->pos, so->pos, sizeof r->pos);
            memcpy(r->angles, so->angles, sizeof r->angles);
            r->yaw = (float)so->angles[1];
        }
        if (g->locked)
            g->locked[i] = (so->flags & BS_SAVED_OBJECT_LOCKED) != 0;
        if (g->gem_percent && g->gem_percent[i] && so->time > sg->clock) {
            g->gem_until[i] = so->time;
            bs_level_renderer_show(g->level.renderer, (uint32_t)i, false);
        }
    }
}

/* The original's controllers after its records (0x4C3D8): each chain's, in the logic's chain order, then the
 * running chains. */
static void load_tree_controllers(game *g, const bs_save_tree *t, bs_arena *a)
{
    bs_level_logic *lg = &g->logic;
    uint32_t *lengths = BS_ARENA_ARRAY(a, uint32_t, lg->object_count + 1);
    uint32_t *chain_object = BS_ARENA_ARRAY(a, uint32_t, lg->object_count + 1);
    if (!lengths || !chain_object)
        return;
    uint32_t chains = 0;
    for (uint32_t i = 0; i < lg->object_count; i++)
        if (lg->objects[i].ctrl_count) {
            chain_object[chains] = i;
            lengths[chains++] = lg->objects[i].ctrl_count;
        }
    bs_save_controller *ctl;
    uint32_t n, na;
    uint8_t *active;
    if (bs_save_controllers_read(t->controls, t->controls_size, lengths, chains, a, &ctl, &n, &active, &na) != BS_OK) {
        BS_WARN("load: the level's controllers don't match the save's");
        return;
    }
    uint32_t at = 0;
    for (uint32_t c = 0; c < chains; c++) {
        const bs_logic_object *o = &lg->objects[chain_object[c]];
        for (uint32_t k = 0; k < o->ctrl_count; k++, at++) {
            bs_logic_ctrl *lc = &lg->ctrls[o->first_ctrl + k];
            const bs_save_controller *sc = &ctl[at];
            lc->reversed = (sc->flags & 2) != 0;
            lc->running = (sc->flags & 1) != 0;
            if (lc->running) {
                /* The start position or angles (+0x14); +0x20 and +0x2C aren't used here. */
                lc->start_ms = sc->start;
                for (int v = 0; v < 3; v++)
                    lc->start[v] = (int32_t)((uint32_t)sc->values[4 * v] | (uint32_t)sc->values[4 * v + 1] << 8 |
                                             (uint32_t)sc->values[4 * v + 2] << 16 |
                                             (uint32_t)sc->values[4 * v + 3] << 24);
            }
        }
    }
    lg->active_count = 0;
    for (uint32_t k = 0; k < na; k++)
        if (active[k] < chains && lg->active_count < lg->object_count) {
            uint32_t oi = chain_object[active[k]];
            lg->objects[oi].active = true;
            lg->active[lg->active_count++] = oi;
        }
}

/* Ours: each controller by its object and place in the chain. */
static void load_controllers(game *g, const bs_saved_game *sg)
{
    for (uint32_t k = 0; k < sg->ctrl_count; k++) {
        const bs_saved_ctrl *c = &sg->ctrls[k];
        uint32_t li = logic_by_id(g, c->kind, c->object);
        if (li == NO_OBJECT || c->index >= g->logic.objects[li].ctrl_count) {
            BS_WARN("load: no controller %u of %u", (unsigned)c->index, (unsigned)c->object);
            continue;
        }
        bs_logic_ctrl *lc = &g->logic.ctrls[g->logic.objects[li].first_ctrl + c->index];
        lc->reversed = c->reversed;
        lc->running = c->running;
        lc->start_ms = c->start_ms;
        memcpy(lc->start, c->start, sizeof lc->start);
    }
}

/* One level's items, in the save's order: those of group (-1 the level the player is in, or another level's
 * number), with objects named by the BS6 ids of level. out must be empty. */
static void items_of(const game *g, const bs_saved_game *sg, int32_t group, const bs_bs6_level *level,
                     int32_t grid_cell, bs_items *out)
{
    uint32_t *index = malloc(sizeof *index * (sg->item_count + 1));
    if (!index)
        return;
    uint32_t n = 0;
    for (uint32_t k = 0; k < sg->item_count; k++)
        index[k] = sg->items[k].level == group || (group < 0 && sg->items[k].place == BS_SAVED_CARRIED) ? n++ : UINT32_MAX;
    out->grid_cell = grid_cell;
    for (uint32_t k = 0; k < sg->item_count; k++) {
        if (index[k] == UINT32_MAX)
            continue;
        const bs_saved_item *si = &sg->items[k];
        bs_item_place place = {BS_ITEM_GONE, 0};
        int32_t object = -1;
        switch (si->place) {
        case BS_SAVED_CARRIED:
            place = (bs_item_place){BS_ITEM_CARRIED, 0};
            break;
        case BS_SAVED_IN_ITEM:
            if (si->ref < sg->item_count && index[si->ref] != UINT32_MAX)
                place = (bs_item_place){BS_ITEM_IN_ITEM, index[si->ref]};
            break;
        case BS_SAVED_MONSTER:
            if (group < 0) /* elsewhere the monsters are made again */
                place = (bs_item_place){BS_ITEM_HELD, si->ref};
            break;
        case BS_SAVED_FLOOR:
            place = (bs_item_place){BS_ITEM_FLOOR, bs_items_cell(grid_cell, si->pos)};
            /* A placed item still where it was is shown by its object (the record keeps the object's id). */
            for (uint32_t o = 0; o < level->object_count && object < 0; o++) {
                const bs_bs6_object *lo = &level->objects[o];
                if (lo->id != si->id)
                    continue;
                bool there = true;
                for (int c = 0; c < 3; c++)
                    there = there && fabsf(si->pos[c] - (float)lo->pos[c]) <= 1.0f;
                object = there ? (int32_t)o : -1;
                break;
            }
            break;
        case BS_SAVED_IN_OBJECT:
            for (uint32_t o = 0; o < level->object_count; o++)
                if (level->objects[o].id == si->ref) {
                    place = (bs_item_place){BS_ITEM_IN_OBJECT, o};
                    break;
                }
            if (place.where == BS_ITEM_GONE)
                BS_WARN("load: no level object %u holding an item", (unsigned)si->ref);
            break;
        case BS_SAVED_NOWHERE:
            break;
        }
        uint32_t i = bs_items_add(out, &si->item, place, object, si->pos);
        if (i == UINT32_MAX) {
            BS_WARN("load: out of memory for the items");
            break;
        }
        out->items[i].equipped = si->worn && place.where == BS_ITEM_CARRIED;
        out->items[i].secondary = si->secondary && place.where == BS_ITEM_CARRIED;
    }
    free(index);
    (void)g;
}

/* The items as the save has them: the level's and what's carried, and those kept in other levels. */
static void load_items(game *g, const bs_saved_game *sg)
{
    int32_t grid_cell = g->world_items.grid_cell;
    bs_items_free(&g->world_items);
    memset(&g->world_items, 0, sizeof g->world_items);
    if (bs_items_level_containers(&g->world_items, &g->level.level) != BS_OK)
        BS_WARN("load: cannot find the level's containers");
    items_of(g, sg, -1, &g->level.level, grid_cell, &g->world_items);
    clear_level_cache(g);
    for (uint32_t k = 0; k < sg->other_level_count && k < sizeof g->level_cache / sizeof g->level_cache[0]; k++) {
        bs_arena a;
        bs_arena_init(&a, 0);
        bs_bs6_level other;
        if (!other_level(g, sg->other_levels[k], &a, &other)) {
            BS_WARN("load: cannot read %s: its items are lost", sg->other_levels[k]);
            bs_arena_free(&a);
            continue;
        }
        const bs_level_info *info = NULL;
        for (size_t i = 0; i < g->levels.count && !info; i++)
            if (bs_strcasecmp(g->levels.levels[i].file, sg->other_levels[k]) == 0)
                info = &g->levels.levels[i];
        bs_items *c = &g->level_cache[g->level_cache_count].items;
        memset(c, 0, sizeof *c);
        if (bs_items_level_containers(c, &other) != BS_OK)
            BS_WARN("load: cannot find %s's containers", sg->other_levels[k]);
        items_of(g, sg, (int32_t)k, &other, info && bs_level_game_type(info) == BS_GAME_CAMPAIGN ? info->grid_cell : 2048,
                 c);
        snprintf(g->level_cache[g->level_cache_count].file, sizeof g->level_cache[0].file, "%s", sg->other_levels[k]);
        g->level_cache_count++;
        bs_arena_free(&a);
    }
}

/*
 * Starts from a saved game: its level is loaded fresh (0x303FE), then the character, the items, the monsters
 * left, the level's objects and controllers, the clock and the rest are put back as the save has them. tree:
 * the original's records, for its controllers; NULL for ours.
 */
static bool apply_saved_game(game *g, const bs_saved_game *sg, const bs_save_tree *tree, bs_arena *a)
{
    /* The level: ours names it (an interior has its campaign level too); the original's has only its number. */
    const char *file = sg->level_file[0] ? sg->level_file : NULL, *campaign = NULL;
    const bs_level_info *campaign_info = NULL;
    for (size_t i = 0; i < g->levels.count && !campaign_info; i++) {
        const bs_level_info *l = &g->levels.levels[i];
        if (sg->campaign_file[0] ? bs_strcasecmp(l->file, sg->campaign_file) == 0
                                 : file ? bs_strcasecmp(l->file, file) == 0
                                        : bs_level_number(l) == sg->level_number && bs_level_game_type(l) == BS_GAME_CAMPAIGN)
            campaign_info = l;
    }
    if (!file)
        file = campaign_info ? campaign_info->file : NULL;
    campaign = campaign_info ? campaign_info->file : NULL;
    if (!file) {
        fprintf(stderr, "openbattlespire: the saved game's level %d isn't in LEVELS.TXT\n", sg->level_number);
        return false;
    }
    BS_INFO("loading %s: %u items, %u monsters", file, (unsigned)sg->item_count, (unsigned)sg->monster_count);

    /* The character. */
    snprintf(g->name_buf, sizeof g->name_buf, "%.31s", sg->name);
    g->name = g->name_buf;
    g->female = sg->female;
    g->items.female = sg->female;
    g->race = creation_race_code(sg->race);
    g->hair = sg->hair;
    g->eyes = sg->eyes;
    g->mouth = sg->mouth;
    g->character = sg->character;
    g->rules = sg->rules;
    g->dialogue = sg->dialogue;
    g->captive_at = sg->captive_at;
    bs_items_free(&g->world_items);
    memset(&g->world_items, 0, sizeof g->world_items);
    bool interior = campaign && bs_strcasecmp(campaign, file) != 0;
    if (interior)
        g->main_info = campaign_info; /* kept by the door's way in */
    if (!enter_level_ex(g, file, -1, interior))
        return false;
    memset(g->seen, 0, sizeof g->seen);

    /* The clock runs on from the saved one, so the saved times (gems, controllers, speakers leaving) hold. */
    g->level_start_ns = SDL_GetTicksNS();
    g->clock_base = sg->clock;
    bs_level_script_init(&g->script, campaign_info ? bs_level_number(campaign_info) : sg->level_number);
    load_items(g, sg);
    fight_env fe = fight_env_of(g);
    fight_restore(&g->fight, &fe, sg->monsters, sg->monster_count);
    load_objects(g, sg);
    if (tree)
        load_tree_controllers(g, tree, a);
    else
        load_controllers(g, sg);
    g->script.progress = sg->script_progress;
    g->script.done = sg->script_done;
    if (sg->script_progress)
        show_counter(g, sg->script_progress);
    for (uint32_t k = 0; k < sg->sigil_gone_count; k++) {
        int32_t o = object_by_id(g, sg->sigils_gone[k]);
        for (uint32_t s = 0; o >= 0 && s < g->sigil_count; s++)
            if (g->sigils[s].object == (uint32_t)o) {
                g->sigils[s].gone = true;
                bs_level_renderer_show(g->level.renderer, (uint32_t)o, false);
                bs_collision_remove(&g->collision, (uint32_t)o);
            }
    }
    g->leaving_count = 0;
    for (int k = 0; k < 8; k++) {
        speaker_kind kind;
        uint32_t index;
        if (!sg->leaving[k].id || g->leaving_count == LEAVING_MAX || !speaker_by_id(g, sg->leaving[k].id, &kind, &index))
            continue;
        g->leaving[g->leaving_count].kind = kind;
        g->leaving[g->leaving_count].index = index;
        g->leaving[g->leaving_count].at = sg->leaving[k].at;
        g->leaving_count++;
    }
    /* The automap: ours keeps every map's; the original one grid for the level. */
    if (sg->have_maps)
        memcpy(g->seen, sg->maps, sizeof g->seen < sizeof sg->maps ? sizeof g->seen : sizeof sg->maps);
    for (uint32_t m = 0; sg->have_map && m < g->maps.count && m < BS_AUTOMAP_MAPS; m++)
        if (bs_strcasecmp(g->maps.maps[m].level, g->level.file) == 0)
            memcpy(g->seen[m], sg->seen, sizeof g->seen[m]);

    /* Where the player stands and looks. */
    float yaw = (float)sg->yaw * 2.0f * SDL_PI_F / 2048.0f;
    bs_player_init(&g->player, sg->pos, yaw);
    g->tick_clock = 0;
    g->cam.yaw = yaw;
    g->cam.pitch = -(float)sg->pitch * 2.0f * SDL_PI_F / 2048.0f;
    bs_player_eye(&g->player, g->cam.pos);
    memcpy(g->fight.last_feet, g->player.pos, sizeof g->fight.last_feet);
    g->stats.speed = g->character.attributes[6];
    g->stats.strength = g->character.attributes[0];
    g->stats.jump_skill = g->character.skills[BS_SKILL_JUMPING];
    /* The boat or balloon ridden: the view keeps its saved way. */
    int32_t ro = sg->ride ? object_by_id(g, sg->ride) : -1, rk = ro >= 0 ? ride_of(g, (uint32_t)ro) : -1;
    if (rk >= 0) {
        board(g, rk);
        g->cam.yaw = yaw;
    }
    update_item_world(g);
    return true;
}

/* The names our files' comments give: base items, creatures. */
static const char *save_item_name(void *ctx, uint32_t base)
{
    const game *g = ctx;
    return g->items.tables && base < BS_ITEM_BASE_COUNT ? g->items.tables->bases[base].name : NULL;
}

static const char *save_creature_name(void *ctx, uint32_t type)
{
    const game *g = ctx;
    return g->fight.have_tables && type < BS_CREATURE_COUNT ? g->fight.tables.creatures[type].name : NULL;
}

static bs_game_save_names save_names(game *g)
{
    return (bs_game_save_names){save_item_name, save_creature_name, NULL, g};
}

/*
 * Loads a saved game: an original one (a folder with SAVETREE.DAT: a slot 0-9 means the install's SAVEn) or one
 * of ours (a GAME.BSS file, or a folder holding one).
 */
static bool load_game(game *g, const char *where)
{
    char dir[BS_PATH_MAX + 8];
    if (where[0] >= '0' && where[0] <= '9' && !where[1])
        snprintf(dir, sizeof dir, "%s/SAVE%c", g->inst.root, where[0]);
    else
        snprintf(dir, sizeof dir, "%s", where);
    bs_arena a;
    bs_arena_init(&a, 0);
    static bs_save_tree tree;
    static bs_save_vars vars;
    static bs_saved_game sg;
    uint8_t *data, *vars_data;
    size_t size, vars_size;
    bool ours = bs_fs_is_file(dir) ? bs_fs_read_file(dir, &a, &data, &size) == BS_OK
                                   : read_save_file(dir, SAVE_FILE, &a, &data, &size);
    bs_result r;
    if (ours) {
        char err[BS_GAME_SAVE_ERROR] = "";
        bs_game_save_names names = save_names(g);
        r = bs_game_save_read((const char *)data, size, g->have_spells ? g->spells : NULL, &names, &a, &sg, err);
        if (r == BS_ERR_FORMAT)
            fprintf(stderr, "openbattlespire: %s: %s\n", dir, err);
    } else {
        bool found = read_save_file(dir, "SAVETREE.DAT", &a, &data, &size) &&
                     read_save_file(dir, "SAVEVARS.DAT", &a, &vars_data, &vars_size);
        r = found ? bs_save_tree_parse(data, size, &a, &tree) : BS_ERR_NOT_FOUND;
        if (r == BS_OK)
            r = bs_save_vars_parse(vars_data, vars_size, &vars);
        if (r == BS_OK)
            r = bs_save_import(&tree, &vars, g->have_spells ? g->spells : NULL, &a, &sg);
    }
    if (r != BS_OK) {
        fprintf(stderr, "openbattlespire: cannot load the saved game in %s: %s\n", dir, bs_result_str(r));
        bs_arena_free(&a);
        return false;
    }
    bool ok = apply_saved_game(g, &sg, ours ? NULL : &tree, &a);
    bs_arena_free(&a);
    return ok;
}

/* ---- saving ---- */

/* The world's items for a save, those of the level the player is in first (group -1) or a kept level's
 * (group k, level its file); ids: a placed item's object's, else counted from 1000000. */
static void save_items(const game *g, const bs_items *items, int32_t group, const bs_bs6_level *level,
                       const uint32_t *monster_index, bs_saved_game *sg, uint32_t *next_id)
{
    uint32_t first = sg->item_count;
    uint32_t *index = malloc(sizeof *index * (items->count + 1));
    if (!index)
        return;
    for (uint32_t i = 0; i < items->count; i++) {
        bool keep = items->items[i].place.where != BS_ITEM_GONE &&
                    (group < 0 || !bs_items_place_carried(items, items->items[i].place));
        index[i] = keep ? first + (sg->item_count++ - first) : UINT32_MAX;
    }
    for (uint32_t i = 0; i < items->count; i++) {
        if (index[i] == UINT32_MAX)
            continue;
        const bs_world_item *w = &items->items[i];
        bs_saved_item *si = &sg->items[index[i]];
        memset(si, 0, sizeof *si);
        si->item = w->item;
        si->level = group;
        si->worn = w->equipped;
        si->secondary = w->secondary;
        memcpy(si->pos, w->pos, sizeof si->pos);
        si->id = (*next_id)++;
        switch (w->place.where) {
        case BS_ITEM_CARRIED:
            si->place = BS_SAVED_CARRIED;
            si->level = -1;
            break;
        case BS_ITEM_IN_ITEM:
            si->place = w->place.ref < items->count && index[w->place.ref] != UINT32_MAX ? BS_SAVED_IN_ITEM : BS_SAVED_NOWHERE;
            si->ref = si->place == BS_SAVED_IN_ITEM ? index[w->place.ref] : 0;
            break;
        case BS_ITEM_FLOOR:
            si->place = BS_SAVED_FLOOR;
            if (w->object >= 0 && (uint32_t)w->object < level->object_count)
                si->id = level->objects[w->object].id; /* placed: its object shows it */
            break;
        case BS_ITEM_IN_OBJECT:
            si->place = w->place.ref < level->object_count ? BS_SAVED_IN_OBJECT : BS_SAVED_NOWHERE;
            si->ref = si->place == BS_SAVED_IN_OBJECT ? level->objects[w->place.ref].id : 0;
            break;
        case BS_ITEM_HELD:
            si->place = monster_index && w->place.ref < g->fight.monsters.count &&
                                monster_index[w->place.ref] != UINT32_MAX
                            ? BS_SAVED_MONSTER
                            : BS_SAVED_NOWHERE;
            si->ref = si->place == BS_SAVED_MONSTER ? monster_index[w->place.ref] : 0;
            break;
        default:
            si->place = BS_SAVED_NOWHERE;
            break;
        }
    }
    free(index);
}

/* The game as it is now, as a saved game (arrays in a). */
static bool game_to_saved(game *g, const char *name, bs_saved_game *sg, bs_arena *a)
{
    memset(sg, 0, sizeof *sg);
    snprintf(sg->save_name, sizeof sg->save_name, "%s", name);
    snprintf(sg->level_file, sizeof sg->level_file, "%s", g->level.file);
    if (g->main_info && bs_strcasecmp(g->main_info->file, g->level.file) != 0)
        snprintf(sg->campaign_file, sizeof sg->campaign_file, "%s", g->main_info->file);
    sg->level_number = g->script.level_number;
    sg->clock = logic_now(g);
    sg->captive_at = g->captive_at;
    snprintf(sg->name, sizeof sg->name, "%s", g->name ? g->name : "");
    sg->female = g->female;
    sg->race = 1;
    for (int r = 0; r < 6; r++)
        if (g->race && !strcmp(creation_race_code(r), g->race))
            sg->race = r;
    sg->hair = (uint8_t)g->hair;
    sg->eyes = (uint8_t)g->eyes;
    sg->mouth = (uint8_t)g->mouth;
    sg->character = g->character;
    sg->rules = g->rules;
    memcpy(sg->pos, g->player.pos, sizeof sg->pos);
    sg->yaw = (int32_t)lroundf(g->cam.yaw * 2048.0f / (2.0f * SDL_PI_F)) & 0x7FF;
    int32_t pitch = (int32_t)lroundf(-g->cam.pitch * 2048.0f / (2.0f * SDL_PI_F));
    sg->pitch = pitch < -1024 ? -1024 : pitch > 1023 ? 1023 : pitch;
    sg->script_progress = g->script.progress;
    sg->script_done = g->script.done;
    sg->dialogue = g->dialogue;
    sg->have_maps = true;
    memcpy(sg->maps, g->seen, sizeof g->seen < sizeof sg->maps ? sizeof g->seen : sizeof sg->maps);

    /* The monsters alive. */
    const bs_monsters *ms = &g->fight.monsters;
    uint32_t *monster_index = BS_ARENA_ARRAY(a, uint32_t, ms->count + 1);
    sg->monsters = BS_ARENA_ARRAY(a, bs_saved_monster, ms->count + 1);
    if (!monster_index || !sg->monsters)
        return false;
    for (uint32_t i = 0; i < ms->count; i++) {
        const bs_monster *m = &ms->list[i];
        monster_index[i] = UINT32_MAX;
        if (m->gone || m->dead)
            continue;
        monster_index[i] = sg->monster_count;
        bs_saved_monster *s = &sg->monsters[sg->monster_count++];
        *s = (bs_saved_monster){.id = monster_id(g, i), .type = m->type, .angle = m->angle, .health = m->health,
                                .health_max = m->health_max, .spell_points = m->spell_points,
                                .spell_points_max = m->spell_points_max, .skill = m->skill, .flags = m->flags,
                                .state = (int32_t)m->state, .greeted = m->greeted, .summoned = m->summoned,
                                .ally = m->ally};
        memcpy(s->pos, m->body.pos, sizeof s->pos);
        memcpy(s->attributes, m->attributes, sizeof s->attributes);
    }

    /* The items: here, and in the levels kept. */
    uint32_t total = g->world_items.count;
    for (uint32_t k = 0; k < g->level_cache_count; k++)
        total += g->level_cache[k].items.count;
    sg->items = BS_ARENA_ARRAY(a, bs_saved_item, total + 1);
    if (!sg->items)
        return false;
    uint32_t next_id = 1000000;
    save_items(g, &g->world_items, -1, &g->level.level, monster_index, sg, &next_id);
    for (uint32_t k = 0; k < g->level_cache_count && sg->other_level_count < BS_SAVED_LEVELS; k++) {
        bs_bs6_level other;
        if (!other_level(g, g->level_cache[k].file, a, &other)) {
            BS_WARN("save: cannot read %s: its items aren't saved", g->level_cache[k].file);
            continue;
        }
        snprintf(sg->other_levels[sg->other_level_count], sizeof sg->other_levels[0], "%s", g->level_cache[k].file);
        save_items(g, &g->level_cache[k].items, (int32_t)sg->other_level_count++, &other, NULL, sg, &next_id);
    }

    /* The level's objects that moved or run, the locks, the gems used; the controllers not as they began. */
    const bs_level_logic *lg = &g->logic;
    const bs_bs6_level *l = &g->level.level;
    sg->objects = BS_ARENA_ARRAY(a, bs_saved_object, lg->object_count + l->object_count + 1);
    sg->ctrls = BS_ARENA_ARRAY(a, bs_saved_ctrl, lg->ctrl_count + 1);
    if (!sg->objects || !sg->ctrls)
        return false;
    sg->have_ctrls = true;
    bool *listed = BS_ARENA_ARRAY(a, bool, l->object_count + 1);
    if (!listed)
        return false;
    memset(listed, 0, sizeof *listed * (l->object_count + 1));
    uint32_t now = logic_now(g);
    for (uint32_t i = 0; i < lg->object_count; i++) {
        const bs_logic_object *o = &lg->objects[i];
        bs_saved_kind kind = o->kind == BS_LOGIC_FLAT    ? BS_SAVED_KIND_FLAT
                             : o->kind == BS_LOGIC_LIGHT ? BS_SAVED_KIND_LIGHT
                                                         : BS_SAVED_KIND_OBJECT;
        for (uint32_t c = 0; c < o->ctrl_count; c++) {
            const bs_logic_ctrl *lc = &lg->ctrls[o->first_ctrl + c];
            if (lc->reversed || lc->running)
                sg->ctrls[sg->ctrl_count++] = (bs_saved_ctrl){o->entity_id, kind, c, lc->reversed, lc->running,
                                                              lc->start_ms, {lc->start[0], lc->start[1], lc->start[2]}};
        }
        bool level_object = o->kind == BS_LOGIC_OBJECT && o->index < l->object_count;
        bool lock = level_object && g->lockable && g->lockable[o->index];
        bool gem = level_object && g->gem_until && g->gem_until[o->index] > now;
        if (!o->moved && !o->active && !lock && !gem)
            continue;
        bs_saved_object *so = &sg->objects[sg->object_count++];
        *so = (bs_saved_object){.id = o->entity_id, .kind = kind, .active = o->active};
        memcpy(so->pos, o->pos, sizeof so->pos);
        memcpy(so->angles, o->angles, sizeof so->angles);
        if (level_object) {
            listed[o->index] = true;
            so->flags = g->locked && g->locked[o->index] ? BS_SAVED_OBJECT_LOCKED : 0;
            so->time = gem ? g->gem_until[o->index] : 0;
        }
    }
    /* Objects without logic that can be locked or used as gems. */
    for (uint32_t i = 0; i < l->object_count; i++) {
        bool lock = g->lockable && g->lockable[i], gem = g->gem_until && g->gem_until[i] > now;
        if (listed[i] || (!lock && !gem))
            continue;
        bs_saved_object *so = &sg->objects[sg->object_count++];
        *so = (bs_saved_object){.id = l->objects[i].id, .kind = BS_SAVED_KIND_OBJECT,
                                .flags = g->locked && g->locked[i] ? BS_SAVED_OBJECT_LOCKED : 0,
                                .time = gem ? g->gem_until[i] : 0};
        for (int c = 0; c < 3; c++) {
            so->pos[c] = (float)l->objects[i].pos[c];
            so->angles[c] = l->objects[i].angles[c];
        }
    }
    /* Boats and the balloon that have moved, and the one ridden. */
    for (uint32_t k = 0; k < g->ride_count; k++) {
        const struct ride *r = &g->rides[k];
        const bs_bs6_object *lo = &l->objects[r->object];
        bool moved = (int32_t)k == g->riding || ((r->angles[1] - lo->angles[1]) & 0x7FF) != 0;
        for (int c = 0; c < 3; c++)
            moved = moved || fabsf(r->pos[c] - (float)lo->pos[c]) > 0.5f;
        if (!moved || listed[r->object])
            continue;
        bs_saved_object *so = &sg->objects[sg->object_count++];
        *so = (bs_saved_object){.id = lo->id, .kind = BS_SAVED_KIND_OBJECT};
        memcpy(so->pos, r->pos, sizeof so->pos);
        memcpy(so->angles, r->angles, sizeof so->angles);
    }
    if (g->riding >= 0)
        sg->ride = l->objects[g->rides[g->riding].object].id;
    for (uint32_t s = 0; s < g->sigil_count && sg->sigil_gone_count < BS_SAVE_SIGILS; s++)
        if (g->sigils[s].gone)
            sg->sigils_gone[sg->sigil_gone_count++] = l->objects[g->sigils[s].object].id;
    for (uint32_t k = 0; k < g->leaving_count && k < 8; k++) {
        sg->leaving[k].id = speaker_id(g, g->leaving[k].kind, g->leaving[k].index);
        sg->leaving[k].at = g->leaving[k].at;
    }
    return true;
}

/* The saves folder: --saves, or the user's data folder (made the first time it's needed). */
static const char *saves_dir(game *g)
{
    if (!g->saves_dir[0]) {
        char *pref = SDL_GetPrefPath("OpenBattlespire", "OpenBattlespire");
        if (!pref)
            return NULL;
        snprintf(g->saves_dir, sizeof g->saves_dir, "%ssaves", pref);
        SDL_free(pref);
    }
    return g->saves_dir;
}

/* The view as the original keeps it with a save: 80 x 50, RGB555 (0x30276), from the window now. */
static void capture_picture(game *g, int w, int h)
{
    uint8_t *px = malloc((size_t)w * (size_t)h * 3);
    if (!px)
        return;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
    for (int y = 0; y < BS_SAVE_IMAGE_HEIGHT; y++)
        for (int x = 0; x < BS_SAVE_IMAGE_WIDTH; x++) {
            int sx = x * w / BS_SAVE_IMAGE_WIDTH, sy = h - 1 - y * h / BS_SAVE_IMAGE_HEIGHT;
            const uint8_t *p = px + ((size_t)sy * (size_t)w + (size_t)sx) * 3;
            uint16_t v = (uint16_t)((p[0] >> 3) << 10 | (p[1] >> 3) << 5 | p[2] >> 3);
            g->save_picture[2 * (y * BS_SAVE_IMAGE_WIDTH + x)] = (uint8_t)v;
            g->save_picture[2 * (y * BS_SAVE_IMAGE_WIDTH + x) + 1] = (uint8_t)(v >> 8);
        }
    free(px);
}

/* Saves the game into one of our slots: GAME.BSS and the picture. */
static bool save_game(game *g, int slot, const char *name)
{
    const char *dir = saves_dir(g);
    char slot_dir[BS_PATH_MAX + 16], path[BS_PATH_MAX + 32];
    if (!dir)
        return false;
    save_slot_dir(dir, slot, slot_dir, sizeof slot_dir);
    if (bs_fs_mkdirs(slot_dir) != BS_OK)
        return false;
    bs_arena a;
    bs_arena_init(&a, 0);
    static bs_saved_game sg;
    bool ok = game_to_saved(g, name, &sg, &a);
    char *text = NULL;
    size_t size;
    bs_game_save_names names = save_names(g);
    ok = ok && bs_game_save_write(&sg, &names, &text, &size) == BS_OK;
    snprintf(path, sizeof path, "%s/%s", slot_dir, SAVE_FILE);
    ok = ok && bs_fs_write_file(path, text, size) == BS_OK;
    snprintf(path, sizeof path, "%s/IMAGE.RAW", slot_dir);
    ok = ok && bs_fs_write_file(path, g->save_picture, sizeof g->save_picture) == BS_OK;
    free(text);
    bs_arena_free(&a);
    if (ok)
        BS_INFO("saved %s", slot_dir);
    return ok;
}

/* ---- the Load and Save screen ---- */

static loadsave_env loadsave_env_of(game *g)
{
    loadsave_env e = {.assets = g->assets, .saves_dir = saves_dir(g), .original_dir = g->inst.root,
                      .now_ms = SDL_GetTicks(), .message = fight_message, .ctx = g};
    for (int i = 1; i < MENU_FONTS; i++)
        e.fonts[i] = g->have_font[i] ? &g->fonts[i] : NULL;
    return e;
}

static void open_loadsave(game *g, loadsave_mode mode)
{
    loadsave_env env = loadsave_env_of(g);
    loadsave_open(&g->loadsave, &env, mode);
    g->had_mouse = SDL_GetWindowRelativeMouseMode(g->window);
    SDL_SetWindowRelativeMouseMode(g->window, false);
}

static void loadsave_did(game *g, loadsave_result r)
{
    loadsave *ls = &g->loadsave;
    if (ls->naming && !SDL_TextInputActive(g->window))
        SDL_StartTextInput(g->window);
    else if (!ls->naming && SDL_TextInputActive(g->window))
        SDL_StopTextInput(g->window);
    switch (r) {
    case LOADSAVE_CLOSED:
        if (g->had_mouse)
            SDL_SetWindowRelativeMouseMode(g->window, true);
        g->overlay_dirty = true;
        break;
    case LOADSAVE_DO_SAVE:
        message(g, save_game(g, ls->slot, ls->name) ? "Game saved." : "The game cannot be saved.");
        if (g->had_mouse)
            SDL_SetWindowRelativeMouseMode(g->window, true);
        g->overlay_dirty = true;
        break;
    case LOADSAVE_DO_LOAD: {
        char where[BS_PATH_MAX + 16];
        if (ls->originals) {
            snprintf(where, sizeof where, "%d", ls->slot);
        } else {
            const char *dir = saves_dir(g);
            save_slot_dir(dir ? dir : ".", ls->slot, where, sizeof where);
        }
        if (!load_game(g, where))
            message(g, "The saved game cannot be loaded.");
        break;
    }
    default:
        break;
    }
}

static void creation_did(game *g, creation_action a)
{
    switch (a) {
    case CREATION_TO_TITLE:
        SDL_StopTextInput(g->window);
        bs_level_view_unload(&g->level);
        g->state = STATE_TITLE; /* the music goes on */
        {
            const bs_bsi_image *img = bs_assets_image(g->assets, TITLE_IMAGE);
            if (img)
                bs_screen_set_bsi(g->screen, img);
        }
        break;
    case CREATION_LOAD: {
        SDL_StopTextInput(g->window);
        char where[BS_PATH_MAX + 16];
        const char *dir = saves_dir(g);
        if (g->creation.load_originals || !dir)
            snprintf(where, sizeof where, "%d", g->creation.load_slot);
        else
            save_slot_dir(dir, g->creation.load_slot, where, sizeof where);
        if (!load_game(g, where))
            message(g, "The saved game cannot be loaded.");
        break;
    }
    case CREATION_PLAY:
        finish_creation(g);
        break;
    default:
        break;
    }
}

static void draw_creation(game *g, int w, int h)
{
    creation_env env = creation_env_of(g);
    creation_update(&g->creation, &env);
    const char *bg = creation_background(&g->creation);
    if (bg && (!g->creation_image || strcmp(bg, g->creation_image) != 0)) {
        const bs_bsi_image *img = bs_assets_image(g->assets, bg);
        if (img && bs_screen_set_bsi(g->screen, img) == BS_OK)
            g->creation_image = bs_arena_strdup(&g->arena, bg);
    }
    bs_screen_draw(g->screen, w, h);
    draw_shown_item(g, w, h);
    if (!g->overlay)
        return;
    bs_overlay_clear(g->overlay);
    creation_draw(&g->creation, &env, g->overlay);
    update_messages(g);
    draw_messages(g);
    bs_overlay_draw(g->overlay, w, h);
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
    case STATE_CREATION: {
        int x = 0, y = 0;
        if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN || e->type == SDL_EVENT_MOUSE_BUTTON_UP)
            window_to_screen(g, e->button.x, e->button.y, &x, &y);
        else if (e->type == SDL_EVENT_MOUSE_MOTION)
            window_to_screen(g, e->motion.x, e->motion.y, &x, &y);
        creation_env env = creation_env_of(g);
        creation_did(g, creation_event(&g->creation, &env, e, x, y));
        break;
    }
    case STATE_LEVEL:
        if (g->loadsave.open) {
            int x = 0, y = 0;
            if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN || e->type == SDL_EVENT_MOUSE_BUTTON_UP)
                window_to_screen(g, e->button.x, e->button.y, &x, &y);
            loadsave_env env = loadsave_env_of(g);
            loadsave_did(g, loadsave_event(&g->loadsave, &env, e, x, y));
            break;
        }
        if (g->talk.open) {
            int x = 0, y = 0;
            if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN || e->type == SDL_EVENT_MOUSE_BUTTON_UP)
                window_to_screen(g, e->button.x, e->button.y, &x, &y);
            else if (e->type == SDL_EVENT_MOUSE_MOTION)
                window_to_screen(g, e->motion.x, e->motion.y, &x, &y);
            talk_env env = talk_env_of(g);
            bs_dialogue_action act[BS_DIALOGUE_ACTIONS];
            uint32_t n;
            talk_result r = talk_event(&g->talk, &env, e, x, y, act, &n);
            if (r == TALK_CHOSE)
                talk_actions(g, act, n);
            else if (r == TALK_CLOSED)
                end_talk(g);
            break;
        }
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
        if (g->menus.open != MENU_NONE) {
            int x = 0, y = 0;
            if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN || e->type == SDL_EVENT_MOUSE_BUTTON_UP)
                window_to_screen(g, e->button.x, e->button.y, &x, &y);
            else if (e->type == SDL_EVENT_MOUSE_MOTION)
                window_to_screen(g, e->motion.x, e->motion.y, &x, &y);
            const bs_bsi_image *doll[STATUSBAR_FACE_LAYERS];
            menu_env me = menu_env_of(g, doll, STATUSBAR_FACE_LAYERS);
            menu_did(g, menus_event(&g->menus, &me, e, x, y));
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
            if (!fight_use(&g->fight, &fe, USE_REACH) && !use_speaker(g))
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
        if (controls_pressed(&g->keys, ACT_LEAVE, e) && g->riding >= 0) {
            leave_ride(g);
            break;
        }
        if (controls_pressed(&g->keys, ACT_SWITCH_WEAPON, e)) {
            fight_env fe = fight_env_of(g);
            fight_switch_weapon(&g->fight, &fe);
            break;
        }
        if (controls_pressed(&g->keys, ACT_SHEET, e)) {
            open_menu(g, MENU_SHEET);
            break;
        }
        if (controls_pressed(&g->keys, ACT_AUTOMAP, e)) {
            open_menu(g, MENU_AUTOMAP);
            break;
        }
        if (e->type == SDL_EVENT_KEY_UP && (e->key.key == SDLK_RETURN || e->key.key == SDLK_KP_ENTER) &&
            g->cursor_held) {
            g->cursor_held = false;
            SDL_SetWindowRelativeMouseMode(g->window, true);
            break;
        }
        if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
            if ((e->key.key == SDLK_RETURN || e->key.key == SDLK_KP_ENTER) && SDL_GetWindowRelativeMouseMode(g->window)) {
                /* Held, the cursor shows (the game's "+" cursor) to click the bar. */
                g->cursor_held = true;
                SDL_SetWindowRelativeMouseMode(g->window, false);
            } else if (e->key.key == SDLK_ESCAPE) {
                /* The options bar (End Game goes back to the title). */
                open_menu(g, MENU_OPTIONS);
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

/* Items carried from the start (for testing): plain items of the given bases. Arrows (twenty), armour
 * and clothes are worn first; then the first weapon is wielded and the next made the secondary. */
static void give_items(game *g, const char *list)
{
    if (!list || !g->items.tables)
        return;
    static const float nowhere[3] = {0, 0, 0};
    int weapons = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (const char *p = list; p && *p;) {
            int base = atoi(p);
            p = strchr(p, ',');
            p = p ? p + 1 : NULL;
            if (base < 0 || base >= BS_ITEM_BASE_COUNT || bs_item_is_weapon((uint32_t)base) != (pass == 1))
                continue;
            bs_item it;
            bs_item_make_plain(&g->items, (uint32_t)base, 1, false, &g->rng, &it);
            bs_item_pick_picture(&g->items, &it, &g->rng);
            if (base == BS_ITEM_ARROW)
                it.quantity = 20;
            uint32_t i = bs_items_add(&g->world_items, &it, (bs_item_place){BS_ITEM_CARRIED, 0}, -1, nowhere);
            if (i == UINT32_MAX)
                continue;
            if (pass == 1 && weapons++ == 1)
                bs_items_set_secondary(&g->world_items, &g->items, i, &g->rules, INT32_MAX);
            else if (pass == 0 || weapons == 1)
                bs_items_equip(&g->world_items, &g->items, i, &g->rules, INT32_MAX);
        }
    }
}

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
            BS_WARN("cannot open TXT.BSA: no books or conversations");
        if (bs_bsa_open_vfs(&g->vfs, "GAMEDATA/FLC.BSA", &g->heads) != BS_OK)
            BS_WARN("cannot open FLC.BSA: no talking heads");
        /* The models items are shown with, and their sizes for the inventory's camera. */
        inventory_models(&g->item_tables, g->model_names);
        if (loaded && bs_item_hand_models_read(&img, &g->item_tables) == BS_OK)
            for (uint32_t i = 0; i <= BS_ITEM_LAST_WEAPON; i++)
                memcpy(g->model_names[INVENTORY_MODELS + i], g->item_tables.hand_models[i], 16);
        else
            BS_WARN("no weapon models in GAME.EXE: no weapon in hand");
        snprintf(g->model_names[INVENTORY_MODELS + FIGHT_MODEL_ARROW], 16, "earrow.3d"); /* (0x48A26) */
        /* The character sheet's names and the automap's table. */
        bs_exe_text names;
        if (loaded && bs_exe_string_table(&img, "Medical", BS_SKILL_COUNT, &g->arena, &names) == BS_OK &&
            names.count >= BS_SKILL_COUNT)
            for (int i = 0; i < BS_SKILL_COUNT; i++)
                g->skill_names[i] = names.messages[i];
        if (loaded && bs_exe_string_table(&img, "STR", 8, &g->arena, &names) == BS_OK && names.count >= 8)
            for (int i = 0; i < 8; i++)
                g->attribute_names[i] = names.messages[i];
        if (loaded && bs_exe_string_table(&img, "No armor allowed", 4, &g->arena, &names) == BS_OK && names.count >= 4)
            for (int i = 0; i < 4; i++)
                g->armour_names[i] = names.messages[i];
        if (bs_automap_table_read(data_exe, size_exe, &g->maps) != BS_OK)
            BS_WARN("no automap table in GAME.EXE: no automap");
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
        /* The interface bar. */
        statusbar_load(&g->bar, g->assets);
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
    for (int i = 1; i < MENU_FONTS; i++) {
        char fn[32];
        uint8_t *fd;
        size_t fs;
        snprintf(fn, sizeof fn, "GAMEDATA/FONT%04d.FNT", i);
        g->have_font[i] = bs_vfs_read(&g->vfs, fn, &g->arena, &fd, &fs) == BS_OK &&
                          bs_font_parse(fd, fs, &g->arena, &g->fonts[i]) == BS_OK;
        if (!g->have_font[i])
            BS_WARN("cannot read %s", fn);
    }
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
    g.hair = 0; /* the first of each, for --level */
    g.eyes = 1;
    g.mouth = 1;
    bs_character_default(&g.character);
    bs_dialogue_state_init(&g.dialogue);
    talk_init(&g.talk);
    menus_init(&g.menus);
    bs_arena_init(&g.book_arena, 0);
    g.monsters_on = !o.no_monsters;
    if (o.saves)
        snprintf(g.saves_dir, sizeof g.saves_dir, "%s", o.saves);
    if (!open_data(&g, o.data_dir))
        return 1;
    /* Spells known from the start, for --level (Single Player makes the character). */
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

    give_items(&g, o.give);

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
    if (o.load) {
        if (!load_game(&g, o.load))
            return 1;
    } else if (o.level)
        enter_level(&g, o.level);
    else if (o.no_intro)
        enter_title(&g);
    else
        enter_intro(&g);

    if (o.inventory && g.state == STATE_LEVEL)
        open_inventory(&g, (bs_item_place){BS_ITEM_FLOOR, bs_items_cell(g.world_items.grid_cell, g.player.pos)});
    if (o.level_up && g.state == STATE_LEVEL) {
        open_menu(&g, MENU_SHEET);
        menus_level_up(&g.menus, &g.character, bs_level_up_points(o.level_up));
    }
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
        case STATE_CREATION:
            draw_creation(&g, w, h);
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
            if (g.loadsave.open && g.overlay) {
                /* The Load and Save screen covers everything; the world waits. */
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0, 0, w, h);
                glClearColor(0, 0, 0, 1);
                glClear(GL_COLOR_BUFFER_BIT);
                bs_overlay_clear(g.overlay);
                loadsave_env env = loadsave_env_of(&g);
                loadsave_draw(&g.loadsave, &env, g.overlay);
                update_messages(&g);
                draw_messages(&g);
                bs_overlay_draw(g.overlay, w, h);
                g.overlay_dirty = true;
                break;
            }
            if (g.talk.open && g.overlay) {
                /* The world waits while a conversation goes on (0x58043). */
                talk_env env = talk_env_of(&g);
                talk_draw(&g.talk, &env, w, h);
                update_messages(&g);
                draw_messages(&g);
                bs_overlay_draw(g.overlay, w, h);
                g.overlay_dirty = true;
                break;
            }
            if (g.menus.open != MENU_NONE && g.overlay) {
                /* The world waits while a screen is open: the game shows behind the options bar, small in the
                 * help pages' window, not at all behind the others. */
                int r[4];
                int view = menus_view(&g.menus, r);
                if (view == 1) {
                    show_instances(&g);
                    bs_level_renderer_draw(g.level.renderer, &g.cam, &g.settings, w, h, (double)(now - start) / 1e9);
                } else if (view == 2) {
                    bs_level_renderer_draw_inset(g.level.renderer, &g.cam, &g.settings, w, h,
                                                 (double)(now - start) / 1e9, r[0], r[1], r[2], r[3]);
                } else {
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    glViewport(0, 0, w, h);
                    glClearColor(0, 0, 0, 1);
                    glClear(GL_COLOR_BUFFER_BIT);
                }
                bs_overlay_clear(g.overlay);
                const bs_bsi_image *doll[STATUSBAR_FACE_LAYERS];
                menu_env me = menu_env_of(&g, doll, STATUSBAR_FACE_LAYERS);
                menus_draw(&g.menus, &me, g.overlay);
                update_messages(&g);
                draw_messages(&g);
                bs_overlay_draw(g.overlay, w, h);
                g.overlay_dirty = true;
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
                    if (g.breath > 0) /* 7 wide at x 610, up from y 220 (0x1E6E0) */
                        bs_overlay_fill(g.overlay, 610, 220 - g.breath, 7, g.breath,
                                        g.breath < 10 ? 0xFF0000FFu : 0x00FF00FFu);
                    int32_t yaw = (int32_t)lroundf(g.player.yaw * 2048.0f / (2.0f * 3.14159265f));
                    /* The face shows what the character wears. */
                    inventory_env ie = inventory_env_of(&g);
                    const bs_bsi_image *face[STATUSBAR_FACE_LAYERS];
                    statusbar_set_face(&g.bar, face, inventory_doll_images(&ie, face, STATUSBAR_FACE_LAYERS),
                                       inventory_doll_eyes(&ie));
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
            if (g.picture_pending) {
                g.picture_pending = false;
                capture_picture(&g, w, h);
                open_loadsave(&g, LOADSAVE_SAVE);
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
    bs_bsa_close(g.heads);
    bs_bsa_close(g.waves);
    bs_arena_free(&g.voice_arena);
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
