/* Keys (the levels' scripts), sigils and gems. */

#include <string.h>

#include "bs/game/level_script.h"
#include "bs_test.h"

static const bs_item_place carried = {BS_ITEM_CARRIED, 0};

static uint32_t give(bs_items *it, uint32_t base, int32_t magic)
{
    bs_item x = {.base = (uint16_t)base, .magic = magic, .quantity = 1};
    return bs_items_add(it, &x, carried, -1, NULL);
}

static bool buttons_pressed;
static bool moved(void *ctx, const char *model)
{
    (void)ctx;
    (void)model;
    return buttons_pressed;
}

static void test_cogs(void)
{
    bs_items it = {0};
    bs_level_script s;
    bs_level_script_init(&s, 1);
    /* Not locked, or no cog: nothing. */
    bs_script_result r = bs_level_script_use(&s, &it, "1cpanel", true, NULL, NULL);
    BS_CHECK(!r.activate && r.counter == -1 && r.locked);
    for (int k = 0; k < 6; k++)
        give(&it, 36, 0);
    r = bs_level_script_use(&s, &it, "1cpanel", false, NULL, NULL);
    BS_CHECK(!r.activate && r.counter == -1 && !r.locked);
    /* Five cogs move the counter 1cnt02 .. 1cnt06; the fifth opens the way. */
    for (int k = 1; k <= 5; k++) {
        r = bs_level_script_use(&s, &it, "1cpanel", true, NULL, NULL);
        BS_CHECK_EQ_INT(r.counter, k);
        BS_CHECK(r.activate == (k == 5) && s.done == (k == 5));
    }
    /* The sixth stays with the player. */
    r = bs_level_script_use(&s, &it, "1cpanel", true, NULL, NULL);
    BS_CHECK(!r.activate && r.counter == -1);
    uint32_t left = 0;
    for (uint32_t i = 0; i < it.count; i++)
        left += it.items[i].place.where != BS_ITEM_GONE;
    BS_CHECK_EQ_INT(left, 0); /* taken but not needed: the game takes it too */
    const char *const *names;
    BS_CHECK_EQ_INT(bs_level_script_counter(2, &names), 5);
    BS_CHECK(strcmp(names[4], "alta05") == 0);
    bs_items_free(&it);
}

static void test_rods(void)
{
    bs_items it = {0};
    bs_level_script s;
    bs_level_script_init(&s, 3);
    bs_script_result r = bs_level_script_use(&s, &it, "l2dr1", false, moved, NULL);
    BS_CHECK(r.locked && r.message_count == 1 && r.messages[0] == 124);
    for (int k = 0; k < 3; k++)
        give(&it, 37, 0);
    buttons_pressed = false;
    r = bs_level_script_use(&s, &it, "l2spool", false, moved, NULL);
    BS_CHECK(r.locked && !r.activate && r.message_count == 0); /* the buttons first */
    buttons_pressed = true;
    r = bs_level_script_use(&s, &it, "l2spool", false, moved, NULL);
    BS_CHECK(r.message_count == 1 && r.messages[0] == 126 && !r.activate);
    bs_level_script_use(&s, &it, "l2spool", false, moved, NULL);
    r = bs_level_script_use(&s, &it, "L2SPOOL", false, moved, NULL);
    BS_CHECK(r.activate && r.messages[0] == 125 && s.done);
    r = bs_level_script_use(&s, &it, "l2dr2", false, moved, NULL);
    BS_CHECK(!r.locked && r.message_count == 0);
    bs_items_free(&it);
}

static void test_temple_keys(void)
{
    bs_items it = {0};
    bs_level_script s;
    bs_level_script_init(&s, 6);
    bs_script_result r = bs_level_script_use(&s, &it, "l6phone", false, NULL, NULL);
    BS_CHECK(r.messages[0] == 129 && !r.activate);
    for (int k = 0; k < 3; k++)
        give(&it, 35, 0);
    r = bs_level_script_use(&s, &it, "l6phone", false, NULL, NULL);
    BS_CHECK(r.message_count == 1 && r.messages[0] == 127);
    bs_level_script_use(&s, &it, "l6phone", false, NULL, NULL);
    r = bs_level_script_use(&s, &it, "l6phone", false, NULL, NULL);
    BS_CHECK(r.activate && r.message_count == 2 && r.messages[1] == 125);

    /* Level 5: six Gatekeys at once. */
    bs_level_script_init(&s, 5);
    for (int k = 0; k < 5; k++)
        give(&it, 38, 0);
    r = bs_level_script_use(&s, &it, "tempd", false, NULL, NULL);
    BS_CHECK(!r.activate && r.messages[0] == 2 && r.locked);
    give(&it, 38, 0);
    r = bs_level_script_use(&s, &it, "tempd", false, NULL, NULL);
    BS_CHECK(r.activate && r.message_count == 0);
    bs_items_free(&it);
}

static void test_sigils(void)
{
    char c;
    BS_CHECK(bs_sigil_model("sigilf", &c) && c == 'f');
    BS_CHECK(bs_sigil_model("SIGILZ", &c) && c == 'z');
    BS_CHECK(!bs_sigil_model("sigil", &c) && !bs_sigil_model("gem", &c));

    bs_items it = {0};
    bs_sigil sg[2] = {{.object = 7, .letter = 'f', .pos = {1000, 0, 0}}, {.object = 8, .letter = 'd', .pos = {0, 0, 0}}};
    uint32_t which;
    bool hurt;
    /* Far away: nothing. Within 300 but not 120: nothing yet. */
    BS_CHECK_EQ_INT(bs_sigils_check(sg, 2, &it, (const float[3]){0, 0, 2000}, 1, &which, &hurt), BS_SIGIL_NONE);
    BS_CHECK_EQ_INT(bs_sigils_check(sg, 2, &it, (const float[3]){0, 0, 200}, 1, &which, &hurt), BS_SIGIL_NONE);
    /* Within 120: blocked, hurt once, not again in the next frame, again after a gap. */
    BS_CHECK_EQ_INT(bs_sigils_check(sg, 2, &it, (const float[3]){0, 0, 100}, 5, &which, &hurt), BS_SIGIL_BLOCKED);
    BS_CHECK(which == 1 && hurt);
    bs_sigils_check(sg, 2, &it, (const float[3]){0, 0, 100}, 6, &which, &hurt);
    BS_CHECK(!hurt);
    bs_sigils_check(sg, 2, &it, (const float[3]){0, 0, 100}, 9, &which, &hurt);
    BS_CHECK(hurt);
    /* Too far below: nothing. */
    BS_CHECK_EQ_INT(bs_sigils_check(sg, 2, &it, (const float[3]){0, 200, 0}, 10, &which, &hurt), BS_SIGIL_NONE);
    /* "Doht Sigil" (9030) isn't "of Entry": no. "Doht Sigil of Entry" (9004): opens d within 300. */
    give(&it, BS_ITEM_SIGIL_AMULET, 9030);
    BS_CHECK(!bs_items_have_sigil(&it, 'd'));
    give(&it, BS_ITEM_SIGIL_AMULET, 9004);
    BS_CHECK(bs_items_have_sigil(&it, 'd') && !bs_items_have_sigil(&it, 'f'));
    BS_CHECK_EQ_INT(bs_sigils_check(sg, 2, &it, (const float[3]){0, 0, 250}, 11, &which, &hurt), BS_SIGIL_OPENED);
    BS_CHECK(which == 1 && sg[1].gone);
    BS_CHECK_EQ_INT(bs_sigils_check(sg, 2, &it, (const float[3]){0, 0, 0}, 12, &which, &hurt), BS_SIGIL_NONE);
    bs_items_free(&it);
}

static void test_gems(void)
{
    bs_character c;
    bs_character_default(&c);
    c.health = 10;
    BS_CHECK_EQ_INT(bs_gem_use(&c, false, 80), 40);
    BS_CHECK_EQ_INT(c.health, 50);
    BS_CHECK_EQ_INT(bs_gem_use(&c, false, -25), -12); /* 50 x -25 / 100 */
    BS_CHECK_EQ_INT(c.health, 38);
    c.spell_points = 0;
    BS_CHECK_EQ_INT(bs_gem_use(&c, true, 100), 50);
    BS_CHECK_EQ_INT(bs_gem_use(&c, false, -200), -38);
    BS_CHECK_EQ_INT(c.health, 0);
}

static void test_exits(void)
{
    bs_level_script s;
    bs_level_script_init(&s, 1);
    bs_exit_query q = {.floor_model = "1zoom", .doorpans_open = 5};
    BS_CHECK(!bs_level_script_exit(&s, &q)); /* the panel's cogs first */
    s.done = true;
    BS_CHECK(bs_level_script_exit(&s, &q));
    q.doorpans_open = 4;
    BS_CHECK(!bs_level_script_exit(&s, &q));
    q.floor_model = NULL;
    BS_CHECK(!bs_level_script_exit(&s, &q));

    bs_level_script_init(&s, 2);
    q = (bs_exit_query){.floor_model = "1blok", .floor_texture = "PORT03"};
    BS_CHECK(!bs_level_script_exit(&s, &q));
    s.done = true;
    BS_CHECK(bs_level_script_exit(&s, &q));
    q.floor_texture = "port04";
    BS_CHECK(!bs_level_script_exit(&s, &q));

    bs_level_script_init(&s, 3);
    q = (bs_exit_query){.floor_model = "l2a46", .floor_texture = "black"};
    BS_CHECK(bs_level_script_exit(&s, &q));
    bs_level_script_init(&s, 4);
    q = (bs_exit_query){.floor_model = "exitgate", .floor_locked = true};
    BS_CHECK(bs_level_script_exit(&s, &q));
    bs_level_script_init(&s, 6);
    BS_CHECK(!bs_level_script_exit(&s, &q));
    s.progress = 3;
    BS_CHECK(bs_level_script_exit(&s, &q));
    bs_level_script_init(&s, 8);
    BS_CHECK(!bs_level_script_exit(&s, &q));

    BS_CHECK_EQ_INT(bs_next_level_number(1), 2);
    BS_CHECK_EQ_INT(bs_next_level_number(6), 8);
    BS_CHECK_EQ_INT(bs_next_level_number(8), 0);
}

int main(void)
{
    BS_RUN(test_cogs);
    BS_RUN(test_rods);
    BS_RUN(test_temple_keys);
    BS_RUN(test_sigils);
    BS_RUN(test_gems);
    BS_RUN(test_exits);
    return BS_TEST_RESULT();
}
