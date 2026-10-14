/* Level logic (triggers, links, moves, rotations, timers, teleports) on a synthetic level. */

#include <math.h>
#include <string.h>

#include "bs/game/level_logic.h"
#include "bs_test.h"
#include "synth.h"

static uint8_t buf[8192];
static bs_arena arena;
static bs_bs6_level level;
static bs_level_logic lg;
static bs_logic_output out;

/* The logic object of entity id; objects are in file order, so ids 10..17 are 0..7. */
static uint32_t obj(uint32_t id)
{
    for (uint32_t i = 0; i < lg.object_count; i++)
        if (lg.objects[i].entity_id == id)
            return i;
    return UINT32_MAX;
}

static void setup(void)
{
    bs_arena_free(&arena);
    bs_arena_init(&arena, 0);
    size_t n = synth_bs6_logic(buf, sizeof buf);
    BS_CHECK(n > 0);
    BS_CHECK_EQ_INT(bs_bs6_parse(buf, n, &arena, &level), BS_OK);
    BS_CHECK_EQ_INT(bs_level_logic_build(&level, &arena, &lg), BS_OK);
}

static void test_build(void)
{
    setup();
    BS_CHECK_EQ_INT(lg.object_count, 10); /* 8 objects, 2 flats */
    BS_CHECK_EQ_INT(lg.link_count, 4);
    const bs_logic_object *button = &lg.objects[obj(10)];
    BS_CHECK(button->ctrl_count == 1 && button->link_count == 1);
    BS_CHECK_EQ_INT(lg.ctrls[button->first_ctrl].sound, 72);
    BS_CHECK_EQ_INT(lg.links[button->first_link], obj(11));
    const bs_logic_ctrl *door = &lg.ctrls[lg.objects[obj(11)].first_ctrl];
    BS_CHECK(door->type == BS_LOGIC_CTRL_ROTATE && door->reversible && !door->continuous);
    BS_CHECK(lg.ctrls[lg.objects[obj(13)].first_ctrl + 1].continuous);
    BS_CHECK(lg.objects[obj(20)].teleport_target && !lg.objects[obj(14)].teleport_target);
    BS_CHECK(!bs_level_logic_interactive(&lg, obj(21)));
    BS_CHECK_EQ_INT(bs_level_logic_find(&lg, BS_LOGIC_FLAT, 0), obj(20));
}

/* A button opens a reversible door through its link; using it again closes the door. */
static void test_door(void)
{
    setup();
    uint32_t b = obj(10), d = obj(11);
    bs_level_logic_activate(&lg, d, BS_LOGIC_EVENT_USE, 0, &out);
    BS_CHECK(!lg.objects[d].active); /* the door itself has no use trigger */

    bs_level_logic_activate(&lg, b, BS_LOGIC_EVENT_STEP, 0, &out);
    BS_CHECK(!lg.objects[b].active); /* wrong event */
    bs_level_logic_activate(&lg, b, BS_LOGIC_EVENT_USE, 1000, &out);
    BS_CHECK(lg.objects[b].active && lg.objects[d].active);
    BS_CHECK(out.sound_count == 1 && out.sounds[0].id == 72 && out.sounds[0].object == b);

    bs_level_logic_update(&lg, 1500, &out);
    BS_CHECK_EQ_INT(lg.objects[d].angles[1], -256); /* half way */
    BS_CHECK(!lg.objects[b].active);                /* the trigger chain is done */
    bs_level_logic_activate(&lg, b, BS_LOGIC_EVENT_USE, 1600, &out);
    BS_CHECK_EQ_INT(lg.active_count, 2);            /* the button's own chain runs again... */
    BS_CHECK_EQ_INT(lg.ctrls[lg.objects[d].first_ctrl].start_ms, 1000); /* ...but the busy door isn't restarted */
    bs_level_logic_update(&lg, 2100, &out);
    BS_CHECK_EQ_INT(lg.objects[d].angles[1], -512);
    BS_CHECK(!lg.objects[d].active);

    bs_level_logic_activate(&lg, b, BS_LOGIC_EVENT_USE, 3000, &out);
    bs_level_logic_update(&lg, 3500, &out);
    BS_CHECK_EQ_INT(lg.objects[d].angles[1], -256); /* closing */
    bs_level_logic_update(&lg, 4000, &out);
    BS_CHECK_EQ_INT(lg.objects[d].angles[1], 0);
}

/* A lift moves when stepped on, reports its movement for carrying, and comes back down next time. */
static void test_lift(void)
{
    setup();
    uint32_t l = obj(12);
    bs_level_logic_activate(&lg, l, BS_LOGIC_EVENT_USE, 0, &out);
    BS_CHECK(!lg.objects[l].active);
    bs_level_logic_activate(&lg, l, BS_LOGIC_EVENT_STEP, 0, &out);
    bs_level_logic_update(&lg, 500, &out);
    BS_CHECK(fabsf(lg.objects[l].pos[1] - -125) < 0.5f);
    BS_CHECK(fabsf(lg.objects[l].delta[1] - -125) < 0.5f);
    bs_level_logic_update(&lg, 600, &out);
    BS_CHECK(fabsf(lg.objects[l].delta[1] - -25) < 0.5f);
    bs_level_logic_update(&lg, 2000, &out);
    BS_CHECK(fabsf(lg.objects[l].pos[1] - -500) < 0.5f && !lg.objects[l].active);
    bs_level_logic_activate(&lg, l, BS_LOGIC_EVENT_STEP, 3000, &out);
    bs_level_logic_update(&lg, 5000, &out);
    BS_CHECK(fabsf(lg.objects[l].pos[1]) < 0.5f && fabsf(lg.objects[l].pos[0] - 200) < 0.5f);
}

/* "c" keeps a wheel turning: the chain never finishes. */
static void test_wheel(void)
{
    setup();
    uint32_t w = obj(13);
    bs_level_logic_activate(&lg, w, BS_LOGIC_EVENT_USE, 0, &out);
    bs_level_logic_update(&lg, 250, &out);
    BS_CHECK_EQ_INT(lg.objects[w].angles[1], 1023);
    bs_level_logic_update(&lg, 500, &out);  /* end of a turn: restarts from the starting angle */
    BS_CHECK_EQ_INT(lg.objects[w].angles[1], 2047);
    bs_level_logic_update(&lg, 750, &out);
    BS_CHECK_EQ_INT(lg.objects[w].angles[1], 1023);
    BS_CHECK(lg.objects[w].active);
}

/* Without "r" an object moves once. (A run only counts once a frame has landed
 * part-way through it, as in the game, so the test steps through the middle.) */
static void test_one_shot(void)
{
    setup();
    uint32_t s = obj(17);
    bs_level_logic_activate(&lg, s, BS_LOGIC_EVENT_USE, 0, &out);
    bs_level_logic_update(&lg, 50, &out);
    bs_level_logic_update(&lg, 100, &out);
    BS_CHECK(fabsf(lg.objects[s].pos[2] - 50) < 0.5f);
    bs_level_logic_update(&lg, 110, &out);
    bs_level_logic_activate(&lg, s, BS_LOGIC_EVENT_USE, 200, &out);
    bs_level_logic_update(&lg, 400, &out);
    BS_CHECK(fabsf(lg.objects[s].pos[2] - 50) < 0.5f);
}

/* Stepping on a portal teleports the player to the target flat plus its offset. */
static void test_teleport(void)
{
    setup();
    uint32_t p = obj(14);
    lg.player[0] = 400;
    lg.player[1] = lg.player[2] = 0;
    bs_level_logic_activate(&lg, p, BS_LOGIC_EVENT_STEP, 0, &out);
    BS_CHECK(out.player_moved);
    BS_CHECK(lg.player[0] == 1000 && lg.player[1] == 0 && lg.player[2] == 0);
    bs_level_logic_update(&lg, 16, &out);
    BS_CHECK(out.player_moved);
    BS_CHECK(lg.player[0] == 1050 && lg.player[1] == -20 && lg.player[2] == 30);
    BS_CHECK(lg.objects[obj(20)].pos[0] == 1000); /* the flat stays put */
    bs_level_logic_update(&lg, 32, &out);
    BS_CHECK(lg.active_count == 0);
    /* Teleports work every time. */
    lg.player[0] = 400;
    bs_level_logic_activate(&lg, p, BS_LOGIC_EVENT_STEP, 100, &out);
    bs_level_logic_update(&lg, 116, &out);
    BS_CHECK(lg.player[0] == 1050);
}

/* Two timers linked in a ring keep each other going. */
static void test_timers(void)
{
    setup();
    uint32_t a = obj(15), b = obj(16);
    bs_level_logic_activate(&lg, a, BS_LOGIC_EVENT_USE, 0, &out);
    BS_CHECK(!lg.objects[a].active); /* no use trigger of its own */
    /* Start a through a link, as a trigger elsewhere would: point the button's link at it. */
    lg.links[lg.objects[obj(10)].first_link] = a;
    bs_level_logic_activate(&lg, obj(10), BS_LOGIC_EVENT_USE, 0, &out);
    BS_CHECK(lg.objects[a].active && !lg.objects[b].active);
    bs_level_logic_update(&lg, 100, &out);
    BS_CHECK(fabsf(lg.objects[a].pos[0] - 510) < 0.5f); /* a moved +10 x */
    bs_level_logic_update(&lg, 300, &out);               /* a's timer fires: b starts */
    BS_CHECK(lg.objects[b].active);
    bs_level_logic_update(&lg, 400, &out);
    BS_CHECK(fabsf(lg.objects[b].pos[1] - 10) < 0.5f);
    BS_CHECK(!lg.objects[a].active);                     /* a finished */
    bs_level_logic_update(&lg, 600, &out);               /* b's timer fires: a again */
    BS_CHECK(lg.objects[a].active);
    bs_level_logic_update(&lg, 700, &out);
    BS_CHECK(fabsf(lg.objects[a].pos[0] - 520) < 0.5f); /* no "r": the second run goes on from 510 */
}

int main(void)
{
    bs_arena_init(&arena, 0);
    BS_RUN(test_build);
    BS_RUN(test_door);
    BS_RUN(test_lift);
    BS_RUN(test_wheel);
    BS_RUN(test_one_shot);
    BS_RUN(test_teleport);
    BS_RUN(test_timers);
    bs_arena_free(&arena);
    return BS_TEST_RESULT();
}
