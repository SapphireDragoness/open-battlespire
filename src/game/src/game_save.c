#include "bs/game/game_save.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/core/fs.h"
#include "bs/core/ini.h"
#include "bs/core/log.h"
#include "bs/game/use.h"

/* See docs/formats/game_save.md. */

#define MAP_COLS 40
#define MAP_ROWS 30

static const char *const race_codes[6] = {"rd", "br", "no", "hi", "dk", "wd"};
static const char *const kind_names[3] = {"object", "flat", "light"};

/* The advantages and disadvantages kept as words in [player] advantages. */
#define ADVANTAGES 8
static const char *const advantage_names[ADVANTAGES] = {
    "acute_hearing", "regenerate_health", "regenerate_sp", "athleticism",
    "adrenaline_rush", "double_healing", "absorbs_spells", "no_spell_point_potions",
};

static bool *advantage(bs_character *c, int k)
{
    bool *const p[ADVANTAGES] = {&c->acute_hearing,   &c->regenerate_health, &c->regenerate_sp,   &c->athleticism,
                                 &c->adrenaline_rush, &c->double_healing,    &c->absorbs_spells, &c->no_spell_point_potions};
    return p[k];
}

static const char *yes(bool b)
{
    return b ? "yes" : "no";
}

/* ---- writing ---- */

static void put_ints(bs_ini_writer *w, const char *key, const char *comment, const int32_t *v, int n)
{
    char s[256];
    size_t at = 0;
    for (int k = 0; k < n && at < sizeof s; k++)
        at += (size_t)snprintf(s + at, sizeof s - at, "%s%d", k ? " " : "", (int)v[k]);
    bs_ini_put(w, key, comment, "%s", s);
}

/* A coordinate to a thousandth, without trailing zeros (positions settle by millionths). */
static void coord(char *out, size_t cap, float v)
{
    double r = (double)lround((double)v * 1000.0) / 1000.0;
    snprintf(out, cap, "%.3f", r == 0.0 ? 0.0 : r);
    char *dot = strchr(out, '.');
    if (dot) {
        char *end = out + strlen(out) - 1;
        while (end > dot && *end == '0')
            *end-- = '\0';
        if (end == dot)
            *end = '\0';
    }
}

static void put_pos(bs_ini_writer *w, const char *key, const float p[3])
{
    char x[32], y[32], z[32];
    coord(x, sizeof x, p[0]);
    coord(y, sizeof y, p[1]);
    coord(z, sizeof z, p[2]);
    bs_ini_put(w, key, NULL, "%s %s %s", x, y, z);
}

static const char *name_of(const bs_game_save_names *n, int which, uint32_t v)
{
    if (!n)
        return NULL;
    if (which == 0 && n->item)
        return n->item(n->ctx, v);
    if (which == 1 && n->creature)
        return n->creature(n->ctx, v);
    if (which == 2 && n->variable)
        return n->variable(n->ctx, v);
    return NULL;
}

static void put_var(bs_ini_writer *w, const bs_game_save_names *names, uint32_t hash, int32_t value)
{
    const char *name = name_of(names, 2, hash);
    if (name)
        bs_ini_put(w, name, NULL, "%d", (int)value);
    else {
        char key[16];
        snprintf(key, sizeof key, "@%u", (unsigned)hash);
        bs_ini_put(w, key, NULL, "%d", (int)value);
    }
}

static void write_player(bs_ini_writer *w, const bs_saved_game *g)
{
    const bs_character *c = &g->character;
    bs_ini_section_begin(w, "player");
    bs_ini_put_str(w, "name", g->name);
    bs_ini_put(w, "female", NULL, "%s", yes(g->female));
    bs_ini_put(w, "race", "rd br no hi dk wd", "%s", g->race >= 0 && g->race < 6 ? race_codes[g->race] : "br");
    bs_ini_put(w, "hair", NULL, "%u", (unsigned)g->hair);
    bs_ini_put(w, "eyes", NULL, "%u", (unsigned)g->eyes);
    bs_ini_put(w, "mouth", NULL, "%u", (unsigned)g->mouth);
    put_pos(w, "pos", g->pos);
    bs_ini_put(w, "yaw", "2048 a turn: 0 faces +z, 512 +x", "%d", (int)g->yaw);
    bs_ini_put(w, "pitch", "+ looks down", "%d", (int)g->pitch);
    bs_ini_put(w, "level", NULL, "%d", (int)c->level);
    bs_ini_put(w, "health", NULL, "%d", (int)c->health);
    bs_ini_put(w, "health_max", NULL, "%d", (int)c->health_max);
    bs_ini_put(w, "spell_points", NULL, "%d", (int)c->spell_points);
    bs_ini_put(w, "spell_points_max", NULL, "%d", (int)c->spell_points_max);
    for (int k = 0; k < 8; k++)
        bs_ini_put(w, bs_attribute_keys[k], NULL, "%d", (int)c->attributes[k]);
    for (int k = 0; k < BS_SKILL_COUNT; k++)
        bs_ini_put(w, bs_skill_keys[k], NULL, "%d", (int)c->skills[k]);
    put_ints(w, "skill_uses", "towards each skill's next rise, in the order above", c->skill_uses, BS_SKILL_COUNT);
    bs_ini_put(w, "points", "level-up points not spent", "%d", (int)c->points);
    if (g->ride)
        bs_ini_put(w, "ride", "the boat or balloon ridden, by its object's id", "%u", (unsigned)g->ride);
    bs_ini_put_str(w, "class", c->class_name);
    int32_t cs[12];
    for (int k = 0; k < 12; k++)
        cs[k] = c->class_skills[k] == 0xFF ? -1 : c->class_skills[k];
    put_ints(w, "class_skills", "primary 3, major 4, minor 5, by skill number; -1 none", cs, 12);
    bs_ini_put(w, "magery", "spell points per Intelligence x 256", "%d", (int)c->magery);
    bs_ini_put(w, "resists", "element bits: 1 magic 2 fire 4 cold 8 shock 16 poison", "%u", (unsigned)c->resists);
    bs_ini_put(w, "immune", NULL, "%u", (unsigned)c->immune);
    bs_ini_put(w, "low_tolerance", NULL, "%u", (unsigned)c->low_tolerance);
    bs_ini_put(w, "critical_weakness", NULL, "%u", (unsigned)c->critical_weakness);
    char adv[256] = "";
    for (int k = 0; k < ADVANTAGES; k++)
        if (*advantage((bs_character *)c, k)) {
            strcat(adv, adv[0] ? " " : "");
            strcat(adv, advantage_names[k]);
        }
    bs_ini_put(w, "advantages", NULL, "%s", adv);
    bs_ini_put(w, "forbidden_weapons", "1 short blades, 2 long, 4 blunt, 8 axes, 16 missile", "%u",
               (unsigned)g->rules.forbidden_weapons);
    bs_ini_put(w, "armour", "the heaviest: 0 none .. 3 heavy", "%u", (unsigned)g->rules.armour);
    bs_ini_put(w, "material", "the best: 0 Iron .. 9 Daedric", "%u", (unsigned)g->rules.material);
    bs_ini_put(w, "shield", NULL, "%d", (int)c->shield);
    bs_ini_put(w, "confused_until", NULL, "%u", (unsigned)c->confused_until);
    bs_ini_put(w, "water_breathing_until", NULL, "%u", (unsigned)c->water_breathing_until);
    if (c->has_anchor)
        put_pos(w, "anchor", c->anchor);
    int32_t hk[BS_HOTKEYS];
    for (int k = 0; k < BS_HOTKEYS; k++)
        hk[k] = c->hotkeys[k];
    put_ints(w, "hotkeys", "F1-F8: a spell's number, 0 none", hk, BS_HOTKEYS);
    bs_ini_put(w, "script_progress", "the level's keys placed", "%u", (unsigned)g->script_progress);
    bs_ini_put(w, "script_done", NULL, "%s", yes(g->script_done));
    for (int n = 1; n <= BS_SPELL_COUNT; n++) {
        if (!(c->spells_known & (1u << (n - 1))))
            continue;
        const bs_spell *s = &c->book[n - 1];
        bs_ini_section_begin(w, "spell");
        bs_ini_put(w, "number", s->name[0] ? s->name : NULL, "%d", n);
        bs_ini_put(w, "icon", NULL, "%u", (unsigned)s->icon);
        bs_ini_put(w, "element", NULL, "%u", (unsigned)s->element);
        bs_ini_put(w, "delivery", NULL, "%u", (unsigned)s->delivery);
        bs_ini_put(w, "detonation", NULL, "%d", (int)s->detonation);
    }
    for (int k = 0; k < BS_EFFECTS; k++) {
        const bs_effect *e = &c->effects[k];
        if (!e->spell)
            continue;
        bs_ini_section_begin(w, "effect");
        bs_ini_put(w, "spell", NULL, "%u", (unsigned)e->spell);
        bs_ini_put(w, "icon", NULL, "%u", (unsigned)e->icon);
        bs_ini_put(w, "flags", NULL, "0x%x", (unsigned)e->flags);
        bs_ini_put(w, "until", NULL, "%u", (unsigned)e->until);
        bs_ini_put(w, "tick_damage", NULL, "%d", (int)e->tick_damage);
        bs_ini_put(w, "tick_ms", NULL, "%u", (unsigned)e->tick_ms);
        bs_ini_put(w, "tick_last", NULL, "%u", (unsigned)e->tick_last);
        put_ints(w, "attributes", NULL, e->attributes, 8);
        put_ints(w, "pools", NULL, e->pools, 3);
    }
}

bs_result bs_game_save_write(const bs_saved_game *g, const bs_game_save_names *names, char **text, size_t *size)
{
    bs_ini_writer w;
    bs_ini_writer_init(&w);
    bs_ini_printf(&w, "# OpenBattlespire saved game (docs/formats/game_save.md)\n");
    bs_ini_section_begin(&w, "save");
    bs_ini_put(&w, "version", NULL, "%d", BS_GAME_SAVE_VERSION);
    bs_ini_put_str(&w, "name", g->save_name);
    bs_ini_put_str(&w, "level", g->level_file);
    if (g->campaign_file[0])
        bs_ini_put_str(&w, "campaign", g->campaign_file);
    for (uint32_t k = 0; k < g->other_level_count; k++)
        bs_ini_put_str(&w, "other_level", g->other_levels[k]);
    bs_ini_put(&w, "level_number", NULL, "%d", g->level_number);
    bs_ini_put(&w, "clock", "ms", "%u", (unsigned)g->clock);
    if (g->captive_at)
        bs_ini_put(&w, "captive_at", NULL, "%u", (unsigned)g->captive_at);
    write_player(&w, g);

    for (uint32_t i = 0; i < g->item_count; i++) {
        const bs_saved_item *it = &g->items[i];
        bs_ini_section_begin(&w, "item");
        bs_ini_put(&w, "id", NULL, "%u", (unsigned)it->id);
        bs_ini_put(&w, "base", name_of(names, 0, it->item.base), "%u", (unsigned)it->item.base);
        if (it->item.material)
            bs_ini_put(&w, "material", NULL, "%u", (unsigned)it->item.material);
        if (it->item.magic)
            bs_ini_put(&w, "magic", NULL, "%d", (int)it->item.magic);
        if (it->item.number)
            bs_ini_put(&w, "number", NULL, "%d", (int)it->item.number);
        if (it->item.quantity != 1)
            bs_ini_put(&w, "quantity", NULL, "%d", (int)it->item.quantity);
        bs_ini_put(&w, "condition", NULL, "%d", (int)it->item.condition);
        bs_ini_put(&w, "condition_max", NULL, "%d", (int)it->item.condition_max);
        if (it->item.picture[0])
            bs_ini_put_str(&w, "picture", it->item.picture);
        switch (it->place) {
        case BS_SAVED_CARRIED: bs_ini_put(&w, "where", NULL, "carried"); break;
        case BS_SAVED_IN_ITEM:
            bs_ini_put(&w, "where", NULL, "item %u", (unsigned)(it->ref < g->item_count ? g->items[it->ref].id : 0));
            break;
        case BS_SAVED_FLOOR: bs_ini_put(&w, "where", NULL, "floor"); break;
        case BS_SAVED_IN_OBJECT: bs_ini_put(&w, "where", NULL, "object %u", (unsigned)it->ref); break;
        case BS_SAVED_MONSTER:
            bs_ini_put(&w, "where", NULL, "monster %u",
                       (unsigned)(it->ref < g->monster_count ? g->monsters[it->ref].id : 0));
            break;
        case BS_SAVED_NOWHERE: bs_ini_put(&w, "where", NULL, "nowhere"); break;
        }
        if (it->place == BS_SAVED_FLOOR)
            put_pos(&w, "pos", it->pos);
        if (it->worn)
            bs_ini_put(&w, "worn", NULL, "yes");
        if (it->secondary)
            bs_ini_put(&w, "secondary", NULL, "yes");
        if (it->level >= 0 && (uint32_t)it->level < g->other_level_count)
            bs_ini_put_str(&w, "level", g->other_levels[it->level]);
    }
    for (uint32_t i = 0; i < g->monster_count; i++) {
        const bs_saved_monster *m = &g->monsters[i];
        bs_ini_section_begin(&w, "monster");
        bs_ini_put(&w, "id", "a spawned one's is its spawn point's flat's BS6 id", "%u", (unsigned)m->id);
        bs_ini_put(&w, "creature", name_of(names, 1, m->type), "%u", (unsigned)m->type);
        put_pos(&w, "pos", m->pos);
        bs_ini_put(&w, "angle", NULL, "%d", (int)m->angle);
        bs_ini_put(&w, "health", NULL, "%d", (int)m->health);
        bs_ini_put(&w, "health_max", NULL, "%d", (int)m->health_max);
        bs_ini_put(&w, "spell_points", NULL, "%d", (int)m->spell_points);
        bs_ini_put(&w, "spell_points_max", NULL, "%d", (int)m->spell_points_max);
        put_ints(&w, "attributes", "strength .. luck", m->attributes, 8);
        bs_ini_put(&w, "skill", NULL, "%d", (int)m->skill);
        bs_ini_put(&w, "flags", NULL, "0x%x", (unsigned)m->flags);
        bs_ini_put(&w, "state", "the AI state: 0 attack, 9 idle, 13 waits to talk ...", "%d", (int)m->state);
        if (m->greeted)
            bs_ini_put(&w, "greeted", NULL, "yes");
        if (m->summoned)
            bs_ini_put(&w, "summoned", NULL, "yes");
        if (m->ally)
            bs_ini_put(&w, "ally", NULL, "yes");
    }
    for (uint32_t i = 0; i < g->object_count; i++) {
        const bs_saved_object *o = &g->objects[i];
        bs_ini_section_begin(&w, "object");
        bs_ini_put(&w, "id", NULL, "%u", (unsigned)o->id);
        if (o->kind != BS_SAVED_KIND_OBJECT)
            bs_ini_put(&w, "kind", NULL, "%s", kind_names[o->kind]);
        put_pos(&w, "pos", o->pos);
        put_ints(&w, "angles", NULL, o->angles, 3);
        if (o->flags & BS_SAVED_OBJECT_LOCKED)
            bs_ini_put(&w, "locked", NULL, "yes");
        if (o->time)
            bs_ini_put(&w, "gem_until", "a used gem comes back", "%u", (unsigned)o->time);
        if (o->active)
            bs_ini_put(&w, "active", NULL, "yes");
    }
    for (uint32_t i = 0; i < g->ctrl_count; i++) {
        const bs_saved_ctrl *c = &g->ctrls[i];
        bs_ini_section_begin(&w, "controller");
        bs_ini_put(&w, "object", NULL, "%u", (unsigned)c->object);
        if (c->kind != BS_SAVED_KIND_OBJECT)
            bs_ini_put(&w, "kind", NULL, "%s", kind_names[c->kind]);
        bs_ini_put(&w, "index", NULL, "%u", (unsigned)c->index);
        bs_ini_put(&w, "reversed", NULL, "%s", yes(c->reversed));
        if (c->running) {
            bs_ini_put(&w, "running", NULL, "yes");
            bs_ini_put(&w, "start_ms", NULL, "%u", (unsigned)c->start_ms);
            put_ints(&w, "start", NULL, c->start, 3);
        }
    }
    if (g->sigil_gone_count) {
        bs_ini_section_begin(&w, "sigils");
        int32_t ids[BS_SAVE_SIGILS];
        for (uint32_t k = 0; k < g->sigil_gone_count; k++)
            ids[k] = (int32_t)g->sigils_gone[k];
        put_ints(&w, "gone", "their BS6 ids", ids, (int)g->sigil_gone_count);
    }
    for (int m = g->have_maps ? 0 : -1; m < (g->have_maps ? BS_SAVED_MAPS : 0); m++) {
        const uint8_t *cells = m < 0 ? g->seen : g->maps[m];
        bool any = false;
        for (int k = 0; k < BS_SAVE_AUTOMAP_CELLS && !any; k++)
            any = cells[k] != 0;
        if (m < 0 && !g->have_map)
            break;
        if (!any)
            continue;
        bs_ini_section_begin(&w, "automap");
        bs_ini_put(&w, "map", m < 0 ? "the level's" : "the automap table's", "%d", m);
        for (int r = 0; r < MAP_ROWS; r++) {
            char row[MAP_COLS + 1];
            for (int c = 0; c < MAP_COLS; c++)
                row[c] = cells[r * MAP_COLS + c] ? 'x' : '.';
            row[MAP_COLS] = '\0';
            bs_ini_put(&w, "row", NULL, "%s", row);
        }
    }
    const bs_dialogue_state *d = &g->dialogue;
    bs_ini_section_begin(&w, "dialogue");
    int32_t visits[BS_DIALOGUE_CREATURES];
    for (int k = 0; k < BS_DIALOGUE_CREATURES; k++)
        visits[k] = d->visits[k];
    put_ints(&w, "visits", "conversations per creature type", visits, BS_DIALOGUE_CREATURES);
    for (int k = 0; k < 8; k++)
        if (g->leaving[k].id)
            bs_ini_put(&w, "leaving", "id, when", "%u %u", (unsigned)g->leaving[k].id, (unsigned)g->leaving[k].at);
    bs_ini_section_begin(&w, "variables");
    for (uint32_t k = 0; k < d->global_count; k++)
        put_var(&w, names, d->globals[k].hash, d->globals[k].value);
    for (int l = 0; l < BS_DIALOGUE_LOCALS; l++) {
        if (!d->locals[l].id)
            continue;
        bs_ini_section_begin(&w, "locals");
        bs_ini_put(&w, "id", NULL, "%u", (unsigned)d->locals[l].id);
        for (int k = 0; k < BS_DIALOGUE_LOCAL_VARS; k++)
            if (d->locals[l].vars[k].hash)
                put_var(&w, names, d->locals[l].vars[k].hash, d->locals[l].vars[k].value);
    }
    if (w.failed) {
        bs_ini_writer_free(&w);
        return BS_ERR_NOMEM;
    }
    *text = w.text;
    *size = w.len;
    return BS_OK;
}

/* ---- reading ---- */

typedef struct reader {
    const bs_game_save_names *names;
    const bs_ini_entry *at;          /* the entry being read, for errors */
    char *err;
    bool failed;
} reader;

static void bad(reader *r, const char *fmt, ...) BS_PRINTF_FMT(2, 3);

static void bad(reader *r, const char *fmt, ...)
{
    if (r->failed)
        return;
    r->failed = true;
    char why[120];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    if (r->err) {
        if (r->at)
            snprintf(r->err, BS_GAME_SAVE_ERROR, "line %u: %s", (unsigned)r->at->line, why);
        else
            snprintf(r->err, BS_GAME_SAVE_ERROR, "%s", why);
    }
}

static int64_t num(reader *r, const char *s, int64_t lo, int64_t hi)
{
    char *end;
    errno = 0;
    long long v = strtoll(s, &end, 0);
    if (errno || end == s || *end || v < lo || v > hi) {
        bad(r, "%s: '%s' isn't a number from %lld to %lld", r->at ? r->at->key : "", s, (long long)lo,
            (long long)hi);
        return lo < 0 && hi > 0 ? 0 : lo;
    }
    return v;
}

static int32_t i32v(reader *r, const char *s)
{
    return (int32_t)num(r, s, INT32_MIN, INT32_MAX);
}

static uint32_t u32v(reader *r, const char *s)
{
    return (uint32_t)num(r, s, 0, UINT32_MAX);
}

static bool boolv(reader *r, const char *s)
{
    if (!strcmp(s, "yes") || !strcmp(s, "true") || !strcmp(s, "1"))
        return true;
    if (!strcmp(s, "no") || !strcmp(s, "false") || !strcmp(s, "0"))
        return false;
    bad(r, "%s: '%s' isn't yes or no", r->at->key, s);
    return false;
}

/* n numbers separated by blanks. */
static void ints(reader *r, const char *s, int32_t *out, int n)
{
    const char *p = s;
    for (int k = 0; k < n; k++) {
        char *end;
        errno = 0;
        long long v = strtoll(p, &end, 0);
        if (errno || end == p || v < INT32_MIN || v > INT32_MAX) {
            bad(r, "%s: expected %d numbers", r->at->key, n);
            return;
        }
        out[k] = (int32_t)v;
        p = end;
    }
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p)
        bad(r, "%s: expected %d numbers", r->at->key, n);
}

static void floats(reader *r, const char *s, float *out, int n)
{
    const char *p = s;
    for (int k = 0; k < n; k++) {
        char *end;
        errno = 0;
        float v = strtof(p, &end);
        if (errno || end == p) {
            bad(r, "%s: expected %d numbers", r->at->key, n);
            return;
        }
        out[k] = v;
        p = end;
    }
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p)
        bad(r, "%s: expected %d numbers", r->at->key, n);
}

static void str(reader *r, const char *s, char *out, size_t cap)
{
    if (strlen(s) >= cap)
        bad(r, "%s: longer than %u characters", r->at->key, (unsigned)cap - 1);
    snprintf(out, cap, "%s", s);
}

static int key_in(const char *key, const char *const *list, int n)
{
    for (int k = 0; k < n; k++)
        if (!strcmp(key, list[k]))
            return k;
    return -1;
}

static void unknown(reader *r, const char *section)
{
    bad(r, "no key '%s' in [%s]", r->at->key, section);
}

static void read_save(reader *r, const bs_ini_section *s, bs_saved_game *g)
{
    for (uint32_t i = 0; i < s->count && !r->failed; i++) {
        const bs_ini_entry *e = r->at = &s->entries[i];
        if (!strcmp(e->key, "version")) {
            int64_t v = num(r, e->value, 1, INT32_MAX);
            if (v > BS_GAME_SAVE_VERSION)
                bad(r, "version %d is newer than this game's (%d)", (int)v, BS_GAME_SAVE_VERSION);
        } else if (!strcmp(e->key, "name"))
            str(r, e->value, g->save_name, sizeof g->save_name);
        else if (!strcmp(e->key, "level"))
            str(r, e->value, g->level_file, sizeof g->level_file);
        else if (!strcmp(e->key, "campaign"))
            str(r, e->value, g->campaign_file, sizeof g->campaign_file);
        else if (!strcmp(e->key, "other_level")) {
            if (g->other_level_count == BS_SAVED_LEVELS)
                bad(r, "more than %d other levels", BS_SAVED_LEVELS);
            else
                str(r, e->value, g->other_levels[g->other_level_count++], sizeof g->other_levels[0]);
        } else if (!strcmp(e->key, "level_number"))
            g->level_number = (int)num(r, e->value, 1, 99);
        else if (!strcmp(e->key, "clock"))
            g->clock = u32v(r, e->value);
        else if (!strcmp(e->key, "captive_at"))
            g->captive_at = u32v(r, e->value);
        else
            unknown(r, "save");
    }
}

static void read_player(reader *r, const bs_ini_section *s, bs_saved_game *g)
{
    bs_character *c = &g->character;
    for (uint32_t i = 0; i < s->count && !r->failed; i++) {
        const bs_ini_entry *e = r->at = &s->entries[i];
        const char *k = e->key, *v = e->value;
        int n;
        if ((n = key_in(k, bs_attribute_keys, 8)) >= 0)
            c->attributes[n] = (int32_t)num(r, v, 0, 1000);
        else if ((n = key_in(k, bs_skill_keys, BS_SKILL_COUNT)) >= 0)
            c->skills[n] = (int32_t)num(r, v, 0, 1000);
        else if (!strcmp(k, "name"))
            str(r, v, g->name, sizeof g->name);
        else if (!strcmp(k, "female"))
            g->female = boolv(r, v);
        else if (!strcmp(k, "race")) {
            g->race = key_in(v, race_codes, 6);
            if (g->race < 0)
                g->race = (int)num(r, v, 0, 5);
        } else if (!strcmp(k, "hair"))
            g->hair = (uint8_t)num(r, v, 0, 255);
        else if (!strcmp(k, "eyes"))
            g->eyes = (uint8_t)num(r, v, 0, 255);
        else if (!strcmp(k, "mouth"))
            g->mouth = (uint8_t)num(r, v, 0, 255);
        else if (!strcmp(k, "pos"))
            floats(r, v, g->pos, 3);
        else if (!strcmp(k, "yaw"))
            g->yaw = (int32_t)num(r, v, 0, 2047);
        else if (!strcmp(k, "pitch"))
            g->pitch = (int32_t)num(r, v, -1024, 1023);
        else if (!strcmp(k, "level"))
            c->level = (int32_t)num(r, v, 1, 100);
        else if (!strcmp(k, "health"))
            c->health = i32v(r, v);
        else if (!strcmp(k, "health_max"))
            c->health_max = (int32_t)num(r, v, 1, 1000000);
        else if (!strcmp(k, "spell_points"))
            c->spell_points = (int32_t)num(r, v, 0, 65535);
        else if (!strcmp(k, "spell_points_max"))
            c->spell_points_max = (int32_t)num(r, v, 0, 65535);
        else if (!strcmp(k, "skill_uses")) {
            ints(r, v, c->skill_uses, BS_SKILL_COUNT);
            for (int q = 0; q < BS_SKILL_COUNT && !r->failed; q++)
                if (c->skill_uses[q] < 0 || c->skill_uses[q] > 20000)
                    bad(r, "skill_uses: %d is out of range (0 .. 20000)", (int)c->skill_uses[q]);
        } else if (!strcmp(k, "points"))
            c->points = (int32_t)num(r, v, -1000000, 1000000);
        else if (!strcmp(k, "ride"))
            g->ride = (uint32_t)num(r, v, 0, UINT32_MAX);
        else if (!strcmp(k, "class"))
            str(r, v, c->class_name, sizeof c->class_name);
        else if (!strcmp(k, "class_skills")) {
            int32_t cs[12];
            ints(r, v, cs, 12);
            for (int q = 0; q < 12 && !r->failed; q++) {
                if (cs[q] < -1 || cs[q] >= BS_SKILL_COUNT)
                    bad(r, "class_skills: no skill %d", (int)cs[q]);
                c->class_skills[q] = cs[q] < 0 ? 0xFF : (uint8_t)cs[q];
            }
        } else if (!strcmp(k, "magery"))
            c->magery = (int32_t)num(r, v, 0, 100000);
        else if (!strcmp(k, "resists"))
            c->resists = (uint8_t)num(r, v, 0, 255);
        else if (!strcmp(k, "immune"))
            c->immune = (uint8_t)num(r, v, 0, 255);
        else if (!strcmp(k, "low_tolerance"))
            c->low_tolerance = (uint8_t)num(r, v, 0, 255);
        else if (!strcmp(k, "critical_weakness"))
            c->critical_weakness = (uint8_t)num(r, v, 0, 255);
        else if (!strcmp(k, "advantages")) {
            for (int q = 0; q < ADVANTAGES; q++)
                *advantage(c, q) = false;
            char word[32];
            for (const char *p = v; *p && !r->failed;) {
                while (*p == ' ' || *p == '\t')
                    p++;
                size_t len = 0;
                while (p[len] && p[len] != ' ' && p[len] != '\t')
                    len++;
                if (!len)
                    break;
                snprintf(word, sizeof word, "%.*s", (int)(len < 31 ? len : 31), p);
                int q = key_in(word, advantage_names, ADVANTAGES);
                if (q < 0)
                    bad(r, "advantages: no '%s'", word);
                else
                    *advantage(c, q) = true;
                p += len;
            }
        } else if (!strcmp(k, "forbidden_weapons"))
            g->rules.forbidden_weapons = (uint8_t)num(r, v, 0, 31);
        else if (!strcmp(k, "armour"))
            g->rules.armour = (uint8_t)num(r, v, 0, 3);
        else if (!strcmp(k, "material"))
            g->rules.material = (uint8_t)num(r, v, 0, 9);
        else if (!strcmp(k, "shield"))
            c->shield = i32v(r, v);
        else if (!strcmp(k, "confused_until"))
            c->confused_until = u32v(r, v);
        else if (!strcmp(k, "water_breathing_until"))
            c->water_breathing_until = u32v(r, v);
        else if (!strcmp(k, "anchor")) {
            floats(r, v, c->anchor, 3);
            c->has_anchor = true;
        } else if (!strcmp(k, "hotkeys")) {
            int32_t hk[BS_HOTKEYS];
            ints(r, v, hk, BS_HOTKEYS);
            for (int q = 0; q < BS_HOTKEYS && !r->failed; q++) {
                if (hk[q] < 0 || hk[q] > BS_SPELL_COUNT)
                    bad(r, "hotkeys: no spell %d", (int)hk[q]);
                else
                    c->hotkeys[q] = (uint8_t)hk[q];
            }
        } else if (!strcmp(k, "script_progress"))
            g->script_progress = (uint8_t)num(r, v, 0, 255);
        else if (!strcmp(k, "script_done"))
            g->script_done = boolv(r, v);
        else
            unknown(r, "player");
    }
}

static void read_spell(reader *r, const bs_ini_section *s, const bs_spell *spells, bs_character *c)
{
    const char *n = bs_ini_get(s, "number");
    r->at = s->count ? &s->entries[0] : NULL;
    if (!n) {
        bad(r, "[spell] at line %u has no number", (unsigned)s->line);
        return;
    }
    int number = (int)num(r, n, 1, BS_SPELL_COUNT);
    if (r->failed)
        return;
    bs_spell sp;
    if (spells)
        sp = spells[number - 1];
    else {
        memset(&sp, 0, sizeof sp);
        sp.effect = (uint8_t)number;
        sp.skill = -1;
    }
    for (uint32_t i = 0; i < s->count && !r->failed; i++) {
        const bs_ini_entry *e = r->at = &s->entries[i];
        if (!strcmp(e->key, "number"))
            continue;
        if (!strcmp(e->key, "icon"))
            sp.icon = (uint8_t)num(r, e->value, 0, 26);
        else if (!strcmp(e->key, "element"))
            sp.element = (uint8_t)num(r, e->value, 0, BS_ELEMENT_COUNT - 1);
        else if (!strcmp(e->key, "delivery"))
            sp.delivery = (uint8_t)num(r, e->value, 0, BS_DELIVERY_COUNT - 1);
        else if (!strcmp(e->key, "detonation"))
            sp.detonation = (int32_t)num(r, e->value, 0, 16);
        else
            unknown(r, "spell");
    }
    c->spells_known |= 1u << (number - 1);
    c->book[number - 1] = sp;
}

static void read_effect(reader *r, const bs_ini_section *s, bs_character *c)
{
    bs_effect *e = NULL;
    for (int k = 0; k < BS_EFFECTS && !e; k++)
        if (!c->effects[k].spell)
            e = &c->effects[k];
    r->at = s->count ? &s->entries[0] : NULL;
    if (!e) {
        bad(r, "more than %d effects", BS_EFFECTS);
        return;
    }
    for (uint32_t i = 0; i < s->count && !r->failed; i++) {
        const bs_ini_entry *en = r->at = &s->entries[i];
        const char *k = en->key, *v = en->value;
        if (!strcmp(k, "spell"))
            e->spell = (uint8_t)num(r, v, 1, BS_SPELL_COUNT);
        else if (!strcmp(k, "icon"))
            e->icon = (uint8_t)num(r, v, 0, 26);
        else if (!strcmp(k, "flags"))
            e->flags = u32v(r, v);
        else if (!strcmp(k, "until"))
            e->until = u32v(r, v);
        else if (!strcmp(k, "tick_damage"))
            e->tick_damage = i32v(r, v);
        else if (!strcmp(k, "tick_ms"))
            e->tick_ms = u32v(r, v);
        else if (!strcmp(k, "tick_last"))
            e->tick_last = u32v(r, v);
        else if (!strcmp(k, "attributes"))
            ints(r, v, e->attributes, 8);
        else if (!strcmp(k, "pools"))
            ints(r, v, e->pools, 3);
        else
            unknown(r, "effect");
    }
    if (!e->spell && !r->failed)
        bad(r, "[effect] at line %u has no spell", (unsigned)s->line);
}

/* A base item by number or name. */
static uint32_t base_of(reader *r, const char *v)
{
    if (v[0] >= '0' && v[0] <= '9')
        return (uint32_t)num(r, v, 0, BS_ITEM_BASE_COUNT - 1);
    for (uint32_t b = 0; r->names && r->names->item && b < BS_ITEM_BASE_COUNT; b++) {
        const char *n = r->names->item(r->names->ctx, b);
        if (n && bs_strcasecmp(n, v) == 0)
            return b;
    }
    bad(r, "base: no item '%s'", v);
    return 0;
}

/* where: carried, floor, nowhere, item ID, object ID, monster ID; the ids resolved later. */
static void read_where(reader *r, const char *v, bs_saved_item *it, uint32_t *ref_id)
{
    static const char *const words[] = {"carried", "floor", "nowhere", "item", "object", "monster"};
    static const bs_saved_place places[] = {BS_SAVED_CARRIED, BS_SAVED_FLOOR,     BS_SAVED_NOWHERE,
                                            BS_SAVED_IN_ITEM, BS_SAVED_IN_OBJECT, BS_SAVED_MONSTER};
    size_t len = 0;
    while (v[len] && v[len] != ' ' && v[len] != '\t')
        len++;
    for (int k = 0; k < 6; k++)
        if (strlen(words[k]) == len && !strncmp(v, words[k], len)) {
            it->place = places[k];
            const char *rest = v + len;
            while (*rest == ' ' || *rest == '\t')
                rest++;
            if (k < 3) {
                if (*rest)
                    bad(r, "where: nothing goes after '%s'", words[k]);
            } else {
                *ref_id = u32v(r, rest);
            }
            return;
        }
    bad(r, "where: '%s' isn't carried, floor, nowhere, item ID, object ID or monster ID", v);
}

static void read_item(reader *r, const bs_ini_section *s, bs_saved_game *g, bs_saved_item *it, uint32_t *ref_id)
{
    memset(it, 0, sizeof *it);
    it->level = -1;
    it->item.quantity = 1;
    it->place = BS_SAVED_CARRIED;
    bool have_base = false;
    for (uint32_t i = 0; i < s->count && !r->failed; i++) {
        const bs_ini_entry *e = r->at = &s->entries[i];
        const char *k = e->key, *v = e->value;
        if (!strcmp(k, "id"))
            it->id = u32v(r, v);
        else if (!strcmp(k, "base")) {
            it->item.base = (uint16_t)base_of(r, v);
            have_base = true;
        } else if (!strcmp(k, "material"))
            it->item.material = (uint16_t)num(r, v, 0, 65535);
        else if (!strcmp(k, "magic"))
            it->item.magic = i32v(r, v);
        else if (!strcmp(k, "number"))
            it->item.number = i32v(r, v);
        else if (!strcmp(k, "quantity"))
            it->item.quantity = (int32_t)num(r, v, 1, 1000000);
        else if (!strcmp(k, "condition"))
            it->item.condition = i32v(r, v);
        else if (!strcmp(k, "condition_max"))
            it->item.condition_max = i32v(r, v);
        else if (!strcmp(k, "picture"))
            str(r, v, it->item.picture, sizeof it->item.picture);
        else if (!strcmp(k, "where"))
            read_where(r, v, it, ref_id);
        else if (!strcmp(k, "pos"))
            floats(r, v, it->pos, 3);
        else if (!strcmp(k, "worn"))
            it->worn = boolv(r, v);
        else if (!strcmp(k, "secondary"))
            it->secondary = boolv(r, v);
        else if (!strcmp(k, "level")) {
            it->level = -1;
            for (uint32_t q = 0; q < g->other_level_count; q++)
                if (!strcmp(g->other_levels[q], v))
                    it->level = (int32_t)q;
            if (it->level < 0)
                bad(r, "level: '%s' isn't one of [save]'s other_level", v);
        } else
            unknown(r, "item");
    }
    if (!have_base && !r->failed) {
        r->at = NULL;
        bad(r, "[item] at line %u has no base", (unsigned)s->line);
    }
}

static void read_monster(reader *r, const bs_ini_section *s, bs_saved_monster *m)
{
    memset(m, 0, sizeof *m);
    for (uint32_t i = 0; i < s->count && !r->failed; i++) {
        const bs_ini_entry *e = r->at = &s->entries[i];
        const char *k = e->key, *v = e->value;
        if (!strcmp(k, "id"))
            m->id = u32v(r, v);
        else if (!strcmp(k, "creature"))
            m->type = (uint32_t)num(r, v, 0, 15);
        else if (!strcmp(k, "pos"))
            floats(r, v, m->pos, 3);
        else if (!strcmp(k, "angle"))
            m->angle = (int32_t)num(r, v, 0, 2047);
        else if (!strcmp(k, "health"))
            m->health = i32v(r, v);
        else if (!strcmp(k, "health_max"))
            m->health_max = i32v(r, v);
        else if (!strcmp(k, "spell_points"))
            m->spell_points = i32v(r, v);
        else if (!strcmp(k, "spell_points_max"))
            m->spell_points_max = i32v(r, v);
        else if (!strcmp(k, "attributes"))
            ints(r, v, m->attributes, 8);
        else if (!strcmp(k, "skill"))
            m->skill = i32v(r, v);
        else if (!strcmp(k, "flags"))
            m->flags = u32v(r, v);
        else if (!strcmp(k, "state"))
            m->state = (int32_t)num(r, v, 0, 31);
        else if (!strcmp(k, "greeted"))
            m->greeted = boolv(r, v);
        else if (!strcmp(k, "summoned"))
            m->summoned = boolv(r, v);
        else if (!strcmp(k, "ally"))
            m->ally = boolv(r, v);
        else
            unknown(r, "monster");
    }
}

static bs_saved_kind kind_of(reader *r, const char *v)
{
    int k = key_in(v, kind_names, 3);
    if (k < 0)
        bad(r, "kind: '%s' isn't object, flat or light", v);
    return k < 0 ? BS_SAVED_KIND_OBJECT : (bs_saved_kind)k;
}

static void read_object(reader *r, const bs_ini_section *s, bs_saved_object *o)
{
    memset(o, 0, sizeof *o);
    for (uint32_t i = 0; i < s->count && !r->failed; i++) {
        const bs_ini_entry *e = r->at = &s->entries[i];
        const char *k = e->key, *v = e->value;
        if (!strcmp(k, "id"))
            o->id = u32v(r, v);
        else if (!strcmp(k, "kind"))
            o->kind = kind_of(r, v);
        else if (!strcmp(k, "pos"))
            floats(r, v, o->pos, 3);
        else if (!strcmp(k, "angles"))
            ints(r, v, o->angles, 3);
        else if (!strcmp(k, "locked"))
            o->flags = boolv(r, v) ? BS_SAVED_OBJECT_LOCKED : 0;
        else if (!strcmp(k, "gem_until"))
            o->time = u32v(r, v);
        else if (!strcmp(k, "active"))
            o->active = boolv(r, v);
        else
            unknown(r, "object");
    }
}

static void read_ctrl(reader *r, const bs_ini_section *s, bs_saved_ctrl *c)
{
    memset(c, 0, sizeof *c);
    for (uint32_t i = 0; i < s->count && !r->failed; i++) {
        const bs_ini_entry *e = r->at = &s->entries[i];
        const char *k = e->key, *v = e->value;
        if (!strcmp(k, "object"))
            c->object = u32v(r, v);
        else if (!strcmp(k, "kind"))
            c->kind = kind_of(r, v);
        else if (!strcmp(k, "index"))
            c->index = (uint32_t)num(r, v, 0, 255);
        else if (!strcmp(k, "reversed"))
            c->reversed = boolv(r, v);
        else if (!strcmp(k, "running"))
            c->running = boolv(r, v);
        else if (!strcmp(k, "start_ms"))
            c->start_ms = u32v(r, v);
        else if (!strcmp(k, "start"))
            ints(r, v, c->start, 3);
        else
            unknown(r, "controller");
    }
}

static void read_automap(reader *r, const bs_ini_section *s, bs_saved_game *g)
{
    const char *m = bs_ini_get(s, "map");
    r->at = s->count ? &s->entries[0] : NULL;
    if (!m) {
        bad(r, "[automap] at line %u has no map", (unsigned)s->line);
        return;
    }
    int map = (int)num(r, m, -1, BS_SAVED_MAPS - 1);
    uint8_t *cells = map < 0 ? g->seen : g->maps[map];
    int row = 0;
    for (uint32_t i = 0; i < s->count && !r->failed; i++) {
        const bs_ini_entry *e = r->at = &s->entries[i];
        if (!strcmp(e->key, "map"))
            continue;
        if (strcmp(e->key, "row")) {
            unknown(r, "automap");
            break;
        }
        if (row == MAP_ROWS || strlen(e->value) != MAP_COLS) {
            bad(r, "the automap has %d rows of %d cells ('x' seen, '.' not)", MAP_ROWS, MAP_COLS);
            break;
        }
        for (int c = 0; c < MAP_COLS; c++)
            cells[row * MAP_COLS + c] = e->value[c] != '.';
        row++;
    }
    if (map < 0)
        g->have_map = true;
    else
        g->have_maps = true;
}

/* A variable's key: a name, or @hash. */
static uint32_t var_hash(reader *r, const char *key)
{
    if (key[0] == '@')
        return u32v(r, key + 1);
    return bs_save_variable_hash(key);
}

static void read_dialogue(reader *r, const bs_ini_section *s, bs_saved_game *g)
{
    uint32_t leaving = 0;
    for (uint32_t i = 0; i < s->count && !r->failed; i++) {
        const bs_ini_entry *e = r->at = &s->entries[i];
        if (!strcmp(e->key, "visits")) {
            int32_t v[BS_DIALOGUE_CREATURES];
            ints(r, e->value, v, BS_DIALOGUE_CREATURES);
            for (int k = 0; k < BS_DIALOGUE_CREATURES; k++)
                g->dialogue.visits[k] = (uint8_t)(v[k] < 0 ? 0 : v[k] > 255 ? 255 : v[k]);
        } else if (!strcmp(e->key, "leaving")) {
            int32_t v[2];
            ints(r, e->value, v, 2);
            if (leaving == 8)
                bad(r, "at most 8 speakers leaving");
            else {
                g->leaving[leaving].id = (uint32_t)v[0];
                g->leaving[leaving++].at = (uint32_t)v[1];
            }
        } else {
            unknown(r, "dialogue");
        }
    }
}

static void read_vars(reader *r, const bs_ini_section *s, bs_dialogue_state *d)
{
    for (uint32_t i = 0; i < s->count && !r->failed; i++) {
        const bs_ini_entry *e = r->at = &s->entries[i];
        uint32_t h = var_hash(r, e->key);
        int32_t v = i32v(r, e->value);
        uint32_t k = 0;
        while (k < d->global_count && d->globals[k].hash != h)
            k++;
        if (k == BS_DIALOGUE_GLOBALS) {
            bad(r, "more than %d variables", BS_DIALOGUE_GLOBALS);
            return;
        }
        if (k == d->global_count)
            d->global_count++;
        d->globals[k] = (bs_dialogue_var){h, v};
    }
}

static void read_locals(reader *r, const bs_ini_section *s, bs_dialogue_state *d)
{
    bs_dialogue_local *l = NULL;
    for (int k = 0; k < BS_DIALOGUE_LOCALS && !l; k++)
        if (!d->locals[k].id)
            l = &d->locals[k];
    r->at = s->count ? &s->entries[0] : NULL;
    const char *id = bs_ini_get(s, "id");
    if (!l || !id) {
        bad(r, l ? "[locals] at line %u has no id" : "too many [locals] (line %u)", (unsigned)s->line);
        return;
    }
    l->id = u32v(r, id);
    int n = 0;
    for (uint32_t i = 0; i < s->count && !r->failed; i++) {
        const bs_ini_entry *e = r->at = &s->entries[i];
        if (!strcmp(e->key, "id"))
            continue;
        if (n == BS_DIALOGUE_LOCAL_VARS) {
            bad(r, "a speaker has at most %d variables", BS_DIALOGUE_LOCAL_VARS);
            return;
        }
        l->vars[n].hash = var_hash(r, e->key);
        l->vars[n++].value = i32v(r, e->value);
    }
}

bs_result bs_game_save_read(const char *text, size_t size, const bs_spell *spells, const bs_game_save_names *names,
                            bs_arena *arena, bs_saved_game *g, char err[BS_GAME_SAVE_ERROR])
{
    memset(g, 0, sizeof *g);
    bs_character_default(&g->character);
    g->rules = bs_equip_rules_any();
    bs_dialogue_state_init(&g->dialogue);
    if (err)
        err[0] = '\0';
    bs_ini ini;
    uint32_t line;
    bs_result res = bs_ini_parse(text, size, arena, &ini, &line);
    if (res != BS_OK) {
        if (err)
            snprintf(err, BS_GAME_SAVE_ERROR, "line %u: not a [section], key = value or # comment", (unsigned)line);
        return res;
    }
    reader r = {.names = names, .err = err};
    /* Counts first, for the arrays. */
    uint32_t n_items = 0, n_monsters = 0, n_objects = 0, n_ctrls = 0;
    bool have_save = false, have_player = false;
    for (uint32_t i = 0; i < ini.count; i++) {
        const char *s = ini.sections[i].name;
        n_items += !strcmp(s, "item");
        n_monsters += !strcmp(s, "monster");
        n_objects += !strcmp(s, "object");
        n_ctrls += !strcmp(s, "controller");
        have_save = have_save || !strcmp(s, "save");
        have_player = have_player || !strcmp(s, "player");
    }
    if (!have_save || !have_player) {
        bad(&r, "no [save] or [player]: not a saved game");
        return BS_ERR_FORMAT;
    }
    g->items = n_items ? BS_ARENA_ARRAY(arena, bs_saved_item, n_items) : NULL;
    uint32_t *item_refs = n_items ? BS_ARENA_ARRAY(arena, uint32_t, n_items) : NULL;
    g->monsters = n_monsters ? BS_ARENA_ARRAY(arena, bs_saved_monster, n_monsters) : NULL;
    g->objects = n_objects ? BS_ARENA_ARRAY(arena, bs_saved_object, n_objects) : NULL;
    g->ctrls = n_ctrls ? BS_ARENA_ARRAY(arena, bs_saved_ctrl, n_ctrls) : NULL;
    if ((n_items && (!g->items || !item_refs)) || (n_monsters && !g->monsters) || (n_objects && !g->objects) ||
        (n_ctrls && !g->ctrls))
        return BS_ERR_NOMEM;
    g->have_ctrls = true;
    /* [save] first: the items' levels name its other levels. */
    for (uint32_t i = 0; i < ini.count && !r.failed; i++)
        if (!strcmp(ini.sections[i].name, "save"))
            read_save(&r, &ini.sections[i], g);
    if (!r.failed && !g->level_number) {
        r.at = NULL;
        bad(&r, "[save] has no level_number");
    }
    for (uint32_t i = 0; i < ini.count && !r.failed; i++) {
        const bs_ini_section *s = &ini.sections[i];
        const char *n = s->name;
        r.at = NULL;
        if (!strcmp(n, "save") || (!strcmp(n, "") && s->count == 0))
            continue;
        if (!strcmp(n, "player"))
            read_player(&r, s, g);
        else if (!strcmp(n, "spell"))
            read_spell(&r, s, spells, &g->character);
        else if (!strcmp(n, "effect"))
            read_effect(&r, s, &g->character);
        else if (!strcmp(n, "item")) {
            item_refs[g->item_count] = 0;
            read_item(&r, s, g, &g->items[g->item_count], &item_refs[g->item_count]);
            g->item_count++;
        } else if (!strcmp(n, "monster"))
            read_monster(&r, s, &g->monsters[g->monster_count++]);
        else if (!strcmp(n, "object"))
            read_object(&r, s, &g->objects[g->object_count++]);
        else if (!strcmp(n, "controller"))
            read_ctrl(&r, s, &g->ctrls[g->ctrl_count++]);
        else if (!strcmp(n, "sigils")) {
            for (uint32_t k = 0; k < s->count && !r.failed; k++) {
                r.at = &s->entries[k];
                if (strcmp(r.at->key, "gone")) {
                    unknown(&r, "sigils");
                    break;
                }
                for (const char *p = r.at->value; *p && !r.failed;) {
                    char *end;
                    unsigned long id = strtoul(p, &end, 10);
                    if (end == p) {
                        bad(&r, "gone: a list of ids");
                        break;
                    }
                    if (g->sigil_gone_count == BS_SAVE_SIGILS)
                        bad(&r, "more than %d sigils", BS_SAVE_SIGILS);
                    else
                        g->sigils_gone[g->sigil_gone_count++] = (uint32_t)id;
                    p = end;
                    while (*p == ' ' || *p == '\t')
                        p++;
                }
            }
        } else if (!strcmp(n, "automap"))
            read_automap(&r, s, g);
        else if (!strcmp(n, "dialogue"))
            read_dialogue(&r, s, g);
        else if (!strcmp(n, "variables"))
            read_vars(&r, s, &g->dialogue);
        else if (!strcmp(n, "locals"))
            read_locals(&r, s, &g->dialogue);
        else
            bad(&r, "line %u: no section [%s]", (unsigned)s->line, n);
    }
    /* Places by id: a container item, a monster. */
    for (uint32_t i = 0; i < g->item_count && !r.failed; i++) {
        bs_saved_item *it = &g->items[i];
        uint32_t want = item_refs[i];
        if (it->place == BS_SAVED_IN_OBJECT) {
            it->ref = want;
        } else if (it->place == BS_SAVED_IN_ITEM) {
            uint32_t k = 0;
            while (k < g->item_count && (g->items[k].id != want || k == i))
                k++;
            if (k == g->item_count)
                bad(&r, "item %u is in item %u, which isn't there", (unsigned)it->id, (unsigned)want);
            it->ref = k;
        } else if (it->place == BS_SAVED_MONSTER) {
            uint32_t k = 0;
            while (k < g->monster_count && g->monsters[k].id != want)
                k++;
            if (k == g->monster_count)
                bad(&r, "item %u is held by monster %u, which isn't there", (unsigned)it->id, (unsigned)want);
            it->ref = k;
        }
    }
    if (r.failed)
        return BS_ERR_FORMAT;
    g->character.spell_points = g->character.spell_points > g->character.spell_points_max
                                    ? g->character.spell_points_max
                                    : g->character.spell_points;
    for (int k = 0; k < BS_HOTKEYS; k++) /* a hot key's spell must be known */
        if (g->character.hotkeys[k] > BS_SPELL_COUNT ||
            (g->character.hotkeys[k] && !(g->character.spells_known & (1u << (g->character.hotkeys[k] - 1)))))
            g->character.hotkeys[k] = 0;
    return BS_OK;
}
