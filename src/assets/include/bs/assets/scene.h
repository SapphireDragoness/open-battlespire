#ifndef BS_ASSETS_SCENE_H
#define BS_ASSETS_SCENE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bs/assets/assets.h"
#include "bs/core/arena.h"
#include "bs/core/result.h"
#include "bs/formats/bs6.h"
#include "bs/formats/bsi.h"

/*
 * A level's static geometry as world-space triangles batched by material,
 * plus its flats. Shared by the renderer and the glTF exporter.
 *
 * Coordinates are level units in the game's own axes (+y down). Texture
 * coordinates are normalised to the material's image (1.0 = one repeat).
 */

typedef struct bs_scene_vertex {
    float pos[3];
    float normal[3];
    float uv[2];
    float face_byte;  /* the model face's header byte (0..63): self-illumination level? (inferred) */
} bs_scene_vertex;

typedef enum bs_scene_material_kind {
    BS_SCENE_MAT_TEXTURE,
    BS_SCENE_MAT_COLOR,
    BS_SCENE_MAT_MISSING  /* texture name that doesn't resolve */
} bs_scene_material_kind;

typedef struct bs_scene_material {
    bs_scene_material_kind kind;
    char name[BS_MODEL_TEX_NAME_MAX];  /* texture name ("" for colours) */
    const bs_bsi_image *image;         /* BS_SCENE_MAT_TEXTURE */
    uint8_t rgb[3];                    /* BS_SCENE_MAT_COLOR */
} bs_scene_material;

/* Triangles of one material: vertices[first .. first + count). */
typedef struct bs_scene_batch {
    uint32_t material;
    uint32_t first;
    uint32_t count;
    int32_t object;   /* -1: static, in level coordinates; else a separate object
                         (level object index), in its model space (units / 256) */
} bs_scene_batch;

typedef struct bs_scene_sprite {
    const char *name;            /* the flat's FILN */
    const bs_bsi_image *image;   /* NULL if the name doesn't resolve */
    float pos[3];                /* the flat's position */
    float center[3];             /* centre of the drawn sprite */
    float size[2];               /* world width and height (level units) */
    bool marker;                 /* editor/logic marker, not drawn in game (inferred) */
} bs_scene_sprite;

typedef struct bs_scene {
    bs_scene_vertex *vertices;
    uint32_t vertex_count;
    bs_scene_material *materials;
    uint32_t material_count;
    bs_scene_batch *batches;     /* static: one per material, in material order; then the
                                    separate objects' batches, grouped by object */
    uint32_t batch_count;
    uint32_t static_batch_count;
    bs_scene_sprite *sprites;
    uint32_t sprite_count;
    float min[3], max[3];        /* bounds of the geometry */
    uint32_t missing_models;     /* objects whose model didn't load */
} bs_scene;

bs_result bs_scene_build(bs_assets *assets, const bs_bs6_level *level, bs_arena *arena, bs_scene *out);

/* As bs_scene_build, but objects with separate[i] set get their own batches
 * in model space, so they can be moved (and picked) on their own. */
bs_result bs_scene_build_ex(bs_assets *assets, const bs_bs6_level *level, const bool *separate, bs_arena *arena,
                            bs_scene *out);

/* An object placement as a transform: rotation (row-major 3x3) then translation. */
typedef struct bs_scene_xform {
    float rot[9];
    float pos[3];
} bs_scene_xform;

bs_scene_xform bs_scene_object_xform(const int32_t angles[3], const float pos[3]);

/*
 * Casts a ray (origin, unit direction) against the scene's triangles up to
 * max_dist, ignoring separate objects beyond the level's own. Separate objects are placed by xforms[object] (indexed by level
 * object; NULL uses their level placement from `level`). Returns the hit
 * distance, or a negative value; *object is the separate object hit or -1.
 */
float bs_scene_raycast(const bs_scene *s, const bs_bs6_level *level, const bs_scene_xform *xforms,
                       const float origin[3], const float dir[3], float max_dist, int32_t *object);

/* Transforms a model point by an object's placement into level units. */
void bs_scene_transform(const float rot[9], const int32_t pos[3], const bs_model_point *p, float out[3]);

#endif
