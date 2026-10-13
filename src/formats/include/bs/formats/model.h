#ifndef BS_FORMATS_MODEL_H
#define BS_FORMATS_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"

/*
 * 3D models (*.3D in 3D.BSA and loose in GAMEDATA/), versions "v2.7" and
 * "v2.6". Daggerfall's model format with a 10-byte face header. See
 * docs/formats/3d.md.
 *
 *   char version[4]          "v2.7" or "v2.6"
 *   i32 point_count
 *   i32 face_count
 *   u32 radius
 *   ...                      offsets of the sections below (64-byte header)
 *   points                   point_count x { i32 x, y, z }
 *   normals                  face_count x { i32 x, y, z }, 256 = 1.0
 *   faces                    face_count x {
 *                                u8 vertex_count; u8 light; u32 texture; u32 zero;
 *                                vertex_count x { u32 point_offset; i16 u; i16 v; }
 *                            }
 *
 * Coordinates are in model units, BS_MODEL_UNITS_PER_WORLD per level unit,
 * with +y pointing down. Faces wind counter-clockwise around their normal
 * (the normal follows the right-hand rule).
 */

#define BS_MODEL_UNITS_PER_WORLD 256  /* inferred: makes placed models meet in levels */
#define BS_MODEL_NORMAL_ONE 256

/* Face texture values. */
#define BS_MODEL_TEX_INVISIBLE 0xFFFFFFFEu  /* face is not drawn (collision or trigger only) */
#define BS_MODEL_TEX_NAME_MAX 7             /* 6 characters + NUL */

typedef enum bs_model_tex_kind {
    BS_MODEL_TEX_IMAGE,  /* texture = a BSI name packed in base 40 */
    BS_MODEL_TEX_COLOR,  /* 0xFFFFxxxx: solid colour, low 16 bits encoded like BSI HICL (inferred) */
    BS_MODEL_TEX_NONE    /* BS_MODEL_TEX_INVISIBLE, or a value not understood */
} bs_model_tex_kind;

typedef struct bs_model_point {
    int32_t x, y, z;
} bs_model_point;

typedef struct bs_model_vertex {
    uint32_t point;  /* index into points */
    int16_t u, v;    /* as stored; use bs_model_face_uvs() */
} bs_model_vertex;

typedef struct bs_model_face {
    uint8_t vertex_count;
    uint8_t light;          /* self-illumination in half shade levels, added to ambient and lights */
    uint32_t texture;
    uint32_t first_vertex;  /* index into vertices */
    int32_t normal[3];      /* BS_MODEL_NORMAL_ONE = 1.0 */
} bs_model_face;

typedef struct bs_model {
    char version[5];
    uint32_t radius;
    uint32_t point_count;
    bs_model_point *points;
    uint32_t face_count;
    bs_model_face *faces;
    uint32_t vertex_count;
    bs_model_vertex *vertices;
} bs_model;

bs_result bs_model_parse(const uint8_t *data, size_t size, bs_arena *arena, bs_model *out);

bs_model_tex_kind bs_model_texture_kind(uint32_t texture);

/*
 * Decodes a BS_MODEL_TEX_IMAGE value into the BSI name without extension
 * (e.g. "WALL96"). Returns false if the value isn't a valid packed name.
 */
bool bs_model_texture_name(uint32_t texture, char out[BS_MODEL_TEX_NAME_MAX]);

/*
 * Texture coordinates of a face in texels (u right, v down in the image),
 * one pair per vertex. Only the first three vertices store usable values:
 * the first is absolute, the second and third are deltas from the previous
 * vertex. The rest are derived from the face plane.
 */
void bs_model_face_uvs(const bs_model *m, const bs_model_face *f, float (*uv)[2]);

#endif
