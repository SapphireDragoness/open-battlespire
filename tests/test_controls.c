/* Key bindings and the Controls screen's settings (src/app/controls.c). */

#include <math.h>
#include <string.h>

#include "bs_test.h"
#include "controls.h"

/* The original layout is the game's table at 0xE8229 (as PC scan codes there). */
static void test_original_layout(void)
{
    controls c;
    controls_set(&c, true);
    BS_CHECK_EQ_INT(c.bind[ACT_INVENTORY][0], SDL_SCANCODE_F10);
    BS_CHECK_EQ_INT(c.bind[ACT_FULL_VIEW][0], SDL_SCANCODE_TAB);
    BS_CHECK_EQ_INT(c.bind[ACT_ATTACK][0], CONTROLS_MOUSE_RIGHT);
    BS_CHECK_EQ_INT(c.bind[ACT_SPELLBOOK][0], SDL_SCANCODE_F12);
    BS_CHECK_EQ_INT(c.bind[ACT_JUMP][0], SDL_SCANCODE_LALT);
    BS_CHECK_EQ_INT(c.bind[ACT_TURN_LEFT][0], SDL_SCANCODE_DELETE);
    BS_CHECK_EQ_INT(c.bind[ACT_CURSOR][0], SDL_SCANCODE_RETURN);
    BS_CHECK_EQ_INT(c.bind[ACT_USE][0], CONTROLS_MOUSE_LEFT);
    BS_CHECK_EQ_INT(c.bind[ACT_LANTERN][0], SDL_SCANCODE_L);
    BS_CHECK(!controls_any_duplicate(&c));
    BS_CHECK(fabsf(c.sensitivity_x - 1) < 1e-6f && !c.reverse);
    BS_CHECK_EQ_INT(c.double_click, 8);
    BS_CHECK_EQ_INT(c.turn_rate, 128);
    controls_set(&c, false);
    BS_CHECK(!controls_any_duplicate(&c));
}

static void test_duplicates_and_reserved(void)
{
    controls c;
    controls_set(&c, true);
    c.bind[ACT_CHAT_BAR][0] = SDL_SCANCODE_UP; /* Forward's */
    BS_CHECK(controls_duplicate(&c, ACT_CHAT_BAR) && controls_duplicate(&c, ACT_FORWARD));
    BS_CHECK(!controls_duplicate(&c, ACT_BACK));
    BS_CHECK(controls_any_duplicate(&c));
    /* Two unbound actions aren't the same key. */
    controls_set(&c, false);
    BS_CHECK(c.bind[ACT_CHAT_ROOM][0] == 0 && c.bind[ACT_SPECIAL][0] == 0 && !controls_any_duplicate(&c));
    BS_CHECK(controls_reserved(SDL_SCANCODE_ESCAPE) && controls_reserved(SDL_SCANCODE_F8));
    BS_CHECK(!controls_reserved(SDL_SCANCODE_F9) && !controls_reserved(CONTROLS_MOUSE_LEFT));
}

/* Default keeps the turn rate and the movement (0x1F703). */
static void test_default(void)
{
    controls c;
    controls_set(&c, true);
    c.bind[ACT_JUMP][0] = SDL_SCANCODE_J;
    c.sensitivity_y = 2;
    c.reverse = true;
    c.double_click = 15;
    c.turn_rate = 40;
    c.cursor_movement = true;
    controls_default(&c);
    BS_CHECK_EQ_INT(c.bind[ACT_JUMP][0], SDL_SCANCODE_LALT);
    BS_CHECK(fabsf(c.sensitivity_y - 1) < 1e-6f && !c.reverse && c.double_click == 8);
    BS_CHECK(c.turn_rate == 40 && c.cursor_movement);
}

static void test_names(void)
{
    /* The game's names by scan code: here a stand-in table for three. */
    const char *names[0x59];
    for (int i = 0; i < 0x59; i++)
        names[i] = "";
    names[0x39] = "SPCBAR";
    names[0x48] = "UP";
    names[0x1D] = "CTRL";
    bs_exe_text keys = {names, 0x59};
    char buf[32];
    BS_CHECK(strcmp(controls_name(SDL_SCANCODE_SPACE, &keys, buf, sizeof buf), "SPCBAR") == 0);
    BS_CHECK(strcmp(controls_name(SDL_SCANCODE_KP_8, &keys, buf, sizeof buf), "UP") == 0);
    BS_CHECK(strcmp(controls_name(SDL_SCANCODE_RCTRL, &keys, buf, sizeof buf), "CTRL") == 0);
    BS_CHECK(strcmp(controls_name(CONTROLS_MOUSE_RIGHT, &keys, buf, sizeof buf), "M2") == 0);
    /* Not in the game's table (or no name there): SDL's, in capitals. */
    BS_CHECK(strcmp(controls_name(SDL_SCANCODE_Q, &keys, buf, sizeof buf), "Q") == 0);
    BS_CHECK(strcmp(controls_name(SDL_SCANCODE_LGUI, NULL, buf, sizeof buf), "LEFT GUI") == 0);
}

static void test_rates(void)
{
    controls c;
    controls_set(&c, true);
    /* 8 ticks of 18.2 a second; 18 is about a second. */
    BS_CHECK_EQ_INT(controls_double_click_ms(&c), 439);
    c.double_click = 18;
    BS_CHECK_EQ_INT(controls_double_click_ms(&c), 989);
    /* 128 x 6 = 768 angle units a second: 2.36 radians. */
    BS_CHECK(fabsf(controls_turn_speed(&c) - 768.0f * 2.0f * 3.14159265f / 2048.0f) < 1e-3f);
}

static void test_settings_round_trip(void)
{
    controls c;
    controls_set(&c, false);
    c.bind[ACT_LANTERN][0] = SDL_SCANCODE_COMMA;
    c.bind[ACT_CHAT_ROOM][0] = CONTROLS_MOUSE_MIDDLE;
    c.bind[ACT_FORWARD][1] = SDL_SCANCODE_KP_8;
    c.sensitivity_x = 2.5f;
    c.reverse = true;
    c.double_click = 12;
    c.turn_rate = 0; /* the slider's 256 */
    c.cursor_movement = true;
    bs_ini_writer w;
    bs_ini_writer_init(&w);
    controls_write(&c, &w);
    BS_CHECK(!w.failed);
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_ini ini;
    BS_CHECK_EQ_INT(bs_ini_parse(w.text, w.len, &a, &ini, NULL), BS_OK);
    controls r;
    controls_set(&r, true); /* the file's layout wins */
    for (uint32_t i = 0; i < ini.count; i++)
        if (strcmp(ini.sections[i].name, "controls") == 0)
            controls_read(&r, &ini.sections[i]);
    BS_CHECK(!r.original);
    BS_CHECK(memcmp(r.bind, c.bind, sizeof r.bind) == 0);
    BS_CHECK(fabsf(r.sensitivity_x - 2.5f) < 1e-6f && r.reverse && r.double_click == 12);
    BS_CHECK(r.turn_rate == 0 && r.cursor_movement);
    bs_arena_free(&a);
    bs_ini_writer_free(&w);
}

int main(void)
{
    BS_RUN(test_original_layout);
    BS_RUN(test_duplicates_and_reserved);
    BS_RUN(test_default);
    BS_RUN(test_names);
    BS_RUN(test_rates);
    BS_RUN(test_settings_round_trip);
    return BS_TEST_RESULT();
}
