#include "bs/game/level_logic.h"

#include <stdlib.h>
#include <string.h>

/* RE references are GAME.EXE addresses; see docs/re/README.md, "Level logic". */

#define PROP_SOUND 121
#define LINK_TELEPORT 2
#define OPTIONS_AT 16      /* option text in move and rotate data */
#define OPTIONS_LEN 12

typedef struct id_index {
    uint32_t id, object;
} id_index;

static int cmp_id(const void *a, const void *b)
{
    uint32_t x = ((const id_index *)a)->id, y = ((const id_index *)b)->id;
    return x < y ? -1 : x > y;
}

static const bs_bs6_chunk *entity_chunk(const bs_bs6_level *lv, uint32_t i, bs_logic_kind *kind, uint32_t *index)
{
    if (i < lv->object_count) {
        *kind = BS_LOGIC_OBJECT;
        *index = i;
        return &lv->objects[i].chunk;
    }
    i -= lv->object_count;
    if (i < lv->flat_count) {
        *kind = BS_LOGIC_FLAT;
        *index = i;
        return &lv->flats[i].chunk;
    }
    i -= lv->flat_count;
    *kind = BS_LOGIC_LIGHT;
    *index = i;
    return &lv->lights[i].chunk;
}

static void read_ctrl(const bs_bs6_record *r, bs_logic_ctrl *c)
{
    memset(c, 0, sizeof *c);
    c->type = r->id;
    for (uint32_t k = 0; k < 6; k++)
        c->v[k] = bs_bs6_record_i32(r, k);
    c->sound = -1;
    if ((c->type == BS_LOGIC_CTRL_MOVE || c->type == BS_LOGIC_CTRL_ROTATE) && r->size >= OPTIONS_AT) {
        /* The game upper-cases this text and looks for 'R' and 'C' (0x4BF0B, 0x4BF72). */
        uint32_t end = r->size < OPTIONS_AT + OPTIONS_LEN ? r->size : OPTIONS_AT + OPTIONS_LEN;
        for (uint32_t k = OPTIONS_AT; k < end && r->data[k]; k++) {
            uint8_t ch = r->data[k];
            c->reversible |= ch == 'r' || ch == 'R';
            c->continuous |= ch == 'c' || ch == 'C';
        }
    }
}

bs_result bs_level_logic_build(const bs_bs6_level *lv, bs_arena *arena, bs_level_logic *out)
{
    memset(out, 0, sizeof *out);
    uint32_t n = lv->object_count + lv->flat_count + lv->light_count;
    bs_bs6_logic *ent = calloc(n ? n : 1, sizeof *ent);
    id_index *ids = calloc(n ? n : 1, sizeof *ids);
    bs_arena tmp;
    bs_arena_init(&tmp, 0);
    bs_result r = ent && ids ? BS_OK : BS_ERR_NOMEM;
    uint32_t nctrl = 0, nlink = 0;
    for (uint32_t i = 0; r == BS_OK && i < n; i++) {
        bs_logic_kind kind;
        uint32_t index;
        r = bs_bs6_entity_logic(entity_chunk(lv, i, &kind, &index), &tmp, &ent[i]);
        nctrl += ent[i].ctrl_count;
        nlink += ent[i].link_count;
    }
    if (r == BS_OK) {
        out->objects = BS_ARENA_ARRAY(arena, bs_logic_object, n ? n : 1);
        out->ctrls = BS_ARENA_ARRAY(arena, bs_logic_ctrl, nctrl ? nctrl : 1);
        out->links = BS_ARENA_ARRAY(arena, uint32_t, nlink ? nlink : 1);
        out->active = BS_ARENA_ARRAY(arena, uint32_t, n ? n : 1);
        if (!out->objects || !out->ctrls || !out->links || !out->active)
            r = BS_ERR_NOMEM;
    }
    if (r == BS_OK) {
        for (uint32_t i = 0; i < n; i++) {
            bs_logic_object *o = &out->objects[i];
            memset(o, 0, sizeof *o);
            const int32_t *pos;
            entity_chunk(lv, i, &o->kind, &o->index);
            if (o->kind == BS_LOGIC_OBJECT) {
                o->entity_id = lv->objects[o->index].id;
                pos = lv->objects[o->index].pos;
                memcpy(o->angles, lv->objects[o->index].angles, sizeof o->angles);
            } else if (o->kind == BS_LOGIC_FLAT) {
                o->entity_id = lv->flats[o->index].id;
                pos = lv->flats[o->index].pos;
            } else {
                o->entity_id = lv->lights[o->index].id;
                pos = lv->lights[o->index].pos;
            }
            for (int k = 0; k < 3; k++)
                o->pos[k] = (float)pos[k];
            ids[i].id = o->entity_id;
            ids[i].object = i;
            o->first_ctrl = out->ctrl_count;
            o->ctrl_count = ent[i].ctrl_count;
            for (uint32_t k = 0; k < ent[i].ctrl_count; k++)
                read_ctrl(&ent[i].ctrls[k], &out->ctrls[out->ctrl_count++]);
            /* STRU 121: a sound for the object's last controller (0x24697). */
            const bs_bs6_record *snd = bs_bs6_logic_prop(&ent[i], PROP_SOUND);
            if (snd && o->ctrl_count)
                out->ctrls[o->first_ctrl + o->ctrl_count - 1].sound = bs_bs6_record_i32(snd, 0);
        }
        qsort(ids, n, sizeof *ids, cmp_id);
        out->object_count = n;
        for (uint32_t i = 0; i < n; i++) {
            bs_logic_object *o = &out->objects[i];
            o->first_link = out->link_count;
            for (uint32_t k = 0; k < ent[i].link_count; k++) {
                id_index key = {ent[i].links[k].target, 0};
                const id_index *hit = bsearch(&key, ids, n, sizeof *ids, cmp_id);
                if (!hit)
                    continue;
                out->links[out->link_count++] = hit->object;
                o->link_count++;
                if (ent[i].links[k].type == LINK_TELEPORT)
                    out->objects[hit->object].teleport_target = true;
            }
        }
    }
    bs_arena_free(&tmp);
    free(ent);
    free(ids);
    if (r != BS_OK)
        memset(out, 0, sizeof *out);
    return r;
}

uint32_t bs_level_logic_find(const bs_level_logic *lg, bs_logic_kind kind, uint32_t index)
{
    for (uint32_t i = 0; i < lg->object_count; i++)
        if (lg->objects[i].kind == kind && lg->objects[i].index == index)
            return i;
    return UINT32_MAX;
}

/* ---- running ---- */

static void add_sound(bs_logic_output *out, int32_t id, uint32_t object)
{
    if (out->sound_count < BS_LOGIC_MAX_SOUNDS) {
        out->sounds[out->sound_count].id = id;
        out->sounds[out->sound_count].object = object;
        out->sound_count++;
    }
}

/* The game converts positions to integers by truncation. */
static int32_t trunc_i32(float v)
{
    return v > 2e9f ? 2000000000 : v < -2e9f ? -2000000000 : (int32_t)v;
}

/* Starts an object's whole chain (0x4AF7C, 0x4BD7E). */
static void start_chain(bs_level_logic *lg, uint32_t oi, bs_logic_output *out)
{
    bs_logic_object *o = &lg->objects[oi];
    o->active = true;
    lg->active[lg->active_count++] = oi;
    if (o->teleport_target) {
        /* The controllers act on the player, who is put here first. */
        memcpy(lg->player, o->pos, sizeof lg->player);
        out->player_moved = true;
    }
    for (uint32_t k = 0; k < o->ctrl_count; k++) {
        bs_logic_ctrl *c = &lg->ctrls[o->first_ctrl + k];
        c->start_ms = lg->now_ms;
        if (c->sound >= 0)
            add_sound(out, c->sound, oi);
        if (c->type == BS_LOGIC_CTRL_MOVE) {
            const float *p = o->teleport_target ? lg->player : o->pos;
            for (int a = 0; a < 3; a++)
                c->start[a] = trunc_i32(p[a]);
            if (o->teleport_target)
                c->reversed = false;
        } else if (c->type == BS_LOGIC_CTRL_ROTATE) {
            memcpy(c->start, o->angles, sizeof c->start);
        }
    }
}

/* Starts an object's chain if an event matches (0x4AD6E). Returns whether
 * the event should go on to the linked objects (0: yes), as the game does. */
static int dispatch(bs_level_logic *lg, uint32_t oi, uint32_t event, bool force, bs_logic_output *out)
{
    bs_logic_object *o = &lg->objects[oi];
    if (o->ctrl_count == 0 || o->active)
        return 1;
    bool timer = false, match = force;
    for (uint32_t k = 0; k < o->ctrl_count; k++) {
        uint32_t t = lg->ctrls[o->first_ctrl + k].type;
        timer |= t == BS_LOGIC_CTRL_TIMER;
        match |= t == event;
    }
    if (match)
        start_chain(lg, oi, out);
    if (!force && timer)
        return 1;
    if (!match)
        return 1;
    return timer;
}

/* Force-starts every object this one links to (0x4AD00). */
static void start_links(bs_level_logic *lg, uint32_t oi, bs_logic_output *out)
{
    const bs_logic_object *o = &lg->objects[oi];
    for (uint32_t k = 0; k < o->link_count; k++)
        dispatch(lg, lg->links[o->first_link + k], BS_LOGIC_CTRL_TIMER, true, out);
}

void bs_level_logic_activate(bs_level_logic *lg, uint32_t oi, uint32_t event, uint32_t now_ms, bs_logic_output *out)
{
    memset(out, 0, sizeof *out);
    lg->now_ms = now_ms;
    if (!bs_level_logic_interactive(lg, oi) || lg->objects[oi].active)
        return;
    if (dispatch(lg, oi, event, false, out) == 0)
        start_links(lg, oi, out);
}

/* One coordinate of a move (0x4B7D7): towards the absolute target if the
 * axis has one, else by the offset. p is progress in 1/1000, negative when
 * running back. */
static int32_t move_axis(int32_t start, int32_t target, int32_t offset, int32_t p, bool final, bool back)
{
    if (final)
        return target ? target : start + (back ? -offset : offset);
    if (target)
        return start + (int32_t)((int64_t)(target - start) * p / 1000);
    return start + (int32_t)((int64_t)offset * p / 1000);
}

/* Returns true when done (0x4B3C6). */
static bool step_move(bs_level_logic *lg, uint32_t oi, bs_logic_ctrl *c, bs_logic_output *out)
{
    bs_logic_object *o = &lg->objects[oi];
    bool player = o->teleport_target;
    float *pos = player ? lg->player : o->pos;
    if (!player && !c->reversible && c->reversed)
        return true; /* moves once */
    int32_t elapsed = (int32_t)(lg->now_ms - c->start_ms), time = c->v[3];
    bool final = elapsed >= time;
    int32_t p = 0;
    if (!final) {
        c->running = true;
        p = (int32_t)((int64_t)elapsed * 1000 / (time ? time : 1));
        if (c->reversed)
            p = -p;
    }
    for (int a = 0; a < 3; a++) {
        float before = pos[a];
        pos[a] = (float)move_axis(c->start[a], lg->target[a], c->v[a], p, final, c->reversed);
        if (!player)
            o->delta[a] += pos[a] - before;
    }
    if (player)
        out->player_moved = true;
    else
        o->moved = true;
    if (!final)
        return false;
    if (c->continuous) {
        c->start_ms = lg->now_ms;
        return false;
    }
    if (c->running) {
        c->running = false;
        c->reversed = !c->reversed;
    }
    return true;
}

/* Returns true when done (0x4B8C3). */
static bool step_rotate(bs_level_logic *lg, uint32_t oi, bs_logic_ctrl *c)
{
    bs_logic_object *o = &lg->objects[oi];
    if (!c->reversible && c->reversed)
        return true;
    int32_t elapsed = (int32_t)(lg->now_ms - c->start_ms), time = c->v[3];
    bool final = elapsed >= time;
    for (int a = 0; a < 3; a++) {
        int32_t off = c->reversed ? -c->v[a] : c->v[a];
        o->angles[a] = final ? c->start[a] + off
                             : c->start[a] + (int32_t)((int64_t)off * elapsed / (time ? time : 1));
    }
    o->moved = true;
    if (!final) {
        c->running = true;
        return false;
    }
    if (c->continuous) {
        c->start_ms = lg->now_ms;
        if (c->reversible)
            c->reversed = !c->reversed; /* swings back and forth */
        return false;
    }
    if (c->running) {
        c->running = false;
        c->reversed = !c->reversed;
    }
    return true;
}

/* Returns true when done (0x4B1A7's per-controller cases). */
static bool step_ctrl(bs_level_logic *lg, uint32_t oi, bs_logic_ctrl *c, bs_logic_output *out)
{
    switch (c->type) {
    case BS_LOGIC_CTRL_MOVE:
        return step_move(lg, oi, c, out);
    case BS_LOGIC_CTRL_ROTATE:
        return step_rotate(lg, oi, c);
    case BS_LOGIC_CTRL_TIMER:
        memcpy(lg->target, &c->v[2], sizeof lg->target);
        if (c->v[1] && (int32_t)(lg->now_ms - c->start_ms) < c->v[1])
            return false;
        start_links(lg, oi, out);
        return true;
    case BS_LOGIC_EVENT_BUMP:
    case BS_LOGIC_EVENT_STEP:
    case BS_LOGIC_EVENT_USE:
        /* Trigger coordinates become the absolute target of later moves. */
        memcpy(lg->target, &c->v[2], sizeof lg->target);
        return true;
    default:
        return true;
    }
}

void bs_level_logic_update(bs_level_logic *lg, uint32_t now_ms, bs_logic_output *out)
{
    memset(out, 0, sizeof *out);
    lg->now_ms = now_ms;
    for (uint32_t i = 0; i < lg->object_count; i++)
        memset(lg->objects[i].delta, 0, sizeof lg->objects[i].delta);
    /* Chains started during the pass (by timers) are appended and stepped too. */
    uint32_t n_done = 0;
    static const uint32_t DONE = 0x80000000u;
    for (uint32_t i = 0; i < lg->active_count; i++) {
        uint32_t oi = lg->active[i];
        const bs_logic_object *o = &lg->objects[oi];
        bool all = true;
        for (uint32_t k = 0; k < o->ctrl_count; k++)
            all &= step_ctrl(lg, oi, &lg->ctrls[o->first_ctrl + k], out);
        if (all) {
            lg->active[i] |= DONE;
            n_done++;
        }
    }
    if (!n_done)
        return;
    uint32_t w = 0;
    for (uint32_t i = 0; i < lg->active_count; i++) {
        if (lg->active[i] & DONE)
            lg->objects[lg->active[i] & ~DONE].active = false;
        else
            lg->active[w++] = lg->active[i];
    }
    lg->active_count = w;
}
