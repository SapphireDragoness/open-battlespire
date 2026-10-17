#include "bs/game/levelling.h"

#include <string.h>

#include "bs/formats/exe_text.h"

/* RE references are GAME.EXE addresses; see docs/re/README.md, "Levelling". */

/* Points by level number (0xED524); number 0 has 10000, which no level uses. */
static const int32_t level_points[8] = {0, 600, 800, 1100, 1400, 1600, 1900, 0};
/* Uses per rise, per skill value (0xED4E3). */
static const int16_t use_rate[BS_SKILL_COUNT] = {12, 2, 2, 2, 1, 1, 2, 50, 1, 2, 1, 1, 2, 1, 2, 2, 2, 2, 2, 1, 4};
/* The skill of each school (0xDA60A): Alteration, Restoration, Destruction, Mysticism, Thaumaturgy, Illusion. */
static const uint8_t school_skill[6] = {11, 9, 8, 13, 12, 10};

#define ATTRIBUTE_MAX 100
#define SKILL_MAX 100
#define SKILL_USE_MAX 95              /* skills rise with use below this (0x1E48E) */
#define USES_MAX 20000
#define HEALTH_GAIN 307               /* / 256 at each level-up (0x5595C) */

int32_t bs_level_up_points(int level_number)
{
    return level_number >= 1 && level_number < 8 ? level_points[level_number] : 0;
}

/* Spell points while the sheet is open (0x55D5E): Intelligence x magery. */
static void spell_points(bs_character *c)
{
    c->spell_points = c->spell_points_max = c->attributes[1] * c->magery >> 8;
}

void bs_level_up_begin(bs_level_up *lu, bs_character *c, int32_t points)
{
    memset(lu, 0, sizeof *lu);
    lu->points = c->points + points;
    if (points) {
        c->health = c->health_max = c->health_max * HEALTH_GAIN >> 8;
        spell_points(c);
    }
    memcpy(lu->attributes_floor, c->attributes, sizeof lu->attributes_floor);
    memcpy(lu->skills_floor, c->skills, sizeof lu->skills_floor);
}

void bs_level_up_end(const bs_level_up *lu, bs_character *c)
{
    c->points = lu->points;
}

int32_t bs_level_up_attribute_cost(int32_t v)
{
    return (v * 256 / 100 * 50 >> 8) + 40;
}

int32_t bs_level_up_skill_cost(int32_t v, int32_t factor)
{
    return (v * 256 / 100 * factor >> 8) + 10;
}

int32_t bs_level_up_skill_factor(const bs_character *c, int skill)
{
    /* By the place in the class's list: the first three 50, the next three 60, the rest 70 (so the fourth
     * major skill counts as a minor one, as the game has it). */
    for (int i = 0; i < (int)sizeof c->class_skills; i++)
        if (c->class_skills[i] == skill)
            return i < 3 ? 50 : i < 6 ? 60 : 70;
    return 80;
}

/* Whether an attribute at v may go down (0x573A8): no skill it governs is as high. */
static bool may_lower(const bs_character *c, int attribute, int32_t v)
{
    for (int s = 0; s < BS_SKILL_COUNT; s++)
        if (bs_skill_attribute[s] == attribute && c->skills[s] >= v)
            return false;
    return true;
}

bs_level_up_result bs_level_up_attribute(bs_level_up *lu, bs_character *c, int attribute, bool upper, int steps)
{
    bs_level_up_result r = {0, -1};
    if (attribute < 0 || attribute >= 8)
        return r;
    int32_t *v = &c->attributes[attribute];
    for (int i = 0; i < steps; i++) {
        int32_t cost = bs_level_up_attribute_cost(*v), back = bs_level_up_attribute_cost(*v - 1);
        if (upper && *v < ATTRIBUTE_MAX && lu->points >= cost) {
            ++*v;
            lu->points -= cost;
        } else if (*v > lu->attributes_floor[attribute]) {
            if (!may_lower(c, attribute, *v)) {
                r.message = BS_MSG_DEPENDENT_SKILL;
                break;
            }
            --*v;
            lu->points += back;
        }
    }
    spell_points(c);
    return r;
}

bs_level_up_result bs_level_up_skill(bs_level_up *lu, bs_character *c, int skill, bool raise, int steps)
{
    bs_level_up_result r = {0, -1};
    if (skill < 0 || skill >= BS_SKILL_COUNT)
        return r;
    int32_t factor = bs_level_up_skill_factor(c, skill), *v = &c->skills[skill];
    for (int i = 0; i < steps; i++) {
        if (raise) {
            if (*v >= SKILL_MAX)
                break;
            if (*v >= c->attributes[bs_skill_attribute[skill]]) {
                r.message = BS_MSG_SKILL_ABOVE;
                r.attribute = bs_skill_attribute[skill];
                break;
            }
            int32_t cost = bs_level_up_skill_cost(*v, factor);
            if (lu->points < cost) {
                r.message = BS_MSG_NOT_ENOUGH_POINTS;
                break;
            }
            lu->points -= cost;
            ++*v;
        } else {
            if (*v == lu->skills_floor[skill])
                break;
            lu->points += bs_level_up_skill_cost(*v - 1, factor);
            --*v;
        }
    }
    return r;
}

/* ---- skills rising with use ---- */

void bs_skill_used(bs_character *c, int skill, int32_t amount)
{
    if (skill < 0 || skill >= BS_SKILL_COUNT)
        return;
    int32_t *u = &c->skill_uses[skill];
    *u += amount;
    *u = *u < 0 ? 0 : *u > USES_MAX ? USES_MAX : *u;
}

int bs_skills_rise(bs_character *c, int rose[BS_SKILL_COUNT])
{
    int n = 0;
    for (int s = 0; s < BS_SKILL_COUNT; s++) {
        int32_t v = c->skills[s];
        /* (0x1E576: 1 + v x rate x 2.0 / 5.0, truncated) */
        if (c->skill_uses[s] < v * use_rate[s] * 2 / 5 + 1)
            continue;
        c->skill_uses[s] = 0;
        if (v < SKILL_USE_MAX && v < c->attributes[bs_skill_attribute[s]]) {
            c->skills[s]++;
            rose[n++] = s;
        }
    }
    return n;
}

int bs_school_skill(int school)
{
    return school >= 0 && school < 6 ? school_skill[school] : -1;
}

void bs_character_five_seconds(bs_character *c, bool sneaking, bool swimming, bool moved)
{
    enum { STEALTH = 3, SWIMMING = 4 };
    if (sneaking && moved)
        bs_skill_used(c, STEALTH, 1);
    if (swimming && moved)
        bs_skill_used(c, SWIMMING, 1);
    if (c->regenerate_health && c->health < c->health_max)
        c->health++;
    if (c->regenerate_sp && c->spell_points < c->spell_points_max)
        c->spell_points++;
}
