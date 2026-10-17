#include "bs/game/creation.h"

#include <string.h>

/* RE references are GAME.EXE addresses; see docs/re/README.md, "Character creation". */

#define NEW_CLASS_COST 300            /* Increase Magery x 1, paid by a new class (0x3B273) */
#define ATTRIBUTE_MIN 10
#define ATTRIBUTE_MAX 75
#define WOUNDS_MIN 50
#define WOUNDS_MAX 200
#define SKILL_MIN 5
#define SKILL_MAX 60
#define SPELL_COST_FACTOR 8
#define SKILL_MEDICAL 0
#define SKILL_LOCKPICKING 2
#define SKILL_RUNNING 7

/* The lists' cost factors (0x38172, 0x382FC, 0x3848A). */
static const int32_t list_factor[3] = {50, 60, 70};
/* What a skill picked for an empty slot starts at (0x38618). */
static const int32_t skill_start[3] = {30, 20, 15};
/* Increase Magery's steps (x 256) and what each costs (0x3A022). */
static const int32_t magery_step[BS_CREATION_MAGERY_STEPS] = {0x100, 0x180, 0x200, 0x300};
static const int32_t magery_cost[BS_CREATION_MAGERY_STEPS] = {300, 1000, 1800, 2500};
/* The armour and material limits' worth (0xEE5B3, 0xEE5C3): none -2000 .. heavy 0, Iron -2000 .. Daedric 0. */
static const int32_t armour_worth[4] = {-2000, -1500, -1000, 0};
static const int32_t material_worth[10] = {-2000, -1800, -1600, -1400, -1200, -1000, -800, -600, -400, 0};
/* Forbidding a weapon group gives (0x3A4CE ..): short blades, long blades, blunt, axes, missile. */
static const int32_t weapon_worth[5] = {400, 800, 600, 400, 1000};
/* The flags' costs (0x39D50 ..); disadvantages negative. */
static const int32_t flag_cost[BS_CREATION_FLAGS] = {200, 2500, 700, 2000, 2000, 400, 200, -1000, 300};
static const int32_t tolerance_cost[BS_TOLERANCES] = {800, 1600, -800, -1600};

/* The game's x 256 / d, x m, >> 8 (an arithmetic shift, as these are never negative). */
static int32_t scaled(int32_t v, int32_t d, int32_t m)
{
    return (v * 256 / d * m) >> 8;
}

int32_t bs_creation_attribute_cost(int32_t v)
{
    return scaled(v, 100, 80) + 40;
}

int32_t bs_creation_wounds_cost(int32_t w)
{
    return scaled(w, 200, 50) + 40;
}

int32_t bs_creation_skill_cost(int32_t v, bs_creation_list list)
{
    /* m x (v x 256 / 100) >> 8 + 10 (0x387A1). */
    return (list_factor[list] * (v * 256 / 100) >> 8) + 10;
}

static bs_creation_list list_of(int slot)
{
    return slot < 3 ? BS_LIST_PRIMARY : slot < 6 ? BS_LIST_MAJOR : BS_LIST_MINOR;
}

void bs_creation_new(bs_creation *c)
{
    memset(c, 0, sizeof *c);
    bs_class *k = &c->cls;
    k->level = 1;
    k->wounds = WOUNDS_MIN;
    for (int i = 0; i < 8; i++)
        k->attributes[i] = 50;
    for (int i = 0; i < BS_SCL_SKILLS; i++)
        k->skills[i] = SKILL_MIN;
    memset(k->class_skills, BS_SCL_NO_SKILL, sizeof k->class_skills);
    k->max_armour = 3;
    k->max_material = 9;
    k->magery = 0x100;
    c->points = BS_CREATION_POINTS - NEW_CLASS_COST;
}

void bs_creation_from_class(bs_creation *c, const bs_class *cls)
{
    memset(c, 0, sizeof *c);
    c->cls = *cls;
    c->points = cls->points_left;
}

bs_creation_result bs_creation_attribute(bs_creation *c, int a, bool up)
{
    if (a < 0 || a >= 8)
        return BS_CREATE_LIMIT;
    int32_t *v = &c->cls.attributes[a];
    if (up) {
        if (*v >= ATTRIBUTE_MAX)
            return BS_CREATE_LIMIT;
        int32_t cost = bs_creation_attribute_cost(*v);
        if (c->points < cost)
            return BS_CREATE_NO_POINTS;
        (*v)++;
        c->points -= cost;
        return BS_CREATE_OK;
    }
    if (*v <= ATTRIBUTE_MIN)
        return BS_CREATE_LIMIT;
    /* Not below a skill it governs (0x573A8). */
    for (int s = 0; s < BS_SCL_SKILLS; s++)
        if (bs_skill_attribute[s] == a && c->cls.skills[s] >= *v)
            return BS_CREATE_SKILL_BELOW;
    (*v)--;
    c->points += bs_creation_attribute_cost(*v);
    return BS_CREATE_OK;
}

bs_creation_result bs_creation_wounds(bs_creation *c, bool up)
{
    int32_t *w = &c->cls.wounds;
    if (up) {
        if (*w >= WOUNDS_MAX)
            return BS_CREATE_LIMIT;
        int32_t cost = bs_creation_wounds_cost(*w);
        if (c->points < cost)
            return BS_CREATE_NO_POINTS;
        (*w)++;
        c->points -= cost;
        return BS_CREATE_OK;
    }
    if (*w <= WOUNDS_MIN)
        return BS_CREATE_LIMIT;
    (*w)--;
    c->points += bs_creation_wounds_cost(*w);
    return BS_CREATE_OK;
}

bs_creation_result bs_creation_skill(bs_creation *c, int slot, bool up)
{
    if (slot < 0 || slot >= BS_SCL_CLASS_SKILLS || c->cls.class_skills[slot] >= BS_SCL_SKILLS)
        return BS_CREATE_LIMIT;
    int s = c->cls.class_skills[slot];
    int32_t *v = &c->cls.skills[s];
    if (up) {
        if (*v >= SKILL_MAX)
            return BS_CREATE_LIMIT;
        if (*v >= c->cls.attributes[bs_skill_attribute[s]])
            return BS_CREATE_ATTRIBUTE;
        int32_t cost = bs_creation_skill_cost(*v, list_of(slot));
        if (c->points < cost)
            return BS_CREATE_NO_POINTS;
        (*v)++;
        c->points -= cost;
        return BS_CREATE_OK;
    }
    if (*v <= SKILL_MIN)
        return BS_CREATE_LIMIT;
    (*v)--;
    c->points += bs_creation_skill_cost(*v, list_of(slot));
    return BS_CREATE_OK;
}

int bs_creation_skill_choices(const bs_creation *c, int out[BS_SCL_SKILLS])
{
    int n = 0;
    for (int s = 0; s < BS_SCL_SKILLS; s++) {
        if (s == SKILL_MEDICAL || s == SKILL_LOCKPICKING || s == SKILL_RUNNING)
            continue;
        if (memchr(c->cls.class_skills, s, BS_SCL_CLASS_SKILLS))
            continue;
        out[n++] = s;
    }
    return n;
}

bool bs_creation_set_skill(bs_creation *c, int slot, int skill)
{
    int choices[BS_SCL_SKILLS];
    int n = bs_creation_skill_choices(c, choices);
    if (slot < 0 || slot >= BS_SCL_CLASS_SKILLS)
        return false;
    for (int i = 0; i < n; i++)
        if (choices[i] == skill) {
            /* 0x38618: a skill in an empty slot starts at 30, 20 or 15 for nothing; one put in place of another
             * takes its value, and the other goes back to 5. */
            bs_class *k = &c->cls;
            int old = k->class_skills[slot];
            if (old >= BS_SCL_SKILLS) {
                k->skills[skill] = skill_start[list_of(slot)];
            } else {
                k->skills[skill] = k->skills[old];
                k->skills[old] = SKILL_MIN;
            }
            k->class_skills[slot] = (uint8_t)skill;
            return true;
        }
    return false;
}

static bool *flag_field(bs_class *k, bs_creation_flag f)
{
    switch (f) {
    case BS_ADV_ACUTE_HEARING: return &k->acute_hearing;
    case BS_ADV_SPELL_ABSORPTION: return &k->spell_absorption;
    case BS_ADV_RAPID_HEALING: return &k->rapid_healing;
    case BS_ADV_REGENERATE_HEALTH: return &k->regenerate_health;
    case BS_ADV_REGENERATE_SP: return &k->regenerate_sp;
    case BS_ADV_ATHLETICISM: return &k->athleticism;
    case BS_ADV_ADRENALINE_RUSH: return &k->adrenaline_rush;
    case BS_DIS_NO_SP_REGEN: return &k->no_sp_regen;
    case BS_DIS_NO_SHIELD: return &k->forbidden_shield;
    default: return NULL;
    }
}

bool bs_creation_excluded_flag(const bs_creation *c, bs_creation_flag f)
{
    if (f == BS_ADV_REGENERATE_SP)
        return c->cls.no_sp_regen;
    if (f == BS_DIS_NO_SP_REGEN)
        return c->cls.regenerate_sp;
    return false;
}

bs_creation_result bs_creation_toggle(bs_creation *c, bs_creation_flag f)
{
    bool *on = flag_field(&c->cls, f);
    if (!on)
        return BS_CREATE_LIMIT;
    if (bs_creation_excluded_flag(c, f))
        return BS_CREATE_CONFLICT;
    int32_t cost = *on ? -flag_cost[f] : flag_cost[f];
    if (cost > 0 && c->points < cost)
        return BS_CREATE_NO_POINTS;
    c->points -= cost;
    *on = !*on;
    return BS_CREATE_OK;
}

static uint8_t *tolerance_field(bs_class *k, bs_tolerance_kind t)
{
    switch (t) {
    case BS_TOL_RESISTANCE: return &k->resists;
    case BS_TOL_IMMUNITY: return &k->immune;
    case BS_TOL_LOW: return &k->low_tolerance;
    case BS_TOL_CRITICAL: return &k->critical_weakness;
    default: return NULL;
    }
}

bool bs_creation_excluded_tolerance(const bs_creation *c, bs_tolerance_kind t, int bit)
{
    bs_class *k = (bs_class *)&c->cls;
    for (int o = 0; o < BS_TOLERANCES; o++)
        if (o != (int)t && (*tolerance_field(k, (bs_tolerance_kind)o) & bit))
            return true;
    return false;
}

bs_creation_result bs_creation_tolerance(bs_creation *c, bs_tolerance_kind t, int bit)
{
    uint8_t *bits = tolerance_field(&c->cls, t);
    if (!bits || bit <= 0 || bit > 0x10)
        return BS_CREATE_LIMIT;
    if (bs_creation_excluded_tolerance(c, t, bit))
        return BS_CREATE_CONFLICT;
    int32_t cost = (*bits & bit) ? -tolerance_cost[t] : tolerance_cost[t];
    /* The game warns of too few points here but takes them anyway (0x39BCC): the total may go below 0,
     * which Done then refuses. */
    bs_creation_result r = cost > 0 && c->points < cost ? BS_CREATE_NO_POINTS : BS_CREATE_OK;
    c->points -= cost;
    *bits ^= (uint8_t)bit;
    return r;
}

static int magery_index(int32_t m)
{
    for (int i = 0; i < BS_CREATION_MAGERY_STEPS; i++)
        if (magery_step[i] == m)
            return i;
    return 0;
}

bs_creation_result bs_creation_magery(bs_creation *c, bool up)
{
    int i = magery_index(c->cls.magery), j = up ? i + 1 : i - 1;
    if (j < 0 || j >= BS_CREATION_MAGERY_STEPS)
        return BS_CREATE_LIMIT;
    int32_t after = c->points + magery_cost[i] - magery_cost[j];
    if (after < 0)
        return up ? BS_CREATE_NO_POINTS : BS_CREATE_LIMIT;
    c->points = after;
    c->cls.magery = magery_step[j];
    return BS_CREATE_OK;
}

static bs_creation_result step_worth(int32_t *points, uint8_t *v, const int32_t *worth, int top, bool up)
{
    int j = up ? *v + 1 : *v - 1;
    if (j < 0 || j > top)
        return BS_CREATE_LIMIT;
    if (up && *points + worth[*v] < worth[j])
        return BS_CREATE_NO_POINTS;
    *points += worth[*v] - worth[j];
    *v = (uint8_t)j;
    return BS_CREATE_OK;
}

bs_creation_result bs_creation_armour(bs_creation *c, bool up)
{
    return step_worth(&c->points, &c->cls.max_armour, armour_worth, 3, up);
}

bs_creation_result bs_creation_material(bs_creation *c, bool up)
{
    return step_worth(&c->points, &c->cls.max_material, material_worth, 9, up);
}

bs_creation_result bs_creation_forbid_weapon(bs_creation *c, uint8_t bit)
{
    int k = 0;
    while (k < 5 && (1u << k) != bit)
        k++;
    if (k == 5)
        return BS_CREATE_LIMIT;
    if (c->cls.forbidden_weapons & bit) {
        /* Allowed again: it costs what forbidding it gave. */
        if (c->points < weapon_worth[k])
            return BS_CREATE_NO_POINTS;
        c->points -= weapon_worth[k];
    } else {
        c->points += weapon_worth[k];
    }
    c->cls.forbidden_weapons ^= bit;
    return BS_CREATE_OK;
}

int32_t bs_creation_spell_cost(const bs_spell *s)
{
    return s->cost * SPELL_COST_FACTOR;
}

bool bs_creation_spell_offered(const bs_creation *c, const bs_spell *s)
{
    if (s->effect == BS_SPELL_DISPEL_SIGIL || s->icon >= 200)
        return false;
    for (uint32_t i = 0; i < c->cls.spell_count; i++)
        if (c->cls.spells[i].effect == s->effect)
            return false;
    return true;
}

bs_creation_result bs_creation_add_spell(bs_creation *c, const bs_spell *s)
{
    if (!bs_creation_spell_offered(c, s) || c->cls.spell_count >= BS_SCL_MAX_SPELLS)
        return BS_CREATE_LIMIT;
    int32_t cost = bs_creation_spell_cost(s);
    if (c->points < cost)
        return BS_CREATE_NO_POINTS;
    c->cls.spells[c->cls.spell_count++] = *s;
    c->points -= cost;
    return BS_CREATE_OK;
}

void bs_creation_remove_spell(bs_creation *c, uint32_t index)
{
    if (index >= c->cls.spell_count)
        return;
    c->points += bs_creation_spell_cost(&c->cls.spells[index]);
    memmove(&c->cls.spells[index], &c->cls.spells[index + 1],
            sizeof c->cls.spells[0] * (c->cls.spell_count - index - 1));
    c->cls.spell_count--;
}

int32_t bs_creation_equipment_cost(const bs_item *it)
{
    if (it->base == BS_ITEM_CLOTHES)
        return 0;
    if (it->base >= 16 && it->base <= 18) /* sacks and chests */
        return 10;
    int32_t c = it->condition_max / 5;
    if (c == 0)
        c = 10;
    return c + it->material * 50;
}

int32_t bs_creation_magic_cost(const bs_item *it)
{
    if (it->base == BS_ITEM_POTION)
        return 60;
    if (it->base > BS_ITEM_LAST_WEAPON && !bs_item_is_armour(it->base)) /* (0x54522) */
        return 500;
    return it->material * 50 + 500;
}

/* The potions' types (0xE34A3) and the enchanted items (0xEE592) the magic shop has. */
static const int32_t shop_potions[10] = {4, 5, 15, 17, 8, 16, 20, 21, 22, 26};
static const int32_t shop_enchanted[12] = {9242, 9265, 9248, 9259, 9204, 9245, 9251, 9254, 9214, 9221, 3014, 1012};
#define SHOP_LEVEL 8                  /* the shops' items are made as for level 8 (0x3B62A) */
#define SHOP_SPEC_RANDOM 10000        /* a placement's "any" base */

static void shop_add(bs_creation_shop *s, const bs_item *it, bool bought)
{
    if (s->count >= BS_CREATION_STOCK)
        return;
    s->items[s->count] = *it;
    s->bought[s->count++] = bought;
}

/* A weapon or armour piece for the shop: its condition goes with the material (0x51A0A, 0x51CA9). */
static void shop_piece(const bs_item_db *db, uint32_t base, bs_rng *r, bs_creation_shop *s)
{
    bs_item it;
    bs_item_make_plain(db, base, SHOP_LEVEL, false, r, &it);
    if (base != BS_ITEM_ARROW)
        it.condition = it.condition_max = it.condition_max * (it.material + 1);
    shop_add(s, &it, false);
}

void bs_creation_stock(const bs_item_db *db, bs_rng *r, bs_creation_shop *equipment, bs_creation_shop *magic)
{
    memset(equipment, 0, sizeof *equipment);
    memset(magic, 0, sizeof *magic);
    magic->magic = true;
    if (!db || !db->tables)
        return;
    bs_item it;
    for (int i = 0; i < 10; i++) {
        bs_item_make_plain(db, BS_ITEM_POTION, SHOP_LEVEL, false, r, &it);
        it.number = shop_potions[i];
        shop_add(magic, &it, false);
    }
    for (int i = 0; i < 5; i++) {
        bs_item_make_plain(db, BS_ITEM_CLOTHES, SHOP_LEVEL, false, r, &it); /* 0x520CC */
        shop_add(equipment, &it, false);
    }
    for (uint32_t b = 0; b <= BS_ITEM_LAST_WEAPON; b++) {
        uint32_t base = b;
        while (base == 12)
            base = (uint32_t)bs_rng_range(r, 0, BS_ITEM_LAST_WEAPON);
        shop_piece(db, base, r, equipment);
    }
    shop_piece(db, BS_ITEM_ARROW, r, equipment);
    bs_item_make_plain(db, (uint32_t)bs_rng_range(r, 16, 18), SHOP_LEVEL, false, r, &it); /* 0x5200F */
    it.picture[0] = '\0';
    shop_add(equipment, &it, false);
    for (uint32_t b = 24; b < 30; b++)
        shop_piece(db, b, r, equipment);
    for (int i = 0; i < 12; i++) {
        const int32_t v[4] = {SHOP_SPEC_RANDOM, 0, 0, shop_enchanted[i]};
        if (bs_item_make(db, BS_ITEM_SPEC_PLACED, v, SHOP_LEVEL, false, r, &it)) /* 0x524F6 */
            shop_add(magic, &it, false);
    }
}

void bs_creation_item(const bs_scl_item *in, bool female, bs_item *out)
{
    memset(out, 0, sizeof *out);
    out->base = in->base < BS_ITEM_BASE_COUNT ? in->base : 0;
    out->material = in->material;
    out->magic = in->magic;
    out->number = in->number < 0 ? 0 : in->number;
    out->quantity = in->quantity > 0 ? in->quantity : 1;
    out->condition = in->condition;
    out->condition_max = in->condition_max;
    memcpy(out->picture, in->picture, sizeof out->picture);
    out->picture[sizeof out->picture - 1] = '\0';
    /* Weapons, clothes and armour (0x54219, 0x54522): the shipped classes' armour was all made female. */
    if (out->picture[0] &&
        (bs_item_is_weapon(out->base) || out->base == BS_ITEM_CLOTHES || bs_item_is_armour(out->base)))
        out->picture[0] = female ? 'F' : 'M';
}

uint32_t bs_creation_class_items(const bs_class *cls, bool female, bs_creation_shop *equipment,
                                 bs_creation_shop *magic, bs_item *others, uint32_t max)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < cls->item_count; i++) {
        bs_item it;
        bs_creation_item(&cls->items[i], female, &it);
        if (cls->items[i].flags & 0x10)
            shop_add(it.magic || it.base == BS_ITEM_POTION ? magic : equipment, &it, true);
        else if (n < max)
            others[n++] = it;
    }
    return n;
}

int32_t bs_creation_shop_cost(const bs_creation_shop *s, uint32_t i)
{
    if (i >= s->count)
        return 0;
    return s->magic ? bs_creation_magic_cost(&s->items[i]) : bs_creation_equipment_cost(&s->items[i]);
}

bs_creation_result bs_creation_buy(bs_creation *c, bs_creation_shop *s, uint32_t i)
{
    if (i >= s->count || s->bought[i])
        return BS_CREATE_LIMIT;
    int32_t cost = bs_creation_shop_cost(s, i);
    if (c->points < cost)
        return BS_CREATE_NO_POINTS;
    c->points -= cost;
    s->bought[i] = true;
    return BS_CREATE_OK;
}

void bs_creation_sell(bs_creation *c, bs_creation_shop *s, uint32_t i)
{
    if (i >= s->count || !s->bought[i])
        return;
    c->points += bs_creation_shop_cost(s, i);
    s->bought[i] = false;
}

bs_creation_check bs_creation_ready(const bs_creation *c)
{
    if (c->points < 0)
        return BS_CHECK_POINTS;
    if (!c->cls.class_name[0])
        return BS_CHECK_NO_NAME;
    for (int i = 0; i < BS_SCL_CLASS_SKILLS; i++)
        if (c->cls.class_skills[i] >= BS_SCL_SKILLS)
            return BS_CHECK_SKILLS;
    return BS_CHECK_READY;
}

void bs_race_skill_bonus(int race, int32_t out[BS_SCL_SKILLS])
{
    /* Skill numbers: 1 Jumping, 6 Dodging, 8-13 the schools, 14 Short Blade, 15 Long Blade, 17 Axe,
     * 18 Blunt, 19 Missile, 20 Critical Strike. */
    static const struct {
        int8_t skill[6], bonus[6];
    } races[6] = {
        {{15, 14, 19, 17, 18, 20}, {10, 10, 10, 5, 5, 5}},  /* Redguard */
        {{8, 13, 10, 11, 12, 9}, {10, 10, 10, 5, 5, 5}},    /* Breton */
        {{17, 18, 20, 15, 14, 19}, {10, 10, 10, 5, 5, 5}},  /* Nord */
        {{13, 11, 12, 9, 15, 14}, {5, 5, 5, 5, 10, 10}},    /* High Elf, and Missile below */
        {{8, 13, 10, 11, 12, 9}, {5, 5, 5, 10, 10, 10}},    /* Dark Elf */
        {{19, 1, 6, -1, -1, -1}, {15, 15, 15, 0, 0, 0}},    /* Wood Elf */
    };
    memset(out, 0, sizeof(int32_t) * BS_SCL_SKILLS);
    if (race < 0 || race >= 6)
        return;
    for (int i = 0; i < 6; i++)
        if (races[race].skill[i] >= 0)
            out[races[race].skill[i]] += races[race].bonus[i];
    if (race == 3)
        out[19] += 5;
}

void bs_creation_apply(const bs_class *cls, int race, bs_character *out, bs_equip_rules *rules)
{
    bs_character_default(out);
    int32_t bonus[BS_SCL_SKILLS];
    bs_race_skill_bonus(race, bonus);
    for (int i = 0; i < 8; i++)
        out->attributes[i] = cls->attributes[i];
    for (int i = 0; i < BS_SCL_SKILLS && i < BS_SKILL_COUNT; i++)
        out->skills[i] = cls->skills[i] + bonus[i];
    out->level = cls->level > 0 ? cls->level : 1;
    out->health = out->health_max = cls->wounds;
    /* Spell points: Intelligence x magery / 256 (0x37508). */
    out->spell_points = out->spell_points_max = (int32_t)((cls->attributes[1] * cls->magery) >> 8);
    out->resists = cls->resists;
    out->immune = cls->immune;
    out->low_tolerance = cls->low_tolerance;
    out->critical_weakness = cls->critical_weakness;
    out->no_spell_point_potions = cls->no_sp_regen;
    out->double_healing = cls->rapid_healing;
    out->absorbs_spells = cls->spell_absorption;
    memcpy(out->class_name, cls->class_name, sizeof out->class_name);
    memcpy(out->class_skills, cls->class_skills, sizeof out->class_skills);
    out->magery = cls->magery;
    out->acute_hearing = cls->acute_hearing;
    out->regenerate_health = cls->regenerate_health;
    out->regenerate_sp = cls->regenerate_sp;
    out->athleticism = cls->athleticism;
    out->adrenaline_rush = cls->adrenaline_rush;
    if (rules)
        *rules = (bs_equip_rules){cls->forbidden_weapons, cls->max_armour, cls->max_material};
}
