/* Arrows: their flight, what they hit, and their damage. */

#include <math.h>
#include <string.h>

#include "bs/game/arrows.h"
#include "bs_test.h"

static const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

static void test_launch(void)
{
    bs_arrows a;
    bs_arrows_clear(&a);
    bs_item arrow = {.base = BS_ITEM_ARROW, .material = 3, .quantity = 20};
    float at[3] = {0, -60, 0};
    /* Facing +x (a quarter turn), level. */
    BS_CHECK(bs_arrows_launch(&a, &arrow, at, 0, 512, identity));
    BS_CHECK(a.list[0].live);
    BS_CHECK_EQ_INT(a.list[0].item.quantity, 1);
    BS_CHECK(fabsf(a.list[0].dir[0] - 1.0f) < 0.001f && fabsf(a.list[0].dir[2]) < 0.001f);
    /* Looking down an eighth: sin only, as the game does, so less steep than the view. */
    BS_CHECK(bs_arrows_launch(&a, &arrow, at, 256, 0, identity));
    float *d = a.list[1].dir;
    BS_CHECK(fabsf(d[1] / d[2] - sinf(3.14159265f / 4.0f)) < 0.001f);
    for (int i = 2; i < BS_ARROWS; i++)
        BS_CHECK(bs_arrows_launch(&a, &arrow, at, 0, 0, identity));
    BS_CHECK(!bs_arrows_launch(&a, &arrow, at, 0, 0, identity));
}

static void test_flight(void)
{
    bs_arrows a;
    bs_arrows_clear(&a);
    bs_item arrow = {.base = BS_ITEM_ARROW, .material = 1, .quantity = 1};
    float at[3] = {0, -60, 0}, player[3] = {0, 0, 0};
    bs_arrows_launch(&a, &arrow, at, 0, 0, identity);
    bs_rng r = {1};
    bs_arrow_hits hits;
    /* 1.6 units a ms, straight on. */
    bs_arrows_update(&a, 100, NULL, NULL, player, &r, &hits);
    BS_CHECK_EQ_INT(hits.count, 0);
    BS_CHECK(fabsf(a.list[0].pos[2] - 160.0f) < 0.01f && fabsf(a.list[0].pos[1] + 60.0f) < 0.01f);
    /* Gone past 3072 units from the player. */
    for (int i = 0; i < 20; i++)
        bs_arrows_update(&a, 100, NULL, NULL, player, &r, &hits);
    BS_CHECK(!a.list[0].live);
}

static void test_hit(void)
{
    bs_arrows a;
    bs_arrows_clear(&a);
    bs_item arrow = {.base = BS_ITEM_ARROW, .material = 1, .quantity = 1};
    float at[3] = {0, -60, 0}, player[3] = {0, 0, 0};
    bs_monster list[2];
    memset(list, 0, sizeof list);
    list[0].active = true;
    list[0].dead = true; /* the dead don't stop arrows */
    list[0].body.pos[2] = 300.0f;
    list[1].active = true;
    list[1].body.pos[2] = 500.0f;
    bs_monsters ms = {.list = list, .count = 2};
    bs_arrows_launch(&a, &arrow, at, 0, 0, identity);
    bs_rng r = {7};
    bs_arrow_hits hits;
    uint32_t frames = 0;
    do {
        bs_arrows_update(&a, 50, NULL, &ms, player, &r, &hits);
        frames++;
    } while (a.list[0].live && frames < 100);
    BS_CHECK_EQ_INT(hits.count, 1);
    BS_CHECK_EQ_INT(hits.list[0].monster, 1);
    /* Reached within 80 units, at 80 a frame: 500 - 60 (the height) ... about 6 frames. */
    BS_CHECK(frames >= 5 && frames <= 7);
}

static void test_damage(void)
{
    bs_rng r = {3};
    bs_item arrow = {.base = BS_ITEM_ARROW, .material = 3};
    /* A long bow 4-4: x 1.5 by the skill, + 1 + 2 materials, + (90 - 70) / 5 Strength, - 2 armour. */
    bs_arrow_shot s = {.bow = 9, .damage_min = 4, .damage_max = 4, .skill = 50, .bow_material = 2, .strength = 90};
    BS_CHECK_EQ_INT(bs_arrow_damage(&s, &arrow, 2, &r), 6 + 1 + 2 + 4 - 2);
    /* The crossbow has no Strength bonus; the short bow counts it from 60. */
    s.bow = 10;
    BS_CHECK_EQ_INT(bs_arrow_damage(&s, &arrow, 0, &r), 9);
    s.bow = 8;
    s.strength = 40;
    BS_CHECK_EQ_INT(bs_arrow_damage(&s, &arrow, 0, &r), 9 - 4);
}

int main(void)
{
    BS_RUN(test_launch);
    BS_RUN(test_flight);
    BS_RUN(test_hit);
    BS_RUN(test_damage);
    return BS_TEST_RESULT();
}
