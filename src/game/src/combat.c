#include "bs/game/combat.h"

#include <math.h>

#include "bs/formats/exe_text.h"

/* The hit location chances (0xEF1AC), walked by a roll of 0..99. */
static const int32_t location_chance[] = {10, 20, 40, 15, 10, 200};

/* The swing factors by direction (0xDA53E, 0xDA54E, 0xDA56E). */
static const uint8_t swing_animation[16] = {7, 8, 8, 5, 5, 9, 9, 9, 9, 9, 9, 10, 10, 11, 11, 7};
static const int16_t swing_skill[16] = {4, 3, 3, 4, 4, 5, 5, 5, 5, 5, 5, 4, 4, 3, 3, 4};
static const int16_t swing_damage[16] = {8, 6, 6, 4, 4, 2, 2, 2, 2, 2, 2, 4, 4, 6, 6, 8};

static int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* rand() % 100 (0x71214). */
static int32_t percent(bs_rng *r)
{
    return bs_rng_range(r, 0, 99);
}

int bs_hit_location(bs_rng *r)
{
    int32_t roll = percent(r);
    int loc = 0;
    while (loc < BS_HIT_LOCATIONS - 1 && location_chance[loc] < roll)
        roll -= location_chance[loc++];
    return loc;
}

bs_melee bs_melee_roll(const bs_fighter *att, const bs_fighter *def, int32_t skill_mult, int32_t damage_mult,
                       bs_rng *r)
{
    bs_melee m = {.outcome = BS_MELEE_MISS, .force = 1, .shield = def->shield};

    int32_t roll;
    int loc = bs_hit_location(r);
    m.location = loc;

    int32_t damage = bs_rng_range(r, att->damage_min, att->damage_max);
    if (att->player)
        damage = att->armed ? damage * damage_mult / 4 : damage + att->hand_to_hand / 7;
    else
        damage *= 2;
    if (m.shield) {
        if (m.shield > damage) {
            m.shield -= damage;
            m.outcome = BS_MELEE_ABSORBED;
            return m;
        }
        damage -= m.shield;
        m.shield = 0;
    }
    damage -= def->armour[loc];
    if (att->wraith && att->health_max > 0)
        damage = damage * (att->health * 100 / att->health_max) / 100;

    int32_t attack = att->weapon_skill + (att->luck - 50) / 5 + att->agility / 5 * skill_mult / 4;
    int32_t defence = def->weapon_skill + def->luck / 10 + def->agility / 5;
    int32_t chance = (attack - defence) / 2 + 50;
    chance -= chance * (def->dodge / 5) / 100;
    chance = clamp(chance, 5, 95);
    int32_t critical = att->critical / 7 + chance / 20;
    int32_t good = chance / 5;
    int32_t fumble = 100 - (100 - chance) / 20;
    roll = bs_rng_range(r, 0, 100);
    bool hit = roll <= chance;
    int32_t luck = def->player ? (def->luck - 50) / 5 : 0;
    critical = critical + luck < 1 ? 1 : critical + luck;
    fumble = fumble + luck > 99 ? 99 : fumble + luck;
    bool is_critical = roll <= critical, is_good = roll <= good, is_fumble = roll >= fumble;

    if (is_fumble && att->player && att->armed) {
        int32_t f = percent(r);
        m.outcome = BS_MELEE_FUMBLE;
        m.message = f < 50 ? BS_MSG_OFF_BALANCE : f < 90 ? BS_MSG_WEAPON_DAMAGED : BS_MSG_WEAPON_KNOCKED;
        return m;
    }
    if (!hit)
        return m;

    m.outcome = BS_MELEE_HIT;
    if (is_critical && att->player) {
        int32_t c = percent(r);
        if (c < 60) {
            damage = att->damage_max * damage_mult / 4 * 3;
            m.message = BS_MSG_TELLING_BLOW;
        } else if (c < 90) {
            m.stun = true;
            m.message = BS_MSG_STUN;
        } else {
            damage = att->damage_max * damage_mult / 4 * 3;
            m.force = 2;
            m.message = BS_MSG_REELING;
        }
    } else if (is_good) {
        m.force = 2;
    }
    float f = (float)att->weapon_skill / 100.0f;
    f = f * f * 2.5f;
    damage = (int32_t)((float)damage * f);
    damage += (att->strength - 50) / 10;
    if (damage <= 0)
        damage = 1;
    if (def->unhurt) {
        damage = 0;
        if (att->player)
            m.message = BS_MSG_NO_DAMAGE;
    }
    if (att->player && att->armed)
        damage += att->weapon_bonus;
    m.damage = damage;
    return m;
}

bool bs_swing_from_motion(int32_t dx, int32_t dy, bs_swing *out)
{
    float len = sqrtf((float)dx * (float)dx + (float)dy * (float)dy);
    if (len < 100.0f)
        return false;
    /* The game's angle (0x1E711): acos scaled by 1024 / 3.14, mirrored for dx <= 0. */
    int32_t a = (int32_t)(acosf((float)dy / len) * 1024.0f / 3.14f);
    if (dx <= 0)
        a = 0x7FF - a;
    int d = (a >> 7) & 15;
    out->direction = d;
    out->skill_mult = swing_skill[d];
    out->damage_mult = swing_damage[d];
    out->animation = swing_animation[d];
    return true;
}

int32_t bs_knockback(int32_t force, int32_t mass, int32_t distance, uint32_t *duration_ms)
{
    *duration_ms = 0;
    if (force > 400)
        force = 400;
    int32_t m = mass / 4;
    if (m <= 0 || force + m == 0)
        return 0;
    int32_t v = distance * ((force - m) * 256 / (force + m)) / 256;
    v = force / m * (distance - v);
    if (v > 64)
        v = 64;
    if (v < 15)
        return 0;
    *duration_ms = (uint32_t)v * 5;
    return v;
}

/* Hit sounds (0xEF1FE by weapon, 0xEF21C by creature) and misses (0xEF23E: which list, 0xEF1B6, 0xEF1C2). */
static const int16_t hit_weapon[16] = {439, 439, 437, 438, 437, 435, 436, 435, 439, 439, 439, 440, 439, 442, 441, 439};
static const int16_t hit_creature[16] = {439, 435, 437, 442, 436, 433, 434, 439, 435, 435, 442, 438, 438, 438, 435, 436};
static const uint8_t miss_list[16] = {0, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0};
static const int16_t miss_a[3] = {91, 373, 374};
static const int16_t miss_b[9] = {291, 292, 293, 294, 295, 296, 297, 298, 299};

int32_t bs_hit_sound(bool player, uint32_t weapon_or_creature)
{
    return (player ? hit_weapon : hit_creature)[weapon_or_creature & 15];
}

int32_t bs_miss_sound(uint32_t defender_creature, bs_rng *r)
{
    return miss_list[defender_creature & 15] ? miss_b[bs_rng_range(r, 0, 8)] : miss_a[bs_rng_range(r, 0, 2)];
}
