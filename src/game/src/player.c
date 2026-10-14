#include "bs/game/player.h"

#include <math.h>
#include <string.h>

/* RE references are GAME.EXE addresses; see docs/re/README.md, "Player movement and collision". */

/* The body probe (0xED6EA): sphere heights above the feet and radii. */
static const float body[3][2] = {{34, 7}, {41, 11}, {65, 16}};

#define WALL_RAY_HEIGHT 30.0f   /* the old-to-new ray (0x2C416) */
#define FLOOR_PROBE_UP 32.0f    /* the floor probe starts this far above the feet */
#define FLOOR_PROBE_DOWN 200.0f
#define FLOOR_SNAP 25.0f        /* a floor nearer than this below the feet is stood on (0x2C723) */
#define GRAVITY 0x18800         /* 1/256 units/s, per second (0x3FA97) */
#define FALL_MAX 0x3E800
#define MAX_HITS 32
#define GROUND_RADIUS 16.0f     /* the ground probe at the feet (0xED6C0) */
#define CHARGE_STANDING 2000    /* the longest jump charge (0x40914) */
#define CHARGE_MOVING 1000

void bs_player_init(bs_player *p, const float feet[3], float yaw)
{
    memset(p, 0, sizeof *p);
    memcpy(p->pos, feet, sizeof p->pos);
    p->yaw = yaw;
    p->eye = BS_PLAYER_EYE;
    p->floor_object = p->touch_object = -1;
}

bool bs_player_jump_target(const bs_player *p, float out[3])
{
    if (!p->charging)
        return false;
    out[0] = p->pos[0] + sinf(p->yaw) * p->jump_distance;
    out[1] = p->pos[1] - 50.0f;
    out[2] = p->pos[2] + cosf(p->yaw) * p->jump_distance;
    return true;
}

void bs_player_eye(const bs_player *p, float out[3])
{
    out[0] = p->pos[0];
    out[1] = p->pos[1] - p->eye; /* up is -y */
    out[2] = p->pos[2];
}

/* The body at feet position f: returns the number of triangles touched; *object is the first object touched. */
static uint32_t body_hits(const bs_collision_world *w, const float f[3], bs_collision_hit *hits, uint32_t max,
                          int32_t *object)
{
    uint32_t n = 0;
    *object = -1;
    for (int s = 0; s < 3; s++) {
        float c[3] = {f[0], f[1] - body[s][0], f[2]};
        uint32_t k = bs_collision_sphere(w, c, body[s][1], hits + (n < max ? n : max), n < max ? max - n : 0);
        n += k;
    }
    for (uint32_t i = 0; i < n && i < max && *object < 0; i++)
        *object = hits[i].object;
    return n;
}

/*
 * Tests the body at `to`, coming from `from`: the ray 30 above the feet in
 * between, and the body spheres there. Returns how strongly the most opposing
 * wall faces against `move` (negative: blocked by it) and that wall's
 * horizontal normal; *ray says whether the ray hit something.
 */
static float wall_test(const bs_collision_world *w, const float from[3], const float to[3], const float move[3],
                       float wall_n[3], bool *ray, int32_t *object)
{
    float a[3] = {from[0], from[1] - WALL_RAY_HEIGHT, from[2]}, b[3] = {to[0], to[1] - WALL_RAY_HEIGHT, to[2]};
    float t;
    int32_t ob;
    *ray = bs_collision_segment(w, a, b, &t, &ob);
    bs_collision_hit hits[MAX_HITS];
    uint32_t n = body_hits(w, to, hits, MAX_HITS, object);
    if (*ray && *object < 0)
        *object = ob;
    float best = 0;
    memset(wall_n, 0, 12);
    for (uint32_t i = 0; i < n && i < MAX_HITS; i++) {
        float h[3] = {hits[i].normal[0], 0, hits[i].normal[2]};
        float len = sqrtf(h[0] * h[0] + h[2] * h[2]);
        if (len < 0.1f)
            continue; /* floors and ceilings don't stop walking */
        h[0] /= len;
        h[2] /= len;
        float d = h[0] * move[0] + h[2] * move[2];
        if (d < best) {
            best = d;
            memcpy(wall_n, h, 12);
        }
    }
    return best;
}

static bool ground_touch(const bs_collision_world *w, const float at[3], float y)
{
    bs_collision_hit h[1];
    float c[3] = {at[0], y, at[2]};
    return bs_collision_sphere(w, c, GROUND_RADIUS, h, 1) > 0;
}

void bs_player_move(bs_player *p, const bs_player_stats *s, const bs_collision_world *w, const bs_player_input *in,
                    uint32_t dt_ms, bs_player_events *ev)
{
    memset(ev, 0, sizeof *ev);
    float dt = (float)dt_ms / 1000.0f;

    /* Eye height: up at 74 units/s (0x41393); ducking down at the same rate (the game's isn't known). */
    float rate = (float)dt_ms * 37.0f / 500.0f;
    p->eye = in->duck ? fmaxf(BS_PLAYER_EYE_DUCKED, p->eye - rate) : fminf(BS_PLAYER_EYE, p->eye + rate);

    /* Walking speeds (0x3F4BD, 0x3F654): forward base + 128, backward
     * (base + 200) / 2, sideways base + 100; keys held together add up. */
    int32_t base = s->speed * 435 / 256;
    if (s->deathmatch)
        base = base * 456 / 256;
    if (in->run)
        base += base * 282 / 256;
    float fwd_speed = (float)(base + 128), back_speed = (float)(base + 200) / 2, side_speed = (float)(base + 100);
    if (in->sneak) {
        /* Sneak Mode: the game halves the speeds for player flag +0x226 bit 0x40 */
        fwd_speed *= 0.5f;
        back_speed *= 0.5f;
        side_speed *= 0.5f;
    }

    /* Jumps (0x40860, 0x40DF9): hold to charge, release to jump. */
    int32_t j = (s->speed + s->attr_20) / 2 + 120;
    j += j / 2 * s->jump_skill / 100;
    if (in->run)
        j += j / 2;
    int32_t js = s->speed + 300 + 2 * s->jump_skill;
    if (in->run)
        js = js * 310 / 256;
    uint32_t charge_max = p->moved ? CHARGE_MOVING : CHARGE_STANDING;
    if (in->jump && p->on_ground) {
        if (!p->charging) {
            p->charging = true;
            p->charge_ms = 0;
        }
        p->charge_ms = p->charge_ms + dt_ms > charge_max ? charge_max : p->charge_ms + dt_ms;
        /* The predicted distance: speed x flight time (196 is half the gravity), at least 24 (64 moving). */
        int32_t k = js * (j * 256 / 196);
        int32_t d = (int32_t)((int64_t)k * p->charge_ms / charge_max) / 256;
        int32_t dmin = p->moved ? 64 : 24;
        p->jump_distance = (float)(d < dmin ? dmin : d);
    } else if (p->charging) {
        p->charging = false;
        if (p->on_ground) {
            p->vy = -j * 256;
            p->jumping = true;
            p->on_ground = false;
            ev->jumped = true;
            float speed = (float)js * (float)p->charge_ms / 1000.0f;
            p->jump_speed = p->moved ? speed * 282.0f / 256.0f : speed * 0.5f;
            p->jump_yaw = p->yaw;
        }
    }

    /* Horizontal movement: walking on the ground, the jump's speed in the air, else nothing. */
    float dx = 0, dz = 0;
    if (p->on_ground) {
        float vf = in->forward > 0 ? in->forward * fwd_speed : in->forward * back_speed;
        float vs = in->strafe * side_speed;
        float sy = sinf(p->yaw), cy = cosf(p->yaw);
        dx = (sy * vf + cy * vs) * dt;
        dz = (cy * vf - sy * vs) * dt;
    } else if (p->jumping) {
        dx = sinf(p->jump_yaw) * p->jump_speed * dt;
        dz = cosf(p->jump_yaw) * p->jump_speed * dt;
    }

    float target[3] = {p->pos[0] + dx, p->pos[1], p->pos[2] + dz};
    int32_t touch = -1;
    if (dx != 0 || dz != 0) {
        float move[3] = {dx, 0, dz}, wall[3];
        bool ray;
        float against = wall_test(w, p->pos, target, move, wall, &ray, &touch);
        if (against < 0) {
            /* Slide along the wall most against the movement, then try once more (0x2C932). */
            float slid[3] = {dx - wall[0] * against, 0, dz - wall[2] * against};
            target[0] = p->pos[0] + slid[0];
            target[2] = p->pos[2] + slid[2];
            float wall2[3];
            int32_t ob;
            if (wall_test(w, p->pos, target, slid, wall2, &ray, &ob) < 0 || ray)
                memcpy(target, p->pos, sizeof target);
        } else if (ray) {
            memcpy(target, p->pos, sizeof target); /* something thin in the way */
        }
    }

    /* Vertical movement under gravity (0x3FA97). */
    float dy = 0;
    if (!p->on_ground) {
        p->vy += (int32_t)((int64_t)GRAVITY * dt_ms / 1000);
        if (p->vy > FALL_MAX)
            p->vy = FALL_MAX;
        /* The game truncates each frame's move to whole units, which at high
         * frame rates would slow falls and jumps; here the fraction is kept. */
        dy = (float)p->vy * dt / 256.0f;
    }
    float newy = p->pos[1] + dy;
    if (dy < 0) {
        bs_collision_hit hits[MAX_HITS];
        int32_t ob;
        float up[3] = {target[0], newy, target[2]};
        if (body_hits(w, up, hits, MAX_HITS, &ob) > 0) {
            newy = p->pos[1]; /* head against the ceiling */
            p->vy = 0;
        }
    }

    /* The floor (0x2C55A): from 32 above the feet to 200 below. */
    float from[3] = {target[0], newy - FLOOR_PROBE_UP, target[2]}, to[3] = {target[0], newy + FLOOR_PROBE_DOWN, target[2]};
    float t;
    int32_t floor_ob = -1;
    bool floor = bs_collision_segment(w, from, to, &t, &floor_ob);
    float fy = from[1] + t * (FLOOR_PROBE_UP + FLOOR_PROBE_DOWN);
    bool was_on_ground = p->on_ground;
    /* Walking, a floor up to 25 below is stood on; during a jump the game
     * doesn't snap (0x2C801), so a jump lands when the feet reach the floor. */
    float snap = p->jumping ? 0.0f : FLOOR_SNAP;
    if (p->vy >= 0 && floor && fy - newy < snap + (p->jumping ? 0.001f : 0.0f)) {
        newy = fy;
        if (!was_on_ground) {
            ev->landed = true;
            ev->fall_speed = p->vy;
        }
        p->on_ground = true;
        p->jumping = false;
        p->vy = 0;
    } else if (p->vy >= 0 && !p->jumping && ground_touch(w, target, newy)) {
        /* Nothing below to stand on, but the feet still touch something: no
         * falling (0x2C770). This holds the player on plank bridges. */
        newy = fminf(newy, p->pos[1]);
        p->on_ground = true;
        p->vy = 0;
        floor_ob = p->floor_object;
    } else {
        p->on_ground = false;
        if (p->vy < 0 && floor && fy < newy)
            newy = fy;
    }

    p->moved = target[0] != p->pos[0] || target[2] != p->pos[2];
    p->pos[0] = target[0];
    p->pos[1] = newy;
    p->pos[2] = target[2];
    int32_t new_floor = p->on_ground ? floor_ob : -1;
    ev->floor_changed = new_floor != p->floor_object;
    p->floor_object = new_floor;
    ev->touch_changed = touch != p->touch_object;
    p->touch_object = touch;
}
