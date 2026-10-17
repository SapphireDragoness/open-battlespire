#include "bs/game/save_import.h"

#include <string.h>

/* RE references are GAME.EXE addresses; see docs/formats/save.md. */

#define FLAG_WORN 0x20                /* header byte +0x14 */
#define FLAG_GREETED 0x40             /* header byte +0x13 */
#define MONSTER_SUMMONED 0x10         /* +0x226 */
#define BODY_TEAM 0x22A
#define BODY_STATE 0x232
#define BODY_SCRIPT 0x25E
#define VARS_SCRIPT_DONE 15232        /* 0x16FFB8 */
#define SIGIL_GONE_Y 10000            /* a dispelled sigil is moved out of the way (0x5B4B8) */

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

/* Where a record is: walks up to the player, a container item, the level, a level object or a monster. */
static void place_item(const bs_save_tree *t, uint32_t i, uint32_t player, const int32_t *item_index,
                       const int32_t *monster_index, bs_saved_item *it)
{
    const bs_save_record *r = &t->records[i];
    memcpy(it->pos, r->pos, sizeof it->pos);
    if (r->parent == BS_SAVE_PARENT_LEVEL) {
        it->place = BS_SAVED_FLOOR;
        return;
    }
    if (r->parent_index < 0) {
        /* A level object that wasn't saved (a chest), by its BS6 id; the root holds nothing. */
        it->place = r->parent == BS_SAVE_PARENT_ROOT ? BS_SAVED_NOWHERE : BS_SAVED_IN_OBJECT;
        it->ref = r->parent;
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
        it->ref = pr->id;
    } else if (pr->type == BS_SAVE_MONSTER && monster_index[p] >= 0) {
        it->place = BS_SAVED_MONSTER;
        it->ref = (uint32_t)monster_index[p];
    } else {
        it->place = BS_SAVED_NOWHERE;
    }
}

bs_result bs_save_import(const bs_save_tree *t, const bs_save_vars *v, const bs_spell *spells, bs_arena *arena,
                         bs_saved_game *out)
{
    memset(out, 0, sizeof *out);
    int32_t player = bs_save_find(t, BS_SAVE_PLAYER_ID);
    if (player < 0 || t->records[player].type != BS_SAVE_PLAYER)
        return BS_ERR_FORMAT;
    const bs_save_record *pr = &t->records[player];
    bs_save_character pc;
    bs_save_character_parse(pr->body, pr->body_size, &pc);

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
    out->music_volume = v->music_volume;
    out->sound_volume = v->sound_volume;
    import_character(&pc, t, (uint32_t)player, spells, out);

    /* Count, then fill: records index into these. */
    uint32_t n_items = 0, n_monsters = 0, n_objects = 0;
    for (uint32_t i = 0; i < t->count; i++) {
        const bs_save_record *r = &t->records[i];
        n_items += r->type == BS_SAVE_ITEM;
        n_monsters += r->type == BS_SAVE_MONSTER && !r->reserve;
        n_objects += r->type == BS_SAVE_OBJECT && (r->parent == BS_SAVE_PARENT_LEVEL || r->parent_index == player);
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
        item_index[i] = monster_index[i] = -1;
        if (r->type == BS_SAVE_ITEM) {
            item_index[i] = (int32_t)out->item_count;
            bs_saved_item *it = &out->items[out->item_count++];
            memset(it, 0, sizeof *it);
            bs_save_import_item(r, &it->item);
            it->id = r->id;
            it->level = -1;
            it->worn = (r->object[0x14] & FLAG_WORN) != 0;
            it->secondary = pc.secondary != 0 && r->id == pc.secondary;
        } else if (r->type == BS_SAVE_MONSTER && !r->reserve) {
            monster_index[i] = (int32_t)out->monster_count;
            import_monster(r, team, &out->monsters[out->monster_count++]);
        } else if (r->type == BS_SAVE_OBJECT && (r->parent == BS_SAVE_PARENT_LEVEL || r->parent_index == player)) {
            /* The level's placed models; those in the root are the engine's own (made at 0x303FE). A boat or the
             * balloon being ridden is the player's (0x4D84D, 0x4D984). */
            if (r->parent_index == player)
                out->ride = r->id;
            bs_saved_object *o = &out->objects[out->object_count++];
            memset(o, 0, sizeof *o);
            o->id = r->id;
            memcpy(o->pos, r->pos, sizeof o->pos);
            for (int k = 0; k < 3; k++)
                o->angles[k] = r->angles[k] & 0x7FF;
            o->flags = r->flags;
            o->time = r->time;
        } else if (r->type == BS_SAVE_AUTOMAP && r->parent_index == player) {
            uint32_t n = r->body_size < BS_SAVE_AUTOMAP_CELLS ? r->body_size : BS_SAVE_AUTOMAP_CELLS;
            for (uint32_t k = 0; k < n; k++)
                out->seen[k] = r->body[k] != 0;
            out->have_map = true;
        }
    }
    /* Items: parents come before children in the file, but placing doesn't depend on it. */
    for (uint32_t i = 0; i < t->count; i++)
        if (item_index[i] >= 0)
            place_item(t, i, (uint32_t)player, item_index, monster_index, &out->items[item_index[i]]);
    /* An item under one not kept is gone with it. */
    for (uint32_t k = 0; k < out->item_count; k++) {
        uint32_t at = k, steps = 0;
        while (out->items[at].place == BS_SAVED_IN_ITEM && steps++ < out->item_count)
            at = out->items[at].ref;
        if (out->items[at].place == BS_SAVED_NOWHERE || steps > out->item_count)
            out->items[k].place = BS_SAVED_NOWHERE;
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
    for (int i = 0; i < 8; i++) {
        out->leaving[i].id = v->leaving[i].id;
        out->leaving[i].at = v->leaving[i].at;
    }
    for (uint32_t i = 0; i < v->sigil_count; i++)
        if (v->sigils[i].y == SIGIL_GONE_Y)
            out->sigils_gone[out->sigil_gone_count++] = v->sigils[i].id;
    for (uint32_t i = 0; i < v->link_count; i++)
        out->links[out->link_count++] = v->links[i].id;
    return BS_OK;
}
