/* Class files: a player record with a spell and an item after it. */

#include <stdlib.h>
#include <string.h>

#include "bs/formats/save.h"
#include "bs/formats/scl.h"
#include "bs_test.h"

static void put32(uint8_t *p, int32_t v)
{
    for (int k = 0; k < 4; k++)
        p[k] = (uint8_t)((uint32_t)v >> (8 * k));
}

/* A record: length, type, the header's rest, then the body. Returns its size. */
static size_t record(uint8_t *at, uint8_t type, size_t body, uint8_t flags)
{
    memset(at, 0, 65 + body);
    put32(at, (int32_t)(61 + body));
    at[4] = type;
    at[24] = flags;
    return 65 + body;
}

static void test_parse(void)
{
    static uint8_t f[4096];
    size_t n = record(f, 3, 0x317, 0);
    uint8_t *b = f + 65;
    memcpy(b + 0x17A, "Acrobat", 8);
    b[0x29F] = 1;
    put32(b + 0x20A, 19);
    put32(b + 0x40, 60);
    put32(b + 0x6E, 75);
    put32(b + 0x7A + 12 * 19, 15);
    b[0x1A6] = 1;
    memset(b + 0x1A7, 0xFF, 11);
    b[0x192] = 2;
    put32(b + 0x19A, 0x180);
    b[0x1A2] = 8;
    b[0x1A3] = 1;
    b[0x1A5] = 9;
    uint8_t *s = f + n;
    n += record(s, 9, 0x77, 2);
    memcpy(s + 65, "Cure Health", 12);
    s[65 + 0x20] = 21;
    put32(s + 65 + 0x5F, 40);
    n += record(f + n, 51, 100, 0); /* the automap: skipped */
    uint8_t *it = f + n;
    n += record(it, 2, 0x2F2, 0x10);
    memcpy(it + 65, "Arrow", 6);
    it[65 + 0x20] = 22;
    it[65 + 0x33] = 3;
    put32(it + 65 + 0x3E, 25);
    it[65 + 0x39] = 0xFF;
    put32(it + 65 + 0x2E2, 1012);

    static bs_class c;
    BS_CHECK_EQ_INT(bs_class_parse(f, n, &c), BS_OK);
    BS_CHECK(strcmp(c.class_name, "Acrobat") == 0);
    BS_CHECK_EQ_INT(c.level, 1);
    BS_CHECK_EQ_INT(c.points_left, 19);
    BS_CHECK_EQ_INT(c.attributes[0], 60);
    BS_CHECK_EQ_INT(c.wounds, 75);
    BS_CHECK_EQ_INT(c.skills[19], 15);
    BS_CHECK_EQ_INT(c.class_skills[0], 1);
    BS_CHECK_EQ_INT(c.class_skills[11], BS_SCL_NO_SKILL);
    BS_CHECK_EQ_INT(c.resists, 2);
    BS_CHECK_EQ_INT(c.magery, 0x180);
    BS_CHECK(c.forbidden_weapons == 8 && c.max_armour == 1 && c.max_material == 9);
    BS_CHECK_EQ_INT(c.spell_count, 1);
    BS_CHECK(c.spells[0].effect == 21 && c.spells[0].cost == 40);
    BS_CHECK_EQ_INT(c.item_count, 1);
    BS_CHECK(c.items[0].base == 22 && c.items[0].material == 3 && c.items[0].quantity == 25);
    BS_CHECK(c.items[0].magic == 1012 && c.items[0].number == -1 && c.items[0].flags == 0x10);
    /* Truncated, or not a player first: refused. */
    BS_CHECK(bs_class_parse(f, n - 1, &c) != BS_OK);
    BS_CHECK(bs_class_parse(s, n - (size_t)(s - f), &c) != BS_OK);
}

static int32_t get32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

/* Written as the CharacterMaker's Save does (0x3B39C), then read back. */
static void test_write(void)
{
    static bs_class c;
    memset(&c, 0, sizeof c);
    strcpy(c.class_name, "Pugilist");
    c.level = 1;
    c.points_left = 120;
    for (int k = 0; k < 8; k++)
        c.attributes[k] = 40 + k;
    c.wounds = 90;
    for (int k = 0; k < BS_SCL_SKILLS; k++)
        c.skills[k] = 5 + k;
    for (int k = 0; k < BS_SCL_CLASS_SKILLS; k++)
        c.class_skills[k] = (uint8_t)(3 + k);
    c.resists = 4;
    c.immune = 16;
    c.athleticism = c.rapid_healing = true;
    c.magery = 0x200;
    c.low_tolerance = 2;
    c.critical_weakness = 8;
    c.no_sp_regen = true;
    c.forbidden_weapons = 16;
    c.max_armour = 2;
    c.max_material = 6;

    /* A spell and two items, as bodies. */
    bs_spell sp;
    memset(&sp, 0, sizeof sp);
    strcpy(sp.name, "Fireball");
    sp.effect = 8;
    sp.element = 1;
    sp.delivery = 2;
    sp.cost = 25;
    sp.detonation = 1;
    static uint8_t spells[1][BS_SPELL_RECORD];
    memset(spells[0], 0xAB, BS_SPELL_RECORD); /* the table's other bytes stay */
    bs_spell_write(&sp, spells[0]);
    BS_CHECK_EQ_INT(spells[0][0x50], 0xAB);
    static uint8_t items[2][BS_SAVE_ITEM_BODY];
    memset(items, 0, sizeof items);
    memcpy(items[0], "Long Sword", 11);
    items[0][0x20] = 2;
    put32(items[0] + 0x2B, 300);
    put32(items[0] + 0x2F, 300);
    items[0][0x33] = 5;
    items[0][0x39] = 0xFF;
    memcpy(items[1], "Potion", 7);
    items[1][0x20] = 15;
    items[1][0x39] = 4;
    put32(items[1] + 0x2E2, 9259);
    const uint8_t flags[2] = {0x10, 0};

    bs_class_out o = {.cls = &c, .female = true, .race = 4, .hair = 13, .eyes = 5, .mouth = 6, .clock = 777,
                      .spells = (const uint8_t(*)[BS_SPELL_RECORD])spells, .spell_count = 1,
                      .items = (const uint8_t(*)[BS_SAVE_ITEM_BODY])items, .item_flags = flags, .item_count = 2};
    strcpy(o.name, "Rhiannon");
    uint8_t *f;
    size_t n;
    BS_CHECK_EQ_INT(bs_class_write(&o, &f, &n), BS_OK);
    BS_CHECK_EQ_INT((int)n, 5 * (4 + BS_SAVE_HEADER) + 0x317 + 0x77 + 0x4B5 + 2 * 0x2F3);

    static bs_class back;
    BS_CHECK_EQ_INT(bs_class_parse(f, n, &back), BS_OK);
    BS_CHECK(strcmp(back.class_name, "Pugilist") == 0 && back.level == 1 && back.points_left == 120);
    BS_CHECK(memcmp(back.attributes, c.attributes, sizeof c.attributes) == 0 && back.wounds == 90);
    BS_CHECK(memcmp(back.skills, c.skills, sizeof c.skills) == 0);
    BS_CHECK(memcmp(back.class_skills, c.class_skills, sizeof c.class_skills) == 0);
    BS_CHECK(back.resists == 4 && back.immune == 16 && back.athleticism && back.rapid_healing && !back.acute_hearing);
    BS_CHECK(back.magery == 0x200 && back.low_tolerance == 2 && back.critical_weakness == 8 && back.no_sp_regen);
    BS_CHECK(back.forbidden_weapons == 16 && back.max_armour == 2 && back.max_material == 6);
    BS_CHECK_EQ_INT(back.spell_count, 1);
    BS_CHECK(strcmp(back.spells[0].name, "Fireball") == 0 && back.spells[0].effect == 8 &&
             back.spells[0].element == 1 && back.spells[0].delivery == 2 && back.spells[0].cost == 25 &&
             back.spells[0].detonation == 1);
    BS_CHECK_EQ_INT(back.item_count, 2);
    BS_CHECK(back.items[0].base == 2 && back.items[0].material == 5 && back.items[0].condition_max == 300 &&
             back.items[0].flags == 0x10 && back.items[0].number == -1);
    BS_CHECK(back.items[1].base == 15 && back.items[1].number == 4 && back.items[1].magic == 9259 &&
             back.items[1].flags == 0);

    /* The records: the player (50000, the root's), the spell, the automap, the items, each the player's. */
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_save_record *r;
    uint32_t count;
    BS_CHECK_EQ_INT(bs_save_records_read(f, n, &a, &r, &count, NULL), BS_OK);
    BS_CHECK_EQ_INT(count, 5);
    if (count == 5) {
        BS_CHECK(r[0].type == BS_SAVE_PLAYER && r[0].id == BS_SAVE_PLAYER_ID && r[0].parent == 0);
        BS_CHECK_EQ_INT(get32(r[0].object + 0x29), 777);
        const uint8_t *b = r[0].body;
        BS_CHECK(strcmp((const char *)b, "Rhiannon") == 0);
        BS_CHECK(get32(b + 0x20) == 50 && get32(b + 0x72) == 5); /* with effects: a new class's start */
        BS_CHECK(memcmp(b + 0x1B2, b + 0x17A, 0x38) == 0);
        BS_CHECK(get32(b + 0x226) == 8 && b[0x23E] == 13 && b[0x23F] == 5 && b[0x240] == 6 && b[0x25D] == 4);
        BS_CHECK(r[1].type == BS_SAVE_SPELL && r[1].flags >> 8 == 2);
        BS_CHECK(r[2].type == BS_SAVE_AUTOMAP && r[2].angles[0] == 40 && r[2].angles[1] == 30 &&
                 r[2].body_size == 0x4B5);
        BS_CHECK(r[3].type == BS_SAVE_ITEM && r[4].type == BS_SAVE_ITEM);
        for (uint32_t i = 1; i < count; i++) {
            BS_CHECK(r[i].parent == BS_SAVE_PLAYER_ID && r[i].parent_index == 0);
            BS_CHECK(r[i].id == (1u << 16 | (9999u + i)) && r[i].link == -1 && r[i].link2 == -1);
        }
    }
    bs_arena_free(&a);
    free(f);

    o.cls = NULL;
    BS_CHECK_EQ_INT(bs_class_write(&o, &f, &n), BS_ERR_ARGS);
}

int main(void)
{
    BS_RUN(test_parse);
    BS_RUN(test_write);
    return BS_TEST_RESULT();
}
