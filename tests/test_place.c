/* The original's sound placement rule (volume and pan from position). */

#include <math.h>

#include "bs/audio/place.h"
#include "bs_test.h"

static bool near(float a, float b)
{
    return fabsf(a - b) < 1e-4f;
}

static void test_distance(void)
{
    const float o[3] = {0, 0, 0};
    BS_CHECK_EQ_INT(bs_sound_distance(o, (const float[3]){100, 0, 0}), 100);
    /* Diagonal in x/z: 100 + 100 - 100/2. */
    BS_CHECK_EQ_INT(bs_sound_distance(o, (const float[3]){100, 0, -100}), 150);
    /* Height adds a quarter of the shorter part. */
    BS_CHECK_EQ_INT(bs_sound_distance(o, (const float[3]){100, 40, -100}), 160);
    BS_CHECK_EQ_INT(bs_sound_distance(o, (const float[3]){10, -200, 0}), 202);
    /* Coordinates are truncated to integers first. */
    BS_CHECK_EQ_INT(bs_sound_distance((const float[3]){0.9f, 0, 0}, (const float[3]){10.9f, 0, 0}), 10);
}

static void test_place(void)
{
    const float o[3] = {0, 0, 0};
    bs_sound_place p;

    /* Close: full volume. Straight ahead: centred. */
    BS_CHECK(bs_sound_place_at(o, 0, (const float[3]){0, 0, 10}, BS_SOUND_DISTANCE_NEAR, false, &p));
    BS_CHECK(near(p.gain, 1) && near(p.pan, 0));

    /* 90 degrees to the right (+x when facing +z): hard right. Volume 127 - 100*127/768 = 111. */
    BS_CHECK(bs_sound_place_at(o, 0, (const float[3]){100, 0, 0}, BS_SOUND_DISTANCE_NEAR, false, &p));
    BS_CHECK(near(p.gain, 111.0f / 127) && near(p.pan, 1));

    /* To the left: the game's angle maths gives pan 2, not 1. */
    BS_CHECK(bs_sound_place_at(o, 0, (const float[3]){-100, 0, 0}, BS_SOUND_DISTANCE_NEAR, false, &p));
    BS_CHECK(near(p.pan, -62.0f / 63));

    /* Behind sounds like in front. */
    BS_CHECK(bs_sound_place_at(o, 0, (const float[3]){0, 0, -100}, BS_SOUND_DISTANCE_NEAR, false, &p));
    BS_CHECK(near(p.pan, 0));

    /* Facing +x (yaw 512), a source at +x is ahead; one at +z is to the left. */
    BS_CHECK(bs_sound_place_at(o, 512, (const float[3]){100, 0, 0}, BS_SOUND_DISTANCE_NEAR, false, &p));
    BS_CHECK(near(p.pan, 0));
    BS_CHECK(bs_sound_place_at(o, 512, (const float[3]){0, 0, 100}, BS_SOUND_DISTANCE_NEAR, false, &p));
    BS_CHECK(p.pan < -0.9f);
    /* Yaw wraps. */
    BS_CHECK(bs_sound_place_at(o, 512 + 2048 * 3, (const float[3]){100, 0, 0}, BS_SOUND_DISTANCE_NEAR, false, &p));
    BS_CHECK(near(p.pan, 0));

    /* Out of range: one-shots are dropped, loops start silent. */
    BS_CHECK(!bs_sound_place_at(o, 0, (const float[3]){800, 0, 0}, BS_SOUND_DISTANCE_NEAR, false, &p));
    BS_CHECK(!bs_sound_place_at(o, 0, (const float[3]){768, 0, 0}, BS_SOUND_DISTANCE_NEAR, false, &p));
    BS_CHECK(bs_sound_place_at(o, 0, (const float[3]){800, 0, 0}, BS_SOUND_DISTANCE_NEAR, true, &p));
    BS_CHECK(near(p.gain, 0));

    /* Far sounds: 127 - 5000*127/7740 = 45. */
    BS_CHECK(bs_sound_place_at(o, 0, (const float[3]){5000, 0, 0}, BS_SOUND_DISTANCE_FAR, false, &p));
    BS_CHECK(near(p.gain, 45.0f / 127));

    /* Same place as the listener: centred, full volume. */
    BS_CHECK(bs_sound_place_at(o, 300, o, BS_SOUND_DISTANCE_NEAR, false, &p));
    BS_CHECK(near(p.gain, 1) && near(p.pan, 0));
}

int main(void)
{
    BS_RUN(test_distance);
    BS_RUN(test_place);
    return BS_TEST_RESULT();
}
