/* Making a class with build points (bs_creation). */

#include <string.h>

#include "bs/game/creation.h"
#include "bs_test.h"

static void test_costs(void)
{
    BS_CHECK_EQ_INT(bs_creation_attribute_cost(50), 80);
    BS_CHECK_EQ_INT(bs_creation_attribute_cost(10), 47);
    BS_CHECK_EQ_INT(bs_creation_attribute_cost(74), 99);
    BS_CHECK_EQ_INT(bs_creation_wounds_cost(50), 52);
    BS_CHECK_EQ_INT(bs_creation_wounds_cost(199), 89);
    BS_CHECK_EQ_INT(bs_creation_skill_cost(5, BS_LIST_PRIMARY), 12);
    BS_CHECK_EQ_INT(bs_creation_skill_cost(5, BS_LIST_MAJOR), 12);
    BS_CHECK_EQ_INT(bs_creation_skill_cost(5, BS_LIST_MINOR), 13);
    BS_CHECK_EQ_INT(bs_creation_skill_cost(59, BS_LIST_MINOR), 51);
}

static void test_new(void)
{
    bs_creation c;
    bs_creation_new(&c);
    BS_CHECK_EQ_INT(c.points, 4700);
    BS_CHECK_EQ_INT(c.cls.attributes[3], 50);
    BS_CHECK_EQ_INT(c.cls.skills[20], 5);
    BS_CHECK_EQ_INT(c.cls.wounds, 50);
    BS_CHECK_EQ_INT(c.cls.magery, 0x100);
    BS_CHECK_EQ_INT(c.cls.class_skills[11], BS_SCL_NO_SKILL);
    BS_CHECK_EQ_INT(bs_creation_ready(&c), BS_CHECK_NO_NAME);
    strcpy(c.cls.class_name, "Tester");
    BS_CHECK_EQ_INT(bs_creation_ready(&c), BS_CHECK_SKILLS);
}

static void test_attributes_and_skills(void)
{
    bs_creation c;
    bs_creation_new(&c);
    BS_CHECK_EQ_INT(bs_creation_attribute(&c, 0, true), BS_CREATE_OK);
    BS_CHECK_EQ_INT(c.points, 4620);
    BS_CHECK_EQ_INT(bs_creation_attribute(&c, 0, false), BS_CREATE_OK);
    BS_CHECK_EQ_INT(c.points, 4700);
    /* Lowering gives back what raising to there cost. */
    BS_CHECK_EQ_INT(bs_creation_attribute(&c, 0, false), BS_CREATE_OK);
    BS_CHECK_EQ_INT(c.points, 4700 + bs_creation_attribute_cost(49));
    while (c.cls.attributes[0] > 10)
        BS_CHECK_EQ_INT(bs_creation_attribute(&c, 0, false), BS_CREATE_OK);
    BS_CHECK_EQ_INT(bs_creation_attribute(&c, 0, false), BS_CREATE_LIMIT);

    /* Skill choices leave out Medical, Lockpicking and Running and those taken. */
    int choices[BS_SCL_SKILLS];
    BS_CHECK_EQ_INT(bs_creation_skill_choices(&c, choices), 18);
    BS_CHECK(!bs_creation_set_skill(&c, 0, 7));
    BS_CHECK(bs_creation_set_skill(&c, 0, 20)); /* Critical Strike, by Intelligence */
    BS_CHECK(!bs_creation_set_skill(&c, 1, 20));
    BS_CHECK_EQ_INT(bs_creation_skill_choices(&c, choices), 17);
    BS_CHECK_EQ_INT(c.cls.skills[20], 30); /* a primary skill starts at 30 for nothing */
    BS_CHECK(bs_creation_set_skill(&c, 6, 18)); /* Blunt Weapon, by Strength (now 10), a minor skill */
    BS_CHECK_EQ_INT(c.cls.skills[18], 15);
    BS_CHECK(bs_creation_set_skill(&c, 6, 17)); /* Axe in its place takes its value; Blunt goes back to 5 */
    BS_CHECK_EQ_INT(c.cls.skills[17], 15);
    BS_CHECK_EQ_INT(c.cls.skills[18], 5);
    BS_CHECK_EQ_INT(c.cls.class_skills[6], 17);
    BS_CHECK_EQ_INT(bs_creation_skill(&c, 6, true), BS_CREATE_ATTRIBUTE); /* 15, over Strength's 10 */
    int32_t before = c.points;
    BS_CHECK_EQ_INT(bs_creation_skill(&c, 6, false), BS_CREATE_OK);
    BS_CHECK_EQ_INT(c.points, before + bs_creation_skill_cost(14, BS_LIST_MINOR));
    while (c.cls.skills[17] > 10)
        BS_CHECK_EQ_INT(bs_creation_skill(&c, 6, false), BS_CREATE_OK);
    BS_CHECK_EQ_INT(bs_creation_skill(&c, 6, true), BS_CREATE_ATTRIBUTE);
    BS_CHECK_EQ_INT(bs_creation_attribute(&c, 0, false), BS_CREATE_LIMIT);
    BS_CHECK_EQ_INT(bs_creation_attribute(&c, 0, true), BS_CREATE_OK);
    BS_CHECK_EQ_INT(bs_creation_skill(&c, 6, true), BS_CREATE_OK);
    BS_CHECK_EQ_INT(bs_creation_attribute(&c, 0, false), BS_CREATE_SKILL_BELOW);
    BS_CHECK_EQ_INT(bs_creation_skill(&c, 1, true), BS_CREATE_LIMIT); /* an empty slot */
    while (c.cls.skills[17] > 5)
        BS_CHECK_EQ_INT(bs_creation_skill(&c, 6, false), BS_CREATE_OK);
    BS_CHECK_EQ_INT(bs_creation_skill(&c, 6, false), BS_CREATE_LIMIT);

    /* Wounds 50 .. 200. */
    BS_CHECK_EQ_INT(bs_creation_wounds(&c, false), BS_CREATE_LIMIT);
    before = c.points;
    BS_CHECK_EQ_INT(bs_creation_wounds(&c, true), BS_CREATE_OK);
    BS_CHECK_EQ_INT(c.points, before - 52);

    /* Not enough points. */
    c.points = 10;
    BS_CHECK_EQ_INT(bs_creation_attribute(&c, 1, true), BS_CREATE_NO_POINTS);
    BS_CHECK_EQ_INT(c.cls.attributes[1], 50);
}

static void test_advantages(void)
{
    bs_creation c;
    bs_creation_new(&c);
    BS_CHECK_EQ_INT(bs_creation_toggle(&c, BS_ADV_ATHLETICISM), BS_CREATE_OK);
    BS_CHECK(c.cls.athleticism);
    BS_CHECK_EQ_INT(c.points, 4300);
    BS_CHECK_EQ_INT(bs_creation_toggle(&c, BS_ADV_ATHLETICISM), BS_CREATE_OK);
    BS_CHECK_EQ_INT(c.points, 4700);

    BS_CHECK_EQ_INT(bs_creation_toggle(&c, BS_DIS_NO_SP_REGEN), BS_CREATE_OK);
    BS_CHECK_EQ_INT(c.points, 5700);
    BS_CHECK(bs_creation_excluded_flag(&c, BS_ADV_REGENERATE_SP));
    BS_CHECK_EQ_INT(bs_creation_toggle(&c, BS_ADV_REGENERATE_SP), BS_CREATE_CONFLICT);
    BS_CHECK_EQ_INT(bs_creation_toggle(&c, BS_DIS_NO_SHIELD), BS_CREATE_OK);
    BS_CHECK_EQ_INT(c.points, 5400);

    /* Fire: resistance, then the others to fire are out. */
    int fire = bs_element_bit(BS_ELEMENT_FIRE);
    BS_CHECK_EQ_INT(bs_creation_tolerance(&c, BS_TOL_RESISTANCE, fire), BS_CREATE_OK);
    BS_CHECK_EQ_INT(c.points, 4600);
    BS_CHECK(bs_creation_excluded_tolerance(&c, BS_TOL_CRITICAL, fire));
    BS_CHECK_EQ_INT(bs_creation_tolerance(&c, BS_TOL_IMMUNITY, fire), BS_CREATE_CONFLICT);
    BS_CHECK_EQ_INT(bs_creation_tolerance(&c, BS_TOL_CRITICAL, bs_element_bit(BS_ELEMENT_COLD)), BS_CREATE_OK);
    BS_CHECK_EQ_INT(c.points, 6200);
    BS_CHECK_EQ_INT(c.cls.critical_weakness, 4);

    /* Magery x 1 .. x 3. */
    BS_CHECK_EQ_INT(bs_creation_magery(&c, false), BS_CREATE_LIMIT);
    BS_CHECK_EQ_INT(bs_creation_magery(&c, true), BS_CREATE_OK);
    BS_CHECK_EQ_INT(c.cls.magery, 0x180);
    BS_CHECK_EQ_INT(c.points, 5500);
    BS_CHECK_EQ_INT(bs_creation_magery(&c, true), BS_CREATE_OK);
    BS_CHECK_EQ_INT(bs_creation_magery(&c, true), BS_CREATE_OK);
    BS_CHECK_EQ_INT(bs_creation_magery(&c, true), BS_CREATE_LIMIT);
    BS_CHECK_EQ_INT(c.cls.magery, 0x300);
    BS_CHECK_EQ_INT(c.points, 4000);

    /* Armour and material limits, weapons. */
    BS_CHECK_EQ_INT(bs_creation_armour(&c, true), BS_CREATE_LIMIT);
    BS_CHECK_EQ_INT(bs_creation_armour(&c, false), BS_CREATE_OK);
    BS_CHECK_EQ_INT(c.points, 5000);
    BS_CHECK_EQ_INT(bs_creation_armour(&c, false), BS_CREATE_OK);
    BS_CHECK_EQ_INT(c.points, 5500);
    BS_CHECK_EQ_INT(bs_creation_material(&c, false), BS_CREATE_OK);
    BS_CHECK_EQ_INT(c.cls.max_material, 8);
    BS_CHECK_EQ_INT(c.points, 5900);
    BS_CHECK_EQ_INT(bs_creation_forbid_weapon(&c, 16), BS_CREATE_OK);
    BS_CHECK_EQ_INT(c.points, 6900);
    BS_CHECK_EQ_INT(bs_creation_forbid_weapon(&c, 16), BS_CREATE_OK);
    BS_CHECK_EQ_INT(c.points, 5900);
    BS_CHECK_EQ_INT(bs_creation_forbid_weapon(&c, 3), BS_CREATE_LIMIT);
}

static void test_spells_and_items(void)
{
    bs_creation c;
    bs_creation_new(&c);
    bs_spell s = {0};
    s.effect = BS_SPELL_CURE_HEALTH;
    s.cost = 40;
    BS_CHECK(bs_creation_spell_offered(&c, &s));
    BS_CHECK_EQ_INT(bs_creation_add_spell(&c, &s), BS_CREATE_OK);
    BS_CHECK_EQ_INT(c.points, 4380);
    BS_CHECK(!bs_creation_spell_offered(&c, &s));
    BS_CHECK_EQ_INT(bs_creation_add_spell(&c, &s), BS_CREATE_LIMIT);
    bs_spell sigil = {0};
    sigil.effect = BS_SPELL_DISPEL_SIGIL;
    BS_CHECK(!bs_creation_spell_offered(&c, &sigil));
    bs_creation_remove_spell(&c, 0);
    BS_CHECK_EQ_INT(c.points, 4700);
    BS_CHECK_EQ_INT(c.cls.spell_count, 0);

    bs_item it = {0};
    it.base = 1;
    it.condition_max = 300;
    it.material = 2;
    BS_CHECK_EQ_INT(bs_creation_equipment_cost(&it), 160);
    it.condition_max = 4;
    BS_CHECK_EQ_INT(bs_creation_equipment_cost(&it), 110);
    it.base = 17;
    BS_CHECK_EQ_INT(bs_creation_equipment_cost(&it), 10);
    it.base = BS_ITEM_CLOTHES;
    BS_CHECK_EQ_INT(bs_creation_equipment_cost(&it), 0);
    it.base = BS_ITEM_POTION;
    BS_CHECK_EQ_INT(bs_creation_magic_cost(&it), 60);
    it.base = 26; /* armour */
    BS_CHECK_EQ_INT(bs_creation_magic_cost(&it), 600);
    it.base = 30; /* a ring */
    BS_CHECK_EQ_INT(bs_creation_magic_cost(&it), 500);
    it.base = 3; /* a weapon */
    BS_CHECK_EQ_INT(bs_creation_magic_cost(&it), 600);

    /* A shop: buying takes the cost, giving back returns it. */
    static bs_creation_shop shop;
    memset(&shop, 0, sizeof shop);
    shop.magic = true;
    shop.items[0] = it;
    shop.count = 1;
    BS_CHECK_EQ_INT(bs_creation_buy(&c, &shop, 0), BS_CREATE_OK);
    BS_CHECK_EQ_INT(c.points, 4100);
    BS_CHECK_EQ_INT(bs_creation_buy(&c, &shop, 0), BS_CREATE_LIMIT);
    bs_creation_sell(&c, &shop, 0);
    BS_CHECK_EQ_INT(c.points, 4700);
    BS_CHECK(!shop.bought[0]);
    c.points = 100;
    BS_CHECK_EQ_INT(bs_creation_buy(&c, &shop, 0), BS_CREATE_NO_POINTS);

    /* A class's item: its picture takes the character's sex letter if a weapon, clothes or armour. */
    bs_scl_item si = {0};
    si.base = 3;
    si.number = -1;
    strcpy(si.picture, "MWPNS603");
    bs_creation_item(&si, true, &it);
    BS_CHECK(strcmp(it.picture, "FWPNS603") == 0);
    BS_CHECK_EQ_INT(it.number, 0);
    BS_CHECK_EQ_INT(it.quantity, 1);
    si.base = 24; /* armour too: the shipped classes' is all female */
    strcpy(si.picture, "fHLMT502");
    bs_creation_item(&si, false, &it);
    BS_CHECK(strcmp(it.picture, "MHLMT502") == 0);
    si.base = 30; /* a ring keeps its picture */
    strcpy(si.picture, "fRING001");
    bs_creation_item(&si, false, &it);
    BS_CHECK(strcmp(it.picture, "fRING001") == 0);
}

static void test_apply(void)
{
    bs_creation c;
    bs_creation_new(&c);
    strcpy(c.cls.class_name, "Tester");
    c.cls.attributes[1] = 60;
    c.cls.magery = 0x180;
    c.cls.wounds = 80;
    c.cls.skills[19] = 30;
    c.cls.max_armour = 1;
    c.cls.forbidden_weapons = 8;
    c.cls.rapid_healing = true;
    bs_character ch;
    bs_equip_rules rules;
    bs_creation_apply(&c.cls, 5, &ch, &rules); /* a Wood Elf */
    BS_CHECK_EQ_INT(ch.spell_points_max, 90);
    BS_CHECK_EQ_INT(ch.spell_points, 90);
    BS_CHECK_EQ_INT(ch.health_max, 80);
    BS_CHECK_EQ_INT(ch.skills[19], 45);
    BS_CHECK_EQ_INT(ch.skills[1], 20);
    BS_CHECK_EQ_INT(ch.skills[6], 20);
    BS_CHECK_EQ_INT(ch.skills[0], 5);
    BS_CHECK_EQ_INT(ch.level, 1);
    BS_CHECK(ch.double_healing);
    BS_CHECK(strcmp(ch.class_name, "Tester") == 0);
    BS_CHECK_EQ_INT(rules.armour, 1);
    BS_CHECK_EQ_INT(rules.forbidden_weapons, 8);

    int32_t bonus[BS_SCL_SKILLS];
    int total = 0;
    for (int r = 0; r < 6; r++) {
        bs_race_skill_bonus(r, bonus);
        int sum = 0;
        for (int i = 0; i < BS_SCL_SKILLS; i++)
            sum += bonus[i];
        BS_CHECK_EQ_INT(sum, 45);
        total += sum;
    }
    BS_CHECK_EQ_INT(total, 270);
    bs_race_skill_bonus(3, bonus); /* High Elf */
    BS_CHECK_EQ_INT(bonus[19], 5);
    BS_CHECK_EQ_INT(bonus[15], 10);
}

int main(void)
{
    BS_RUN(test_costs);
    BS_RUN(test_new);
    BS_RUN(test_attributes_and_skills);
    BS_RUN(test_advantages);
    BS_RUN(test_spells_and_items);
    BS_RUN(test_apply);
    return BS_TEST_RESULT();
}
