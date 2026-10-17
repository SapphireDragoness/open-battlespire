/* Sectioned text files: sections, keys, comments, quoted values, errors; and writing them. */

#include <string.h>

#include "bs/core/ini.h"
#include "bs_test.h"

static void test_parse(void)
{
    static const char text[] = "top = 1\r\n"
                               "# a comment\n"
                               "  ; another\n"
                               "[player]\n"
                               "name = \"Ald #ric \\\"the\\\" \\\\ \"  # quoted\n"
                               "  health =  70   # with a comment\n"
                               "race=dk\n"
                               "empty =\n"
                               "tag = a#b\n"
                               "[item]\n"
                               "base = 2\n"
                               "[ item ]\n"
                               "base = 3\n"
                               "base = 4\n";
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_ini ini;
    uint32_t line;
    BS_CHECK_EQ_INT(bs_ini_parse(text, sizeof text - 1, &a, &ini, &line), BS_OK);
    BS_CHECK_EQ_INT(ini.count, 4);
    BS_CHECK(strcmp(ini.sections[0].name, "") == 0 && strcmp(bs_ini_get(&ini.sections[0], "top"), "1") == 0);
    const bs_ini_section *p = &ini.sections[1];
    BS_CHECK(strcmp(p->name, "player") == 0);
    BS_CHECK_EQ_INT(p->line, 4);
    BS_CHECK(strcmp(bs_ini_get(p, "name"), "Ald #ric \"the\" \\ ") == 0);
    BS_CHECK(strcmp(bs_ini_get(p, "health"), "70") == 0);
    BS_CHECK(strcmp(bs_ini_get(p, "race"), "dk") == 0);
    BS_CHECK(strcmp(bs_ini_get(p, "empty"), "") == 0);
    BS_CHECK(strcmp(bs_ini_get(p, "tag"), "a#b") == 0);
    BS_CHECK(bs_ini_get(p, "nothing") == NULL);
    BS_CHECK_EQ_INT(p->entries[1].line, 6);
    BS_CHECK(strcmp(ini.sections[3].name, "item") == 0);
    BS_CHECK_EQ_INT(ini.sections[3].count, 2);
    BS_CHECK(strcmp(bs_ini_get(&ini.sections[3], "base"), "3") == 0);

    static const char bad1[] = "[ok]\nno equals sign here\n";
    BS_CHECK_EQ_INT(bs_ini_parse(bad1, sizeof bad1 - 1, &a, &ini, &line), BS_ERR_FORMAT);
    BS_CHECK_EQ_INT(line, 2);
    static const char bad2[] = "a = 1\n[unclosed\n";
    BS_CHECK_EQ_INT(bs_ini_parse(bad2, sizeof bad2 - 1, &a, &ini, &line), BS_ERR_FORMAT);
    BS_CHECK_EQ_INT(line, 2);
    static const char bad3[] = "a = \"open\n";
    BS_CHECK_EQ_INT(bs_ini_parse(bad3, sizeof bad3 - 1, &a, &ini, &line), BS_ERR_FORMAT);
    static const char bad4[] = " = 1\n";
    BS_CHECK_EQ_INT(bs_ini_parse(bad4, sizeof bad4 - 1, &a, &ini, &line), BS_ERR_FORMAT);
    BS_CHECK_EQ_INT(bs_ini_parse("", 0, &a, &ini, &line), BS_OK);
    BS_CHECK_EQ_INT(ini.count, 1);
    bs_arena_free(&a);
}

static void test_write(void)
{
    bs_ini_writer w;
    bs_ini_writer_init(&w);
    bs_ini_printf(&w, "# saved\n");
    bs_ini_section_begin(&w, "player");
    bs_ini_put(&w, "health", NULL, "%d", 70);
    bs_ini_put(&w, "base", "Long Sword", "%d", 2);
    bs_ini_put_str(&w, "name", "plain name");
    bs_ini_put_str(&w, "title", "No. #1 \"best\"");
    bs_ini_put_str(&w, "pad", " x ");
    BS_CHECK(!w.failed);
    BS_CHECK(strcmp(w.text, "# saved\n\n[player]\nhealth = 70\nbase = 2  # Long Sword\nname = plain name\n"
                            "title = \"No. #1 \\\"best\\\"\"\npad = \" x \"\n") == 0);
    /* Read back as written. */
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_ini ini;
    BS_CHECK_EQ_INT(bs_ini_parse(w.text, w.len, &a, &ini, NULL), BS_OK);
    BS_CHECK(strcmp(bs_ini_get(&ini.sections[1], "title"), "No. #1 \"best\"") == 0);
    BS_CHECK(strcmp(bs_ini_get(&ini.sections[1], "pad"), " x ") == 0);
    BS_CHECK(strcmp(bs_ini_get(&ini.sections[1], "base"), "2") == 0);
    bs_arena_free(&a);
    bs_ini_writer_free(&w);
}

int main(void)
{
    BS_RUN(test_parse);
    BS_RUN(test_write);
    return BS_TEST_RESULT();
}
