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
#define SLOW_FALL_MAX 0x3C00    /* 60 units/s with Slow Fall */
#define MAX_HITS 32
#define GROUND_RADIUS 16.0f     /* the ground probe at the feet (0xED6C0) */
#define CHARGE_STANDING 2000    /* the longest jump charge (0x40914) */
#define CHARGE_MOVING 1000
/* Water (0x3FB10, 0x3FDA0, 0x2C299). */
#define SWIM_DEPTH 50.0f        /* the feet this far below the surface swim; a fall into water stops here */
#define SWIM_VERTICAL 100.0f    /* rising and sinking, units/s (0x3FECB) */
#define SWIM_PROBE_UP 110.0f    /* a swimmer at the surface looks for floors this far above the feet (0x2C64C) */
#define SWIM_AHEAD 35.0f        /* a blocked swimmer's move is tried this much further on (0x40215) */
/* The Swimming skill adds half itself to the forward step of each frame (0x3F5E2), and the game splits a frame
 * into steps of about 15 units (0x3FCF0), each with the bonus: the speed depends on the frame rate. The game
 * ran at about 20 frames a second; that is assumed here. */
#define SWIM_FRAME_MS 50
#define FRAME_STEP 15
#define BALLOON_VERTICAL 160.0f /* (0x3FFAC) */

/* A boat's spheres (0xED77E): 10 above the feet, 90 ahead and behind (radius 35) and in the middle (20). The
 * balloon's (0xED7C8): its envelope, radius 700 centred 700 up, and its basket, 50 at 50 up. */
static const float boat_body[3][3] = {{90, 10, 35}, {0, 10, 20}, {-90, 10, 35}};
static const float balloon_body[2][2] = {{700, 700}, {50, 50}};

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

/* The forward speed in water (0x3F4BD in the steps of 0x3FCF0): fwd is base + 128 in units/s. */
static float swim_forward(int32_t fwd, bool sneak, int32_t skill)
{
    int32_t d = fwd * SWIM_FRAME_MS / 1000;
    if (sneak)
        d >>= 1;
    d = (d >> 1) + skill / 2;
    if (d < FRAME_STEP)
        return (float)d * 1000.0f / SWIM_FRAME_MS;
    int32_t step = (int32_t)((float)SWIM_FRAME_MS * ((float)FRAME_STEP / (float)d));
    if (step < 1)
        step = 1;
    int32_t ds = fwd * step / 1000;
    if (sneak)
        ds >>= 1;
    ds = (ds >> 1) + skill / 2;
    int32_t n = (SWIM_FRAME_MS + step - 1) / step;
    return (float)(n * ds) * 1000.0f / SWIM_FRAME_MS;
}

/* Whether the boat or balloon at feet position f touches anything (0x2C459, 0x2C4E5). */
static bool ride_blocked(const bs_collision_world *w, const bs_player_input *in, const float f[3])
{
    bs_collision_hit h[1];
    if (in->ride == BS_RIDE_BOAT) {
        float sx = sinf(in->ride_heading), cz = cosf(in->ride_heading);
        for (int k = 0; k < 3; k++) {
            float c[3] = {f[0] + sx * boat_body[k][0], f[1] - boat_body[k][1], f[2] + cz * boat_body[k][0]};
            if (bs_collision_sphere(w, c, boat_body[k][2], h, 1))
                return true;
        }
        return false;
    }
    for (int k = 0; k < 2; k++) {
        float c[3] = {f[0], f[1] - balloon_body[k][0], f[2]};
        if (bs_collision_sphere(w, c, balloon_body[k][1], h, 1))
            return true;
    }
    return false;
}

/* The floor below (x, z): from up above y to 200 below. Returns its height in *fy. */
static bool floor_at(const bs_collision_world *w, float x, float y, float z, float up, float *fy, int32_t *object)
{
    float from[3] = {x, y - up, z}, to[3] = {x, y + FLOOR_PROBE_DOWN, z}, t;
    *object = -1;
    if (!bs_collision_segment(w, from, to, &t, object))
        return false;
    *fy = from[1] + t * (up + FLOOR_PROBE_DOWN);
    return true;
}

/* A swimmer that can't go on is tried 35 further on before sliding, finding a floor from higher up: onto a
 * ledge out of the water (0x2C8A7). Returns whether it climbed. */
static bool climb(bs_player *p, const bs_collision_world *w, float dx, float dz, float y, float probe_up,
                  int32_t touch, bs_player_events *ev)
{
    float len = sqrtf(dx * dx + dz * dz), fy;
    int32_t ob, hit_ob;
    bs_collision_hit hits[1];
    float ahead[3] = {p->pos[0] + dx / len * (len + SWIM_AHEAD), 0, p->pos[2] + dz / len * (len + SWIM_AHEAD)};
    if (!floor_at(w, ahead[0], y, ahead[2], probe_up, &fy, &ob) || fy > y)
        return false;
    ahead[1] = fy;
    if (body_hits(w, ahead, hits, 1, &hit_ob) > 0)
        return false;
    memcpy(p->pos, ahead, sizeof ahead);
    p->moved = true;
    p->swimming = false;
    p->on_ground = true;
    p->vy = 0;
    ev->floor_changed = ob != p->floor_object;
    p->floor_object = ob;
    ev->touch_changed = touch != p->touch_object;
    p->touch_object = touch;
    return true;
}

void bs_player_move(bs_player *p, const bs_player_stats *s, const bs_collision_world *w, const bs_player_input *in,
                    uint32_t dt_ms, bs_player_events *ev)
{
    memset(ev, 0, sizeof *ev);
    float dt = (float)dt_ms / 1000.0f;
    bool riding = in->ride != BS_RIDE_NONE;

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
    if (in->walk_speed > 0)
        fwd_speed = in->walk_speed;
    if (in->sneak) {
        /* Sneak Mode: the game halves the speeds for player flag +0x226 bit 0x40 */
        fwd_speed *= 0.5f;
        back_speed *= 0.5f;
        side_speed *= 0.5f;
    }
    if (p->swimming || in->ride == BS_RIDE_BALLOON) {
        /* Swimming and the balloon halve them again; in water the Swimming skill adds to going forward. */
        fwd_speed = p->swimming && in->walk_speed <= 0 ? swim_forward(base + 128, in->sneak, s->swim_skill)
                                                       : fwd_speed * 0.5f;
        back_speed *= 0.5f;
        side_speed *= 0.5f;
    }

    /* Jumps (0x40860, 0x40DF9): hold to charge, release to jump. Not in water, nor riding. */
    int32_t j = (s->speed + s->strength) / 2 + 120;
    j += j / 2 * s->jump_skill / 100;
    if (in->run)
        j += j / 2;
    int32_t js = s->speed + 300 + 2 * s->jump_skill;
    if (in->jumping)
        js = js * 330 / 256; /* 0x408C5 */
    if (in->run)
        js = js * 310 / 256;
    uint32_t charge_max = p->moved ? CHARGE_MOVING : CHARGE_STANDING;
    bool can_jump = p->on_ground && !p->swimming && !riding;
    if (in->jump && can_jump) {
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
        if (can_jump) {
            p->vy = -j * 256;
            p->jumping = true;
            p->on_ground = false;
            ev->jumped = true;
            float speed = (float)js * (float)p->charge_ms / 1000.0f;
            p->jump_speed = p->moved ? speed * 282.0f / 256.0f : speed * 0.5f;
            p->jump_yaw = p->yaw;
        }
    }
    if (p->swimming)
        p->jumping = false; /* 0x4036B */

    /* Horizontal movement: walking on the ground, in water or riding (a boat goes its own way and has no
     * sideways step); the jump's speed in the air, else nothing. */
    float dx = 0, dz = 0;
    if (p->on_ground || p->swimming || riding) {
        float vf = in->forward > 0 ? in->forward * fwd_speed : in->forward * back_speed;
        float vs = in->ride == BS_RIDE_BOAT ? 0.0f : in->strafe * side_speed;
        float heading = in->ride == BS_RIDE_BOAT ? in->ride_heading : p->yaw;
        float sy = sinf(heading), cy = cosf(heading);
        dx = (sy * vf + cy * vs) * dt;
        dz = (cy * vf - sy * vs) * dt;
    } else if (p->jumping) {
        dx = sinf(p->jump_yaw) * p->jump_speed * dt;
        dz = cosf(p->jump_yaw) * p->jump_speed * dt;
    }

    /* Up and down (0x3FE57, 0x3FF8D): a swimmer rises to the surface or sinks (carrying too much, it sinks
     * anyway) until it stands on something; the balloon rises, and sinks while above the water. */
    float y = p->pos[1];
    if (p->swimming) {
        if (!in->heavy && in->up) {
            if (y > in->water_y + SWIM_DEPTH)
                y -= SWIM_VERTICAL * dt;
        } else if ((in->heavy || in->down) && !p->on_ground &&
                   (in->dive_max <= 0 || y < in->water_y + in->dive_max)) {
            y += SWIM_VERTICAL * dt;
        }
    }
    if (in->ride == BS_RIDE_BALLOON) {
        if (in->up)
            y -= BALLOON_VERTICAL * dt;
        else if (in->down && !p->on_ground && (!in->water || y < in->water_y))
            y += BALLOON_VERTICAL * dt;
    }

    /* Gravity and the water (0x3FA97, 0x3FB10): no falling in water; falling into it stops 50 below the
     * surface. Riding, nothing falls. */
    float dy = 0;
    bool in_water = false;
    if (!p->swimming) {
        p->vy += (int32_t)((int64_t)GRAVITY * dt_ms / 1000);
        if (p->vy > FALL_MAX)
            p->vy = FALL_MAX;
        if (in->slow_fall && p->vy > SLOW_FALL_MAX) {
            if (p->vy - (int32_t)((int64_t)GRAVITY * dt_ms / 1000) < SLOW_FALL_MAX)
                ev->slow_fall = true; /* 0x3FBE9 */
            p->vy = SLOW_FALL_MAX; /* 0x3FBC2 */
        }
        /* The game truncates each frame's move to whole units, which at high
         * frame rates would slow falls and jumps; here the fraction is kept. */
        dy = (float)p->vy * dt / 256.0f;
    }
    if (in->water && y >= in->water_y + SWIM_DEPTH) {
        if (!p->swimming) {
            dy = 0;
            y = in->water_y + SWIM_DEPTH;
            ev->splashed = true;
        }
        in_water = true;
    }
    if (in_water || riding) {
        p->vy = 0;
        if (riding)
            dy = 0;
    }
    bool was_swimming = p->swimming;
    p->swimming = in_water && !riding;
    float newy = y + dy;

    /* Riding: the boat's or balloon's spheres at the new place; touching anything, nothing moves (0x2C4C5). */
    if (riding) {
        float to[3] = {p->pos[0] + dx, newy, p->pos[2] + dz};
        if (ride_blocked(w, in, to)) {
            ev->ride_blocked = true;
            memcpy(to, p->pos, sizeof to);
        }
        p->moved = to[0] != p->pos[0] || to[2] != p->pos[2];
        memcpy(p->pos, to, sizeof to);
        p->on_ground = p->jumping = p->charging = false;
        ev->floor_changed = p->floor_object != -1;
        ev->touch_changed = p->touch_object != -1;
        p->floor_object = p->touch_object = -1;
        return;
    }

    float target[3] = {p->pos[0] + dx, p->pos[1], p->pos[2] + dz};
    int32_t touch = -1;
    float probe_up = was_swimming && fabsf(p->pos[1] - (in->water_y + SWIM_DEPTH)) < 0.5f ? SWIM_PROBE_UP
                                                                                       : FLOOR_PROBE_UP;
    if (dx != 0 || dz != 0) {
        float move[3] = {dx, 0, dz}, wall[3];
        bool ray;
        float against = wall_test(w, p->pos, target, move, wall, &ray, &touch);
        if ((against < 0 || ray) && p->swimming && climb(p, w, dx, dz, newy, probe_up, touch, ev))
            return;
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

    if (newy < p->pos[1]) {
        bs_collision_hit hits[MAX_HITS];
        int32_t ob;
        float up[3] = {target[0], newy, target[2]};
        if (body_hits(w, up, hits, MAX_HITS, &ob) > 0) {
            newy = p->pos[1]; /* head against the ceiling */
            p->vy = 0;
        }
    }

    /* The floor (0x2C55A): from 32 above the feet (110 for a swimmer at the surface) to 200 below. */
    float fy = 0;
    int32_t floor_ob = -1;
    bool floor = floor_at(w, target[0], newy, target[2], probe_up, &fy, &floor_ob);
    if (floor && fy < newy - FLOOR_PROBE_UP) {
        /* Only a swimmer looks this high: the body must fit there (the game's head room test, 0x40334). */
        bs_collision_hit hits[1];
        int32_t ob;
        float at[3] = {target[0], fy, target[2]};
        if (body_hits(w, at, hits, 1, &ob) > 0)
            floor = false;
    }
    bool was_on_ground = p->on_ground;
    /* Walking, a floor up to 25 below is stood on. During a jump the game doesn't snap (0x2C801), so a jump
     * lands when the feet reach the floor; nor in water, nor over deep water from above it (a floor more than
     * 50 below the surface, 0x2C67C): only floors the feet reach count. */
    bool deep = in->water && !p->swimming && p->pos[1] <= in->water_y + SWIM_DEPTH &&
                fy - SWIM_DEPTH > in->water_y;
    bool strict = p->jumping || p->swimming || deep;
    float snap = strict ? 0.001f : FLOOR_SNAP;
    if (p->vy >= 0 && floor && fy - newy < snap) {
        newy = fy;
        if (!was_on_ground && !p->swimming) {
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
