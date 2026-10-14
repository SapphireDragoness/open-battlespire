/*
 * openbattlespire: the game.
 *
 * So far: the start-up sequence (the intro video, the title menu with its
 * music), and Single Player entering the first level, which can be explored
 * with its music and ambient sounds. The level's moving parts work: using
 * objects, stepping onto them, doors, lifts and teleports (docs/re/README.md,
 * "Level logic"). The player walks with the original's collision, speeds,
 * gravity and jumping ("Player movement and collision"); F toggles flying.
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
#include "bs/formats/levels.h"
#include "bs/game/collision.h"
#include "bs/game/level_logic.h"
#include "bs/game/player.h"
#include "bs/render/gl.h"
#include "bs/render/level_renderer.h"
#include "bs/render/level_view.h"
#include "bs/render/screen.h"
#include "controls.h"

#define MOUSE_SENSITIVITY 0.0025f
/* Keyboard turning and looking (the original's turn rate is an option; these are guesses). */
#define TURN_RATE 3.0f       /* radians per second */
#define LOOK_RATE 1.5f
#define FLY_SPEED 400.0f     /* level units per second, flying */
#define SOUND_JUMP 401       /* 400 for a female character (0x40BAB) */
#define TITLE_TRACK 6        /* RE: 0x31B50 */
#define TITLE_IMAGE "mult01.bsi"
/* Not from the game yet: how far away an object can be used. */
#define USE_REACH 192.0f
#define NO_OBJECT UINT32_MAX

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
} options;

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
            "  --keys LAYOUT       modern (default) or original (the game's own defaults)\n"
            "\n"
            "Any key skips the intro; on the title, Enter = Single Player, Esc = Exit.\n"
            "In a level, click to capture the mouse; the mouse looks around and the left button\n"
            "uses what's under the crosshair. Hold the jump key to charge a jump (the pointer\n"
            "shows where you'll land), release to jump. Sneak toggles slow movement.\n"
            "  modern:   W A S D move, Space jump, C duck, Ctrl sneak, E use, PgUp/PgDn (R/V) up/down\n"
            "  original: arrows move, Alt jump, Delete/End turn, Insert/Home look, S sneak,\n"
            "            Space + mouse back duck, PgUp/PgDn up/down\n"
            "  both:     F fly (Shift faster), F2 classic/enhanced rendering, M sound, Esc title\n");
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
        bs_level_audio_stop(&g->level_audio);
        bs_level_view_unload(&g->level);
        SDL_SetWindowRelativeMouseMode(g->window, false);
        break;
    case STATE_QUIT:
        break;
    }
}

static void enter_title(game *g);

/* The intro plays STORY.SMK from the CD; without it, the title comes straight up. */
static void enter_intro(game *g)
{
    leave_state(g);
    g->state = STATE_INTRO;
    uint8_t *data;
    size_t size;
    bs_arena_init(&g->video_arena, 0);
    bs_result r = bs_vfs_read(&g->vfs, "VIDEOS/STORY.SMK", &g->video_arena, &data, &size);
    if (r == BS_OK)
        r = bs_video_open(g->audio, data, size, &g->video);
    if (r != BS_OK) {
        BS_WARN("intro video: %s", bs_result_str(r));
        enter_title(g);
    }
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

static bool enter_level(game *g, const char *file)
{
    leave_state(g);
    g->state = STATE_QUIT; /* nothing to leave if loading fails */
    const bs_level_info *info = NULL;
    for (size_t i = 0; i < g->levels.count && !info; i++)
        if (bs_strcasecmp(g->levels.levels[i].file, file) == 0)
            info = &g->levels.levels[i];
    bs_result r = bs_level_view_load(&g->level, g->assets, &g->vfs, file, info, 0.0f, &g->settings);
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
    g->level_start_ns = SDL_GetTicksNS();
    g->state = STATE_LEVEL;
    /* The player starts at the START marker and drops to the floor. */
    bs_level_view_place_camera(&g->level, -1, &g->cam);
    bs_player_init(&g->player, g->cam.pos, g->cam.yaw);
    g->stats = (bs_player_stats){50, 50, 0, info && bs_level_game_type(info) != BS_GAME_CAMPAIGN};
    bs_player_eye(&g->player, g->cam.pos);
    bs_level_audio_start(&g->level_audio, info);
    char title[128];
    snprintf(title, sizeof title, "OpenBattlespire - %s", info ? info->name : file);
    SDL_SetWindowTitle(g->window, title);
    return true;
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

/* ---- level logic ---- */

static uint32_t logic_now(const game *g)
{
    return (uint32_t)((SDL_GetTicksNS() - g->level_start_ns) / 1000000);
}

/* The interactive object a ray hits first (walls block it), or NO_OBJECT. */
static uint32_t object_along(const game *g, const float dir[3], float reach)
{
    int32_t ob;
    float t = bs_scene_raycast(&g->level.scene, &g->level.level, bs_level_renderer_xforms(g->level.renderer),
                               g->cam.pos, dir, reach, &ob);
    return t >= 0 && ob >= 0 ? bs_level_logic_find(&g->logic, BS_LOGIC_OBJECT, (uint32_t)ob) : NO_OBJECT;
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

static void use_object(game *g)
{
    float cp = cosf(g->cam.pitch);
    float fwd[3] = {sinf(g->cam.yaw) * cp, -sinf(g->cam.pitch), cosf(g->cam.yaw) * cp};
    uint32_t o = object_along(g, fwd, USE_REACH);
    if (o != NO_OBJECT)
        activate(g, (int32_t)g->logic.objects[o].index, BS_LOGIC_EVENT_USE);
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
    };
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
    bs_player_move(p, &g->stats, &g->collision, &in, dt_ms, &ev);
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
    if (g->flying)
        fly(g, (float)dt_ms / 1000.0f);
    else
        walk(g, dt_ms);
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
            enter_title(g);
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
        if (controls_pressed(&g->keys, ACT_USE, e) &&
            (e->type != SDL_EVENT_MOUSE_BUTTON_DOWN || SDL_GetWindowRelativeMouseMode(g->window))) {
            use_object(g);
            break;
        }
        if (controls_pressed(&g->keys, ACT_SNEAK, e)) {
            g->sneaking = !g->sneaking;
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
            } else if (e->key.key == SDLK_F2) {
                g->settings.classic = !g->settings.classic;
            } else if (e->key.key == SDLK_M && g->audio) {
                static bool paused;
                paused = !paused;
                bs_audio_pause(g->audio, paused);
            }
        } else if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            /* The first click captures the mouse (later clicks use, above). */
            SDL_SetWindowRelativeMouseMode(g->window, true);
        } else if (e->type == SDL_EVENT_MOUSE_MOTION && SDL_GetWindowRelativeMouseMode(g->window)) {
            if (g->keys.original && controls_held(&g->keys, ACT_SPECIAL)) {
                /* Special moves: pulling the mouse back ducks (pushing it shoves, not implemented). */
                if (e->motion.yrel > 0)
                    g->ducking = true;
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
    char path[32];
    snprintf(path, sizeof path, "MUSIC/Track%02d.ogg", TITLE_TRACK);
    if (bs_vfs_read(&g->vfs, path, &g->arena, &g->title_music, &g->title_music_size) != BS_OK) {
        BS_WARN("no title music (%s)", path);
        g->title_music = NULL;
    }
    return true;
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
    if (!open_data(&g, o.data_dir))
        return 1;

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

    uint64_t start = SDL_GetTicksNS(), last = start;
    while (g.state != STATE_QUIT) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            handle_event(&g, &e);
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
                    BS_WARN("intro video: %s", bs_result_str(bs_video_error(g.video)));
                enter_title(&g);
            }
            bs_screen_draw(g.screen, w, h);
            break;
        }
        case STATE_TITLE:
            bs_screen_draw(g.screen, w, h);
            break;
        case STATE_LEVEL:
            run_level(&g, dt);
            bs_level_audio_update(&g.level_audio, g.cam.pos, g.cam.yaw, dt);
            bs_level_renderer_draw(g.level.renderer, &g.cam, &g.settings, w, h, (double)(now - start) / 1e9);
            break;
        case STATE_QUIT:
            break;
        }
        SDL_GL_SwapWindow(g.window);
    }

    bs_level_audio_close(&g.level_audio);
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
