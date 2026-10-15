/* The weapon in hand: its states, swings, the blow's timing, poses and swing times. */

#include <math.h>

#include "bs/game/hand.h"
#include "bs_test.h"

/* Runs the hand from `from` to `to` ms in 10 ms frames; returns the time of the first blow, or 0. */
static uint32_t run(bs_hand *h, uint32_t from, uint32_t to, bool moving, bool drawing)
{
    uint32_t blow = 0;
    for (uint32_t t = from; t <= to; t += 10)
        if (bs_hand_update(h, t, moving, drawing) && !blow)
            blow = t;
    return blow;
}

static void test_equip_and_raise(void)
{
    bs_hand h;
    bs_hand_reset(&h);
    bs_hand_pose p;
    BS_CHECK(!bs_hand_pose_at(&h, 0, &p));
    BS_CHECK(!bs_hand_can_swing(&h));
    bs_hand_equip(&h, 3, 1000);
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
    bs_hand_equip(&h, -1, 2110);
    BS_CHECK(!bs_hand_pose_at(&h, 2110, &p));
}

static void test_swing(void)
{
    bs_hand h;
    bs_hand_reset(&h);
    bs_hand_equip(&h, 0, 0);
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
    bs_hand_update(&h, 2110, true, false);
    BS_CHECK_EQ_INT(h.state, BS_HAND_WINDUP);
    blow = run(&h, 2120, 3000, true, false);
    BS_CHECK(blow >= 2110 + 75 + 150 && blow <= 2110 + 75 + 170);
}

static void test_bow(void)
{
    bs_hand h;
    bs_hand_reset(&h);
    bs_hand_equip(&h, 9, 0);
    run(&h, 0, 1100, false, false);
    run(&h, 1110, 1200, false, true);
    BS_CHECK_EQ_INT(h.state, BS_HAND_DRAW);
    run(&h, 1210, 3000, false, true);
    BS_CHECK_EQ_INT(h.state, BS_HAND_HOLD); /* held while drawn */
    run(&h, 3010, 3020, false, false);
    BS_CHECK_EQ_INT(h.state, BS_HAND_LOOSE);
    run(&h, 3030, 3300, false, false);
    BS_CHECK_EQ_INT(h.state, BS_HAND_IDLE);
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
    BS_RUN(test_bow);
    BS_RUN(test_swing_time);
    BS_RUN(test_rotation);
    return BS_TEST_RESULT();
}
