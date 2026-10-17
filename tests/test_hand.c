/* The weapon in hand: its states, swings, the blow's timing, poses, swing times, switching, walls and
 * bows. */

#include <math.h>

#include "bs/game/hand.h"
#include "bs_test.h"

/* Runs the hand from `from` to `to` ms in 10 ms frames; returns the time of the first report of `bit`, or 0. */
static uint32_t run_for(bs_hand *h, uint32_t from, uint32_t to, bool moving, bool blocked, int bit)
{
    uint32_t when = 0;
    bs_hand_input in = {.moving = moving, .blocked = blocked, .speed = 50};
    for (uint32_t t = from; t <= to; t += 10)
        if ((bs_hand_update(h, t, &in) & bit) && !when)
            when = t;
    return when;
}

static uint32_t run(bs_hand *h, uint32_t from, uint32_t to, bool moving, bool blocked)
{
    return run_for(h, from, to, moving, blocked, BS_HAND_STRUCK);
}

static void test_equip_and_raise(void)
{
    bs_hand h;
    bs_hand_reset(&h);
    bs_hand_pose p;
    BS_CHECK(!bs_hand_pose_at(&h, 0, &p));
    BS_CHECK(!bs_hand_can_swing(&h));
    bs_hand_equip(&h, 3, 1000, 0);
    BS_CHECK_EQ_INT(h.state, BS_HAND_RAISE);
    BS_CHECK(!bs_hand_can_swing(&h));
    /* Raised from pointing down (pitch 1536) to level in a second. */
    BS_CHECK(bs_hand_pose_at(&h, 1000, &p));
    BS_CHECK_EQ_INT(p.angles[0], 1536);
    run(&h, 1000, 2010, false, false);
    BS_CHECK_EQ_INT(h.state, BS_HAND_IDLE);
    BS_CHECK(bs_hand_can_swing(&h));
    /* At rest: 20 right, 50 above the feet, 10 back. */
    BS_CHECK(bs_hand_pose_at(&h, 2010, &p));
    BS_CHECK(fabsf(p.offset[0] - 20.0f) < 0.01f);
    BS_CHECK(fabsf(p.offset[1] + 50.0f) < 0.15f);
    BS_CHECK(fabsf(p.offset[2] + 10.0f) < 0.01f);
    BS_CHECK_EQ_INT(p.angles[0], 0);
    /* Walking bobs; emptying the hand hides the model at once. */
    run(&h, 2020, 2100, true, false);
    BS_CHECK_EQ_INT(h.state, BS_HAND_WALK);
    bs_hand_equip(&h, -1, 2110, 0);
    BS_CHECK(!bs_hand_pose_at(&h, 2110, &p));
}

static void test_swing(void)
{
    bs_hand h;
    bs_hand_reset(&h);
    bs_hand_equip(&h, 0, 0, 0);
    run(&h, 0, 1000, false, false);
    BS_CHECK_EQ_INT(h.state, BS_HAND_IDLE);
    BS_CHECK(!bs_hand_swing(&h, 6, 300)); /* not a swing */
    BS_CHECK(bs_hand_swing(&h, 9, 400));
    BS_CHECK(!bs_hand_swing(&h, 9, 400)); /* one at a time */
    /* Idle runs out (110 ms from 1000), the wind-up takes 75 ms, the blow lands half-way through 400. */
    uint32_t blow = run(&h, 1010, 2000, false, false);
    BS_CHECK(blow >= 1110 + 75 + 200 && blow <= 1110 + 75 + 220);
    BS_CHECK_EQ_INT(h.state, BS_HAND_IDLE);
    /* Walking is cut short for a swing. */
    run(&h, 2000, 2100, true, false);
    BS_CHECK_EQ_INT(h.state, BS_HAND_WALK);
    BS_CHECK(bs_hand_swing(&h, 5, 300));
    bs_hand_input walking = {.moving = true, .speed = 50};
    bs_hand_update(&h, 2110, &walking);
    BS_CHECK_EQ_INT(h.state, BS_HAND_WINDUP);
    blow = run(&h, 2120, 3000, true, false);
    BS_CHECK(blow >= 2110 + 75 + 150 && blow <= 2110 + 75 + 170);
}

static void test_wall(void)
{
    bs_hand h;
    bs_hand_reset(&h);
    bs_hand_equip(&h, 2, 0, 0);
    run(&h, 0, 1100, false, false);
    /* A wall ahead pulls the weapon back and holds it there. */
    run(&h, 1110, 1200, false, true);
    BS_CHECK_EQ_INT(h.state, BS_HAND_PULL_BACK);
    BS_CHECK(!bs_hand_can_swing(&h));
    run(&h, 1210, 3000, false, true);
    BS_CHECK_EQ_INT(h.state, BS_HAND_HELD_BACK);
    /* Gone: pushed out again, then at rest. */
    run(&h, 3010, 3020, false, false);
    BS_CHECK_EQ_INT(h.state, BS_HAND_PUSH_OUT);
    run(&h, 3030, 3300, false, false);
    BS_CHECK_EQ_INT(h.state, BS_HAND_IDLE);
    BS_CHECK(bs_hand_can_swing(&h));
}

static void test_switch(void)
{
    bs_hand h;
    bs_hand_reset(&h);
    /* Nothing in hand, or no secondary weapon: at once. */
    BS_CHECK_EQ_INT(bs_hand_request_switch(&h, 3, false, 0), BS_SWITCH_NOW);
    bs_hand_equip(&h, 3, 0, 0);
    BS_CHECK_EQ_INT(bs_hand_request_switch(&h, -1, false, 10), BS_SWITCH_NOW);
    /* A bow needs arrows, and then comes at once. */
    BS_CHECK_EQ_INT(bs_hand_request_switch(&h, 9, false, 10), BS_SWITCH_NO_ARROWS);
    BS_CHECK_EQ_INT(bs_hand_request_switch(&h, 9, true, 10), BS_SWITCH_NOW);
    /* Another weapon: the one in hand is lowered after the raising and idle, then it's time to swap. */
    BS_CHECK_EQ_INT(bs_hand_request_switch(&h, 0, false, 10), BS_SWITCH_LOWERING);
    BS_CHECK(!bs_hand_can_swing(&h));
    uint32_t swap = run_for(&h, 10, 4000, false, false, BS_HAND_SWITCH);
    BS_CHECK(swap >= 1000 + 110 + 1000 && swap <= 1000 + 110 + 1000 + 20);
    BS_CHECK_EQ_INT(h.weapon, -1);
    /* A bow in hand is lowered through the states too. */
    bs_hand_equip(&h, 9, 5000, 0);
    BS_CHECK_EQ_INT(bs_hand_request_switch(&h, 0, true, 5000), BS_SWITCH_LOWERING);
    BS_CHECK(!h.bow);
    swap = run_for(&h, 5010, 8000, false, false, BS_HAND_SWITCH);
    BS_CHECK(swap >= 5000 + 110 + 1000 && swap <= 5000 + 110 + 1000 + 20);
}

static void test_bow(void)
{
    bs_hand h;
    bs_hand_reset(&h);
    /* After a switch a bow shows 2 s later. */
    bs_hand_equip(&h, 9, 0, 2000);
    bs_hand_pose p;
    bool arrow;
    BS_CHECK(!bs_hand_pose_at(&h, 0, &p));
    BS_CHECK(!bs_hand_bow_pose(&h, 0, &p, &arrow));
    bs_hand_input in = {.speed = 50};
    for (uint32_t t = 10; t <= 2000; t += 10)
        bs_hand_update(&h, t, &in);
    BS_CHECK(bs_hand_bow_pose(&h, 100, &p, &arrow));
    BS_CHECK(arrow);
    BS_CHECK(fabsf(p.offset[0] - 10.0f) < 0.01f && fabsf(p.offset[1] + 60.0f) < 0.01f);
    BS_CHECK_EQ_INT(p.angles[0], 2048 - 100); /* tilted with the view */
    BS_CHECK_EQ_INT(p.angles[1], 0);
    /* A press looses the arrow; the next is nocked after the bow turns aside and back. */
    in.attack_pressed = true;
    BS_CHECK(bs_hand_update(&h, 2010, &in) & BS_HAND_SHOOT);
    in.attack_pressed = false;
    BS_CHECK(bs_hand_bow_pose(&h, 0, &p, &arrow));
    BS_CHECK(!arrow);
    in.attack_pressed = true;
    BS_CHECK(!(bs_hand_update(&h, 2020, &in) & BS_HAND_SHOOT)); /* not at rest */
    in.attack_pressed = false;
    uint32_t t = 2020, nocked = 0, rest = 0;
    while (t < 6000 && !rest) {
        t += 10;
        bs_hand_update(&h, t, &in);
        bs_hand_bow_pose(&h, 0, &p, &arrow);
        if (arrow && !nocked)
            nocked = t;
        if (nocked && p.angles[1] == 0)
            rest = t;
    }
    /* 711 angle units aside at 512 a second, then 712 back at the same rate (Speed 50). */
    BS_CHECK(nocked >= 2010 + 1380 && nocked <= 2010 + 1400);
    BS_CHECK(rest >= nocked + 1380 && rest <= nocked + 1400);
    in.attack_pressed = true;
    BS_CHECK(bs_hand_update(&h, t + 10, &in) & BS_HAND_SHOOT);
    /* A wall ahead turns the bow aside at once, and it can't shoot. */
    bs_hand_equip(&h, 8, 10000, 0);
    in = (bs_hand_input){.blocked = true, .speed = 50};
    bs_hand_update(&h, 10010, &in);
    bs_hand_bow_pose(&h, 0, &p, &arrow);
    BS_CHECK_EQ_INT(p.angles[1], 0x538);
    in.attack_pressed = true;
    BS_CHECK(!(bs_hand_update(&h, 10020, &in) & BS_HAND_SHOOT));
}

static void test_swing_time(void)
{
    /* A dagger with skill, Strength and Speed 50: 5 x 1.0022 x 0.9375 x 62.5. */
    BS_CHECK_EQ_INT(bs_swing_time(9, 2, 50, 50, 50), 293);
    /* Animation 11 is quicker; a claymore is slower; more skill is quicker. */
    BS_CHECK(bs_swing_time(11, 2, 50, 50, 50) < bs_swing_time(9, 2, 50, 50, 50));
    BS_CHECK(bs_swing_time(9, 30, 50, 50, 50) > bs_swing_time(9, 2, 50, 50, 50));
    BS_CHECK(bs_swing_time(9, 2, 90, 50, 50) < bs_swing_time(9, 2, 50, 50, 50));
}

static void test_rotation(void)
{
    /* Facing +x (a quarter turn): the right hand side is -z, ahead is +x. */
    float off[3] = {20, -50, -10}, out[3];
    bs_hand_offset(512, off, out);
    BS_CHECK(fabsf(out[0] + 10.0f) < 0.01f && fabsf(out[1] + 50.0f) < 0.01f && fabsf(out[2] + 20.0f) < 0.01f);
    /* A level weapon points ahead along the body. */
    int32_t level[3] = {0, 0, 0};
    float m[9];
    bs_hand_rotation(512, level, m);
    BS_CHECK(fabsf(m[2] - 1.0f) < 0.001f && fabsf(m[8]) < 0.001f);
    /* Raised from pitch 1536: the blade (+z) points down (+y). */
    int32_t down[3] = {1536, 0, 0};
    bs_hand_rotation(0, down, m);
    BS_CHECK(fabsf(m[5] - 1.0f) < 0.001f);
}

int main(void)
{
    BS_RUN(test_equip_and_raise);
    BS_RUN(test_swing);
    BS_RUN(test_wall);
    BS_RUN(test_switch);
    BS_RUN(test_bow);
    BS_RUN(test_swing_time);
    BS_RUN(test_rotation);
    return BS_TEST_RESULT();
}
