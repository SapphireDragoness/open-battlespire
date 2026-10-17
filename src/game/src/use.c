#include "bs/game/use.h"

#include <string.h>

/* RE references are GAME.EXE addresses; see docs/formats/items.md, "Using items". */

#define SPELL_SCROLL 10000           /* a scroll's number from here on is a spell (0x501E3) */
#define POTION_RESTORATION 40
#define POTION_WATER_BREATHING 39
#define WATER_BREATHING_MS 300000    /* 0x493E0 */
#define DURATION_UNIT_MS 5000        /* 0x28A69 */

enum {
    MSG_HAVE_SPELL = 243, MSG_SPELL_ADDED = 244, MSG_NO_REGENERATION = 245, MSG_FULL_SPELL_POINTS = 246,
    MSG_SPELL_POINTS_RESTORED = 247, MSG_WATER_BREATHING = 432, MSG_DRINK = 433,
};

const char *const bs_attribute_keys[8] = {"strength", "intelligence", "willpower", "agility",
                                          "endurance", "personality", "speed", "luck"};
/* The skill list's order (GAME.EXE's names: Medical .. Critical Strike). */
const char *const bs_skill_keys[BS_SKILL_COUNT] = {
    "medical",     "jumping",     "lockpicking",  "stealth",   "swimming",    "backstabbing", "dodging",
    "running",     "destruction", "restoration",  "illusion",  "alteration",  "thaumaturgy",  "mysticism",
    "short_blade", "long_blade",  "hand_to_hand", "axe",       "blunt_weapon", "missile",     "critical_strike",
};
const uint8_t bs_skill_attribute[BS_SKILL_COUNT] = {0, 6, 3, 3, 4, 3, 6, 6, 2, 5, 5, 2, 2, 5, 3, 0, 0, 0, 0, 3, 1};

void bs_character_default(bs_character *c)
{
    memset(c, 0, sizeof *c);
    c->level = 1;
    c->health = c->health_max = 50;
    c->spell_points = c->spell_points_max = 50;
    for (int i = 0; i < 8; i++)
        c->attributes[i] = 50;
    for (int i = 0; i < BS_SKILL_COUNT; i++)
        c->skills[i] = 50;
    memset(c->class_skills, 0xFF, sizeof c->class_skills);
    c->magery = 0x100;
}

uint32_t bs_spell_duration(const bs_spell *s, int32_t caster_level)
{
    if (s->duration_base == 0)
        return 0;
    int32_t step = s->duration_step > 0 ? s->duration_step : 1;
    int32_t units = s->duration_base + caster_level / step * s->duration_increase;
    return units > 0 ? (uint32_t)units * DURATION_UNIT_MS : 0;
}

int32_t bs_spell_magnitude(const bs_spell *s, int level_number, bs_rng *r)
{
    if (s->magnitude_min == 0)
        return 0;
    int32_t step = s->magnitude_step > 0 ? s->magnitude_step : 1;
    int32_t extra = level_number / step * bs_rng_range(r, s->increase_min, s->increase_max);
    return bs_rng_range(r, s->magnitude_min, s->magnitude_max) + extra;
}

int32_t bs_spell_cost(const bs_spell *s, int32_t skill)
{
    static const int32_t delivery_percent[BS_DELIVERY_COUNT] = {80, 90, 100, 110, 120};
    int32_t cost = s->cost * (110 - skill) / 100;
    cost = cost * delivery_percent[s->delivery < BS_DELIVERY_COUNT ? s->delivery : BS_DELIVERY_RANGE] / 100;
    return cost < 5 ? 5 : cost;
}

uint8_t bs_spell_next_delivery(const bs_spell *s, uint8_t delivery)
{
    if (s->target == BS_TARGET_SELF)
        return delivery;
    uint8_t d = (uint8_t)((delivery + 1) % BS_DELIVERY_COUNT);
    if ((s->target & 1) && d == BS_DELIVERY_CASTER)
        d++;
    return d;
}

static void say(bs_use_result *out, uint32_t id, int32_t a, int32_t b, int32_t c)
{
    if (out->message_count < BS_USE_MESSAGES)
        out->messages[out->message_count++] = (bs_use_message){id, {a, b, c}};
}

bool bs_character_has(const bs_character *c, int spell, uint32_t now_ms)
{
    for (uint32_t i = 0; i < BS_EFFECTS; i++)
        if (c->effects[i].spell && c->effects[i].spell == spell && c->effects[i].until > now_ms)
            return true;
    return false;
}

bool bs_character_learn(bs_character *c, const bs_spell *s)
{
    if (s->effect < 1 || s->effect > BS_SPELL_COUNT || (c->spells_known & (1u << (s->effect - 1))))
        return false;
    c->spells_known |= 1u << (s->effect - 1);
    c->book[s->effect - 1] = *s;
    return true;
}

/* One of a stack used up, or the item. */
static void use_up(bs_items *it, uint32_t i)
{
    bs_world_item *w = &it->items[i];
    if (w->item.quantity > 1)
        w->item.quantity--;
    else
        w->place.where = BS_ITEM_GONE;
}

bs_use_result bs_items_use(bs_items *it, const bs_item_db *db, uint32_t i, bs_character *c,
                           const bs_spell spells[BS_SPELL_COUNT], int level_number, uint32_t now_ms, bs_rng *r)
{
    bs_use_result out;
    memset(&out, 0, sizeof out);
    (void)db;
    (void)level_number; /* the spell potions' rolls happen in bs_magic_cast_on_player */
    (void)r;
    if (i >= it->count || it->items[i].place.where == BS_ITEM_GONE)
        return out;
    const bs_item *x = &it->items[i].item;
    if (x->base == BS_ITEM_SCROLL && x->number >= SPELL_SCROLL) {
        int32_t n = x->number - SPELL_SCROLL;
        if (n < 1 || n > BS_SPELL_COUNT)
            return out;
        out.kind = BS_USE_DONE;
        bs_spell learnt;
        if (spells) {
            learnt = spells[n - 1];
        } else {
            memset(&learnt, 0, sizeof learnt);
            learnt.effect = (uint8_t)n;
        }
        if (!bs_character_learn(c, &learnt)) {
            say(&out, MSG_HAVE_SPELL, 0, 0, 0);
        } else {
            say(&out, MSG_SPELL_ADDED, 0, 0, 0);
            use_up(it, i);
        }
        return out;
    }
    if (x->base == BS_ITEM_SCROLL || x->base == BS_ITEM_BOOK) {
        out.kind = BS_USE_READ;
        out.book = x->number;
        return out;
    }
    if (x->base == BS_ITEM_POTION) {
        out.kind = BS_USE_DONE;
        if (x->number == POTION_RESTORATION) {
            /* 0x50315 */
            if (c->no_spell_point_potions) {
                say(&out, MSG_NO_REGENERATION, 0, 0, 0);
            } else if (c->spell_points == c->spell_points_max) {
                say(&out, MSG_FULL_SPELL_POINTS, 0, 0, 0);
            } else {
                c->spell_points = c->spell_points_max;
                say(&out, MSG_SPELL_POINTS_RESTORED, 0, 0, 0);
            }
        } else if (x->number == POTION_WATER_BREATHING) {
            c->water_breathing_until = now_ms + WATER_BREATHING_MS;
            say(&out, MSG_WATER_BREATHING, 0, 0, 0);
        } else {
            say(&out, MSG_DRINK, 0, 0, 0);
            if (x->number >= 1 && x->number <= BS_SPELL_COUNT && spells)
                out.spell = x->number;
        }
        /* Every potion drunk is used up, even one that did nothing (0x50438). */
        use_up(it, i);
        return out;
    }
    return out;
}
