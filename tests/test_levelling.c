/* Levelling: the level-up screen's costs and limits, and skills rising with use (GAME.EXE's rules). */

#include <string.h>

#include "bs/formats/exe_text.h"
#include "bs/game/levelling.h"
#include "bs_test.h"

enum { STRENGTH = 0, INTELLIGENCE = 1, JUMPING = 1, SHORT_BLADE = 14, LONG_BLADE = 15, AXE = 17, CRITICAL = 20 };

static void test_points(void)
{
    BS_CHECK_EQ_INT(bs_level_up_points(1), 600);
    BS_CHECK_EQ_INT(bs_level_up_points(6), 1900);
    BS_CHECK_EQ_INT(bs_level_up_points(8), 0);
    BS_CHECK_EQ_INT(bs_level_up_points(0), 0);
}

static void test_costs(void)
{
    BS_CHECK_EQ_INT(bs_level_up_attribute_cost(50), 65);
    BS_CHECK_EQ_INT(bs_level_up_attribute_cost(49), 64);
    BS_CHECK_EQ_INT(bs_level_up_attribute_cost(99), 89);
    BS_CHECK_EQ_INT(bs_level_up_skill_cost(50, 50), 35);
    BS_CHECK_EQ_INT(bs_level_up_skill_cost(50, 80), 50);

    bs_character c;
    bs_character_default(&c);
    const uint8_t skills[12] = {SHORT_BLADE, 2, 3, AXE, 5, 6, CRITICAL, 8, 9, 10, 11, 12};
    memcpy(c.class_skills, skills, sizeof skills);
    BS_CHECK_EQ_INT(bs_level_up_skill_factor(&c, SHORT_BLADE), 50);
    BS_CHECK_EQ_INT(bs_level_up_skill_factor(&c, AXE), 60);
    BS_CHECK_EQ_INT(bs_level_up_skill_factor(&c, CRITICAL), 70);   /* the fourth major skill */
    BS_CHECK_EQ_INT(bs_level_up_skill_factor(&c, LONG_BLADE), 80);
}

static void test_begin_end(void)
{
    bs_character c;
    bs_character_default(&c);
    c.health = 20;
    c.spell_points = 3;
    c.points = 40;
    bs_level_up lu;
    bs_level_up_begin(&lu, &c, 600);
    BS_CHECK_EQ_INT(lu.points, 640);
    BS_CHECK_EQ_INT(c.health_max, 59);
    BS_CHECK_EQ_INT(c.health, 59);
    BS_CHECK_EQ_INT(c.spell_points, 50);
    BS_CHECK_EQ_INT(c.spell_points_max, 50);
    lu.points = 7;
    bs_level_up_end(&lu, &c);
    BS_CHECK_EQ_INT(c.points, 7);
}

static void test_attributes(void)
{
    bs_character c;
    bs_character_default(&c);
    bs_level_up lu;
    bs_level_up_begin(&lu, &c, 600);
    bs_level_up_result r = bs_level_up_attribute(&lu, &c, STRENGTH, true, 1);
    BS_CHECK_EQ_INT(r.message, 0);
    BS_CHECK_EQ_INT(c.attributes[STRENGTH], 51);
    BS_CHECK_EQ_INT(lu.points, 535);
    /* Down gives the cost back, but not below where it was. */
    bs_level_up_attribute(&lu, &c, STRENGTH, false, 10);
    BS_CHECK_EQ_INT(c.attributes[STRENGTH], 50);
    BS_CHECK_EQ_INT(lu.points, 600);
    /* Intelligence brings the spell points along. */
    bs_level_up_attribute(&lu, &c, INTELLIGENCE, true, 2);
    BS_CHECK_EQ_INT(c.spell_points_max, 52);
    BS_CHECK_EQ_INT(c.spell_points, 52);
    /* A skill it governs holds it up. */
    bs_level_up_attribute(&lu, &c, STRENGTH, true, 2);
    c.skills[LONG_BLADE] = 51;
    r = bs_level_up_attribute(&lu, &c, STRENGTH, false, 5);
    BS_CHECK_EQ_INT(c.attributes[STRENGTH], 51);
    BS_CHECK_EQ_INT(r.message, BS_MSG_DEPENDENT_SKILL);
    /* Without the points the upper half lowers it, as the game does. */
    c.skills[LONG_BLADE] = 50;
    lu.points = 0;
    bs_level_up_attribute(&lu, &c, STRENGTH, true, 1);
    BS_CHECK_EQ_INT(c.attributes[STRENGTH], 50);
    BS_CHECK_EQ_INT(lu.points, 65);
    /* And at 100 too. */
    c.attributes[STRENGTH] = 100;
    lu.points = 1000;
    bs_level_up_attribute(&lu, &c, STRENGTH, true, 1);
    BS_CHECK_EQ_INT(c.attributes[STRENGTH], 99);
}

static void test_skills(void)
{
    bs_character c;
    bs_character_default(&c);
    c.skills[LONG_BLADE] = 48;
    bs_level_up lu;
    bs_level_up_begin(&lu, &c, 100);
    bs_level_up_result r = bs_level_up_skill(&lu, &c, LONG_BLADE, true, 10);
    /* 48 -> 49 costs 48, 49 -> 50 another 49; then it's at Strength. */
    BS_CHECK_EQ_INT(c.skills[LONG_BLADE], 50);
    BS_CHECK_EQ_INT(lu.points, 100 - 48 - 49);
    BS_CHECK_EQ_INT(r.message, BS_MSG_SKILL_ABOVE);
    BS_CHECK_EQ_INT(r.attribute, STRENGTH);
    c.attributes[STRENGTH] = 60;
    r = bs_level_up_skill(&lu, &c, LONG_BLADE, true, 1);
    BS_CHECK_EQ_INT(r.message, BS_MSG_NOT_ENOUGH_POINTS);
    BS_CHECK_EQ_INT(c.skills[LONG_BLADE], 50);
    bs_level_up_skill(&lu, &c, LONG_BLADE, false, 10);
    BS_CHECK_EQ_INT(c.skills[LONG_BLADE], 48);
    BS_CHECK_EQ_INT(lu.points, 100);
}

static void test_use(void)
{
    bs_character c;
    bs_character_default(&c);
    int rose[BS_SKILL_COUNT];
    c.attributes[6] = 60;
    /* Jumping at 50 rises after 50 x 2 x 2 / 5 + 1 = 41 uses. */
    bs_skill_used(&c, JUMPING, 40);
    BS_CHECK_EQ_INT(bs_skills_rise(&c, rose), 0);
    bs_skill_used(&c, JUMPING, 1);
    BS_CHECK_EQ_INT(bs_skills_rise(&c, rose), 1);
    BS_CHECK_EQ_INT(rose[0], JUMPING);
    BS_CHECK_EQ_INT(c.skills[JUMPING], 51);
    BS_CHECK_EQ_INT(c.skill_uses[JUMPING], 0);
    /* Not above its attribute (Speed), and not past 95 by use: the count starts again anyway. */
    c.skills[JUMPING] = 50;
    c.attributes[6] = 50;
    bs_skill_used(&c, JUMPING, 100);
    BS_CHECK_EQ_INT(bs_skills_rise(&c, rose), 0);
    BS_CHECK_EQ_INT(c.skill_uses[JUMPING], 0);
    c.attributes[6] = 100;
    c.skills[JUMPING] = 95;
    bs_skill_used(&c, JUMPING, 30000);
    BS_CHECK_EQ_INT(c.skill_uses[JUMPING], 20000);
    BS_CHECK_EQ_INT(bs_skills_rise(&c, rose), 0);
    BS_CHECK_EQ_INT(c.skills[JUMPING], 95);
    BS_CHECK_EQ_INT(bs_school_skill(2), 8);
    BS_CHECK_EQ_INT(bs_school_skill(9), -1);
}

static void test_five_seconds(void)
{
    bs_character c;
    bs_character_default(&c);
    c.health = 10;
    c.spell_points = 10;
    c.regenerate_health = true;
    bs_character_five_seconds(&c, true, false, true);
    bs_character_five_seconds(&c, true, true, false);
    BS_CHECK_EQ_INT(c.health, 12);
    BS_CHECK_EQ_INT(c.spell_points, 10);
    BS_CHECK_EQ_INT(c.skill_uses[3], 1);
    BS_CHECK_EQ_INT(c.skill_uses[4], 0);
}

int main(void)
{
    BS_RUN(test_points);
    BS_RUN(test_costs);
    BS_RUN(test_begin_end);
    BS_RUN(test_attributes);
    BS_RUN(test_skills);
    BS_RUN(test_use);
    BS_RUN(test_five_seconds);
    return BS_TEST_RESULT();
}
