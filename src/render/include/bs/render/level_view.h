#ifndef BS_RENDER_LEVEL_VIEW_H
#define BS_RENDER_LEVEL_VIEW_H

#include <stdbool.h>

#include "bs/assets/assets.h"
#include "bs/assets/scene.h"
#include "bs/core/vfs.h"
#include "bs/formats/bs6.h"
#include "bs/formats/levels.h"
#include "bs/render/level_renderer.h"

/*
 * A level loaded for display: its BS6 data, scene and renderer, with fog,
 * ambient light and water set up from its LEVELS.TXT entry as GAME.EXE does.
 */

typedef struct bs_level_view {
    char file[32];
    const bs_level_info *info;  /* NULL if the level isn't in LEVELS.TXT */
    bs_arena arena;
    bs_bs6_level level;
    bs_scene scene;
    bs_level_renderer *renderer;
    uint32_t jump_pointer;      /* renderer object of the jump pointer (jumper.3d), or UINT32_MAX */
    bool loaded;
} bs_level_view;

/*
 * Loads file (e.g. "l1.bs6") and sets the fog, ambient light and fog colour in
 * *settings. info may be NULL. fog_scale multiplies the fog distances; 0 uses
 * the default (see level_view.c). Unloads any level loaded before; v must
 * start zeroed.
 */
bs_result bs_level_view_load(bs_level_view *v, bs_assets *assets, const bs_vfs *vfs, const char *file,
                             const bs_level_info *info, float fog_scale, bs_render_settings *settings);
void bs_level_view_unload(bs_level_view *v);

/* Puts the camera at viewpoint `view`, or with view < 0 (or out of range) at the
 * START marker (the level's middle if there is none), level with the horizon. */
void bs_level_view_place_camera(const bs_level_view *v, int view, bs_camera *cam);

/* Moves the camera along its view direction, sideways and vertically (-y is up), by distance units. */
void bs_camera_fly(bs_camera *cam, float forward, float right, float up, float distance);

#endif
