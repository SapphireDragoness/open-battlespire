/* Class files: a player record with a spell and an item after it. */

#include <string.h>

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

int main(void)
{
    BS_RUN(test_parse);
    return BS_TEST_RESULT();
}
