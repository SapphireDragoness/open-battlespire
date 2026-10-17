/* The original game's saves: the record tree, the controllers after it, SAVEVARS.DAT, the variables' hash. */

#include <string.h>

#include "bs/formats/save.h"
#include "bs_test.h"

static void put32(uint8_t *p, uint32_t v)
{
    for (int k = 0; k < 4; k++)
        p[k] = (uint8_t)(v >> (8 * k));
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* A record: length, then the object (a 0x3D-byte header and the body). Returns its size. */
static size_t record(uint8_t *at, uint8_t type, uint32_t id, uint32_t parent, size_t body)
{
    memset(at, 0, 4 + BS_SAVE_HEADER + body);
    put32(at, (uint32_t)(BS_SAVE_HEADER + body));
    uint8_t *o = at + 4;
    o[0] = type;
    put32(o + 0x1D, id);
    put32(o + 0x39, parent);
    put16(o + 0x25, 0xFFFF);
    put16(o + 0x27, 0xFFFF);
    return 4 + BS_SAVE_HEADER + body;
}

static void test_tree(void)
{
    static uint8_t f[16384];
    size_t n = 0;
    put32(f, BS_SAVE_VERSION);
    n += 4;
    uint8_t *player = f + n;
    n += record(player, BS_SAVE_PLAYER, BS_SAVE_PLAYER_ID, BS_SAVE_PARENT_ROOT, BS_SAVE_PLAYER_BODY);
    uint8_t *pb = player + 4 + BS_SAVE_HEADER;
    memcpy(pb, "Aldric", 7);
    put32(pb + 0x40, 55);                   /* Strength, base */
    put16(pb + 0x62, 82);                   /* spell points maximum */
    put32(pb + 0x6A, 65);                   /* wounds maximum */
    put32(pb + 0x72 + 12 * 19, 47);         /* Missile now */
    put32(pb + 0x76 + 12 * 19, 3);          /* its uses */
    memcpy(pb + 0x17A, "Ranger", 7);
    pb[0x226] = 8;                          /* female */
    pb[0x23E] = 4;
    pb[0x25D] = 5;                          /* Wood Elf */
    pb[0x1FE] = 21;                         /* F1: Cure Health */
    pb[0x1FF] = 64;                         /* F2: the first hot key item */
    put32(pb + 0x2E7, 10007);
    put32(pb + 0x260, 10007);
    pb[0x29F] = 2;
    uint8_t *item = f + n;
    n += record(item, BS_SAVE_ITEM, 10007, BS_SAVE_PLAYER_ID, BS_SAVE_ITEM_BODY);
    item[4 + 0x14] = 0x20;                  /* worn */
    uint8_t *ib = item + 4 + BS_SAVE_HEADER;
    memcpy(ib, "Long Sword", 11);
    put16(ib + 0x20, 2);
    put32(ib + 0x2F, 300);
    put16(ib + 0x33, 4);
    ib[0x39] = 0xFF;
    put32(ib + 0x2E2, 9242);
    put32(ib + 0x2D2, 7);
    uint8_t *door = f + n;
    n += record(door, BS_SAVE_OBJECT, 123, BS_SAVE_PARENT_LEVEL, 0x6F);
    put16(door + 4 + 0x25, 3);              /* linked */
    n += record(f + n, BS_SAVE_AUTOMAP, 1000, BS_SAVE_PLAYER_ID, BS_SAVE_AUTOMAP_CELLS + 5);
    uint8_t *reserve = f + n;               /* a hidden reserve monster naming the player as its parent */
    n += record(reserve, BS_SAVE_MONSTER, 81470, BS_SAVE_PLAYER_ID, BS_SAVE_PLAYER_BODY);
    reserve[4 + 0x14] = 2;
    put32(f + n, 0); /* the end of the records */
    n += 4;
    /* The controllers: two chains of 2 and 1; the second of the first is running. Then the active list. */
    f[n++] = 0;
    f[n++] = 1;
    put32(f + n, 123);
    f[n + 4] = 0xAB;
    put32(f + n + 40, 5000);
    n += 44;
    f[n++] = 2;
    put32(f + n, 1);
    n += 4;
    f[n++] = 0;

    bs_arena a;
    bs_arena_init(&a, 0);
    bs_save_tree t;
    BS_CHECK_EQ_INT(bs_save_tree_parse(f, n, &a, &t), BS_OK);
    BS_CHECK_EQ_INT(t.version, BS_SAVE_VERSION);
    BS_CHECK_EQ_INT(t.count, 5);
    BS_CHECK(t.records[4].reserve && t.records[4].parent_index == -1);
    BS_CHECK(!t.records[1].reserve);
    BS_CHECK_EQ_INT(t.records[1].parent_index, 0);
    BS_CHECK_EQ_INT(t.records[2].parent_index, -1);
    BS_CHECK_EQ_INT(t.records[2].parent, BS_SAVE_PARENT_LEVEL);
    BS_CHECK_EQ_INT(t.records[2].link, 3);
    BS_CHECK_EQ_INT(t.records[3].body_size, BS_SAVE_AUTOMAP_CELLS + 5);
    BS_CHECK_EQ_INT(bs_save_find(&t, 10007), 1);
    BS_CHECK_EQ_INT(bs_save_find(&t, 77), -1);
    BS_CHECK_EQ_INT(t.controls_size, 1 + 1 + 44 + 1 + 4 + 1);

    bs_save_character c;
    bs_save_character_parse(t.records[0].body, t.records[0].body_size, &c);
    BS_CHECK(strcmp(c.name, "Aldric") == 0);
    BS_CHECK_EQ_INT(c.attributes_base[0], 55);
    BS_CHECK_EQ_INT(c.spell_points_max, 82);
    BS_CHECK_EQ_INT(c.wounds_max, 65);
    BS_CHECK_EQ_INT(c.skills[19], 47);
    BS_CHECK_EQ_INT(c.skill_counts[19], 3);
    BS_CHECK(strcmp(c.cls.class_name, "Ranger") == 0);
    BS_CHECK_EQ_INT(c.flags & 8, 8);
    BS_CHECK_EQ_INT(c.hair, 4);
    BS_CHECK_EQ_INT(c.race, 5);
    BS_CHECK_EQ_INT(c.hotkeys[0], 21);
    BS_CHECK_EQ_INT(c.hotkeys[1], 64);
    BS_CHECK_EQ_INT(c.hotkey_items[0], 10007);
    BS_CHECK_EQ_INT(c.weapon, 10007);
    BS_CHECK_EQ_INT(c.level, 2);

    bs_scl_item it;
    bs_save_item_parse(&t.records[1], &it);
    BS_CHECK(strcmp(it.name, "Long Sword") == 0);
    BS_CHECK_EQ_INT(it.base, 2);
    BS_CHECK_EQ_INT(it.material, 4);
    BS_CHECK_EQ_INT(it.magic, 9242);
    BS_CHECK_EQ_INT(it.charges, 7);
    BS_CHECK_EQ_INT(it.number, -1);
    BS_CHECK_EQ_INT(it.flags, 0x20);

    const uint32_t chains[2] = {2, 1};
    bs_save_controller *ctl;
    uint32_t nc, na;
    uint8_t *active;
    BS_CHECK_EQ_INT(bs_save_controllers_read(t.controls, t.controls_size, chains, 2, &a, &ctl, &nc, &active, &na),
                    BS_OK);
    BS_CHECK_EQ_INT(nc, 3);
    BS_CHECK_EQ_INT(ctl[0].flags, 0);
    BS_CHECK_EQ_INT(ctl[1].flags, 1);
    BS_CHECK_EQ_INT(ctl[1].object, 123);
    BS_CHECK_EQ_INT(ctl[1].values[0], 0xAB);
    BS_CHECK_EQ_INT(ctl[1].start, 5000);
    BS_CHECK_EQ_INT(ctl[2].flags, 2);
    BS_CHECK_EQ_INT(na, 1);
    BS_CHECK_EQ_INT(active[0], 0);
    /* Too few bytes for the layout given. */
    const uint32_t more[2] = {2, 3};
    BS_CHECK_EQ_INT(bs_save_controllers_read(t.controls, t.controls_size, more, 2, &a, &ctl, &nc, &active, &na),
                    BS_ERR_FORMAT);

    /* Bad data: an old version, a record running past the end. */
    put32(f, 0x80);
    BS_CHECK_EQ_INT(bs_save_tree_parse(f, n, &a, &t), BS_ERR_FORMAT);
    put32(f, BS_SAVE_VERSION);
    BS_CHECK_EQ_INT(bs_save_tree_parse(f, 100, &a, &t), BS_ERR_FORMAT);
    bs_arena_free(&a);
}

static void test_vars(void)
{
    static uint8_t v[BS_SAVE_VARS_SIZE];
    memset(v, 0, sizeof v);
    memcpy(v, "Aldric", 7);
    put32(v + 1051, 3);                     /* level number */
    put32(v + 1055, 123456);                /* clock */
    memcpy(v + 1075, "ou drink the potion.", 21);
    put32(v + 2423, 100);                   /* music volume */
    put32(v + 4492, 10050);                 /* a conversation */
    memcpy(v + 4496, "MET", 3);
    put32(v + 4492 + 1024, 1);
    put32(v + 5520, 10060);                 /* a monster override */
    put32(v + 5520 + 4, 3);
    put32(v + 5520 + 28, 13);
    put32(v + 5520 + 7168, 1);
    put32(v + 12692, 10070);                /* a gem */
    put32(v + 12696, 25);
    v[12700] = 1;
    put32(v + 12692 + 1152, 1);
    put32(v + 14364, 10080);                /* a sigil */
    put32(v + 14368, 512);
    put32(v + 14364 + 768, 1);
    put32(v + 15233, 2);                    /* globals */
    put32(v + 15237, bs_save_variable_hash("PCFemale"));
    put32(v + 15241, 1);
    put32(v + 17285 + 68 * 255, 10060);     /* the last local record */
    put32(v + 17285 + 68 * 255 + 4, bs_save_variable_hash("MethatsNice"));
    put32(v + 17285 + 68 * 255 + 8, 1);
    v[34693 + 2] = 1;                       /* a Dremora has been spoken to */
    put32(v + 3464, 10090);                 /* a dialogue link */
    put32(v + 3468, 13);
    put32(v + 3464 + 1024, 1);
    put32(v + 15136, 10095);                /* a speaker leaving */
    put32(v + 15140, 61000);

    static bs_save_vars s;
    BS_CHECK_EQ_INT(bs_save_vars_parse(v, sizeof v, &s), BS_OK);
    BS_CHECK(strcmp(s.player.name, "Aldric") == 0);
    BS_CHECK_EQ_INT(s.level_number, 3);
    BS_CHECK_EQ_INT(s.clock, 123456);
    BS_CHECK(strcmp(s.log, "ou drink the potion.") == 0);
    BS_CHECK_EQ_INT(s.music_volume, 100);
    BS_CHECK_EQ_INT(s.conversation_count, 1);
    BS_CHECK(strcmp(s.conversations[0].file, "MET") == 0);
    BS_CHECK_EQ_INT(s.monster_count, 1);
    BS_CHECK_EQ_INT(s.monsters[0].creature, 3);
    BS_CHECK_EQ_INT(s.monsters[0].goal, 13);
    BS_CHECK_EQ_INT(s.gem_count, 1);
    BS_CHECK_EQ_INT(s.gems[0].amount, 25);
    BS_CHECK(s.gems[0].spell_points);
    BS_CHECK_EQ_INT(s.sigil_count, 1);
    BS_CHECK_EQ_INT(s.sigils[0].y, 512);
    BS_CHECK_EQ_INT(s.global_count, 2);
    BS_CHECK_EQ_INT(s.globals[0].hash, 1202329098u);
    BS_CHECK_EQ_INT(s.globals[0].value, 1);
    BS_CHECK_EQ_INT(s.locals[255].id, 10060);
    BS_CHECK_EQ_INT(s.locals[255].vars[0].value, 1);
    BS_CHECK_EQ_INT(s.visits[2], 1);
    BS_CHECK_EQ_INT(s.link_count, 1);
    BS_CHECK(s.links[0].id == 10090 && s.links[0].link == 13);
    BS_CHECK(s.leaving[0].id == 10095 && s.leaving[0].at == 61000);
    BS_CHECK_EQ_INT(bs_save_vars_parse(v, sizeof v - 1, &s), BS_ERR_FORMAT);
}

static void test_hash(void)
{
    /* Values from the UESP's save notes. */
    BS_CHECK_EQ_INT(bs_save_variable_hash("Cat"), 18276u);
    BS_CHECK_EQ_INT(bs_save_variable_hash("cat"), 18276u);
    BS_CHECK_EQ_INT(bs_save_variable_hash("  &Cat"), 18276u);
    BS_CHECK_EQ_INT(bs_save_variable_hash("Cat dog"), 18276u);
    BS_CHECK_EQ_INT(bs_save_variable_hash("PCFemale"), 1202329098u);
    BS_CHECK_EQ_INT(bs_save_variable_hash("SkelServe"), 4195854332u);
    BS_CHECK_EQ_INT(bs_save_variable_hash("ChimereJournal"), 2747749378u);
    BS_CHECK_EQ_INT(bs_save_variable_hash("InvVoidguide1"), 3642233908u);
}

static void test_image(void)
{
    static uint8_t raw[BS_SAVE_IMAGE_SIZE], rgb[BS_SAVE_IMAGE_WIDTH * BS_SAVE_IMAGE_HEIGHT * 3];
    memset(raw, 0, sizeof raw);
    put16(raw, 0x7C00);                     /* red */
    put16(raw + 2, 0x001F);                 /* blue */
    bs_save_image_rgb(raw, rgb);
    BS_CHECK(rgb[0] == 255 && rgb[1] == 0 && rgb[2] == 0);
    BS_CHECK(rgb[3] == 0 && rgb[4] == 0 && rgb[5] == 255);
}

int main(void)
{
    BS_RUN(test_tree);
    BS_RUN(test_vars);
    BS_RUN(test_hash);
    BS_RUN(test_image);
    return BS_TEST_RESULT();
}
