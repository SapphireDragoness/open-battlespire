#include "bs/game/hand.h"

#include <math.h>
#include <string.h>

/* RE references are GAME.EXE addresses; see docs/re/README.md, "The weapon in hand". */

#define NEXT_HIDE 100      /* the model goes (0x48F86) */
#define NEXT_RETURN 101    /* to the pending state, or idle / walking (0x48FD3) */
#define NEXT_HOLD 102      /* stays while the bow is drawn, else loosed (0x48F13) */

/* A curve: amplitude x sin(start + t x step), t running 0..1 over the state. */
typedef struct curve {
    int32_t amp, start, step;
} curve;

/* An angle: start + t x step, or a there-and-back (the state's mode) reaching start + step / 2. */
typedef struct turn {
    int32_t start, step;
} turn;

typedef struct state_def {
    int32_t ms;
    int32_t next;
    int32_t there_and_back;
    curve pos[3];
    turn angle[3];             /* pitch, yaw, roll */
} state_def;

/* The states (0xEF7D4: pointers to 0x4C-byte records). */
static const state_def states[BS_HAND_STATES] = {
    {110, NEXT_RETURN, 0, {{0, 0, 0}, {1, 0, 2047}, {0, 0, 0}}, {{0, 0}, {0, 0}, {0, 0}}},
    {1000, NEXT_RETURN, 1, {{0, 0, 0}, {0, 0, 0}, {30, 0, 2047}}, {{0, 60}, {0, 0}, {0, 0}}},
    {1000, 0, 0, {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, {{1536, 512}, {0, 0}, {0, 0}}},
    {1000, NEXT_HIDE, 0, {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, {{0, -512}, {0, 0}, {0, 0}}},
    {75, NEXT_RETURN, 0, {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, {{0, -512}, {0, 0}, {0, 0}}},
    {300, 6, 0, {{-300, 768, 512}, {-100, 512, 1}, {100, 512, 1}}, {{0, 0}, {1818, 460}, {1536, 1}}},
    {75, 0, 0, {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, {{1536, 512}, {0, 0}, {0, 0}}},
    {300, 6, 1, {{0, 0, 0}, {-100, 512, 512}, {300, 512, 1}}, {{300, -500}, {1950, 1}, {0, 0}}},
    {300, 6, 0, {{-300, 768, -512}, {-100, 512, 1}, {100, 512, 1}}, {{230, -460}, {1818, 460}, {1792, 1}}},
    {400, 6, 0, {{-150, 512, 1}, {0, 0, 0}, {300, 0, 800}}, {{0, 0}, {0, 0}, {0, 512}}},
    {300, 6, 0, {{-300, 256, 512}, {-100, 512, 1}, {100, 512, 1}}, {{0, 0}, {230, -460}, {512, 1}}},
    {300, 6, 0, {{-300, 256, 512}, {-100, 512, 1}, {100, 512, 1}}, {{230, -460}, {230, -460}, {256, 1}}},
    {300, BS_HAND_HOLD, 0, {{-60, 512, 1}, {0, 0, 0}, {100, 512, 1}}, {{0, 420}, {0, 0}, {0, 0}}},
    {150, NEXT_RETURN, 0, {{-60, 512, 1}, {0, 0, 0}, {100, 512, 1}}, {{420, -420}, {0, 0}, {0, 0}}},
    {300, NEXT_HOLD, 0, {{-160, 0, 512}, {0, 0, 0}, {240, 512, 1}}, {{420, 1}, {512, 1}, {0, 0}}},
};

/* Where the curves are centred, in tenths of a unit (0x490F7, 0x49138, 0x49179). */
static const int32_t centre[3] = {200, -450, -100};

/* The swings' base times by animation (0xDA58E), in units of 62.5 ms. */
static const int16_t swing_base[16] = {5, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 4, 4, 4, 4, 5};

static bool is_swing(int s)
{
    return s == 5 || (s >= 7 && s <= 11);
}

static int32_t state_ms(const bs_hand *h, int s)
{
    return is_swing(s) && h->swing_ms[s] > 0 ? h->swing_ms[s] : states[s].ms;
}

static int next_of(const bs_hand *h, int s)
{
    return s == BS_HAND_WINDUP && h->windup_next >= 0 ? h->windup_next : states[s].next;
}

/* The game's fixed-point sine (0x1B9FE: table 0xF2300 of sin x 2^28). */
static int32_t sine(int32_t amp, int32_t angle)
{
    double a = (double)(angle & 0x7FF) * (2.0 * 3.14159265358979323846 / 2048.0);
    int64_t s = (int64_t)llround(sin(a) * 268435456.0);
    return (int32_t)(((int64_t)amp * s) >> 28);
}

/* x * t / 2048, rounding towards zero as the game's shifts do. */
static int32_t scale(int32_t x, int32_t t)
{
    return (int32_t)((int64_t)x * t / 2048);
}

/* 0x4920E. */
static int32_t turn_at(const turn *a, int32_t t, bool there_and_back)
{
    if (a->step == 0)
        return 0;
    int32_t v = scale(a->step, t);
    if (there_and_back) {
        int32_t half = a->step / 2;
        if ((half < 0 && v < half) || (half > 0 && v > half))
            v = half - (v - half);
    }
    return (v + a->start) & 0x7FF;
}

void bs_hand_reset(bs_hand *h)
{
    memset(h, 0, sizeof *h);
    h->weapon = -1;
    h->pending = -1;
    h->windup_next = -1;
}

void bs_hand_equip(bs_hand *h, int weapon, uint32_t now_ms)
{
    int32_t keep[BS_HAND_STATES];
    memcpy(keep, h->swing_ms, sizeof keep);
    bs_hand_reset(h);
    memcpy(h->swing_ms, keep, sizeof keep);
    if (weapon < 0 || weapon > 14)
        return;
    h->weapon = weapon;
    h->state = BS_HAND_RAISE;
    h->start = now_ms;
}

bool bs_hand_can_swing(const bs_hand *h)
{
    /* The game would take a second swing before the first starts, the last one winning; here it waits. */
    return h->weapon >= 0 && h->pending < 0 &&
           (h->state == BS_HAND_IDLE || h->state == BS_HAND_WALK || h->state == BS_HAND_RECOVER);
}

bool bs_hand_swing(bs_hand *h, int animation, int32_t swing_ms)
{
    if (!bs_hand_can_swing(h) || !is_swing(animation))
        return false;
    h->swing_ms[animation] = swing_ms;
    h->windup_next = animation;
    /* Walking ends at once; idle and the recovery run out first. */
    if (h->state == BS_HAND_WALK)
        h->cut = true;
    h->pending = BS_HAND_WINDUP;
    return true;
}

static void enter(bs_hand *h, int s, uint32_t now_ms)
{
    h->state = s;
    h->start = now_ms;
    h->cut = false;
}

bool bs_hand_update(bs_hand *h, uint32_t now_ms, bool moving, bool drawing)
{
    bool strike = false;
    if (h->weapon < 0)
        return false;
    /* A bow drawn from idle or walking (0x48AE7). */
    if (drawing && (h->state == BS_HAND_IDLE || h->state == BS_HAND_WALK))
        enter(h, BS_HAND_DRAW, now_ms);
    for (int guard = 0; guard < 8; guard++) {
        if (moving && h->state == BS_HAND_IDLE)
            enter(h, BS_HAND_WALK, now_ms);
        int32_t ms = state_ms(h, h->state) + h->extra_ms;
        int32_t elapsed = (int32_t)(now_ms - h->start);
        /* The blow lands half-way (0x49053). The game checks this only while the state runs, so a frame
         * longer than half a swing would skip it; here it can't be skipped. */
        if (!h->struck && ms / 2 < elapsed) {
            h->struck = true;
            strike |= is_swing(h->state);
        }
        if (!h->cut && ms > elapsed)
            break;
        h->extra_ms = 0;
        h->struck = false;
        int next = next_of(h, h->state);
        if (h->state == BS_HAND_WINDUP)
            h->windup_next = -1;
        if (next == NEXT_HOLD) {
            if (drawing) {
                /* Held at the end of the draw. */
                h->start = now_ms - (uint32_t)ms + 1;
                h->cut = false;
                break;
            }
            h->pending = -1;
            enter(h, BS_HAND_LOOSE, now_ms);
        } else if (next == NEXT_HIDE) {
            h->weapon = -1;
            break;
        } else if (next != NEXT_RETURN) {
            enter(h, next, now_ms);
        } else if (h->pending >= 0) {
            enter(h, h->pending, now_ms);
            h->pending = -1;
        } else {
            enter(h, moving ? BS_HAND_WALK : BS_HAND_IDLE, now_ms);
        }
    }
    return strike;
}

bool bs_hand_pose_at(const bs_hand *h, uint32_t now_ms, bs_hand_pose *out)
{
    if (h->weapon < 0 || h->state < 0 || h->state >= BS_HAND_STATES)
        return false;
    const state_def *d = &states[h->state];
    int32_t ms = state_ms(h, h->state) + h->extra_ms;
    int32_t elapsed = (int32_t)(now_ms - h->start);
    if (elapsed < 0)
        elapsed = 0;
    if (elapsed > ms)
        elapsed = ms;
    int32_t t = ms > 0 ? (int32_t)((int64_t)elapsed * 2048 / ms) : 2048;
    for (int k = 0; k < 3; k++) {
        const curve *c = &d->pos[k];
        int32_t v = sine(c->amp, c->start + scale(c->step, t)) + centre[k];
        out->offset[k] = (float)v / 10.0f;
        out->angles[k] = turn_at(&d->angle[k], t, d->there_and_back != 0);
    }
    out->offset[1] -= 5.0f; /* 0x48B5E */
    return true;
}

int32_t bs_swing_time(int animation, int32_t weapon_weight, int32_t skill, int32_t strength, int32_t speed)
{
    if (animation < 0 || animation >= 16)
        return 0;
    float a = (float)((double)weapon_weight / 30.0);
    float heavy = (float)((double)(a * a) * 0.5 + 1.0);
    float b = (float)((double)(skill + strength / 5 + speed / 5) / 140.0);
    float quick = (float)((double)(b * b * b) * -0.5 + 1.0);
    float ms = (float)swing_base[animation] * (heavy * quick);
    ms = (float)((double)(ms * 0.25f) * 500.0);
    ms *= 0.5f;
    return (int32_t)ms;
}

/* The game's rotations (0x89568, 0x89654, 0x89740), each applied after the matrix so far. */
static void apply(float m[9], const float r[9])
{
    float o[9];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            o[i * 3 + j] = m[i * 3] * r[j] + m[i * 3 + 1] * r[3 + j] + m[i * 3 + 2] * r[6 + j];
    memcpy(m, o, sizeof o);
}

static void angle_cs(int32_t a, float *c, float *s)
{
    double r = (double)(a & 0x7FF) * (2.0 * 3.14159265358979323846 / 2048.0);
    *c = (float)cos(r);
    *s = (float)sin(r);
}

static void yaw_of(int32_t a, float r[9])
{
    float c, s;
    angle_cs(a, &c, &s);
    const float m[9] = {c, 0, -s, 0, 1, 0, s, 0, c};
    memcpy(r, m, sizeof m);
}

void bs_hand_rotation(int32_t body_yaw, const int32_t angles[3], float m[9])
{
    float r[9], c, s;
    /* The body: 0x89004 with the yaw negated (0x493E8). */
    yaw_of(-body_yaw, m);
    yaw_of(angles[1], r);
    apply(m, r);
    angle_cs(angles[0], &c, &s);
    const float pitch[9] = {1, 0, 0, 0, c, -s, 0, s, c};
    apply(m, pitch);
    angle_cs(angles[2], &c, &s);
    const float roll[9] = {c, -s, 0, s, c, 0, 0, 0, 1};
    apply(m, roll);
}

void bs_hand_offset(int32_t body_yaw, const float offset[3], float out[3])
{
    float m[9];
    yaw_of(-body_yaw, m);
    for (int i = 0; i < 3; i++)
        out[i] = m[i * 3] * offset[0] + m[i * 3 + 1] * offset[1] + m[i * 3 + 2] * offset[2];
}
