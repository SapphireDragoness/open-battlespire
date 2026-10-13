/* bsview: free-fly level viewer. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "bs/assets/assets.h"
#include "bs/assets/scene.h"
#include "bs/core/fs.h"
#include "bs/core/install.h"
#include "bs/core/log.h"
#include "bs/core/png.h"
#include "bs/formats/bs6.h"
#include "bs/formats/levels.h"
#include "bs/render/gl.h"
#include "bs/render/level_renderer.h"

#define MOUSE_SENSITIVITY 0.0025f
#define BASE_SPEED 400.0f  /* level units per second */
#define PI_F 3.14159265f

typedef struct options {
    const char *data_dir;
    const char *level;
    int view;            /* -1: start marker */
    bool classic;
    bool markers;
    bool vertex_light;
    const char *screenshot;
    const char *aux;     /* with --screenshot: also write <aux>_unlit.png, _normal.png, _depth.f32 */
    int width, height;
    bool have_camera;
    float camera[5];     /* x y z yaw pitch (degrees) */
    float fov;           /* vertical field of view, degrees */
    float fog_scale;     /* multiplies the level's fog distances (for comparing with the original) */
} options;

typedef struct loaded_level {
    char file[32];
    const bs_level_info *info;  /* NULL if not in LEVELS.TXT */
    bs_arena arena;
    bs_bs6_level level;
    bs_scene scene;
    bs_level_renderer *renderer;
} loaded_level;

typedef struct app {
    bs_install inst;
    bs_vfs vfs;
    bs_cdimage *cd;
    bs_assets *assets;
    bs_arena arena;
    bs_level_table levels;
    loaded_level cur;
    bool have_level;
    int level_index;     /* index into levels, or -1 */
    bs_camera cam;
    bs_render_settings settings;
    float fog_scale;
    SDL_Window *window;
} app;

static void usage(FILE *out)
{
    fprintf(out,
            "usage: bsview [-d install-dir] [options]\n"
            "  --level NAME        level file, e.g. l1.bs6 or inkeep.bs6 (default: first in LEVELS.TXT)\n"
            "  --view N            start at viewpoint N (default: the level's start marker)\n"
            "  --camera x,y,z,yaw,pitch   start camera (level units, degrees)\n"
            "  --fov DEGREES       vertical field of view (default 53.13, as the game at 640x480)\n"
            "  --fog-scale K       multiply the level's fog distances by K (default: 1 for fog-wall\n"
            "                      levels, verified; 3 for hazy outdoor levels, fitted)\n"
            "  --classic           start in classic mode\n"
            "  --markers           show editor markers\n"
            "  --vertex-light      light per vertex instead of per pixel\n"
            "  --size WxH          window size (default 1280x960)\n"
            "  --screenshot FILE   render one frame to FILE (PNG) and exit\n"
            "  --aux PREFIX        with --screenshot, also write PREFIX_unlit.png (no light, fog,\n"
            "                      sprites or sky), PREFIX_normal.png, PREFIX_facebyte.png and\n"
            "                      PREFIX_depth.f32 (raw\n"
            "                      window depth, bottom row first), for comparing with the original\n"
            "\n"
            "controls:\n"
            "  mouse (click to capture, Esc to release)   look\n"
            "  W A S D / arrows   move      Space / C   up / down      Shift / Ctrl   faster / slower\n"
            "  PgUp / PgDn        previous / next level from LEVELS.TXT\n"
            "  0                  level start          1-9   viewpoints\n"
            "  F2 classic/enhanced  F3 fog  F4 lighting  F5 markers  F6 sprites  F7 sky  F8 back-face culling\n"
            "  F9 per-vertex/per-pixel light   F10 player light\n"
            "  + / -              ambient light       P   print camera      F12   screenshot\n");
}

/* Returns false on a usage error; sets *help for -h/--help. */
static bool parse_args(int argc, char **argv, options *o, bool *help)
{
    *help = false;
    memset(o, 0, sizeof *o);
    o->view = -1;
    /* GAME.EXE sets focal lengths of 0.75 x screen width (both axes at 640x480):
     * 67.4 x 53.1 degrees (RE: 0x10F5C, 0x6EB78). */
    o->fov = 53.13f;
    o->fog_scale = 0.0f; /* 0: per level, see apply_level_settings() */
    o->width = 1280;
    o->height = 960;
    o->data_dir = getenv("BS_DATA_DIR");
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        bool has = i + 1 < argc;
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            *help = true;
            return true;
        } else if ((strcmp(a, "-d") == 0 || strcmp(a, "--data") == 0) && has)
            o->data_dir = argv[++i];
        else if (strcmp(a, "--level") == 0 && has)
            o->level = argv[++i];
        else if (strcmp(a, "--view") == 0 && has)
            o->view = atoi(argv[++i]);
        else if (strcmp(a, "--classic") == 0)
            o->classic = true;
        else if (strcmp(a, "--markers") == 0)
            o->markers = true;
        else if (strcmp(a, "--vertex-light") == 0)
            o->vertex_light = true;
        else if (strcmp(a, "--screenshot") == 0 && has)
            o->screenshot = argv[++i];
        else if (strcmp(a, "--aux") == 0 && has)
            o->aux = argv[++i];
        else if (strcmp(a, "--size") == 0 && has) {
            if (sscanf(argv[++i], "%dx%d", &o->width, &o->height) != 2 || o->width < 64 || o->height < 64)
                return false;
        } else if (strcmp(a, "--fog-scale") == 0 && has) {
            o->fog_scale = strtof(argv[++i], NULL);
            if (!(o->fog_scale > 0.0f))
                return false;
        } else if (strcmp(a, "--fov") == 0 && has) {
            o->fov = strtof(argv[++i], NULL);
            if (!(o->fov >= 10.0f && o->fov <= 150.0f))
                return false;
        } else if (strcmp(a, "--camera") == 0 && has) {
            float *c = o->camera;
            if (sscanf(argv[++i], "%f,%f,%f,%f,%f", &c[0], &c[1], &c[2], &c[3], &c[4]) != 5)
                return false;
            o->have_camera = true;
        } else
            return false;
    }
    return o->data_dir != NULL;
}

/* ---- levels ---- */

static void unload_level(app *a)
{
    if (!a->have_level)
        return;
    bs_level_renderer_destroy(a->cur.renderer);
    bs_arena_free(&a->cur.arena);
    memset(&a->cur, 0, sizeof a->cur);
    a->have_level = false;
}

static void place_camera(app *a, int view)
{
    const bs_bs6_level *lvl = &a->cur.level;
    a->cam.pitch = 0;
    a->cam.yaw = 0;
    if (view >= 0 && (uint32_t)view < lvl->view_count) {
        for (int c = 0; c < 3; c++)
            a->cam.pos[c] = (float)lvl->views[view].pos[c];
        return;
    }
    for (uint32_t i = 0; i < lvl->flat_count; i++) {
        if (bs_strcasecmp(lvl->flats[i].name, "start") == 0) {
            for (int c = 0; c < 3; c++)
                a->cam.pos[c] = (float)lvl->flats[i].pos[c];
            return;
        }
    }
    /* No start marker: the middle of the level, a little above the floor. */
    for (int c = 0; c < 3; c++)
        a->cam.pos[c] = (a->cur.scene.min[c] + a->cur.scene.max[c]) * 0.5f;
}

/* Average colour of an image's bottom rows: the sky at the horizon. */
static void horizon_color(const bs_bsi_image *sky, float out[3])
{
    double sum[3] = {0, 0, 0};
    uint32_t rows = sky->height < 8 ? sky->height : 8, n = 0;
    for (uint32_t y = sky->height - rows; y < sky->height; y++)
        for (uint32_t x = 0; x < sky->width; x++, n++) {
            uint8_t rgb[3];
            bs_bsi_pixel_rgb(sky, sky->pixels[(size_t)y * sky->width + x], rgb);
            for (int c = 0; c < 3; c++)
                sum[c] += rgb[c];
        }
    for (int c = 0; c < 3; c++)
        out[c] = n ? (float)(sum[c] / n / 255.0) : 0.0f;
}

/*
 * Full-fog colour of a HAZE.* table: three channel tables (R, G, B), each 32
 * fog levels x 32 input values of u32, where level 0 is full fog. See
 * docs/formats/palettes.md.
 */
static bool haze_color(app *a, const char *suffix, float out[3])
{
    char name[64];
    snprintf(name, sizeof name, "GAMEDATA/HAZE.%s", suffix);
    uint8_t *data;
    size_t size;
    if (bs_vfs_read(&a->vfs, name, &a->cur.arena, &data, &size) != BS_OK || size < 12288)
        return false;
    for (int c = 0; c < 3; c++) {
        size_t off = (size_t)c * 4096 + 16 * 4; /* channel c, level 0, input 16 */
        uint32_t v = (uint32_t)data[off] | (uint32_t)data[off + 1] << 8;
        out[c] = (float)((v >> (10 - 5 * c)) & 31) / 31.0f;
    }
    return true;
}

static void apply_level_settings(app *a)
{
    const bs_level_info *info = a->cur.info;
    /*
     * Fog, as GAME.EXE sets it up (0x11150, 0x6B778): linear from a start
     * distance to the level's view distance, towards the full-fog colour of
     * its HAZE table. The start is view distance - 512 ("F" levels: a fog
     * wall at the edge of view) or 8 ("C" levels: haze over the whole range).
     */
    memset(a->settings.fog_color, 0, sizeof a->settings.fog_color);
    if (info && info->haze[0] && !haze_color(a, info->haze, a->settings.fog_color)) {
        const bs_bsi_image *sky = info->sky[0] ? bs_assets_image(a->assets, info->sky) : NULL;
        if (sky && sky->width && sky->height && sky->pixels)
            horizon_color(sky, a->settings.fog_color);
    }
    float view = info && info->view_distance > 0 ? (float)info->view_distance : 6000.0f;
    bool gradual = info && info->mode[2] == 'C';
    /*
     * "F" levels: verified against two memory dumps of level 1 (the decoded
     * distances fit; scaling them by 2 or 3 fits worse). "C" levels: the
     * decoded rule gives fog about 3x too dense against a screenshot of level 5
     * (UESP "Point E", camera 14384,-400,32696); 3 is fitted, not derived, and
     * the start value for "C" levels may be misread (0x1122F).
     */
    float scale = a->fog_scale > 0.0f ? a->fog_scale : (gradual ? 3.0f : 1.0f);
    a->settings.fog_end = view * scale;
    a->settings.fog_start = (gradual ? 8.0f : view - 512.0f) * scale;
    /*
     * Ambient light: LEVELS.TXT field 7 in 1/512ths of a shade level,
     * clamped to 16128 (RE: 0x6F20D, 0x8D013; measured against the original:
     * 1024 = 2 levels in level 1). The clear outdoor levels (31000) are fully lit.
     */
    int32_t amb = info ? info->ambient : 0;
    if (amb > 16128)
        amb = 16128;
    if (amb < 0)
        amb = 0;
    a->settings.ambient = (float)amb / 512.0f / 31.0f;
}

static bool load_level(app *a, const char *file, const bs_level_info *info)
{
    unload_level(a);
    loaded_level *l = &a->cur;
    snprintf(l->file, sizeof l->file, "%s", file);
    l->info = info;
    bs_arena_init(&l->arena, 0);
    uint8_t *data;
    size_t size;
    bs_result r = bs_assets_read(a->assets, file, &l->arena, &data, &size);
    if (r == BS_OK)
        r = bs_bs6_parse(data, size, &l->arena, &l->level);
    if (r == BS_OK)
        r = bs_scene_build(a->assets, &l->level, &l->arena, &l->scene);
    const bs_bsi_image *sky = info && info->sky[0] ? bs_assets_image(a->assets, info->sky) : NULL;
    if (r == BS_OK)
        r = bs_level_renderer_create(&l->scene, &l->level, sky, &l->renderer);
    if (r != BS_OK) {
        fprintf(stderr, "bsview: cannot load %s: %s\n", file, bs_result_str(r));
        bs_arena_free(&l->arena);
        return false;
    }
    a->have_level = true;
    apply_level_settings(a);
    /* Water: LEVELS.TXT mode[1] 'Y' loads water2.bsi (RE: 0x262AC); the height is the level's WATR. */
    if (info && info->mode[1] == 'Y')
        bs_level_renderer_set_water(l->renderer, bs_assets_image(a->assets, "water2.bsi"), (float)l->level.water);
    printf("%s%s%s: %u triangles, %u materials, %u lights, %u flats\n", file, info ? " - " : "",
           info ? info->name : "", l->scene.vertex_count / 3, l->scene.material_count, l->level.light_count,
           l->level.flat_count);
    return true;
}

static bool load_index(app *a, int index)
{
    if (a->levels.count == 0)
        return false;
    int n = (int)a->levels.count;
    index = ((index % n) + n) % n;
    if (!load_level(a, a->levels.levels[index].file, &a->levels.levels[index]))
        return false;
    a->level_index = index;
    place_camera(a, -1);
    return true;
}

/* ---- output ---- */

static void update_title(app *a)
{
    char title[256];
    const bs_level_info *info = a->cur.info;
    snprintf(title, sizeof title, "bsview - %s%s%s - %s%s", a->cur.file, info ? " " : "", info ? info->name : "",
             a->settings.classic ? "classic" : "enhanced", a->settings.lighting ? "" : ", fullbright");
    SDL_SetWindowTitle(a->window, title);
}

static bool screenshot(const char *path, int w, int h)
{
    uint8_t *px = malloc((size_t)w * (size_t)h * 3);
    uint8_t *flipped = malloc((size_t)w * (size_t)h * 3);
    bool ok = false;
    if (px && flipped) {
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
        size_t row = (size_t)w * 3;
        for (int y = 0; y < h; y++)
            memcpy(flipped + (size_t)y * row, px + (size_t)(h - 1 - y) * row, row);
        ok = bs_png_write_rgb(path, (uint32_t)w, (uint32_t)h, flipped) == BS_OK;
    }
    free(px);
    free(flipped);
    if (ok)
        printf("saved %s\n", path);
    else
        fprintf(stderr, "bsview: cannot write %s\n", path);
    return ok;
}

/* Diagnostic renders for measuring the original's lighting against ours. */
static bool write_aux(app *a, const char *prefix, int w, int h)
{
    char path[BS_PATH_MAX];
    bs_render_settings s = a->settings;
    s.lighting = s.fog = s.sprites = s.markers = s.sky = false;
    s.classic = false;
    s.fog_color[0] = s.fog_color[1] = s.fog_color[2] = 0;

    bs_level_renderer_draw(a->cur.renderer, &a->cam, &s, w, h, 0.0);
    snprintf(path, sizeof path, "%s_unlit.png", prefix);
    bool ok = screenshot(path, w, h);

    float *depth = malloc((size_t)w * (size_t)h * sizeof *depth);
    if (depth) {
        glReadPixels(0, 0, w, h, GL_DEPTH_COMPONENT, GL_FLOAT, depth);
        snprintf(path, sizeof path, "%s_depth.f32", prefix);
        ok = ok && bs_fs_write_file(path, depth, (size_t)w * (size_t)h * sizeof *depth) == BS_OK;
        free(depth);
    }

    s.debug_view = 1;
    bs_level_renderer_draw(a->cur.renderer, &a->cam, &s, w, h, 0.0);
    snprintf(path, sizeof path, "%s_normal.png", prefix);
    ok = ok && screenshot(path, w, h);

    s.debug_view = 2; /* face header byte, as the grey value */
    bs_level_renderer_draw(a->cur.renderer, &a->cam, &s, w, h, 0.0);
    snprintf(path, sizeof path, "%s_facebyte.png", prefix);
    return ok && screenshot(path, w, h);
}

/* ---- input ---- */

static void move_camera(app *a, float dt)
{
    const bool *keys = SDL_GetKeyboardState(NULL);
    float speed = BASE_SPEED;
    if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT])
        speed *= 4;
    if (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL])
        speed *= 0.25f;
    float cy = cosf(a->cam.yaw), sy = sinf(a->cam.yaw), cp = cosf(a->cam.pitch), sp = sinf(a->cam.pitch);
    float fwd[3] = {sy * cp, -sp, cy * cp};
    float right[3] = {cy, 0, -sy};
    float f = (float)((keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) - (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]));
    float s = (float)((keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) - (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]));
    float u = (float)(keys[SDL_SCANCODE_SPACE] - keys[SDL_SCANCODE_C]);
    for (int c = 0; c < 3; c++)
        a->cam.pos[c] += (fwd[c] * f + right[c] * s) * speed * dt;
    a->cam.pos[1] -= u * speed * dt; /* up is -y */
}

static void key_down(app *a, SDL_Keycode key, int w, int h, bool *quit)
{
    bs_render_settings *s = &a->settings;
    switch (key) {
    case SDLK_ESCAPE:
        if (SDL_GetWindowRelativeMouseMode(a->window))
            SDL_SetWindowRelativeMouseMode(a->window, false);
        else
            *quit = true;
        break;
    case SDLK_F2: s->classic = !s->classic; break;
    case SDLK_F3: s->fog = !s->fog; break;
    case SDLK_F4: s->lighting = !s->lighting; break;
    case SDLK_F5: s->markers = !s->markers; break;
    case SDLK_F6: s->sprites = !s->sprites; break;
    case SDLK_F7: s->sky = !s->sky; break;
    case SDLK_F8: s->cull = !s->cull; break;
    case SDLK_F9: s->vertex_lighting = !s->vertex_lighting; break;
    case SDLK_F10: s->player_light = !s->player_light; break;
    case SDLK_EQUALS:
    case SDLK_KP_PLUS: s->ambient = fminf(s->ambient + 0.05f, 1.5f); break;
    case SDLK_MINUS:
    case SDLK_KP_MINUS: s->ambient = fmaxf(s->ambient - 0.05f, 0.0f); break;
    case SDLK_PAGEUP: load_index(a, a->level_index - 1); break;
    case SDLK_PAGEDOWN: load_index(a, a->level_index + 1); break;
    case SDLK_P:
        printf("--level %s --camera %.0f,%.0f,%.0f,%.1f,%.1f\n", a->cur.file, (double)a->cam.pos[0],
               (double)a->cam.pos[1], (double)a->cam.pos[2], (double)(a->cam.yaw * 180 / PI_F),
               (double)(a->cam.pitch * 180 / PI_F));
        break;
    case SDLK_F12: {
        static int n;
        char path[64];
        snprintf(path, sizeof path, "bsview_%03d.png", n++);
        screenshot(path, w, h);
        break;
    }
    default:
        if (key >= SDLK_0 && key <= SDLK_9)
            place_camera(a, key == SDLK_0 ? -1 : (int)(key - SDLK_1));
        break;
    }
    update_title(a);
}

static SDL_FunctionPointer gl_proc_any;
static bs_gl_proc gl_loader(const char *name)
{
    gl_proc_any = SDL_GL_GetProcAddress(name);
    return (bs_gl_proc)gl_proc_any;
}

/* ---- main ---- */

static bool open_data(app *a, const char *dir)
{
    bs_result r = bs_install_detect(dir, &a->inst);
    if (r != BS_OK) {
        fprintf(stderr, "bsview: '%s' is not a Battlespire installation (%s)\n", dir, bs_result_str(r));
        return false;
    }
    bs_vfs_init(&a->vfs);
    if (bs_install_mount(&a->inst, &a->vfs, &a->cd) != BS_OK || bs_assets_open(&a->vfs, &a->assets) != BS_OK)
        return false;
    uint8_t *text;
    size_t size;
    if (bs_vfs_read(&a->vfs, "GAMEDATA/LEVELS.TXT", &a->arena, &text, &size) != BS_OK ||
        bs_levels_parse(text, size, &a->arena, &a->levels) != BS_OK)
        BS_WARN("cannot read LEVELS.TXT");
    return true;
}

int main(int argc, char **argv)
{
    options o;
    bool help;
    bool parsed = parse_args(argc, argv, &o, &help);
    if (help) {
        usage(stdout);
        return 0;
    }
    if (!parsed) {
        if (!o.data_dir)
            fprintf(stderr, "bsview: no install directory; use -d <dir> or set BS_DATA_DIR\n\n");
        usage(stderr);
        return 2;
    }

    static app a;
    bs_arena_init(&a.arena, 0);
    a.settings = bs_render_settings_default();
    a.settings.classic = o.classic;
    a.settings.markers = o.markers;
    a.settings.vertex_lighting = o.vertex_light;
    a.fog_scale = o.fog_scale;
    a.cam.fov_y = o.fov * PI_F / 180.0f;
    a.level_index = -1;
    if (!open_data(&a, o.data_dir))
        return 1;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "bsview: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | (o.screenshot ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE);
    a.window = SDL_CreateWindow("bsview", o.width, o.height, flags);
    SDL_GLContext ctx = a.window ? SDL_GL_CreateContext(a.window) : NULL;
    const char *missing;
    if (!ctx || !bs_gl_load(gl_loader, &missing)) {
        fprintf(stderr, "bsview: cannot create an OpenGL 3.3 context: %s\n", ctx ? missing : SDL_GetError());
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    BS_INFO("OpenGL %s on %s", (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER));

    bool ok;
    if (o.level) {
        const bs_level_info *info = NULL;
        for (size_t i = 0; i < a.levels.count && !info; i++)
            if (bs_strcasecmp(a.levels.levels[i].file, o.level) == 0) {
                info = &a.levels.levels[i];
                a.level_index = (int)i;
            }
        ok = load_level(&a, o.level, info);
        if (ok)
            place_camera(&a, o.view);
    } else {
        ok = load_index(&a, 0);
        if (ok && o.view >= 0)
            place_camera(&a, o.view);
    }
    if (!ok)
        return 1;
    if (o.have_camera) {
        memcpy(a.cam.pos, o.camera, sizeof a.cam.pos);
        a.cam.yaw = o.camera[3] * PI_F / 180.0f;
        a.cam.pitch = o.camera[4] * PI_F / 180.0f;
    }
    update_title(&a);
    if (!o.screenshot)
        usage(stdout);

    uint64_t start = SDL_GetTicksNS(), last = start;
    bool quit = false;
    while (!quit) {
        int w, h;
        SDL_GetWindowSizeInPixels(a.window, &w, &h);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT)
                quit = true;
            else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat)
                key_down(&a, e.key.key, w, h, &quit);
            else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                SDL_SetWindowRelativeMouseMode(a.window, true);
            else if (e.type == SDL_EVENT_MOUSE_MOTION && SDL_GetWindowRelativeMouseMode(a.window)) {
                a.cam.yaw += e.motion.xrel * MOUSE_SENSITIVITY;
                a.cam.pitch -= e.motion.yrel * MOUSE_SENSITIVITY;
                a.cam.pitch = fmaxf(fminf(a.cam.pitch, 1.55f), -1.55f);
            }
        }
        uint64_t now = SDL_GetTicksNS();
        float dt = (float)(now - last) / 1e9f;
        last = now;
        move_camera(&a, dt > 0.1f ? 0.1f : dt);

        bs_level_renderer_draw(a.cur.renderer, &a.cam, &a.settings, w, h, (double)(now - start) / 1e9);
        if (o.screenshot) {
            ok = screenshot(o.screenshot, w, h);
            if (ok && o.aux)
                ok = write_aux(&a, o.aux, w, h);
            break;
        }
        SDL_GL_SwapWindow(a.window);
    }

    unload_level(&a);
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(a.window);
    SDL_Quit();
    bs_assets_close(a.assets);
    bs_cdimage_close(a.cd);
    bs_arena_free(&a.arena);
    return ok ? 0 : 1;
}
