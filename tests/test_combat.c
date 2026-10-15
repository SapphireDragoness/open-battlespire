/* Melee combat: the hit roll, damage, swings, knockback, sounds. */

#include <string.h>

#include "bs/formats/exe_text.h"
#include "bs/game/combat.h"
#include "bs_test.h"

/* A Scamp: attributes 20 40 10 60 20 10 65 30, every skill 20, damage 1-6, armour 2. */
static bs_fighter scamp(void)
{
    bs_fighter f;
    memset(&f, 0, sizeof f);
    f.strength = 20;
    f.agility = 60;
    f.luck = 30;
    f.weapon_skill = f.dodge = f.critical = 20;
    f.damage_min = 1;
    f.damage_max = 6;
    for (int i = 0; i < BS_HIT_LOCATIONS; i++)
        f.armour[i] = 2;
    f.health = f.health_max = 30;
    return f;
}

/* The player until character creation: everything 50, no armour, bare hands (1-2). */
static bs_fighter player(void)
{
    bs_fighter f;
    memset(&f, 0, sizeof f);
    f.player = true;
    f.strength = f.agility = f.luck = 50;
    f.weapon_skill = f.dodge = f.critical = f.hand_to_hand = 50;
    f.damage_min = 1;
    f.damage_max = 2;
    f.health = f.health_max = 50;
    return f;
}

static void test_monster_hit_chance(void)
{
    /* attack 20 + (30 - 50) / 5 + 60 / 5 = 28; defence 50 + 5 + 10 = 65; (28 - 65) / 2 + 50 = 32, less 10 %
     * for the dodge skill: 29; hits on a roll of 0..29 of 0..100. */
    bs_fighter a = scamp(), d = player();
    bs_rng r = {12345};
    int hits = 0, n = 20000;
    for (int k = 0; k < n; k++) {
        bs_melee m = bs_melee_roll(&a, &d, 4, 4, &r);
        BS_CHECK(m.outcome == BS_MELEE_HIT || m.outcome == BS_MELEE_MISS);
        BS_CHECK(m.location >= 0 && m.location < BS_HIT_LOCATIONS);
        if (m.outcome == BS_MELEE_HIT) {
            hits++;
            /* (2..12 doubled roll) x (20 / 100)^2 x 2.5 - 3: never above 0, so the least, 1. */
            BS_CHECK_EQ_INT(m.damage, 1);
            BS_CHECK_EQ_INT(m.message, 0);
        }
    }
    BS_CHECK(hits > n * 27 / 101 && hits < n * 33 / 101);
}

static void test_player_damage(void)
{
    /* With a weapon, skill 50: (roll x mult / 4 - armour) x 0.625 + 0, at least 1, + the weapon's bonus. */
    bs_fighter a = player(), d = scamp();
    a.armed = true;
    a.damage_min = a.damage_max = 10;
    a.weapon_bonus = 2;
    a.critical = 0;
    bs_rng r = {7};
    int hits = 0;
    for (int k = 0; k < 2000; k++) {
        bs_melee m = bs_melee_roll(&a, &d, 4, 8, &r);
        if (m.outcome != BS_MELEE_HIT || m.message)
            continue;
        hits++;
        /* 10 x 8 / 4 = 20, - 2 armour = 18, x 0.625 = 11, + 2 */
        BS_CHECK_EQ_INT(m.damage, 13);
    }
    BS_CHECK(hits > 0);
    /* A Herne on level 5 takes nothing, and the player is told. */
    d.unhurt = true;
    for (int k = 0; k < 200; k++) {
        bs_melee m = bs_melee_roll(&a, &d, 4, 4, &r);
        if (m.outcome == BS_MELEE_HIT && (m.message == BS_MSG_NO_DAMAGE)) {
            BS_CHECK_EQ_INT(m.damage, 2); /* only the weapon's bonus */
        }
    }
}

static void test_criticals_and_fumbles(void)
{
    bs_fighter a = player(), d = scamp();
    a.armed = true;
    a.critical = 700; /* a critical on every hit */
    a.damage_min = 1;
    a.damage_max = 4;
    bs_rng r = {99};
    int telling = 0, stun = 0, reeling = 0, fumbles = 0;
    for (int k = 0; k < 3000; k++) {
        bs_melee m = bs_melee_roll(&a, &d, 4, 4, &r);
        if (m.outcome == BS_MELEE_FUMBLE) {
            fumbles++;
            BS_CHECK(m.message == BS_MSG_OFF_BALANCE || m.message == BS_MSG_WEAPON_DAMAGED ||
                     m.message == BS_MSG_WEAPON_KNOCKED);
            continue;
        }
        if (m.outcome != BS_MELEE_HIT)
            continue;
        telling += m.message == BS_MSG_TELLING_BLOW;
        stun += m.stun && m.message == BS_MSG_STUN;
        reeling += m.message == BS_MSG_REELING && m.force == 2;
    }
    BS_CHECK(telling > stun && stun > reeling && reeling > 0);
    BS_CHECK(fumbles > 0);
    /* Monsters don't fumble or strike critically. */
    bs_fighter s = scamp();
    s.critical = 700;
    for (int k = 0; k < 500; k++) {
        bs_melee m = bs_melee_roll(&s, &a, 4, 4, &r);
        BS_CHECK(m.outcome != BS_MELEE_FUMBLE && m.message == 0 && !m.stun);
    }
}

static void test_shield(void)
{
    bs_fighter a = scamp(), d = player();
    d.shield = 100;
    bs_rng r = {3};
    bs_melee m = bs_melee_roll(&a, &d, 4, 4, &r);
    BS_CHECK_EQ_INT(m.outcome, BS_MELEE_ABSORBED);
    BS_CHECK(m.shield < 100 && m.shield >= 100 - 12);
}

static void test_swings(void)
{
    bs_swing s;
    BS_CHECK(!bs_swing_from_motion(30, 40, &s));
    /* Straight down and up fall on the mirrored side (dx <= 0), 15 and 7, which match 0 and 8. */
    BS_CHECK(bs_swing_from_motion(0, 150, &s)); /* pulled down: a chop */
    BS_CHECK_EQ_INT(s.direction, 15);
    BS_CHECK_EQ_INT(s.damage_mult, 8);
    BS_CHECK_EQ_INT(s.skill_mult, 4);
    BS_CHECK(bs_swing_from_motion(0, -150, &s)); /* pushed up: a thrust */
    BS_CHECK_EQ_INT(s.direction, 7);
    BS_CHECK_EQ_INT(s.skill_mult, 5);
    BS_CHECK_EQ_INT(s.damage_mult, 2);
    BS_CHECK(bs_swing_from_motion(5, 150, &s));
    BS_CHECK_EQ_INT(s.direction, 0);
    BS_CHECK(bs_swing_from_motion(150, 0, &s)); /* across */
    BS_CHECK_EQ_INT(s.direction, 4);
    BS_CHECK_EQ_INT(s.damage_mult, 4);
    BS_CHECK(bs_swing_from_motion(-150, 0, &s));
    BS_CHECK_EQ_INT(s.direction, 11);
    BS_CHECK_EQ_INT(s.damage_mult, 4);
}

static void test_knockback(void)
{
    uint32_t ms;
    /* 5 damage: force 150, distance 15, a Scamp's mass 20: 15 x (145 x 256 / 155) / 256 = 14, 150 / 5 x 1 */
    BS_CHECK_EQ_INT(bs_knockback(150, 20, 15, &ms), 30);
    BS_CHECK_EQ_INT(ms, 150);
    BS_CHECK_EQ_INT(bs_knockback(150, 0, 15, &ms), 0); /* ghosts don't move */
    BS_CHECK_EQ_INT(bs_knockback(30, 200, 3, &ms), 0); /* too heavy */
    BS_CHECK_EQ_INT(ms, 0);
    BS_CHECK_EQ_INT(bs_knockback(9000, 20, 300, &ms), 64); /* capped */
}

static void test_sounds(void)
{
    BS_CHECK_EQ_INT(bs_hit_sound(true, 15), 439);
    BS_CHECK_EQ_INT(bs_hit_sound(false, 3), 442);
    bs_rng r = {1};
    for (int k = 0; k < 50; k++) {
        int32_t a = bs_miss_sound(0, &r), b = bs_miss_sound(1, &r);
        BS_CHECK(a == 91 || a == 373 || a == 374);
        BS_CHECK(b >= 291 && b <= 299);
    }
}

int main(void)
{
    BS_RUN(test_monster_hit_chance);
    BS_RUN(test_player_damage);
    BS_RUN(test_criticals_and_fumbles);
    BS_RUN(test_shield);
    BS_RUN(test_swings);
    BS_RUN(test_knockback);
    BS_RUN(test_sounds);
    return BS_TEST_RESULT();
}
