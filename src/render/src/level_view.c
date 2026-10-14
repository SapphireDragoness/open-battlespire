#include "bs/render/level_view.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "bs/core/fs.h"

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
static bool haze_color(bs_level_view *v, const bs_vfs *vfs, const char *suffix, float out[3])
{
    char name[64];
    snprintf(name, sizeof name, "GAMEDATA/HAZE.%s", suffix);
    uint8_t *data;
    size_t size;
    if (bs_vfs_read(vfs, name, &v->arena, &data, &size) != BS_OK || size < 12288)
        return false;
    for (int c = 0; c < 3; c++) {
        size_t off = (size_t)c * 4096 + 16 * 4; /* channel c, level 0, input 16 */
        uint32_t word = (uint32_t)data[off] | (uint32_t)data[off + 1] << 8;
        out[c] = (float)((word >> (10 - 5 * c)) & 31) / 31.0f;
    }
    return true;
}

static void apply_settings(bs_level_view *v, bs_assets *assets, const bs_vfs *vfs, float fog_scale,
                           bs_render_settings *s)
{
    const bs_level_info *info = v->info;
    /*
     * Fog, as GAME.EXE sets it up (0x11150, 0x6B778): linear from a start
     * distance to the level's view distance, towards the full-fog colour of
     * its HAZE table. The start is view distance - 512 ("F" levels: a fog
     * wall at the edge of view) or 8 ("C" levels: haze over the whole range).
     */
    memset(s->fog_color, 0, sizeof s->fog_color);
    if (info && info->haze[0] && !haze_color(v, vfs, info->haze, s->fog_color)) {
        const bs_bsi_image *sky = info->sky[0] ? bs_assets_image(assets, info->sky) : NULL;
        if (sky && sky->width && sky->height && sky->pixels)
            horizon_color(sky, s->fog_color);
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
    float scale = fog_scale > 0.0f ? fog_scale : (gradual ? 3.0f : 1.0f);
    s->fog_end = view * scale;
    s->fog_start = (gradual ? 8.0f : view - 512.0f) * scale;
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
    s->ambient = (float)amb / 512.0f / 31.0f;
}

bs_result bs_level_view_load(bs_level_view *v, bs_assets *assets, const bs_vfs *vfs, const char *file,
                             const bs_level_info *info, float fog_scale, bs_render_settings *settings)
{
    bs_level_view_unload(v);
    snprintf(v->file, sizeof v->file, "%s", file);
    v->info = info;
    bs_arena_init(&v->arena, 0);
    uint8_t *data;
    size_t size;
    bs_result r = bs_assets_read(assets, file, &v->arena, &data, &size);
    if (r == BS_OK)
        r = bs_bs6_parse(data, size, &v->arena, &v->level);
    /* Objects with controllers or links get their own geometry so they can move and be used. */
    bool *separate = NULL;
    if (r == BS_OK) {
        separate = BS_ARENA_ARRAY(&v->arena, bool, v->level.object_count + 2);
        r = separate ? BS_OK : BS_ERR_NOMEM;
    }
    for (uint32_t i = 0; r == BS_OK && i < v->level.object_count; i++) {
        bs_bs6_logic g;
        r = bs_bs6_entity_logic(&v->level.objects[i].chunk, &v->arena, &g);
        separate[i] = r == BS_OK && (g.ctrl_count || g.link_count);
    }
    /* The jump pointer (jumper.3d, loaded by the game at 0x1087B) as one more,
     * hidden, separate object after the level's own. */
    bs_bs6_level drawn = v->level;
    v->jump_pointer = UINT32_MAX;
    if (r == BS_OK && bs_assets_model(assets, "jumper")) {
        bs_bs6_object *objs = BS_ARENA_ARRAY(&v->arena, bs_bs6_object, v->level.object_count + 1);
        const char **names = BS_ARENA_ARRAY(&v->arena, const char *, v->level.model_count + 1);
        if (objs && names) {
            memcpy(objs, v->level.objects, sizeof *objs * v->level.object_count);
            memcpy(names, v->level.model_names, sizeof *names * v->level.model_count);
            names[v->level.model_count] = "jumper";
            memset(&objs[v->level.object_count], 0, sizeof *objs);
            objs[v->level.object_count].id = UINT32_MAX;
            objs[v->level.object_count].model = v->level.model_count;
            drawn.objects = objs;
            drawn.object_count++;
            drawn.model_names = names;
            drawn.model_count++;
            separate[v->level.object_count] = true;
            v->jump_pointer = v->level.object_count;
        }
    }
    if (r == BS_OK)
        r = bs_scene_build_ex(assets, &drawn, separate, &v->arena, &v->scene);
    const bs_bsi_image *sky = info && info->sky[0] ? bs_assets_image(assets, info->sky) : NULL;
    if (r == BS_OK)
        r = bs_level_renderer_create(&v->scene, &drawn, sky, &v->renderer);
    if (r == BS_OK && v->jump_pointer != UINT32_MAX)
        bs_level_renderer_show(v->renderer, v->jump_pointer, false);
    if (r != BS_OK) {
        bs_arena_free(&v->arena);
        memset(v, 0, sizeof *v);
        return r;
    }
    v->loaded = true;
    apply_settings(v, assets, vfs, fog_scale, settings);
    /* Water: LEVELS.TXT mode[1] 'Y' loads water2.bsi (RE: 0x262AC); the height is the level's WATR. */
    if (info && info->mode[1] == 'Y')
        bs_level_renderer_set_water(v->renderer, bs_assets_image(assets, "water2.bsi"), (float)v->level.water);
    return BS_OK;
}

void bs_level_view_unload(bs_level_view *v)
{
    if (!v->loaded)
        return;
    bs_level_renderer_destroy(v->renderer);
    bs_arena_free(&v->arena);
    memset(v, 0, sizeof *v);
}

void bs_level_view_place_camera(const bs_level_view *v, int view, bs_camera *cam)
{
    const bs_bs6_level *lvl = &v->level;
    cam->pitch = 0;
    cam->yaw = 0;
    if (view >= 0 && (uint32_t)view < lvl->view_count) {
        for (int c = 0; c < 3; c++)
            cam->pos[c] = (float)lvl->views[view].pos[c];
        return;
    }
    for (uint32_t i = 0; i < lvl->flat_count; i++) {
        if (bs_strcasecmp(lvl->flats[i].name, "start") == 0) {
            for (int c = 0; c < 3; c++)
                cam->pos[c] = (float)lvl->flats[i].pos[c];
            return;
        }
    }
    /* No start marker: the middle of the level, a little above the floor. */
    for (int c = 0; c < 3; c++)
        cam->pos[c] = (v->scene.min[c] + v->scene.max[c]) * 0.5f;
}

void bs_camera_fly(bs_camera *cam, float forward, float right, float up, float distance)
{
    float cy = cosf(cam->yaw), sy = sinf(cam->yaw), cp = cosf(cam->pitch), sp = sinf(cam->pitch);
    float fwd[3] = {sy * cp, -sp, cy * cp};
    float side[3] = {cy, 0, -sy};
    for (int c = 0; c < 3; c++)
        cam->pos[c] += (fwd[c] * forward + side[c] * right) * distance;
    cam->pos[1] -= up * distance; /* up is -y */
}
