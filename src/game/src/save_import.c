#include "bs/game/save_import.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "bs/core/fs.h"

/* RE references are GAME.EXE addresses; see docs/formats/save.md. */

#define FLAG_WORN 0x20                /* header byte +0x14 */
#define FLAG_GONE 0x02                /* header byte +0x14: a speaker that has left (0x5817E) */
#define FLAG_GREETED 0x40             /* header byte +0x13 */
#define MONSTER_SUMMONED 0x10         /* +0x226 */
#define BODY_TEAM 0x22A
#define BODY_STATE 0x232
#define BODY_SCRIPT 0x25E
#define BODY_SPACE 0x298              /* the door of the space the player is in, 0 the level (0x3065C) */
#define VARS_SCRIPT_DONE 15232        /* 0x16FFB8 */
#define SIGIL_GONE_Y 10000            /* a dispelled sigil is moved out of the way (0x5B4B8) */
/* A timed effect's body (0x295A0): the spell's name, then from +0x20 its changes, 12 bytes each in 16.16
 * (the eight attributes, spell points, health, the 21 skills), then: */
#define EFFECT_ATTRIBUTES 0x20
#define EFFECT_TICK_DAMAGE 0x194
#define EFFECT_TICK_LAST 0x198
#define EFFECT_TICK_MS 0x19C
#define EFFECT_ICON 0x1A0             /* the icon + 1 */
#define EFFECT_UNTIL 0x1A4
#define EFFECT_FLAGS 0x1A8
#define EFFECT_POOLS 0x1B4
#define EFFECT_BODY 0x1C0

static int32_t i32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

static int32_t body_i32(const bs_save_record *r, uint32_t at)
{
    return at + 4 <= r->body_size ? i32(r->body + at) : 0;
}

void bs_save_import_item(const bs_save_record *r, bs_item *out)
{
    bs_scl_item s;
    bs_save_item_parse(r, &s);
    memset(out, 0, sizeof *out);
    out->base = s.base < BS_ITEM_BASE_COUNT ? s.base : 0;
    out->magic = s.magic;
    out->quantity = s.quantity > 0 ? s.quantity : 1;
    out->condition = s.condition;
    out->condition_max = s.condition_max;
    memcpy(out->picture, s.picture, sizeof out->picture);
    out->picture[sizeof out->picture - 1] = '\0';
    /* Scrolls, books and Sigil Amulets keep their number where weapons keep the material (0x522D7,
     * 0x52384); a potion its type at +0x39 (0x51EAA). */
    if (out->base == BS_ITEM_SCROLL || out->base == BS_ITEM_BOOK || out->base == BS_ITEM_SIGIL_AMULET)
        out->number = s.material;
    else {
        out->material = s.material;
        out->number = s.number < 0 ? 0 : s.number;
    }
}

/* The character: what creation sets (bs_creation_apply), from the record as the game left it. */
static void import_character(const bs_save_character *pc, const bs_save_tree *t, uint32_t player,
                             const bs_spell *spells, bs_saved_game *out)
{
    bs_character *c = &out->character;
    bs_character_default(c);
    const bs_class *cls = &pc->cls;
    for (int i = 0; i < 8; i++)
        c->attributes[i] = pc->attributes_base[i];
    for (int i = 0; i < BS_SKILL_COUNT && i < BS_SCL_SKILLS; i++) {
        c->skills[i] = pc->skills_base[i];
        c->skill_uses[i] = pc->skill_counts[i];
    }
    c->points = pc->points;
    c->level = pc->monster_level > 0 ? pc->monster_level : cls->level > 0 ? cls->level : 1;
    c->health = pc->wounds;
    c->health_max = pc->wounds_max;
    c->spell_points = pc->spell_points;
    c->spell_points_max = pc->spell_points_max;
    c->resists = cls->resists;
    c->immune = cls->immune;
    c->low_tolerance = cls->low_tolerance;
    c->critical_weakness = cls->critical_weakness;
    c->no_spell_point_potions = cls->no_sp_regen;
    c->double_healing = cls->rapid_healing;
    c->absorbs_spells = cls->spell_absorption;
    memcpy(c->class_name, cls->class_name, sizeof c->class_name);
    memcpy(c->class_skills, cls->class_skills, sizeof c->class_skills);
    c->magery = cls->magery;
    c->acute_hearing = cls->acute_hearing;
    c->regenerate_health = cls->regenerate_health;
    c->regenerate_sp = cls->regenerate_sp;
    c->athleticism = cls->athleticism;
    c->adrenaline_rush = cls->adrenaline_rush;
    c->shield = pc->shield;
    c->confused_until = pc->confused_until;
    c->water_breathing_until = pc->water_breathing_until;
    c->has_anchor = pc->anchor[0] != 0.0f || pc->anchor[1] != 0.0f || pc->anchor[2] != 0.0f;
    memcpy(c->anchor, pc->anchor, sizeof c->anchor);
    out->rules = (bs_equip_rules){cls->forbidden_weapons, cls->max_armour, cls->max_material};
    /* The spells known are the player's spell objects, as the spell book left them. */
    for (uint32_t i = 0; i < t->count; i++) {
        const bs_save_record *r = &t->records[i];
        if (r->type != BS_SAVE_SPELL || r->parent_index != (int32_t)player || r->body_size < BS_SPELL_RECORD)
            continue;
        bs_spell sp;
        bs_spell_parse(r->body, &sp);
        if (spells && sp.effect >= 1 && sp.effect <= BS_SPELL_COUNT)
            sp.skill = spells[sp.effect - 1].skill;
        bs_character_learn(c, &sp);
    }
    /* Timed effects: the player's type-66 children, one per spell, which they name (0x295A0). Spell points,
     * health and skills changed by one aren't kept: the engine's effects don't change them. */
    uint32_t n = 0;
    for (uint32_t i = 0; i < t->count && n < BS_EFFECTS; i++) {
        const bs_save_record *r = &t->records[i];
        if (r->type != BS_SAVE_EFFECT || r->parent_index != (int32_t)player || r->body_size < EFFECT_BODY)
            continue;
        char name[33];
        memcpy(name, r->body, 32);
        name[32] = '\0';
        int spell = 0;
        for (int k = 0; k < BS_SPELL_COUNT && !spell; k++)
            if (((c->spells_known >> k) & 1 && strcmp(c->book[k].name, name) == 0) ||
                (spells && strcmp(spells[k].name, name) == 0))
                spell = k + 1;
        if (!spell)
            continue;
        bs_effect *e = &c->effects[n++];
        memset(e, 0, sizeof *e);
        e->spell = (uint8_t)spell;
        e->icon = r->body[EFFECT_ICON] ? (uint8_t)(r->body[EFFECT_ICON] - 1) : 0;
        e->until = (uint32_t)body_i32(r, EFFECT_UNTIL);
        e->flags = (uint32_t)body_i32(r, EFFECT_FLAGS);
        e->tick_damage = body_i32(r, EFFECT_TICK_DAMAGE);
        e->tick_last = (uint32_t)body_i32(r, EFFECT_TICK_LAST);
        e->tick_ms = (uint32_t)body_i32(r, EFFECT_TICK_MS);
        for (int k = 0; k < 8; k++)
            e->attributes[k] = body_i32(r, EFFECT_ATTRIBUTES + 12 * (uint32_t)k) >> 16; /* 0x294A0 */
        for (int k = 0; k < 3; k++)
            e->pools[k] = body_i32(r, EFFECT_POOLS + 4 * (uint32_t)k);
    }
    /* Hot keys: a spell's number, or 64 + n for an item (not kept: the engine's keys take spells). */
    for (int k = 0; k < BS_HOTKEYS; k++) {
        uint8_t h = pc->hotkeys[k];
        c->hotkeys[k] = h >= 1 && h <= BS_SPELL_COUNT && (c->spells_known & (1u << (h - 1))) ? h : 0;
    }
}

static void import_monster(const bs_save_record *r, int32_t team, bs_saved_monster *m)
{
    bs_save_character mc;
    bs_save_character_parse(r->body, r->body_size, &mc);
    memset(m, 0, sizeof *m);
    m->id = r->id;
    m->type = mc.creature >= 0 ? (uint32_t)mc.creature : 0;
    memcpy(m->pos, r->pos, sizeof m->pos);
    m->angle = r->angles[1] & 0x7FF;
    memcpy(m->attributes, mc.attributes_base, sizeof m->attributes);
    m->health = mc.wounds;
    m->health_max = mc.wounds_max;
    m->spell_points = mc.spell_points;
    m->spell_points_max = mc.spell_points_max;
    m->skill = mc.skills[0];
    m->flags = mc.effects;
    m->state = body_i32(r, BODY_STATE);
    m->greeted = (r->flags & FLAG_GREETED) != 0;
    m->summoned = (mc.flags & MONSTER_SUMMONED) != 0;
    m->ally = m->summoned && body_i32(r, BODY_TEAM) == team;
}

/* Where a record is: walks up to the player, a container item, the level, a level object or a monster. A
 * space's door holds what lies in it, as the level does; object ids are in their space's own file. */
static void place_item(const bs_save_tree *t, uint32_t i, uint32_t player, const int32_t *item_index,
                       const int32_t *monster_index, const bs_level_spaces *spaces, bs_saved_item *it)
{
    const bs_save_record *r = &t->records[i];
    memcpy(it->pos, r->pos, sizeof it->pos);
    uint32_t local = r->parent;
    if (r->parent == BS_SAVE_PARENT_LEVEL || (spaces && bs_level_spaces_by_door(spaces, r->parent) > 0)) {
        it->place = BS_SAVED_FLOOR;
        return;
    }
    if (r->parent_index < 0) {
        /* A level object that wasn't saved (a chest), by its BS6 id; the root holds nothing. */
        it->place = r->parent == BS_SAVE_PARENT_ROOT ? BS_SAVED_NOWHERE : BS_SAVED_IN_OBJECT;
        if (spaces)
            bs_level_spaces_of_id(spaces, r->parent, &local);
        it->ref = local;
        return;
    }
    uint32_t p = (uint32_t)r->parent_index;
    const bs_save_record *pr = &t->records[p];
    if (p == player) {
        it->place = BS_SAVED_CARRIED;
    } else if (pr->type == BS_SAVE_ITEM && item_index[p] >= 0) {
        it->place = BS_SAVED_IN_ITEM;
        it->ref = (uint32_t)item_index[p];
    } else if (pr->type == BS_SAVE_OBJECT) {
        it->place = BS_SAVED_IN_OBJECT;
        if (spaces)
            bs_level_spaces_of_id(spaces, pr->id, &local);
        it->ref = local;
    } else if (pr->type == BS_SAVE_MONSTER && monster_index[p] >= 0) {
        it->place = BS_SAVED_MONSTER;
        it->ref = (uint32_t)monster_index[p];
    } else {
        it->place = BS_SAVED_NOWHERE;
    }
}

/* The space each record is in (0 without spaces): the door it hangs under, or the space of the level object
 * holding it; what the player has is where the player is. */
static void record_spaces(const bs_save_tree *t, const bs_level_spaces *spaces, uint32_t player, int here,
                          int8_t *space)
{
    for (uint32_t i = 0; i < t->count; i++) {
        uint32_t at = i, steps = 0;
        int s = 0;
        while (spaces && steps++ <= t->count) {
            const bs_save_record *r = &t->records[at];
            uint32_t local;
            int d = bs_level_spaces_by_door(spaces, r->parent);
            if (at == player) {
                s = here;
            } else if (d > 0) {
                s = d;
            } else if (r->parent_index >= 0) {
                at = (uint32_t)r->parent_index;
                continue;
            } else if (r->parent > BS_SAVE_PARENT_LEVEL) {
                s = bs_level_spaces_of_id(spaces, r->parent, &local);
                s = s < 0 ? 0 : s;
            }
            break;
        }
        space[i] = (int8_t)s;
    }
}

/* An id in the space the player is in, as its file has it; false if it's in another. */
static bool id_here(const bs_level_spaces *spaces, int here, uint32_t id, uint32_t *local)
{
    *local = id;
    if (!spaces)
        return true;
    uint32_t l;
    int s = bs_level_spaces_of_id(spaces, id, &l);
    if (s < 0)
        return here == 0; /* not a level entity's id */
    *local = l;
    return s == here;
}

/* Each space's level in out: -1 the player's, else other_levels[k], which lists every other space with its
 * monsters (the whole world is saved). Without spaces, everything is the player's level. */
static void space_levels(const bs_level_spaces *spaces, int here, bs_saved_game *out, int32_t *group)
{
    group[0] = -1;
    for (uint32_t s = 0; spaces && s < spaces->count; s++) {
        group[s] = -1;
        if ((int)s == here || out->other_level_count == BS_SAVED_LEVELS)
            continue;
        snprintf(out->other_levels[out->other_level_count], sizeof out->other_levels[0], "%s", spaces->spaces[s].file);
        out->other_monsters[out->other_level_count] = true;
        group[s] = (int32_t)out->other_level_count++;
    }
}

/* The controllers (after the records, 0x4C3D8) as ours: each not as it began, by its chain's entity, and the
 * running chains' entities active. */
static bs_result import_controllers(const bs_save_tree *t, const bs_level_spaces *spaces, const int32_t *group,
                                    bs_arena *arena, bs_saved_game *out)
{
    bs_save_controller *ctl;
    uint32_t n, na;
    uint8_t *active;
    bs_result r = bs_save_controllers_read(t->controls, t->controls_size, spaces->chain_lengths, spaces->chain_count,
                                           arena, &ctl, &n, &active, &na);
    if (r != BS_OK)
        return r;
    out->ctrls = BS_ARENA_ARRAY(arena, bs_saved_ctrl, n + 1);
    if (!out->ctrls)
        return BS_ERR_NOMEM;
    uint32_t at = 0, s = 0;
    for (uint32_t c = 0; c < spaces->chain_count; c++) {
        while (s + 1 < spaces->count && c >= spaces->spaces[s + 1].first_chain)
            s++;
        for (uint32_t k = 0; k < spaces->chain_lengths[c] && at < n; k++, at++) {
            const bs_save_controller *sc = &ctl[at];
            if (!(sc->flags & 3))
                continue;
            bs_saved_ctrl *o = &out->ctrls[out->ctrl_count++];
            memset(o, 0, sizeof *o);
            o->object = spaces->chain_ids[c];
            o->kind = (bs_saved_kind)spaces->chain_kinds[c];
            o->index = k;
            o->reversed = (sc->flags & 2) != 0;
            o->running = (sc->flags & 1) != 0;
            o->level = group[s];
            if (o->running) {
                /* The start position or angles (+0x14, floats: 0x4BE40, 0x4BEBC); +0x20 and +0x2C aren't used. */
                o->start_ms = sc->start;
                for (int v = 0; v < 3; v++) {
                    int32_t bits = i32(sc->values + 4 * v);
                    float f;
                    memcpy(&f, &bits, sizeof f);
                    o->start[v] = isfinite(f) && fabsf(f) < 1e9f ? (int32_t)lroundf(f) : 0;
                }
            }
        }
    }
    out->have_ctrls = true;
    /* A running chain's entity is saved (it has controllers): it's active. */
    for (uint32_t k = 0; k < na; k++) {
        uint32_t c = active[k], sp = 0;
        if (c >= spaces->chain_count)
            continue;
        while (sp + 1 < spaces->count && c >= spaces->spaces[sp + 1].first_chain)
            sp++;
        for (uint32_t i = 0; i < out->object_count; i++) {
            bs_saved_object *o = &out->objects[i];
            if (o->id == spaces->chain_ids[c] && o->kind == (bs_saved_kind)spaces->chain_kinds[c] &&
                o->level == group[sp])
                o->active = true;
        }
    }
    return BS_OK;
}

bs_result bs_save_import(const bs_save_tree *t, const bs_save_vars *v, const bs_level_spaces *spaces,
                         const bs_spell *spells, bs_arena *arena, bs_saved_game *out)
{
    memset(out, 0, sizeof *out);
    int32_t player = bs_save_find(t, BS_SAVE_PLAYER_ID);
    if (player < 0 || t->records[player].type != BS_SAVE_PLAYER)
        return BS_ERR_FORMAT;
    const bs_save_record *pr = &t->records[player];
    bs_save_character pc;
    bs_save_character_parse(pr->body, pr->body_size, &pc);

    /* The space the player is in: an interior is loaded as its own level, in the campaign level's. */
    uint32_t door = (uint32_t)body_i32(pr, BODY_SPACE);
    int here = spaces && door ? bs_level_spaces_by_door(spaces, door) : 0;
    here = here < 0 ? 0 : here;
    if (here > 0) {
        snprintf(out->level_file, sizeof out->level_file, "%s", spaces->spaces[here].file);
        snprintf(out->campaign_file, sizeof out->campaign_file, "%s", spaces->spaces[0].file);
    }
    uint32_t here_door = here > 0 ? spaces->spaces[here].door : 0;

    out->level_number = v->level_number;
    out->clock = v->clock;
    memcpy(out->name, pc.name, sizeof out->name);
    out->female = (pc.flags & 8) != 0;
    out->race = pc.race;
    out->hair = pc.hair;
    out->eyes = pc.eyes;
    out->mouth = pc.mouth;
    memcpy(out->pos, pr->pos, sizeof out->pos);
    out->yaw = pr->angles[1] & 0x7FF;
    out->pitch = pr->angles[0] & 0x7FF;
    if (out->pitch >= 1024)
        out->pitch -= 2048;
    out->script_progress = pr->body_size > BODY_SCRIPT ? pr->body[BODY_SCRIPT] : 0;
    out->script_done = v->data && v->data[VARS_SCRIPT_DONE] != 0;
    out->beamed = pc.beamed;
    out->captive_at = pc.captive_at;
    out->music_volume = v->music_volume;
    out->sound_volume = v->sound_volume;
    import_character(&pc, t, (uint32_t)player, spells, out);

    int8_t *space = t->count ? BS_ARENA_ARRAY(arena, int8_t, t->count) : NULL;
    if (t->count && !space)
        return BS_ERR_NOMEM;
    record_spaces(t, spaces, (uint32_t)player, here, space);
    int32_t group[BS_LEVEL_SPACES_MAX];
    space_levels(spaces, here, out, group);

    /* Count, then fill: records index into these. A space's placed models and flats lie in it: under the
     * level or its door; a boat or the balloon being ridden is the player's (0x4D84D, 0x4D984). Those in the
     * root are the engine's own (made at 0x303FE). */
    uint32_t n_items = 0, n_monsters = 0, n_objects = 0;
    for (uint32_t i = 0; i < t->count; i++) {
        const bs_save_record *r = &t->records[i];
        bool top = r->parent == BS_SAVE_PARENT_LEVEL || r->parent_index == player ||
                   (spaces && bs_level_spaces_by_door(spaces, r->parent) > 0);
        n_items += r->type == BS_SAVE_ITEM;
        n_monsters += r->type == BS_SAVE_MONSTER && !r->reserve;
        n_objects += (r->type == BS_SAVE_OBJECT || r->type == BS_SAVE_FLAT) && top;
    }
    int32_t *item_index = t->count ? BS_ARENA_ARRAY(arena, int32_t, t->count) : NULL;
    int32_t *monster_index = t->count ? BS_ARENA_ARRAY(arena, int32_t, t->count) : NULL;
    out->items = n_items ? BS_ARENA_ARRAY(arena, bs_saved_item, n_items) : NULL;
    out->monsters = n_monsters ? BS_ARENA_ARRAY(arena, bs_saved_monster, n_monsters) : NULL;
    out->objects = n_objects ? BS_ARENA_ARRAY(arena, bs_saved_object, n_objects) : NULL;
    if ((t->count && (!item_index || !monster_index)) || (n_items && !out->items) ||
        (n_monsters && !out->monsters) || (n_objects && !out->objects))
        return BS_ERR_NOMEM;

    int32_t team = body_i32(pr, BODY_TEAM);
    for (uint32_t i = 0; i < t->count; i++) {
        const bs_save_record *r = &t->records[i];
        bool top = r->parent == BS_SAVE_PARENT_LEVEL || r->parent_index == player ||
                   (spaces && bs_level_spaces_by_door(spaces, r->parent) > 0);
        int32_t level = group[space[i]];
        item_index[i] = monster_index[i] = -1;
        /* A level entity's id in its own file; the game's own (kind << 16 | n) stay. */
        uint32_t local = r->id;
        if (spaces && space[i] > 0) {
            const bs_level_space *sp = &spaces->spaces[space[i]];
            if (r->id >= sp->base && r->id - sp->base <= sp->last_id)
                local = r->id - sp->base;
        }
        if (r->type == BS_SAVE_ITEM) {
            item_index[i] = (int32_t)out->item_count;
            bs_saved_item *it = &out->items[out->item_count++];
            memset(it, 0, sizeof *it);
            bs_save_import_item(r, &it->item);
            it->id = local;
            it->level = level;
            it->worn = (r->object[0x14] & FLAG_WORN) != 0;
            it->secondary = pc.secondary != 0 && r->id == pc.secondary;
        } else if (r->type == BS_SAVE_MONSTER && !r->reserve) {
            monster_index[i] = (int32_t)out->monster_count;
            bs_saved_monster *m = &out->monsters[out->monster_count++];
            import_monster(r, team, m);
            m->id = local;
            m->level = level;
        } else if ((r->type == BS_SAVE_OBJECT || r->type == BS_SAVE_FLAT) && top) {
            if (r->parent_index == player)
                out->ride = local;
            bs_saved_object *o = &out->objects[out->object_count++];
            memset(o, 0, sizeof *o);
            o->id = local;
            o->level = level;
            o->kind = r->type == BS_SAVE_FLAT ? BS_SAVED_KIND_FLAT : BS_SAVED_KIND_OBJECT;
            o->gone = (r->object[0x14] & FLAG_GONE) != 0;
            memcpy(o->pos, r->pos, sizeof o->pos);
            for (int k = 0; k < 3; k++)
                o->angles[k] = r->angles[k] & 0x7FF;
            o->flags = r->flags;
            o->time = r->time;
        } else if (r->type == BS_SAVE_AUTOMAP && r->parent_index == player && r->time == here_door) {
            /* Each space has its grid, with its door's id at +0x21 (0x454C4). */
            uint32_t n = r->body_size < BS_SAVE_AUTOMAP_CELLS ? r->body_size : BS_SAVE_AUTOMAP_CELLS;
            for (uint32_t k = 0; k < n; k++)
                out->seen[k] = r->body[k] != 0;
            out->have_map = true;
        }
    }
    /* Items: parents come before children in the file, but placing doesn't depend on it. */
    for (uint32_t i = 0; i < t->count; i++)
        if (item_index[i] >= 0)
            place_item(t, i, (uint32_t)player, item_index, monster_index, spaces, &out->items[item_index[i]]);
    /* A controller table that doesn't fit the spaces leaves them as the levels begin. */
    if (spaces && import_controllers(t, spaces, group, arena, out) == BS_ERR_NOMEM)
        return BS_ERR_NOMEM;
    /* An item under one not kept is gone with it. */
    for (uint32_t k = 0; k < out->item_count; k++) {
        uint32_t at = k, steps = 0;
        while (out->items[at].place == BS_SAVED_IN_ITEM && steps++ < out->item_count)
            at = out->items[at].ref;
        if (out->items[at].place == BS_SAVED_NOWHERE || steps > out->item_count) {
            out->items[k].place = BS_SAVED_NOWHERE;
            out->items[k].ref = 0;
        }
    }

    /* The variables (0x5AB76), as the dialogue keeps them. */
    bs_dialogue_state *d = &out->dialogue;
    bs_dialogue_state_init(d);
    d->global_count = v->global_count < BS_DIALOGUE_GLOBALS ? v->global_count : BS_DIALOGUE_GLOBALS;
    for (uint32_t i = 0; i < d->global_count; i++)
        d->globals[i] = (bs_dialogue_var){v->globals[i].hash, v->globals[i].value};
    for (uint32_t i = 0; i < BS_DIALOGUE_LOCALS && i < BS_SAVE_LOCALS; i++) {
        d->locals[i].id = v->locals[i].id;
        for (int k = 0; k < BS_DIALOGUE_LOCAL_VARS; k++)
            d->locals[i].vars[k] = (bs_dialogue_var){v->locals[i].vars[k].hash, v->locals[i].vars[k].value};
    }
    memcpy(d->visits, v->visits, sizeof d->visits);
    /* Speakers leaving: those of the space the player is in (the engine keeps them for its level only). */
    for (int i = 0, n = 0; i < 8; i++) {
        uint32_t id;
        if (!v->leaving[i].id || !id_here(spaces, here, v->leaving[i].id, &id))
            continue;
        out->leaving[n].id = id;
        out->leaving[n++].at = v->leaving[i].at;
    }
    /* Sigils dispelled, each in its space's level. */
    for (uint32_t i = 0; i < v->sigil_count; i++) {
        uint32_t id = v->sigils[i].id;
        int s = spaces ? bs_level_spaces_of_id(spaces, id, &id) : 0;
        if (v->sigils[i].y != SIGIL_GONE_Y || s < 0)
            continue;
        out->sigils_gone[out->sigil_gone_count] = id;
        out->sigil_gone_level[out->sigil_gone_count++] = group[s];
    }
    for (uint32_t i = 0; i < v->link_count; i++)
        out->links[out->link_count++] = v->links[i].id;
    return BS_OK;
}
