#include "bs/game/arrows.h"

#include <math.h>
#include <string.h>

#include "bs/game/combat.h"

/* RE references are GAME.EXE addresses; see docs/re/README.md, "Arrows". */

#define WALL_TEST_RAISE 30.0f    /* the wall test runs this much higher (0x2B83C) */
#define STEPS 10                 /* points along a move tested for creatures (0x4A1FE) */

void bs_arrows_clear(bs_arrows *a)
{
    memset(a, 0, sizeof *a);
}

static void angle_cs(int32_t a, float *c, float *s)
{
    double r = (double)(a & 0x7FF) * (2.0 * 3.14159265358979323846 / 2048.0);
    *c = (float)cos(r);
    *s = (float)sin(r);
}

bool bs_arrows_launch(bs_arrows *a, const bs_item *arrow, const float pos[3], int32_t pitch, int32_t yaw,
                      const float rot[9])
{
    for (int i = 0; i < BS_ARROWS; i++) {
        bs_arrow *x = &a->list[i];
        if (x->live)
            continue;
        memset(x, 0, sizeof *x);
        x->live = true;
        x->item = *arrow;
        x->item.quantity = 1;
        memcpy(x->pos, pos, sizeof x->pos);
        memcpy(x->rot, rot, sizeof x->rot);
        float cy, sy, cp, sp;
        angle_cs(yaw, &cy, &sy);
        angle_cs(pitch, &cp, &sp);
        float d[3] = {sy, sp, cy};
        float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        for (int k = 0; k < 3; k++)
            x->dir[k] = d[k] / len;
        return true;
    }
    return false;
}

static float dist3(const float a[3], const float b[3])
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* The first monster that acts within reach of p (0x2B502, the shooter left out). */
static bool monster_near(const bs_monsters *ms, const float p[3], uint32_t *out)
{
    for (uint32_t i = 0; ms && i < ms->count; i++) {
        const bs_monster *m = &ms->list[i];
        if (m->dead || m->gone || !m->active)
            continue;
        if (dist3(m->body.pos, p) <= BS_ARROW_REACH) {
            *out = i;
            return true;
        }
    }
    return false;
}

void bs_arrows_update(bs_arrows *a, uint32_t dt_ms, const bs_collision_world *w, const bs_monsters *ms,
                      const float player_pos[3], bs_rng *r, bs_arrow_hits *out)
{
    out->count = 0;
    for (int i = 0; i < BS_ARROWS; i++) {
        bs_arrow *x = &a->list[i];
        if (!x->live)
            continue;
        if (dist3(player_pos, x->pos) > BS_ARROW_RANGE) {
            x->live = false;
            continue;
        }
        /* The move, in whole units (0x1E847 takes an integer distance). */
        float step = (float)((int32_t)dt_ms * BS_ARROW_SPEED / 1000);
        float to[3];
        for (int k = 0; k < 3; k++)
            to[k] = x->pos[k] + x->dir[k] * step;
        float from_up[3] = {x->pos[0], x->pos[1] - WALL_TEST_RAISE, x->pos[2]};
        float to_up[3] = {to[0], to[1] - WALL_TEST_RAISE, to[2]}, t;
        int32_t ob;
        bool clear = !w || !bs_collision_segment(w, from_up, to_up, &t, &ob);
        /* Creatures at ten points along the move, from where it is (0x4A205). */
        for (int s = 0; s < STEPS; s++) {
            float d = (float)(s * (BS_ARROW_SPEED / STEPS) * (int32_t)dt_ms / 1000), p[3];
            for (int k = 0; k < 3; k++)
                p[k] = x->pos[k] + x->dir[k] * d;
            uint32_t m;
            if (!monster_near(ms, p, &m))
                continue;
            if (out->count < BS_ARROW_HITS) {
                /* Half the time it sticks in the creature (0x4A2D4: 1..100 below 50). */
                bool stuck = bs_rng_range(r, 1, 100) < 50;
                out->list[out->count++] = (bs_arrow_hit){m, x->item, stuck};
            }
            x->live = false;
            break;
        }
        if (!x->live)
            continue;
        if (!clear) {
            x->live = false;
            continue;
        }
        memcpy(x->pos, to, sizeof x->pos);
    }
}

int32_t bs_arrow_damage(const bs_arrow_shot *s, const bs_item *arrow, int32_t armour, bs_rng *r)
{
    int32_t d = bs_rng_range(r, s->damage_min, s->damage_max);
    d = d * (100 + s->skill) / 100;
    d += s->bow_material - 1;
    d += (int32_t)arrow->material - 1;
    if (s->bow == 8)
        d += (s->strength - 60) / 5;
    else if (s->bow == 9)
        d += (s->strength - 70) / 5;
    return d - armour;
}
