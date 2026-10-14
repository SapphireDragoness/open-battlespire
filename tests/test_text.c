/* Windows-1252 conversion, TEXT.RSC and LEVELS.TXT parsing on synthetic data. */

#include <string.h>

#include "bs/core/text.h"
#include "bs/formats/levels.h"
#include "bs/formats/rsc.h"
#include "bs_test.h"
#include "synth.h"

static void test_cp1252(void)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    static const uint8_t src[] = { 'P', 'a', 'p', 'r', 0xE9, ' ', 0x85, 0x92, 0x93, 0x94, 0x81 };
    char *s = bs_cp1252_to_utf8(&a, src, sizeof src);
    BS_CHECK(s && strcmp(s, "Papr\xC3\xA9 \xE2\x80\xA6\xE2\x80\x99\xE2\x80\x9C\xE2\x80\x9D\xC2\x81") == 0);
    BS_CHECK_EQ_INT(bs_cp1252_to_unicode(0x80), 0x20AC);
    BS_CHECK_EQ_INT(bs_cp1252_to_unicode('A'), 'A');
    bs_arena_free(&a);
}

static void test_rsc(void)
{
    uint8_t data[256];
    size_t n = synth_rsc(data, sizeof data);
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_rsc rsc;
    BS_CHECK_EQ_INT(bs_rsc_parse(data, n, &a, &rsc), BS_OK);
    BS_CHECK_EQ_INT(rsc.count, 3);

    const bs_rsc_record *r5 = bs_rsc_find(&rsc, 5);
    BS_CHECK(r5 && r5->size == 8); /* trailing 0xFE stripped */
    BS_CHECK(r5 && strcmp(bs_rsc_to_utf8(&a, r5), "Hi\nthere") == 0);
    const bs_rsc_record *r9 = bs_rsc_find(&rsc, 9);
    BS_CHECK(r9 && r9->size == 0);
    const bs_rsc_record *r12 = bs_rsc_find(&rsc, 12);
    BS_CHECK(r12 && strcmp(bs_rsc_to_utf8(&a, r12), "a{FA:00}b") == 0);
    BS_CHECK(bs_rsc_find(&rsc, 6) == NULL);
    BS_CHECK(bs_rsc_find(&rsc, 0xFFFF) == NULL);

    /* Missing sentinel: drop the last table entry by shrinking the table size. */
    uint8_t bad[256];
    memcpy(bad, data, n);
    bad[0] = 18;
    BS_CHECK_EQ_INT(bs_rsc_parse(bad, n, &a, &rsc), BS_ERR_FORMAT);
    /* Table size not a multiple of 6. */
    bad[0] = 23;
    BS_CHECK_EQ_INT(bs_rsc_parse(bad, n, &a, &rsc), BS_ERR_FORMAT);
    bs_arena_free(&a);
}

static void test_levels(void)
{
    static const char text[] =
        "l1.bs6\t\tLv1\t\t1336\tNNF\txxxx\t\t555\t1024\tnodes.l1\t1500\t4096\t80\tThe Weir Gate\r\n"
        "\r\n"
        "dmkk.bs6\tCf1\t\t2000\tCNC\tsky1.bsi\t444\t31000\txxxx\t\t2000\t3072\t100\tCastle flag capture (foggy)  \r\n"
        "\x1a";
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_level_table t;
    BS_CHECK_EQ_INT(bs_levels_parse((const uint8_t *)text, sizeof text - 1, &a, &t), BS_OK);
    BS_CHECK_EQ_INT(t.count, 2);
    if (t.count == 2) {
        const bs_level_info *l = &t.levels[0];
        BS_CHECK(strcmp(l->file, "l1.bs6") == 0 && strcmp(l->id, "Lv1") == 0);
        BS_CHECK(l->sky[0] == '\0' && strcmp(l->haze, "555") == 0 && strcmp(l->nodes, "nodes.l1") == 0);
        BS_CHECK(l->view_distance == 1336 && l->ambient == 1024 && l->monster_percent == 80);
        BS_CHECK(strcmp(l->name, "The Weir Gate") == 0);
        BS_CHECK(bs_level_game_type(l) == BS_GAME_CAMPAIGN);
        l = &t.levels[1];
        BS_CHECK(strcmp(l->sky, "sky1.bsi") == 0 && l->nodes[0] == '\0');
        BS_CHECK(strcmp(l->name, "Castle flag capture (foggy)") == 0);
        BS_CHECK(bs_level_game_type(l) == BS_GAME_CTF);
        BS_CHECK_EQ_INT(bs_level_number(l), 4);
        BS_CHECK_EQ_INT(bs_level_music_track(l), 5);
    }

    /* Level numbers: l2/l3 swap, 7 becomes 8. */
    bs_level_info li = {.file = "l1.bs6", .mode = "NNF"};
    BS_CHECK_EQ_INT(bs_level_number(&li), 1);
    BS_CHECK_EQ_INT(bs_level_music_track(&li), 2);
    li.file[1] = '2';
    BS_CHECK_EQ_INT(bs_level_number(&li), 3);
    li.file[1] = '3';
    BS_CHECK_EQ_INT(bs_level_number(&li), 2);
    li.file[1] = '7';
    BS_CHECK_EQ_INT(bs_level_number(&li), 8);
    li.file[1] = '8';
    BS_CHECK_EQ_INT(bs_level_music_track(&li), 9);
    li.file[1] = 'x';
    BS_CHECK_EQ_INT(bs_level_music_track(&li), 0);

    static const char bad[] = "l1.bs6 Lv1 notanumber NNF xxxx 555 1024 nodes.l1 1500 4096 80 Name\n";
    BS_CHECK_EQ_INT(bs_levels_parse((const uint8_t *)bad, sizeof bad - 1, &a, &t), BS_ERR_FORMAT);
    static const char short_line[] = "l1.bs6 Lv1 1336\n";
    BS_CHECK_EQ_INT(bs_levels_parse((const uint8_t *)short_line, sizeof short_line - 1, &a, &t), BS_ERR_FORMAT);
    bs_arena_free(&a);
}

int main(void)
{
    BS_RUN(test_cp1252);
    BS_RUN(test_rsc);
    BS_RUN(test_levels);
    return BS_TEST_RESULT();
}
