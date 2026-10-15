#ifndef BS_GAME_COLLISION_H
#define BS_GAME_COLLISION_H

#include <stdbool.h>
#include <stdint.h>

#include "bs/assets/scene.h"
#include "bs/core/arena.h"
#include "bs/core/result.h"

/*
 * A level's triangles for collision: the static geometry in an x/z grid, and
 * the separate (movable) objects in their own model space, placed by their
 * current transforms. Queries are spheres (what the player's body touches)
 * and segments (floor and ceiling probes). Triangles count from both sides.
 */

typedef struct bs_collision_tri {
    float v[3][3];
    float n[3];        /* unit face normal (model's winding) */
    uint32_t material; /* the scene material */
} bs_collision_tri;

typedef struct bs_collision_object {
    uint32_t first, count;   /* triangles, in model space */
    float radius;            /* bounding sphere around the model origin */
    bs_scene_xform xform;
    bool present;
} bs_collision_object;

typedef struct bs_collision_world {
    bs_collision_tri *tris;  /* static (level coordinates), then the objects' (model space) */
    uint32_t tri_count, static_count;
    float origin[2];         /* grid corner (x, z) */
    float cell;
    uint32_t cols, rows;
    uint32_t *cell_first;    /* cols * rows + 1 offsets into cell_tris */
    uint32_t *cell_tris;
    bs_collision_object *objects;  /* indexed by level object */
    uint32_t object_count;
} bs_collision_world;

typedef struct bs_collision_hit {
    float normal[3];   /* from the triangle towards the query, unit length */
    float depth;       /* how far the sphere reaches into the triangle */
    int32_t object;    /* level object, or -1 for static geometry */
} bs_collision_hit;

/* Builds from a scene (bs_scene_build_ex). object_count is the level's. */
bs_result bs_collision_build(const bs_scene *scene, const bs_bs6_level *level, bs_arena *arena,
                             bs_collision_world *out);

/* Moves a separate object. */
void bs_collision_place(bs_collision_world *w, uint32_t object, const float pos[3], const int32_t angles[3]);

/* Takes a separate object out of the world (an item picked up). */
void bs_collision_remove(bs_collision_world *w, uint32_t object);

/* Triangles a sphere overlaps; returns how many (at most max are stored). */
uint32_t bs_collision_sphere(const bs_collision_world *w, const float center[3], float radius,
                             bs_collision_hit *hits, uint32_t max);

/* The first triangle a segment crosses: returns false if none. *t is the
 * fraction along the segment, *object the level object or -1. */
bool bs_collision_segment(const bs_collision_world *w, const float from[3], const float to[3], float *t,
                          int32_t *object);

/* As bs_collision_segment, also giving the scene material of the triangle crossed (the game's floor face,
 * 0x3DADEC, whose texture some level exits look for). */
bool bs_collision_segment_material(const bs_collision_world *w, const float from[3], const float to[3], float *t,
                                   int32_t *object, uint32_t *material);

#endif
