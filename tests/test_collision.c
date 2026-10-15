/* Collision world: spheres and segments against static triangles and a movable object. */

#include <math.h>
#include <string.h>

#include "bs/game/collision.h"
#include "bs_test.h"

static bs_scene_vertex verts[12 * 3];
static bs_scene_batch batches[2];
static bs_scene scene;
static bs_bs6_object objects[1];
static bs_bs6_level level;

static void quad(bs_scene_vertex *v, const float a[3], const float b[3], const float c[3], const float d[3])
{
    const float *p[6] = {a, b, c, a, c, d};
    for (int i = 0; i < 6; i++) {
        memset(&v[i], 0, sizeof v[i]);
        memcpy(v[i].pos, p[i], 12);
    }
}

static void setup(bs_arena *a, bs_collision_world *w)
{
    /* Static: a floor at y = 0 and a wall at x = 100 rising to y = -300 (up is -y). */
    static const float f0[3] = {-1000, 0, -1000}, f1[3] = {1000, 0, -1000}, f2[3] = {1000, 0, 1000},
                       f3[3] = {-1000, 0, 1000};
    static const float w0[3] = {100, 0, -1000}, w1[3] = {100, -300, -1000}, w2[3] = {100, -300, 1000},
                       w3[3] = {100, 0, 1000};
    quad(&verts[0], f0, f1, f2, f3);
    quad(&verts[6], w0, w1, w2, w3);
    /* Object 0: a 100 x 100 platform in model space at its origin. */
    static const float p0[3] = {-50, 0, -50}, p1[3] = {50, 0, -50}, p2[3] = {50, 0, 50}, p3[3] = {-50, 0, 50};
    quad(&verts[12], p0, p1, p2, p3);
    batches[0] = (bs_scene_batch){0, 0, 12, -1};
    batches[1] = (bs_scene_batch){3, 12, 6, 0};
    scene = (bs_scene){.vertices = verts, .vertex_count = 18, .batches = batches, .batch_count = 2,
                       .static_batch_count = 1};
    objects[0] = (bs_bs6_object){.id = 1, .pos = {500, -20, 0}};
    level = (bs_bs6_level){.objects = objects, .object_count = 1};
    bs_arena_init(a, 0);
    BS_CHECK_EQ_INT(bs_collision_build(&scene, &level, a, w), BS_OK);
}

static bool near(float a, float b)
{
    return fabsf(a - b) < 1e-3f;
}

static void test_sphere(void)
{
    bs_arena a;
    bs_collision_world w;
    setup(&a, &w);
    BS_CHECK_EQ_INT(w.static_count, 4);
    BS_CHECK_EQ_INT(w.tri_count, 6);
    bs_collision_hit h[8];
    BS_CHECK_EQ_INT(bs_collision_sphere(&w, (const float[3]){0, -34, 0}, 7, h, 8), 0);
    BS_CHECK_EQ_INT(bs_collision_sphere(&w, (const float[3]){10, -5, -300}, 7, h, 8), 1); /* off the diagonal */
    BS_CHECK(near(h[0].normal[1], -1) && near(h[0].depth, 2) && h[0].object == -1);
    /* Against the wall: pushed back towards -x. */
    BS_CHECK_EQ_INT(bs_collision_sphere(&w, (const float[3]){95, -40, 0}, 7, h, 8), 1);
    BS_CHECK(near(h[0].normal[0], -1) && near(h[0].depth, 2));
    /* Both: in the corner. */
    BS_CHECK_EQ_INT(bs_collision_sphere(&w, (const float[3]){95, -5, 0}, 7, h, 8), 2);
    /* On the object, which is 20 up at x = 500. */
    BS_CHECK_EQ_INT(bs_collision_sphere(&w, (const float[3]){500, -25, 10}, 7, h, 8), 1);
    BS_CHECK(h[0].object == 0 && near(h[0].normal[1], -1) && near(h[0].depth, 2));
    bs_arena_free(&a);
}

static void test_segment(void)
{
    bs_arena a;
    bs_collision_world w;
    setup(&a, &w);
    float t;
    int32_t ob;
    /* The floor probe: 32 above the feet to 200 below. */
    BS_CHECK(bs_collision_segment(&w, (const float[3]){0, -32, 0}, (const float[3]){0, 200, 0}, &t, &ob));
    BS_CHECK(near(t, 32.0f / 232.0f) && ob == -1);
    BS_CHECK(!bs_collision_segment(&w, (const float[3]){0, -32, 0}, (const float[3]){0, -10, 0}, &t, &ob));
    /* The platform is found before the floor under it, with its material. */
    uint32_t mat;
    BS_CHECK(bs_collision_segment_material(&w, (const float[3]){500, -52, 0}, (const float[3]){500, 200, 0}, &t, &ob,
                                           &mat));
    BS_CHECK(ob == 0 && near(t, 32.0f / 252.0f) && mat == 3);
    BS_CHECK(bs_collision_segment_material(&w, (const float[3]){0, -32, 0}, (const float[3]){0, 200, 0}, &t, &ob, &mat));
    BS_CHECK(ob == -1 && mat == 0);
    /* Moved up by 40: the probe now starts below it and finds the floor. */
    bs_collision_place(&w, 0, (const float[3]){500, -60, 0}, (const int32_t[3]){0, 0, 0});
    BS_CHECK(bs_collision_segment(&w, (const float[3]){500, -52, 0}, (const float[3]){500, 200, 0}, &t, &ob));
    BS_CHECK(ob == -1 && near(t, 52.0f / 252.0f));
    /* Turned a quarter about x: the platform stands on edge across z = 0. */
    bs_collision_place(&w, 0, (const float[3]){500, -60, 0}, (const int32_t[3]){512, 0, 0});
    BS_CHECK(bs_collision_segment(&w, (const float[3]){500, -60, -100}, (const float[3]){500, -60, 100}, &t, &ob));
    BS_CHECK(ob == 0 && near(t, 0.5f));
    /* The wall, crossed horizontally. */
    BS_CHECK(bs_collision_segment(&w, (const float[3]){50, -30, 0}, (const float[3]){150, -30, 0}, &t, &ob));
    BS_CHECK(near(t, 0.5f) && ob == -1);
    /* Removed (an item taken): neither probe nor body finds it any more. */
    bs_collision_remove(&w, 0);
    BS_CHECK(bs_collision_segment(&w, (const float[3]){500, -60, -100}, (const float[3]){500, -60, 100}, &t, &ob) ==
             false);
    bs_collision_hit h[4];
    BS_CHECK_EQ_INT(bs_collision_sphere(&w, (const float[3]){500, -60, 0}, 7, h, 4), 0);
    bs_collision_place(&w, 0, (const float[3]){500, -60, 0}, (const int32_t[3]){0, 0, 0}); /* no effect */
    BS_CHECK_EQ_INT(bs_collision_sphere(&w, (const float[3]){500, -60, 0}, 7, h, 4), 0);
    bs_arena_free(&a);
}

int main(void)
{
    BS_RUN(test_sphere);
    BS_RUN(test_segment);
    return BS_TEST_RESULT();
}
