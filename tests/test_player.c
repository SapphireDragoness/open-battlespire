/* The player controller on a small synthetic level, against the decoded rules. */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "bs/game/player.h"
#include "bs_test.h"

#define MAXV 800
static bs_scene_vertex verts[MAXV];
static uint32_t nv;
static bs_scene_batch batch;
static bs_scene scene;
static bs_bs6_level level;
static bs_arena arena;
static bs_collision_world world;
static const bs_player_stats stats = {50, 50, 0, false};

static void tri(const float a[3], const float b[3], const float c[3])
{
    const float *p[3] = {a, b, c};
    for (int i = 0; i < 3; i++) {
        memset(&verts[nv], 0, sizeof verts[nv]);
        memcpy(verts[nv++].pos, p[i], 12);
    }
}

static void quad(const float a[3], const float b[3], const float c[3], const float d[3])
{
    tri(a, b, c);
    tri(a, c, d);
}

/* A box from (x0, top, z0) to (x1, 0, z1); up is -y. */
static void box(float x0, float z0, float x1, float z1, float top)
{
    const float t00[3] = {x0, top, z0}, t10[3] = {x1, top, z0}, t11[3] = {x1, top, z1}, t01[3] = {x0, top, z1};
    const float b00[3] = {x0, 0, z0}, b10[3] = {x1, 0, z0}, b11[3] = {x1, 0, z1}, b01[3] = {x0, 0, z1};
    quad(t00, t10, t11, t01);
    quad(b00, b10, t10, t00);
    quad(b10, b11, t11, t10);
    quad(b11, b01, t01, t11);
    quad(b01, b00, t00, t01);
}

static void setup(void)
{
    nv = 0;
    static const float f0[3] = {-2000, 0, -2000}, f1[3] = {2000, 0, -2000}, f2[3] = {2000, 0, 2000},
                       f3[3] = {-2000, 0, 2000};
    quad(f0, f1, f2, f3);                       /* floor at y = 0 */
    /* A raised deck at y = -100 over x 1000..1400 with a 20-unit gap (z 0..20)
     * and a 60-unit gap (z 400..460), like planks on a bridge. */
    static const float d0[3] = {1000, -100, -400}, d1[3] = {1400, -100, -400}, d2[3] = {1400, -100, 0},
                       d3[3] = {1000, -100, 0};
    static const float e0[3] = {1000, -100, 20}, e1[3] = {1400, -100, 20}, e2[3] = {1400, -100, 400},
                       e3[3] = {1000, -100, 400};
    static const float g0[3] = {1000, -100, 460}, g1[3] = {1400, -100, 460}, g2[3] = {1400, -100, 800},
                       g3[3] = {1000, -100, 800};
    quad(d0, d1, d2, d3);
    quad(e0, e1, e2, e3);
    quad(g0, g1, g2, g3);
    static const float w0[3] = {100, 0, -2000}, w1[3] = {100, -300, -2000}, w2[3] = {100, -300, 2000},
                       w3[3] = {100, 0, 2000};
    quad(w0, w1, w2, w3);                       /* wall at x = 100 */
    box(-400, -100, -200, 100, -20);            /* a 20-unit step to the west */
    box(-100, -600, 0, -400, -40);              /* a 40-unit block to the south-west */
    batch = (bs_scene_batch){0, 0, nv, -1};
    scene = (bs_scene){.vertices = verts, .vertex_count = nv, .batches = &batch, .batch_count = 1,
                       .static_batch_count = 1};
    level = (bs_bs6_level){0};
    bs_arena_free(&arena);
    bs_arena_init(&arena, 0);
    BS_CHECK_EQ_INT(bs_collision_build(&scene, &level, &arena, &world), BS_OK);
}

static void run(bs_player *p, const bs_player_input *in, int frames, bs_player_events *last)
{
    for (int i = 0; i < frames; i++)
        bs_player_move(p, &stats, &world, in, 20, last);
}

/* Dropped from 100 up, the player falls under 392 units/s^2 and lands on the floor. */
static void test_fall(void)
{
    setup();
    bs_player p;
    bs_player_init(&p, (const float[3]){0, -100, 0}, 0);
    bs_player_input in = {0};
    bs_player_events ev;
    int frames = 0;
    do {
        bs_player_move(&p, &stats, &world, &in, 20, &ev);
        frames++;
    } while (!ev.landed && frames < 200);
    BS_CHECK(ev.landed && p.on_ground && fabsf(p.pos[1]) < 0.01f);
    /* sqrt(2 * 100 / 392) = 0.71 s; the probe catches the floor up to 25 early. */
    BS_CHECK(frames * 20 > 550 && frames * 20 < 800);
    float eye[3];
    bs_player_eye(&p, eye);
    BS_CHECK(fabsf(eye[1] + 72) < 0.01f);
}

/* Speed 50 walks 213 units a second (128 + 50 * 435 / 256). */
static void test_walk_speed(void)
{
    setup();
    bs_player p;
    bs_player_init(&p, (const float[3]){-1000, 0, 1000}, 0); /* facing +z, open floor */
    bs_player_input in = {0};
    bs_player_events ev;
    run(&p, &in, 1, &ev); /* players start in the air: land first */
    in.forward = 1;
    run(&p, &in, 50, &ev);
    BS_CHECK(fabsf(p.pos[2] - 1000 - 213) < 2 && fabsf(p.pos[1]) < 0.01f && p.on_ground);
}

/* A wall stops the body (radius up to 16) and the body slides along it. */
static void test_wall(void)
{
    setup();
    bs_player p;
    bs_player_init(&p, (const float[3]){0, 0, 1000}, 1.5707963f); /* facing +x */
    bs_player_input in = {.forward = 1};
    bs_player_events ev;
    run(&p, &in, 100, &ev);
    BS_CHECK(p.pos[0] < 100 - 15 && p.pos[0] > 100 - 30);
    BS_CHECK(fabsf(p.pos[2] - 1000) < 1);
    /* At 45 degrees into the wall it slides north. */
    p.yaw = 0.785398f;
    float z0 = p.pos[2];
    run(&p, &in, 50, &ev);
    BS_CHECK(p.pos[0] < 100 - 15 && p.pos[2] > z0 + 100);
}

/* 20 units is a step; 40 is a wall. */
static void test_steps(void)
{
    setup();
    bs_player p;
    bs_player_init(&p, (const float[3]){-100, 0, 0}, -1.5707963f); /* facing -x */
    bs_player_input in = {.forward = 1};
    bs_player_events ev;
    run(&p, &in, 50, &ev);
    BS_CHECK(p.pos[0] < -250 && fabsf(p.pos[1] + 20) < 0.01f && p.on_ground);
    bs_player_init(&p, (const float[3]){-50, 0, -200}, 3.14159265f); /* facing -z, towards the block */
    run(&p, &in, 50, &ev);
    /* The 40-unit block only reaches the middle sphere (41 / radius 11). */
    BS_CHECK(p.pos[2] > -400 + 10 && p.pos[2] < -400 + 14 && fabsf(p.pos[1]) < 0.01f);
}

/* A standing jump rises about 37 units with 50/50 and comes back down. */
static void test_jump(void)
{
    setup();
    bs_player p;
    bs_player_init(&p, (const float[3]){-1000, 0, -1000}, 0);
    bs_player_input in = {0};
    bs_player_events ev;
    bs_player_move(&p, &stats, &world, &in, 20, &ev); /* settle */
    in.jump = true;  /* a tap: charge one frame, then release */
    bs_player_move(&p, &stats, &world, &in, 20, &ev);
    BS_CHECK(p.charging && !ev.jumped);
    in.jump = false;
    bs_player_move(&p, &stats, &world, &in, 20, &ev);
    BS_CHECK(ev.jumped && !p.on_ground);
    float top = 0;
    int frames = 0;
    do {
        bs_player_move(&p, &stats, &world, &in, 10, &ev);
        top = fminf(top, p.pos[1]);
        frames++;
    } while (!p.on_ground && frames < 500);
    BS_CHECK(p.on_ground && top < -30 && top > -40);
    BS_CHECK(fabsf(p.pos[0] + 1000) < 0.01f && fabsf(p.pos[2] + 1000) < 5); /* barely forward */
}

/* A wall blocks a forward jump too. */
static void test_jump_forward(void)
{
    setup();
    bs_player p;
    bs_player_init(&p, (const float[3]){0, 0, 1500}, 1.5707963f);
    bs_player_input in = {.forward = 1};
    bs_player_events ev;
    run(&p, &in, 2, &ev);
    in.jump = true;
    run(&p, &in, 25, &ev); /* charge while running at the wall */
    in.jump = false;
    run(&p, &in, 60, &ev);
    BS_CHECK(p.on_ground && p.pos[0] < 100 - 15);
}

/* A full standing charge (2 s) jumps about the distance the pointer predicts. */
static void test_charged_jump(void)
{
    setup();
    bs_player p;
    bs_player_init(&p, (const float[3]){-1000, 0, -1500}, 0);
    bs_player_input in = {0};
    bs_player_events ev;
    run(&p, &in, 2, &ev);
    in.jump = true;
    run(&p, &in, 110, &ev);
    BS_CHECK(p.charging && p.charge_ms == 2000);
    float predicted = p.jump_distance, target[3];
    BS_CHECK(bs_player_jump_target(&p, target) && fabsf(target[2] - (-1500 + predicted)) < 0.5f &&
             fabsf(target[1] + 50) < 0.5f);
    /* 350 * (170 * 256 / 196) * 2000 / 2000 / 256 = 303 */
    BS_CHECK(predicted > 295 && predicted < 310);
    in.jump = false;
    int frames = 0;
    do {
        bs_player_move(&p, &stats, &world, &in, 10, &ev);
        frames++;
    } while ((frames < 2 || !p.on_ground) && frames < 500);
    float flown = p.pos[2] + 1500;
    BS_CHECK(fabsf(flown - predicted) < 0.1f * predicted);
}

/* Strafing is base + 100, backing up (base + 200) / 2: 185 and 142 with speed 50. */
static void test_side_and_back(void)
{
    setup();
    bs_player p;
    bs_player_init(&p, (const float[3]){-1000, 0, 1000}, 0);
    bs_player_input in = {0};
    bs_player_events ev;
    run(&p, &in, 1, &ev);
    in.strafe = 1;
    run(&p, &in, 50, &ev);
    BS_CHECK(fabsf(p.pos[0] + 1000 - 185) < 2);
    in.strafe = 0;
    in.forward = -1;
    float z0 = p.pos[2];
    run(&p, &in, 50, &ev);
    BS_CHECK(fabsf(z0 - p.pos[2] - 142) < 2);
}

/* Small gaps between planks hold the player (the ground sphere at the feet); wide ones don't. */
static void test_gaps(void)
{
    setup();
    bs_player p;
    bs_player_init(&p, (const float[3]){1200, -100, 10}, 0); /* over the 20-unit gap */
    bs_player_input in = {0};
    bs_player_events ev;
    run(&p, &in, 50, &ev);
    BS_CHECK(p.on_ground && fabsf(p.pos[1] + 100) < 0.5f);
    bs_player_init(&p, (const float[3]){1200, -100, 430}, 0); /* over the 60-unit gap */
    run(&p, &in, 100, &ev);
    BS_CHECK(p.on_ground && fabsf(p.pos[1]) < 0.5f); /* fell to the floor below */
}

int main(void)
{
    bs_arena_init(&arena, 0);
    BS_RUN(test_fall);
    BS_RUN(test_walk_speed);
    BS_RUN(test_wall);
    BS_RUN(test_steps);
    BS_RUN(test_jump);
    BS_RUN(test_jump_forward);
    BS_RUN(test_charged_jump);
    BS_RUN(test_side_and_back);
    BS_RUN(test_gaps);
    bs_arena_free(&arena);
    return BS_TEST_RESULT();
}
