#ifndef BS_RENDER_LEVEL_RENDERER_H
#define BS_RENDER_LEVEL_RENDERER_H

#include <stdbool.h>

#include "bs/assets/scene.h"
#include "bs/core/result.h"
#include "bs/formats/bs6.h"
#include "bs/formats/bsi.h"

/*
 * Draws a level scene with OpenGL 3.3 (bs_gl_load() must have succeeded).
 *
 * Lighting follows the rule measured from the original (docs/re/README.md):
 * per pixel, face light byte plus front-facing lights falling off as
 * 1 - d^2/R^2, plus the player's light. Fog is linear from the level's fog
 * start to its view distance. The sky mapping is still an approximation.
 *
 * "Classic" mode renders at 320x240, as the game does (it doubles each pixel
 * to fill 640x480), with nearest-neighbour textures and light quantised to the
 * 32 shade levels, then scales up; "enhanced" renders at full resolution with
 * filtered textures and smooth light.
 */

typedef struct bs_camera {
    float pos[3];  /* level units, game axes (+y down) */
    float yaw;     /* radians; 0 looks along +z (north), positive turns towards +x */
    float pitch;   /* radians; positive looks up */
    float fov_y;   /* radians */
} bs_camera;

typedef struct bs_render_settings {
    bool classic;
    bool lighting;
    bool fog;
    bool sky;
    bool sprites;
    bool markers;      /* editor markers (spawn points etc.) */
    bool cull;         /* back-face culling */
    float ambient;     /* added to the light, as a fraction of full; the game seems to use none */
    bool player_light; /* the light the player carries (40 levels, radius 384) */
    bool vertex_lighting; /* compute light per vertex (Gouraud) instead of per pixel */
    float fog_start, fog_end;  /* level units */
    float fog_color[3];
    int debug_view;    /* 0 normal; 1 surface normals as colour (n * 0.5 + 0.5, game axes) */
} bs_render_settings;

typedef struct bs_level_renderer bs_level_renderer;

/* sky may be NULL. The scene and images must stay alive while the renderer exists. */
bs_result bs_level_renderer_create(const bs_scene *scene, const bs_bs6_level *level, const bs_bsi_image *sky,
                                   bs_level_renderer **out);
void bs_level_renderer_destroy(bs_level_renderer *r);

/*
 * Water plane at a level height (+y down), as the game draws for levels whose
 * LEVELS.TXT mode[1] is 'Y': the BS6 WATR value, textured with water2.bsi.
 * NULL image removes it. Texture scale (one texel per unit) is a guess, and
 * the game's water colour treatment (likely WATER.001) isn't applied yet.
 */
bs_result bs_level_renderer_set_water(bs_level_renderer *r, const bs_bsi_image *image, float height);

/* Draws into the default framebuffer of size width x height. time: seconds, for animation. */
void bs_level_renderer_draw(bs_level_renderer *r, const bs_camera *cam, const bs_render_settings *s, int width,
                            int height, double time);

/* Moves a separate object (see bs_scene_build_ex) to a new placement. */
void bs_level_renderer_place(bs_level_renderer *r, uint32_t object, const float pos[3], const int32_t angles[3]);

/* Shows or hides a separate object. */
void bs_level_renderer_show(bs_level_renderer *r, uint32_t object, bool visible);

/* Current placements, indexed by level object (for bs_scene_raycast). */
const bs_scene_xform *bs_level_renderer_xforms(const bs_level_renderer *r);

/* Default settings. */
bs_render_settings bs_render_settings_default(void);

#endif
