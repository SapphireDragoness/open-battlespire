#include "bs/game/monsters.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "bs/core/fs.h"
#include "bs/formats/exe_text.h"
#include "bs/game/use.h"

/* RE references are GAME.EXE addresses; see docs/formats/creatures.md. */

#define PROP_CREATURE 106
#define PROP_CODE 108
#define PROP_LOOT 116

#define TICK_MS 50.0f           /* the original's frame, for per-frame rates (inferred: about 20 fps) */
#define REACH 90.0f             /* attacks the player within this (0x4421A) */
#define AWARE_DISTANCE 1024.0f  /* sees the player within this, if in sight (0x4568C) */
#define TALK_DISTANCE 512.0f    /* greets within this (0x487F1) */
#define TURN_SNAP 64            /* turns straight to the goal within this (0x459BE) ... */
#define TURN_STEP 128           /* ... else this much a tick */
#define STEP_CAP 20.0f          /* the furthest step a tick (0x45A4C) */
#define AT_GOAL 12.0f           /* at the goal within this across ... (0x458CA) */
#define AT_GOAL_HEIGHT 60.0f    /* ... and this up or down */
#define MOVER_RADIUS 40.0f      /* keeps this far from other movers (0x45BB8) */
#define EYE 50.0f               /* the height lines of sight start from (inferred) */
#define WANDER_MS 3000          /* (0x4488A) */
#define SOUND_CHANCE (2001.0f / 32768.0f)  /* an unforced sound plays on rand() <= 2000 (0x48760) */
#define PLAN_MS 1000            /* not planning paths more often (an addition) */
#define LEDGE 64.0f             /* doesn't walk off a drop deeper than this (inferred) */
#define SIDESTEP_MS 1000        /* walks sideways after bumping into something at most this long (an addition) */
#define STUCK_MS 1500           /* not 16 units nearer the goal in this long: blocked (an addition) */
#define STUCK_GAIN 16.0f
#define POOL 150                /* the spell pools of a new monster (0x46BD2) */
#define HERNE_POOL 1000000      /* level 5's Hernes reflect everything (0x46601) */
#define CAST_CHANCE 25          /* a roll of 0..100 under this casts (0x4770F) */
#define SECOND_LIST_CHANCE 20   /* a roll of 1..100 under this picks from the second list (0x477C5) */

static const uint8_t mirror_view[8] = {0, 1, 0, 1, 0, 3, 2, 1}; /* 0xEF257 */

int32_t bs_angle_to(float ax, float az, float bx, float bz)
{
    float dx = bx - ax, dz = bz - az;
    float len = sqrtf(dx * dx + dz * dz);
    if (len <= 0)
        return 0;
    float c = dz / len;
    c = c > 1 ? 1 : c < -1 ? -1 : c;
    int32_t a = (int32_t)(acosf(c) * 1024.0f / 3.14f);
    if (dx <= 0)
        a = 0x7FF - a;
    return a & 0x7FF;
}

/* How far from `from` to `to` (0..1024) and which way to turn (0x1D14A). */
static int32_t angle_diff(int32_t from, int32_t to, int32_t *sign)
{
    int32_t d = (to - from) & 0x7FF;
    if (d > 1024) {
        *sign = -1;
        return 2048 - d;
    }
    *sign = 1;
    return d;
}

static void turn_towards(bs_monster *m, int32_t to, float ticks)
{
    int32_t sign;
    int32_t d = angle_diff(m->angle, to, &sign);
    int32_t step = (int32_t)(TURN_STEP * ticks);
    if (d < TURN_SNAP || step >= d)
        m->angle = to;
    else
        m->angle = (m->angle + sign * step) & 0x7FF;
}

static float dist3(const float a[3], const float b[3])
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static float dist2(const float a[3], const float b[3])
{
    float dx = a[0] - b[0], dz = a[2] - b[2];
    return sqrtf(dx * dx + dz * dz);
}

static bool chance(bs_rng *r, float p)
{
    return (float)(bs_rng_next(r) & 0xFFFF) < p * 65536.0f;
}

static void emit(bs_monster_events *ev, bs_monster_event_kind kind, uint32_t monster, int32_t value)
{
    if (ev && ev->count < BS_MONSTER_EVENTS)
        ev->list[ev->count++] = (bs_monster_event){kind, monster, value};
}

/* A creature sound (0x48749): forced, or on 6 % of calls a tick. */
static void sound(const bs_monster_world *w, bs_monster_events *ev, uint32_t i, const bs_monster *m, int kind,
                  bool forced, float ticks)
{
    if (!forced && !chance(w->rng, SOUND_CHANCE * ticks))
        return;
    emit(ev, BS_MONSTER_SOUND, i, w->tables->sounds[m->type][kind]);
}

/* ---- Spawning ---- */

void bs_monsters_free(bs_monsters *ms)
{
    free(ms->list);
    free(ms->points);
    free(ms->dist);
    free(ms->prev);
    free(ms->heap);
    memset(ms, 0, sizeof *ms);
}

bs_result bs_monsters_find_points(bs_monsters *ms, const bs_bs6_level *level)
{
    free(ms->points);
    ms->points = NULL;
    ms->point_count = 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < level->flat_count; i++) {
        const char *name = level->flats[i].name;
        if (name && (bs_strncasecmp(name, "monster1", 8) == 0 || bs_strncasecmp(name, "monster2", 8) == 0))
            n++;
    }
    if (!n)
        return BS_OK;
    ms->points = calloc(n, sizeof *ms->points);
    if (!ms->points)
        return BS_ERR_NOMEM;
    bs_arena tmp;
    bs_arena_init(&tmp, 0);
    bs_result res = BS_OK;
    for (uint32_t i = 0; i < level->flat_count && res == BS_OK; i++) {
        const bs_bs6_flat *fl = &level->flats[i];
        if (!fl->name || (bs_strncasecmp(fl->name, "monster1", 8) != 0 && bs_strncasecmp(fl->name, "monster2", 8) != 0))
            continue;
        bs_spawn_point *p = &ms->points[ms->point_count++];
        p->flat = i;
        for (int c = 0; c < 3; c++)
            p->pos[c] = (float)fl->pos[c];
        bs_bs6_logic lg;
        bs_arena_mark mark = bs_arena_get_mark(&tmp);
        res = bs_bs6_entity_logic(&fl->chunk, &tmp, &lg);
        for (uint32_t k = 0; res == BS_OK && k < lg.prop_count; k++) {
            const bs_bs6_record *rec = &lg.props[k];
            if (rec->id == PROP_CREATURE && !p->has_record) {
                p->has_record = true;
                for (uint32_t f = 0; f < 13; f++)
                    p->record[f] = bs_bs6_record_i32(rec, f);
            } else if (rec->id == PROP_CODE && !p->code[0]) {
                for (uint32_t c = 0; c < 3 && c < rec->size && rec->data[c]; c++)
                    p->code[c] = (char)rec->data[c];
            } else if (rec->id == PROP_LOOT) {
                p->loot++;
            }
        }
        bs_arena_rewind(&tmp, mark);
    }
    bs_arena_free(&tmp);
    return res;
}

/* The floor under a point (0x2BA21), or the point itself. */
static float floor_under(const bs_collision_world *w, const float p[3])
{
    float from[3] = {p[0], p[1] - 50.0f, p[2]}, to[3] = {p[0], p[1] + 2000.0f, p[2]}, t;
    int32_t ob;
    if (w && bs_collision_segment(w, from, to, &t, &ob))
        return from[1] + t * 2050.0f;
    return p[1];
}

/* A new monster of creature `type` at a point (0x46A51): the table's stats. */
static bs_monster *make(bs_monsters *ms, const bs_creature_tables *t, uint32_t type, const float pos[3],
                        const bs_collision_world *w)
{
    if (ms->count == ms->cap) {
        uint32_t cap = ms->cap ? ms->cap * 2 : 32;
        bs_monster *list = realloc(ms->list, cap * sizeof *list);
        if (!list)
            return NULL;
        ms->list = list;
        ms->cap = cap;
    }
    bs_monster *m = &ms->list[ms->count++];
    memset(m, 0, sizeof *m);
    const bs_creature *c = &t->creatures[type];
    m->type = type;
    float feet[3] = {pos[0], floor_under(w, pos) - 4.0f, pos[2]};
    bs_player_init(&m->body, feet, 0);
    memcpy(m->attributes, c->attributes, sizeof m->attributes);
    m->health = m->health_max = c->health;
    m->spell_points = m->spell_points_max = c->spell_points;
    for (int k = 0; k < 3; k++)
        m->pools[k] = POOL;
    m->skill = c->skill;
    m->flags = c->flags;
    m->state = BS_AI_IDLE;
    m->anim = BS_ANIM_STAND;
    m->path_target = UINT32_MAX;
    return m;
}

bs_result bs_monsters_spawn(bs_monsters *ms, const bs_creature_tables *t, const bs_collision_world *w,
                            int level_number, int32_t percent, bs_rng *r)
{
    ms->count = 0;
    int row = level_number >= 0 && level_number < BS_CREATURE_LEVELS ? level_number : 0;
    for (uint32_t i = 0; i < ms->point_count; i++) {
        const bs_spawn_point *p = &ms->points[i];
        uint32_t type;
        if (p->has_record) {
            if (p->record[0] < 1 || p->record[0] > BS_CREATURE_COUNT)
                continue; /* "Invalid monster override" */
            type = (uint32_t)p->record[0] - 1;
        } else {
            if (!p->code[0] && bs_rng_range(r, 0, 100) > percent && !p->loot)
                continue;
            type = t->level_creatures[row][bs_rng_range(r, 0, 3)];
        }
        bs_monster *m = make(ms, t, type, p->pos, w);
        if (!m)
            return BS_ERR_NOMEM;
        m->spawn = i;
        m->has_code = p->code[0] != 0;
        if (p->has_record) {
            /* The record's overrides where not 0 (0x4672E). */
            const int32_t *f = p->record;
            if (f[1])
                m->attributes[6] = f[1];   /* Speed */
            if (f[2])
                m->attributes[0] = f[2];   /* Strength */
            if (f[3])
                m->spell_points = m->spell_points_max = f[3];
            if (f[4])
                m->health = m->health_max = f[4];
            if (f[5])
                m->skill = f[5];
            if (f[6])
                m->state = (bs_ai_state)f[6];
            m->flags |= (uint32_t)f[7];
            for (int s = 0; s < 5; s++)
                m->spells[s] = f[8 + s];
        }
        /* Level 5's Hernes reflect spells and take no harm from weapons (0x465DD, 0x426BF). */
        m->unhurt = level_number == 5 && type == BS_CREATURE_HERNE;
        if (m->unhurt) {
            m->flags |= BS_FLAG_REFLECT;
            m->pools[BS_POOL_REFLECT] = HERNE_POOL;
        }
    }
    return BS_OK;
}

bs_monster *bs_monsters_add(bs_monsters *ms, const bs_creature_tables *t, uint32_t type, const float pos[3],
                            const bs_collision_world *w)
{
    if (type >= BS_CREATURE_COUNT)
        return NULL;
    bs_monster *m = make(ms, t, type, pos, w);
    if (m)
        m->spawn = UINT32_MAX;
    return m;
}

/* ---- Fighting ---- */

void bs_monster_fighter(const bs_monster *m, const bs_creature *c, bs_fighter *out)
{
    memset(out, 0, sizeof *out);
    out->strength = m->attributes[0];
    out->agility = m->attributes[3];
    out->luck = m->attributes[7];
    out->weapon_skill = m->skill;
    out->dodge = m->skill;
    out->critical = m->skill;
    out->damage_min = c->weapon.damage_min;
    out->damage_max = c->weapon.damage_max;
    for (int i = 0; i < BS_HIT_LOCATIONS; i++)
        out->armour[i] = c->armour;
    out->health = m->health;
    out->health_max = m->health_max;
    out->wraith = m->type == BS_CREATURE_WRAITH;
    out->unhurt = m->unhurt;
}

bool bs_monster_hurt(bs_monsters *ms, uint32_t i, const bs_monster_world *w, int32_t damage, int32_t force,
                     const float from[3], int32_t attacker_health, bs_monster_events *ev)
{
    bs_monster *m = &ms->list[i];
    if (m->dead || damage <= 0)
        return false;
    const bs_creature *c = &w->tables->creatures[m->type];
    if (m->state == BS_AI_TALK)
        m->state = BS_AI_APPROACH; /* (0x4355D) */
    /* Much weaker than the attacker: may run (0x433B0), the cleverer the likelier. */
    if (m->state != BS_AI_FLEE && m->state != BS_AI_FLEE_PATH) {
        int32_t ratio = attacker_health ? m->health * 100 / attacker_health : 100;
        if (ratio < 20 && bs_rng_range(w->rng, 0, 100) < m->attributes[1]) {
            m->state = BS_AI_FLEE;
            m->path_len = 0;
        }
    }
    m->delay = -20; /* ready to answer at once (0x42500) */
    m->health -= damage;
    if (m->health < 1) {
        m->dead = true;
        m->anim = BS_ANIM_DEATH;
        m->anim_start = w->now_ms;
        m->anim_done = false;
        m->frame = 0;
        emit(ev, BS_MONSTER_SOUND, i, w->tables->sounds[m->type][BS_CREATURE_SOUND_DEATH]);
        return true;
    }
    emit(ev, BS_MONSTER_SOUND, i, w->tables->sounds[m->type][BS_CREATURE_SOUND_HURT]);
    /* The push (0x439B8): force 30 x damage, distance 3 x damage, both times the blow's force. */
    uint32_t ms_len;
    int32_t v = bs_knockback(damage * 30 * force, c->unk_7c, damage * 3 * force, &ms_len);
    if (v) {
        m->push_angle = bs_angle_to(from[0], from[2], m->body.pos[0], m->body.pos[2]);
        m->push_speed = v;
        m->push_start = w->now_ms;
        m->push_end = w->now_ms + ms_len;
    }
    return false;
}

/* ---- Animation (0x47143) ---- */

static uint32_t anim_slot(const bs_creature *c, bs_monster_anim anim, int view)
{
    uint32_t slot;
    switch (anim) {
    case BS_ANIM_WALK: slot = BS_CREATURE_IMAGE_WALK + (uint32_t)view; break;
    case BS_ANIM_ATTACK: slot = BS_CREATURE_IMAGE_ATTACK + (uint32_t)view; break;
    case BS_ANIM_STAND: slot = BS_CREATURE_IMAGE_STAND + (uint32_t)view; break;
    case BS_ANIM_KNOCKED:
    case BS_ANIM_DEATH: slot = BS_CREATURE_IMAGE_DEATH; break;
    default: slot = 0; break;
    }
    return c->images[slot][0] ? slot : 0;
}

static void set_anim(bs_monster *m, const bs_monster_world *w, bs_monster_anim anim)
{
    if (m->anim != anim) {
        m->anim = anim;
        m->anim_start = w->now_ms;
        m->anim_done = false;
    }
    const bs_creature *c = &w->tables->creatures[m->type];
    const bs_anim_info *info = w->anims ? &w->anims[m->type][anim_slot(c, anim, 0)] : NULL;
    uint32_t frames = info && info->frames ? info->frames : 1, ms = info && info->frame_ms ? info->frame_ms : 100;
    bool back_and_forth = anim == BS_ANIM_STAND;
    uint32_t frame = anim == BS_ANIM_KNOCKED ? 0 : (w->now_ms - m->anim_start) / ms;
    uint32_t n = back_and_forth && frames > 1 ? 2 * frames - 2 : frames;
    if (frame >= n) {
        frame = n ? frame % n : 0;
        m->anim_done = true;
    }
    if (back_and_forth && frame >= frames)
        frame = 2 * frames - 2 - frame;
    m->frame = frame;
}

uint32_t bs_monster_image(const bs_monster *m, const float eye[3], bool *mirror)
{
    /* A creature's image names don't depend on the tables here: the slot is enough. */
    int32_t to = bs_angle_to(m->body.pos[0], m->body.pos[2], eye[0], eye[2]);
    int view = (((m->angle + 0x80) - to) & 0x7FF) >> 8;
    *mirror = view > 4;
    if (view > 4)
        view = mirror_view[view];
    switch (m->anim) {
    case BS_ANIM_WALK: return BS_CREATURE_IMAGE_WALK + (uint32_t)view;
    case BS_ANIM_ATTACK: return BS_CREATURE_IMAGE_ATTACK + (uint32_t)view;
    case BS_ANIM_STAND: return BS_CREATURE_IMAGE_STAND + (uint32_t)view;
    case BS_ANIM_KNOCKED:
    case BS_ANIM_DEATH: *mirror = false; return BS_CREATURE_IMAGE_DEATH;
    }
    return 0;
}

/* ---- Senses ---- */

static void eye_of(const float feet[3], float height, float out[3])
{
    out[0] = feet[0];
    out[1] = feet[1] - height;
    out[2] = feet[2];
}

/* A line of sight between the monster's and the player's eyes (0x2B703, approximated). */
static bool in_sight(const bs_monster_world *w, const bs_monster *m)
{
    float a[3], b[3], t;
    int32_t ob;
    eye_of(m->body.pos, EYE, a);
    bs_player_eye(w->player, b);
    return !bs_collision_segment(w->collision, a, b, &t, &ob);
}

/* ---- Navigation nodes ---- */

/* The node nearest a point (0x1A660, approximated): of the eight nearest, the nearest in sight from
 * eye height, else the nearest. */
static uint32_t nearest_node(const bs_nav_graph *g, const bs_collision_world *cw, const float p[3])
{
    enum { CANDIDATES = 8 };
    uint32_t best[CANDIDATES];
    float bd[CANDIDATES];
    uint32_t n = 0;
    for (uint32_t i = 0; g && i < g->count; i++) {
        float d = dist3(g->nodes[i].pos, p);
        if (n == CANDIDATES && d >= bd[n - 1])
            continue;
        uint32_t k = n < CANDIDATES ? n++ : n - 1;
        while (k > 0 && bd[k - 1] > d) {
            best[k] = best[k - 1];
            bd[k] = bd[k - 1];
            k--;
        }
        best[k] = i;
        bd[k] = d;
    }
    if (!n)
        return UINT32_MAX;
    float from[3];
    eye_of(p, EYE, from);
    for (uint32_t k = 0; cw && k < n; k++) {
        float to[3], t;
        int32_t ob;
        eye_of(g->nodes[best[k]].pos, EYE, to);
        if (!bs_collision_segment(cw, from, to, &t, &ob))
            return best[k];
    }
    return best[0];
}

static bool ensure_scratch(bs_monsters *ms, uint32_t n)
{
    if (ms->scratch_count >= n)
        return true;
    float *d = realloc(ms->dist, n * sizeof *d);
    if (d)
        ms->dist = d;
    uint32_t *p = realloc(ms->prev, n * sizeof *p);
    if (p)
        ms->prev = p;
    uint32_t *h = realloc(ms->heap, n * 8 * sizeof *h);
    if (h)
        ms->heap = h;
    if (!d || !p || !h)
        return false;
    ms->scratch_count = n;
    return true;
}

/* A shortest path over the node links from `from` to `to`; fills m->path with its first nodes. */
static bool plan(bs_monsters *ms, const bs_nav_graph *g, bs_monster *m, uint32_t from, uint32_t to)
{
    m->path_len = m->path_at = 0;
    if (!g || from >= g->count || to >= g->count || !ensure_scratch(ms, g->count))
        return false;
    for (uint32_t i = 0; i < g->count; i++) {
        ms->dist[i] = INFINITY;
        ms->prev[i] = UINT32_MAX;
    }
    /* A binary heap of node indices keyed by dist (entries may repeat; stale ones are skipped). */
    uint32_t *heap = ms->heap, hn = 0, hcap = g->count * 8;
    ms->dist[from] = 0;
    heap[hn++] = from;
    while (hn) {
        uint32_t u = heap[0];
        heap[0] = heap[--hn];
        for (uint32_t k = 0;;) {
            uint32_t l = 2 * k + 1, r = l + 1, s = k;
            if (l < hn && ms->dist[heap[l]] < ms->dist[heap[s]])
                s = l;
            if (r < hn && ms->dist[heap[r]] < ms->dist[heap[s]])
                s = r;
            if (s == k)
                break;
            uint32_t tmp = heap[k];
            heap[k] = heap[s];
            heap[s] = tmp;
            k = s;
        }
        if (u == to)
            break;
        const bs_nav_node *n = &g->nodes[u];
        for (uint32_t e = 0; e < n->link_count; e++) {
            uint32_t v = n->links[e].target;
            float d = ms->dist[u] + (float)n->links[e].distance;
            if (v < g->count && d < ms->dist[v] && hn < hcap) {
                ms->dist[v] = d;
                ms->prev[v] = u;
                uint32_t k = hn++;
                heap[k] = v;
                while (k && ms->dist[heap[(k - 1) / 2]] > ms->dist[heap[k]]) {
                    uint32_t pa = (k - 1) / 2, tmp = heap[k];
                    heap[k] = heap[pa];
                    heap[pa] = tmp;
                    k = pa;
                }
            }
        }
    }
    if (ms->dist[to] == INFINITY)
        return false;
    /* Walk back, keeping the first nodes after `from`. */
    uint32_t len = 0;
    for (uint32_t v = to; v != from && v != UINT32_MAX; v = ms->prev[v])
        len++;
    uint32_t skip = len > 32 ? len - 32 : 0, k = len - skip;
    m->path_len = k;
    for (uint32_t v = to, i = len; v != from && v != UINT32_MAX; v = ms->prev[v], i--)
        if (i <= len - skip)
            m->path[--k] = (uint16_t)v;
    m->path_target = to;
    return m->path_len > 0;
}

/* ---- Moving (0x45843) ---- */

typedef enum move_result { MOVE_BLOCKED, MOVE_ARRIVED, MOVE_MOVING } move_result;

static bool near_mover(const bs_monsters *ms, const bs_monster_world *w, const bs_monster *self, const float p[3])
{
    if (!w->player_dead && dist2(p, w->player->pos) < MOVER_RADIUS && fabsf(p[1] - w->player->pos[1]) < 80.0f)
        return true;
    for (uint32_t i = 0; i < ms->count; i++) {
        const bs_monster *o = &ms->list[i];
        if (o == self || o->dead || o->gone)
            continue;
        if (dist2(p, o->body.pos) < MOVER_RADIUS && fabsf(p[1] - o->body.pos[1]) < 80.0f &&
            dist2(p, o->body.pos) < dist2(self->body.pos, o->body.pos))
            return true;
    }
    return false;
}

/* Walks the body along an angle at a speed for dt; false if a wall or a ledge stopped it. */
static bool walk(bs_monsters *ms, const bs_monster_world *w, bs_monster *m, int32_t angle, float speed,
                 uint32_t dt_ms)
{
    float before[3];
    memcpy(before, m->body.pos, sizeof before);
    bool was_on_ground = m->body.on_ground;
    m->body.yaw = (float)angle * 2.0f * 3.14159265f / 2048.0f;
    bs_player_stats stats = {.speed = m->attributes[6], .strength = m->attributes[0]};
    bs_player_input in = {.forward = speed > 0 ? 1.0f : 0.0f, .walk_speed = speed, .water = w->water,
                          .water_y = w->water_y};
    bs_player_events pev;
    bs_player_move(&m->body, &stats, w->collision, &in, dt_ms, &pev);
    if (was_on_ground && !m->body.on_ground) {
        /* Walked off something: only onto a floor not far below. */
        float from[3] = {m->body.pos[0], m->body.pos[1], m->body.pos[2]}, to[3] = {from[0], from[1] + LEDGE, from[2]}, t;
        int32_t ob;
        if (!bs_collision_segment(w->collision, from, to, &t, &ob)) {
            memcpy(m->body.pos, before, sizeof before);
            m->body.on_ground = true;
            m->body.vy = 0;
            return false;
        }
    }
    if (near_mover(ms, w, m, m->body.pos)) {
        memcpy(m->body.pos, before, sizeof before);
        m->body.on_ground = was_on_ground;
        return false;
    }
    /* Falling it can't walk; that isn't a wall. */
    float want = speed * (float)dt_ms / 1000.0f;
    return want <= 0 || !was_on_ground || dist2(before, m->body.pos) >= want * 0.3f;
}

static float walk_speed(const bs_monster *m)
{
    return (float)(m->attributes[6] * 435 / 256 + 160);
}

static move_result move_to_goal(bs_monsters *ms, const bs_monster_world *w, bs_monster *m, uint32_t dt_ms,
                                bool goal_is_player)
{
    float ticks = (float)dt_ms / TICK_MS;
    float speed = walk_speed(m);
    float step = speed * (float)dt_ms / 1000.0f;
    if (step > STEP_CAP * ticks) {
        step = STEP_CAP * ticks;
        speed = step * 1000.0f / (float)dt_ms;
    }
    int32_t ang = m->angle;
    float d = 1000.0f;
    bool at = false;
    if (m->has_goal) {
        d = dist2(m->body.pos, m->goal);
        at = d <= AT_GOAL && fabsf(m->body.pos[1] - m->goal[1]) <= AT_GOAL_HEIGHT;
        if (!at)
            ang = bs_angle_to(m->body.pos[0], m->body.pos[2], m->goal[0], m->goal[2]);
    }
    if (m->has_goal && (d <= step || at)) {
        if (goal_is_player)
            return MOVE_BLOCKED;
        if (!near_mover(ms, w, m, m->goal)) {
            m->body.pos[0] = m->goal[0];
            m->body.pos[2] = m->goal[2];
        }
        return MOVE_ARRIVED;
    }
    turn_towards(m, ang, ticks);
    /* The game walks sideways until it bumps into something again; here at most SIDESTEP_MS, so that a
     * monster doesn't circle its goal in the open. */
    if (m->sidestep && w->now_ms - m->sidestep_start > SIDESTEP_MS)
        m->sidestep = false;
    if (m->sidestep)
        ang = (ang + 0x200) & 0x7FF;
    if (walk(ms, w, m, ang, speed, dt_ms)) {
        /* Sliding along a wall can go on forever: no nearer for a while is blocked too. */
        if (!m->has_goal)
            return MOVE_MOVING;
        float now_d = dist2(m->body.pos, m->goal);
        if (m->progress_ms == 0 || now_d < m->progress_best - STUCK_GAIN) {
            m->progress_best = now_d;
            m->progress_ms = w->now_ms ? w->now_ms : 1;
        } else if (w->now_ms - m->progress_ms > STUCK_MS) {
            m->progress_ms = 0;
            m->sidestep = false;
            return MOVE_BLOCKED;
        }
        return MOVE_MOVING;
    }
    /* Something in the way: it tries sideways, then gives up (0x45CD6). */
    m->sidestep = !m->sidestep;
    m->sidestep_start = w->now_ms;
    return m->sidestep ? MOVE_MOVING : MOVE_BLOCKED;
}

/* ---- Thinking (0x43ECF) ---- */

static void go_idle(bs_monster *m)
{
    m->progress_ms = 0;
    m->state = BS_AI_IDLE;
    m->path_len = m->path_at = 0;
    m->force_path = false;
}

/* The melee attack (0x47502): attack animations, a blow at frame 4, pauses between attacks. */
static bs_monster_anim melee(bs_monster *m, const bs_monster_world *w, uint32_t i, bs_monster_events *ev,
                             float ticks)
{
    int32_t to = bs_angle_to(m->body.pos[0], m->body.pos[2], w->player->pos[0], w->player->pos[2]);
    turn_towards(m, to, ticks);
    if (m->anim == BS_ANIM_ATTACK && !m->anim_done) {
        if (m->frame >= 4 && !m->struck) {
            m->struck = true;
            emit(ev, BS_MONSTER_STRIKE, i, 0);
        }
        return BS_ANIM_ATTACK;
    }
    m->struck = false;
    uint32_t pause = (uint32_t)((100 - m->attributes[6]) * 5 > 0 ? (100 - m->attributes[6]) * 5 : 0);
    if (w->now_ms - m->last_attack > pause) {
        m->last_attack = w->now_ms;
        if (bs_rng_range(w->rng, 0, 100) < 85) {
            if (m->skip_attacks == 0) {
                m->anim = BS_ANIM_STAND; /* restart the attack animation */
                return BS_ANIM_ATTACK;
            }
            if (m->skip_attacks < 3)
                m->skip_attacks--;
            else
                m->skip_attacks = 0;
        }
    }
    return BS_ANIM_STAND;
}

/* The greeting of a monster with a dialogue code that waits to talk (0x487B5). */
static void greet(bs_monster *m, const bs_monster_world *w, uint32_t i, bs_monster_events *ev, float distance)
{
    if (m->greeted || !m->has_code || m->state != BS_AI_TALK || distance > TALK_DISTANCE)
        return;
    int32_t to = bs_angle_to(w->player->pos[0], w->player->pos[2], m->body.pos[0], m->body.pos[2]);
    int32_t facing = (int32_t)(w->player->yaw * 2048.0f / (2.0f * 3.14159265f)) & 0x7FF, sign;
    if (angle_diff(facing, to, &sign) > 200 || !in_sight(w, m) || !w->player_can_talk)
        return;
    /* Half the time it talks, else one of seven greetings; once either way. */
    m->greeted = true;
    if (bs_rng_range(w->rng, 1, 100) < 50)
        emit(ev, BS_MONSTER_TALK, i, 0);
    else
        emit(ev, BS_MONSTER_GREET, i, BS_MSG_GREETING + bs_rng_range(w->rng, 0, 6));
}

/* Whether a monster may cast (0x4424A): it has spells and spell points, sees its target, and hasn't given up. */
static bool can_cast(const bs_monster *m, const bs_monster_world *w)
{
    if (!w->spells || m->cast_stop || m->spell_points == 0 || !m->aware)
        return false;
    const bs_creature *c = &w->tables->creatures[m->type];
    return c->spells[0][0] || c->spells[1][0] || m->spells[0];
}

/* A spell from a 0-terminated list, at random; 0 if empty. */
static int pick_spell(const uint8_t *list, uint32_t cap, bs_rng *r)
{
    uint32_t n = 0;
    while (n < cap && list[n])
        n++;
    return n ? list[bs_rng_range(r, 0, (int32_t)n - 1)] : 0;
}

/* Casting at the player (0x47645). */
static void cast(bs_monster *m, const bs_monster_world *w, uint32_t i, bs_monster_events *ev, float distance)
{
    memcpy(m->goal, w->player->pos, sizeof m->goal);
    m->has_goal = true;
    /* Spells come back off a reflecting target: a clever monster that has cast gives up. */
    if (m->spell_points < m->spell_points_max && (w->player_flags & BS_FLAG_REFLECT) &&
        bs_rng_range(w->rng, 1, 100) < m->attributes[1]) {
        m->cast_stop = true;
        return;
    }
    int32_t interval = (100 - m->attributes[1]) * 10;
    if ((int32_t)(w->now_ms - m->last_attack) <= interval)
        return;
    m->last_attack = w->now_ms;
    if (bs_rng_range(w->rng, 0, 100) >= CAST_CHANCE)
        return;
    /* The spawn record's spells, or the creature's lists. */
    const bs_creature *c = &w->tables->creatures[m->type];
    int spell = 0;
    uint8_t own[5];
    for (int k = 0; k < 5; k++)
        own[k] = m->spells[k] > 0 && m->spells[k] <= BS_SPELL_COUNT ? (uint8_t)m->spells[k] : 0;
    if (own[0])
        spell = pick_spell(own, 5, w->rng);
    else if (c->spells[1][0] && bs_rng_range(w->rng, 1, 100) < SECOND_LIST_CHANCE)
        spell = pick_spell(c->spells[1], BS_CREATURE_SPELLS, w->rng);
    else
        spell = pick_spell(c->spells[0], BS_CREATURE_SPELLS, w->rng);
    if (spell < 1 || spell > BS_SPELL_COUNT)
        return;
    const bs_spell *s = &w->spells[spell - 1];
    if (m->spell_points < bs_spell_cost(s, m->skill)) {
        m->spell_points = 0;
        return;
    }
    /* Fire Daedra cast fire, Frost Daedra cold, others any element; on others by touch within reach, else at
     * range. */
    int element = m->type == 9 ? BS_ELEMENT_COLD : m->type == 8 ? BS_ELEMENT_FIRE : bs_rng_range(w->rng, 0, 4);
    bs_spell as_cast = *s;
    as_cast.delivery = s->target == BS_TARGET_SELF ? BS_DELIVERY_CASTER
                       : distance > REACH          ? BS_DELIVERY_RANGE
                                                   : BS_DELIVERY_TOUCH;
    int32_t cost = bs_spell_cost(&as_cast, m->skill);
    /* The casting checks and pays (0x27B67, 0x27BB9), and then the caster pays again (0x47976), even when
     * the casting refused (the delivery chosen may cost more than the check above). */
    bool paid = m->spell_points >= cost;
    m->spell_points -= paid ? 2 * cost : cost;
    if (!paid)
        return;
    emit(ev, BS_MONSTER_CAST, i, spell | element << 8 | as_cast.delivery << 16);
}

/* Turns to the target and casts once facing it (0x456E1, 0x47645). */
static void turn_and_cast(bs_monster *m, const bs_monster_world *w, uint32_t i, bs_monster_events *ev,
                          float distance, float ticks)
{
    int32_t to = bs_angle_to(m->body.pos[0], m->body.pos[2], w->player->pos[0], w->player->pos[2]);
    turn_towards(m, to, ticks);
    if (m->angle == to)
        cast(m, w, i, ev, distance);
}

static void think(bs_monsters *ms, const bs_monster_world *w, uint32_t i, uint32_t dt_ms, bs_monster_events *ev)
{
    bs_monster *m = &ms->list[i];
    float ticks = (float)dt_ms / TICK_MS;
    float distance = dist3(m->body.pos, w->player->pos);
    /* An invisible player is unseen without Detect Invisibility (0x4613C); a monster on the player's team
     * has no target. */
    bool hidden = (w->player_flags & BS_FLAG_INVISIBLE) && !(m->flags & BS_FLAG_DETECT_INVISIBILITY);
    m->aware = !w->player_dead && !hidden && !m->ally && distance <= AWARE_DISTANCE && in_sight(w, m);
    greet(m, w, i, ev, distance);
    if (m->ally) {
        go_idle(m);
        set_anim(m, w, BS_ANIM_STAND);
        return;
    }

    /* Knocked back (0x4407F): pushed, fading out, showing the death image's first frame. */
    if (w->now_ms < m->push_end) {
        uint32_t len = m->push_end - m->push_start, t = w->now_ms - m->push_start;
        float v = (float)(1024 - (int32_t)((t << 10) / (len ? len : 1))) * (float)m->push_speed / 1024.0f;
        walk(ms, w, m, m->push_angle, v * 1000.0f / TICK_MS, dt_ms);
        set_anim(m, w, BS_ANIM_KNOCKED);
        return;
    }

    /* Seducers run once at a fifth of their health (0x43FA5). */
    if (m->type == 12 && m->state != BS_AI_WANDER && m->state != BS_AI_WANDERING &&
        m->health_max * 51 / 256 > m->health && !m->fled) {
        m->state = BS_AI_WANDER;
        m->fled = true;
    }
    if (w->player_dead)
        go_idle(m);

    bs_monster_anim anim = m->anim == BS_ANIM_ATTACK && !m->anim_done ? BS_ANIM_ATTACK : BS_ANIM_WALK;
    bool reach = distance <= REACH;
    bs_ai_state was = m->state;
    switch (m->state) {
    case BS_AI_ATTACK: /* 0x441BA */
        if (!reach && can_cast(m, w)) {
            /* Out of reach, a caster stays and casts (0x442DD). */
            turn_and_cast(m, w, i, ev, distance, ticks);
            anim = BS_ANIM_STAND;
            break;
        }
        if (!reach || w->player_dead) {
            m->state = BS_AI_APPROACH;
            break;
        }
        sound(w, ev, i, m, BS_CREATURE_SOUND_ATTACK, false, ticks);
        m->force_path = false;
        memcpy(m->goal, w->player->pos, sizeof m->goal);
        m->has_goal = true;
        anim = melee(m, w, i, ev, ticks);
        break;

    case BS_AI_PATH: { /* 0x44371: from the nearest node towards the player's */
        if (m->aware && !m->force_path) {
            m->state = BS_AI_APPROACH;
            break;
        }
        uint32_t from = nearest_node(w->nodes, w->collision, m->body.pos), to = ms->player_node;
        if (from == UINT32_MAX || to == UINT32_MAX ||
            w->nodes->nodes[from].group != w->nodes->nodes[to].group || w->now_ms < m->next_plan) {
            if (m->force_path)
                go_idle(m);
            else
                m->state = BS_AI_APPROACH;
            m->delay = 10;
            break;
        }
        m->next_plan = w->now_ms + PLAN_MS;
        if (from == to) {
            m->force_path = false;
            m->state = BS_AI_APPROACH;
            break;
        }
        memcpy(m->goal, w->nodes->nodes[from].pos, sizeof m->goal);
        m->has_goal = true;
        if (plan(ms, w->nodes, m, from, to))
            m->state = BS_AI_TO_NODE;
        else
            go_idle(m);
        m->path_at = 0;
        /* First to the nearest node itself. */
        m->path_len = m->path_len < 32 ? m->path_len : 31;
        memmove(m->path + 1, m->path, m->path_len * sizeof *m->path);
        m->path[0] = (uint16_t)from;
        m->path_len++;
        break;
    }

    case BS_AI_TO_NODE: /* 0x4451E / 0x445F9, the way followed node by node */
    case BS_AI_FLEE_PATH: {
        bool fleeing = m->state == BS_AI_FLEE_PATH;
        if (!fleeing && m->aware && reach) {
            sound(w, ev, i, m, BS_CREATURE_SOUND_ATTACK, true, ticks);
            m->state = BS_AI_ATTACK;
            break;
        }
        if (!fleeing && m->aware && !m->force_path) {
            m->state = BS_AI_APPROACH;
            break;
        }
        if (m->path_at >= m->path_len) {
            if (fleeing || m->path_target != ms->player_node)
                go_idle(m);
            else
                m->state = BS_AI_APPROACH;
            m->force_path = false;
            break;
        }
        memcpy(m->goal, w->nodes->nodes[m->path[m->path_at]].pos, sizeof m->goal);
        m->has_goal = true;
        move_result r = move_to_goal(ms, w, m, dt_ms, false);
        if (r == MOVE_ARRIVED) {
            m->path_at++;
            m->progress_ms = 0;
        } else if (r == MOVE_BLOCKED) {
            m->state = BS_AI_WANDER;
            m->last_attack = w->now_ms;
        }
        break;
    }

    case BS_AI_FLEE: { /* 0x44797: to a node 1000 .. 10000 away (or 500 ..), the way over the nodes */
        uint32_t from = nearest_node(w->nodes, w->collision, m->body.pos);
        uint32_t to = UINT32_MAX;
        for (int pass = 0; pass < 2 && to == UINT32_MAX && w->nodes && from != UINT32_MAX; pass++) {
            float lo = pass ? 500.0f : 1000.0f;
            uint32_t seen = 0;
            for (uint32_t k = 0; k < w->nodes->count; k++) {
                float d = dist3(w->nodes->nodes[k].pos, m->body.pos);
                if (d >= lo && d <= 10000.0f && w->nodes->nodes[k].group == w->nodes->nodes[from].group &&
                    bs_rng_range(w->rng, 0, (int32_t)seen++) == 0)
                    to = k;
            }
        }
        if (to == UINT32_MAX || !plan(ms, w->nodes, m, from, to)) {
            m->state = BS_AI_WANDER;
            break;
        }
        m->state = BS_AI_FLEE_PATH;
        break;
    }

    case BS_AI_APPROACH: { /* 0x44B23 */
        if (can_cast(m, w)) {
            /* Casters cast instead of coming nearer (0x44BC2). */
            turn_and_cast(m, w, i, ev, distance, ticks);
            anim = BS_ANIM_STAND;
            break;
        }
        memcpy(m->goal, w->player->pos, sizeof m->goal);
        m->has_goal = true;
        move_result r = move_to_goal(ms, w, m, dt_ms, true);
        if (r == MOVE_BLOCKED && !reach) {
            uint32_t from = nearest_node(w->nodes, w->collision, m->body.pos);
            if (from == UINT32_MAX || ms->player_node == UINT32_MAX ||
                w->nodes->nodes[from].group != w->nodes->nodes[ms->player_node].group) {
                m->state = BS_AI_WANDER;
                m->last_attack = w->now_ms;
            } else {
                m->force_path = true;
                m->state = BS_AI_PATH;
            }
        } else if (m->aware && reach) {
            sound(w, ev, i, m, BS_CREATURE_SOUND_ATTACK, true, ticks);
            m->state = BS_AI_ATTACK;
        }
        break;
    }

    case BS_AI_TALK: /* 0x44E5F */
        memcpy(m->goal, w->player->pos, sizeof m->goal);
        m->has_goal = true;
        turn_towards(m, bs_angle_to(m->body.pos[0], m->body.pos[2], m->goal[0], m->goal[2]), ticks);
        anim = BS_ANIM_STAND;
        break;

    case BS_AI_WANDER: /* 0x44847 */
        m->angle = bs_rng_range(w->rng, 0, 0x7FF);
        m->state = BS_AI_WANDERING;
        m->has_goal = false;
        m->last_attack = w->now_ms;
        break;

    case BS_AI_WANDERING: /* 0x4488A */
        if (w->now_ms - m->last_attack > WANDER_MS) {
            go_idle(m);
            break;
        }
        if (move_to_goal(ms, w, m, dt_ms, false) == MOVE_BLOCKED)
            m->angle = bs_rng_range(w->rng, 0, 0x7FF);
        break;

    case BS_AI_IDLE: /* 0x44CAB */
    default:
        if (m->state != BS_AI_IDLE)
            go_idle(m);
        if (m->has_goal)
            turn_towards(m, bs_angle_to(m->body.pos[0], m->body.pos[2], m->goal[0], m->goal[2]), ticks);
        sound(w, ev, i, m, BS_CREATURE_SOUND_IDLE, false, ticks);
        anim = BS_ANIM_STAND;
        if (w->player_dead)
            break;
        if (m->delay > 0) {
            m->delay--;
            break;
        }
        if (m->delay < 0)
            m->delay++;
        if (m->aware && reach) {
            sound(w, ev, i, m, BS_CREATURE_SOUND_ATTACK, true, ticks);
            m->state = BS_AI_ATTACK;
        } else if (m->aware && can_cast(m, w)) {
            turn_and_cast(m, w, i, ev, distance, ticks); /* (0x44E11) */
        } else if (m->aware) {
            m->state = BS_AI_APPROACH;
        } else if (w->nodes) {
            m->state = BS_AI_PATH;
        }
        break;
    }
    if (m->state != was)
        m->progress_ms = 0; /* a new goal */
    if (m->state == BS_AI_IDLE || m->state == BS_AI_TALK)
        anim = BS_ANIM_STAND;
    if (anim != BS_ANIM_ATTACK)
        m->struck = false;
    set_anim(m, w, anim);
}

void bs_monsters_update(bs_monsters *ms, const bs_monster_world *w, uint32_t dt_ms, bs_monster_events *ev)
{
    if (dt_ms == 0)
        return;
    if (w->nodes && (ms->player_node == UINT32_MAX || w->now_ms - ms->player_node_ms >= 250 || !ms->player_node_ms)) {
        ms->player_node = nearest_node(w->nodes, w->collision, w->player->pos);
        ms->player_node_ms = w->now_ms ? w->now_ms : 1;
    }
    if (!w->nodes)
        ms->player_node = UINT32_MAX;
    for (uint32_t i = 0; i < ms->count; i++) {
        bs_monster *m = &ms->list[i];
        if (m->gone)
            continue;
        if (m->dead) {
            set_anim(m, w, BS_ANIM_DEATH);
            if (m->anim_done) {
                m->gone = true;
                emit(ev, BS_MONSTER_GONE, i, 0);
            }
            continue;
        }
        /* The active set (0x450F8): within the level's distance; on level 2 also within 400 up or down. */
        float d = dist3(m->body.pos, w->player->pos);
        m->active = d <= (float)w->active_distance &&
                    (w->level_number != 2 || fabsf(m->body.pos[1] - w->player->pos[1]) <= 400.0f);
        if (!m->active) {
            /* Not thinking, but still standing on the floor. */
            bs_player_stats stats = {.speed = m->attributes[6]};
            bs_player_input in = {.water = w->water, .water_y = w->water_y};
            bs_player_events pev;
            if (!m->body.on_ground && !m->body.swimming)
                bs_player_move(&m->body, &stats, w->collision, &in, dt_ms, &pev);
            continue;
        }
        think(ms, w, i, dt_ms, ev);
    }
}
