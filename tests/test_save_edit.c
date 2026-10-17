/* Editing an original save: fields by name, items given and taken, variables; untouched bytes stay. */

#include <stdlib.h>
#include <string.h>

#include "bs/game/save_edit.h"
#include "bs_test.h"

static void put32(uint8_t *p, uint32_t v)
{
    for (int k = 0; k < 4; k++)
        p[k] = (uint8_t)(v >> (8 * k));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint8_t *record(uint8_t *f, size_t *n, uint8_t type, uint32_t id, uint32_t parent, size_t body)
{
    uint8_t *at = f + *n;
    memset(at, 0, 4 + BS_SAVE_HEADER + body);
    put32(at, (uint32_t)(BS_SAVE_HEADER + body));
    uint8_t *o = at + 4;
    o[0] = type;
    put32(o + 0x1D, id);
    put32(o + 0x39, parent);
    *n += 4 + BS_SAVE_HEADER + body;
    return o;
}

static uint8_t *item(uint8_t *f, size_t *n, uint32_t id, uint32_t parent, uint16_t base)
{
    uint8_t *o = record(f, n, BS_SAVE_ITEM, id, parent, BS_SAVE_ITEM_BODY);
    o[BS_SAVE_HEADER + 0x20] = (uint8_t)base;
    o[BS_SAVE_HEADER + 0x39] = 0xFF;
    return o;
}

static void test_edit(void)
{
    static uint8_t f[65536];
    size_t n = 4;
    put32(f, BS_SAVE_VERSION);
    uint8_t *player = record(f, &n, BS_SAVE_PLAYER, BS_SAVE_PLAYER_ID, BS_SAVE_PARENT_ROOT, BS_SAVE_PLAYER_BODY);
    uint8_t *pb = player + BS_SAVE_HEADER;
    memcpy(pb, "Aldric", 7);
    put32(pb + 0x20, 70);                   /* Strength now, with an effect */
    put32(pb + 0x40, 55);                   /* ... and its base */
    pb[0x29F] = 2;
    put32(pb + 0x260, 1000);                /* the sword in hand */
    put32(pb + 0x2E7 + 4, 1001);            /* the sack on hot key item 1 ... */
    pb[0x1FE + 3] = 64 + 1;                 /* ... which is F4 */
    uint8_t *sword = item(f, &n, 1000, BS_SAVE_PLAYER_ID, 2);
    sword[0x14] = 0x20;
    memcpy(sword + BS_SAVE_HEADER + 0x22, "MWPNS602", 9);
    put32(sword + BS_SAVE_HEADER + 0x2E2, 9259);
    memset(sword + BS_SAVE_HEADER + BS_SAVE_MAGIC_AT, 0x5A, 16);
    item(f, &n, 1001, BS_SAVE_PLAYER_ID, 16); /* a sack ... */
    item(f, &n, 1002, 1001, 22);              /* ... with arrows */
    item(f, &n, 1003, BS_SAVE_PLAYER_ID, 1);  /* a short sword */
    record(f, &n, BS_SAVE_MONSTER, 577, BS_SAVE_PARENT_LEVEL, BS_SAVE_PLAYER_BODY);
    item(f, &n, 1004, 577, 16);               /* its sack */
    put32(f + n, 0);
    n += 4;
    f[n++] = 2;                               /* a controller's state */
    put32(f + n, 0);
    n += 4;
    static uint8_t vars[BS_SAVE_VARS_SIZE];
    memset(vars, 0x11, sizeof vars);
    put32(vars + 15233, 1);
    put32(vars + 15237, bs_save_variable_hash("PCMale"));
    put32(vars + 15241, 1);
    put32(vars + 15136, 577);                 /* the monster is leaving */
    put32(vars + 0x260, 0xDEADBEEF);          /* SAVEVARS' weapon: the pointer the game had */

    static bs_save_edit e;
    BS_CHECK_EQ_INT(bs_save_edit_open(&e, f, n, vars, sizeof vars), BS_OK);
    BS_CHECK_EQ_INT(e.count, 7);
    uint8_t *out;
    size_t size;
    /* Untouched, the tree comes back the same; the variables only take the player's copy. */
    BS_CHECK_EQ_INT(bs_save_edit_write(&e, &out, &size), BS_OK);
    BS_CHECK(size == n && memcmp(out, f, n) == 0);
    free(out);
    BS_CHECK(memcmp(e.vars, pb, 0x260) == 0);
    BS_CHECK_EQ_INT(get32(e.vars + 0x260), 0xDEADBEEF);

    char v[64], err[BS_SAVE_EDIT_ERROR];
    BS_CHECK(bs_save_edit_get(&e, "strength", v, sizeof v) && strcmp(v, "55 (now 70)") == 0);
    BS_CHECK(bs_save_edit_get(&e, "level", v, sizeof v) && strcmp(v, "2") == 0);
    BS_CHECK(bs_save_edit_set(&e, "strength", "90", err));
    BS_CHECK(bs_save_edit_set(&e, "long_blade", "61", err));
    BS_CHECK(bs_save_edit_set(&e, "name", "Hero", err));
    BS_CHECK(bs_save_edit_set(&e, "race", "wd", err));
    BS_CHECK(bs_save_edit_set(&e, "x", "-11.5", err));
    BS_CHECK(bs_save_edit_set(&e, "level", "4", err));
    BS_CHECK(bs_save_edit_set(&e, "female", "yes", err));
    BS_CHECK(!bs_save_edit_set(&e, "strength", "lots", err));
    BS_CHECK(!bs_save_edit_set(&e, "wings", "2", err) && strstr(err, "wings"));
    BS_CHECK(!bs_save_edit_set(&e, "name", "a name far too long for the thirty-two bytes", err));
    int32_t pi = bs_save_edit_find(&e, BS_SAVE_PLAYER_ID);
    uint8_t *p = e.records[pi].object + BS_SAVE_HEADER;
    BS_CHECK_EQ_INT(get32(p + 0x20), 90);
    BS_CHECK_EQ_INT(get32(p + 0x40), 90);
    BS_CHECK_EQ_INT(get32(p + 0x72 + 12 * 15), 61);
    BS_CHECK_EQ_INT(get32(p + 0x7A + 12 * 15), 61);
    BS_CHECK(strcmp((const char *)p, "Hero") == 0 && p[5] == 0);
    BS_CHECK_EQ_INT(p[0x25D], 5);
    BS_CHECK_EQ_INT(p[0x29F], 4);
    BS_CHECK_EQ_INT(p[0x226] & 8, 8);
    BS_CHECK_EQ_INT(e.records[1].object[BS_SAVE_HEADER + 0x22], 'F'); /* the sword's picture is a woman's */
    BS_CHECK(bs_save_edit_get(&e, "x", v, sizeof v) && strcmp(v, "-11.5") == 0);

    /* A new item: the base record, then what the maker sets; an enchantment copied from an item with it. */
    static uint8_t base[0x2F2];
    memset(base, 0, sizeof base);
    memcpy(base, "Long Sword", 11);
    base[0x20] = 2;
    const uint8_t *magic = bs_save_edit_find_magic(&e, 9259);
    BS_CHECK(magic != NULL && magic[0] == 0x5A);
    BS_CHECK(bs_save_edit_find_magic(&e, 1) == NULL);
    bs_item it = {.base = 2, .material = 9, .magic = 9259, .quantity = 1, .condition = 400, .condition_max = 400,
                  .picture = "FWPNS602"};
    uint8_t body[BS_SAVE_ITEM_BODY];
    bs_save_item_body(base, &it, magic, body);
    BS_CHECK(memcmp(body, "Long Sword", 11) == 0);
    BS_CHECK_EQ_INT(body[0x33], 9);
    BS_CHECK_EQ_INT(get32(body + 0x2B), 400);
    BS_CHECK_EQ_INT(body[0x39], 0xFF);
    BS_CHECK_EQ_INT(body[0x2C3], 0xFF);
    BS_CHECK_EQ_INT(get32(body + 0x2E2), 9259);
    BS_CHECK_EQ_INT(body[BS_SAVE_MAGIC_AT], 0x5A);
    BS_CHECK(memcmp(body + 0x22, "FWPNS602", 9) == 0);
    uint32_t id = bs_save_edit_give(&e, body, BS_SAVE_PLAYER_ID);
    BS_CHECK_EQ_INT(id, BS_SAVE_PLAYER_ID + 1); /* past every id */
    BS_CHECK_EQ_INT(bs_save_edit_find(&e, id), 1); /* right after its parent */
    BS_CHECK_EQ_INT(bs_save_edit_give(&e, body, 4242), 0);
    BS_CHECK_EQ_INT(bs_save_edit_give(&e, body, 1001), BS_SAVE_PLAYER_ID + 2); /* into the sack */

    /* Wielding it puts the old one away; the secondary weapon. */
    BS_CHECK(bs_save_edit_item_set(&e, id, "worn", "yes", err));
    BS_CHECK_EQ_INT(get32(p + 0x260), id);
    BS_CHECK_EQ_INT(e.records[bs_save_edit_find(&e, 1000)].object[0x14] & 0x20, 0);
    BS_CHECK(bs_save_edit_item_set(&e, 1003, "secondary", "yes", err));
    BS_CHECK_EQ_INT(get32(p + 0x264), 1003);
    BS_CHECK(!bs_save_edit_item_set(&e, BS_SAVE_PLAYER_ID + 2, "worn", "yes", err)); /* in the sack */
    BS_CHECK(bs_save_edit_item_set(&e, 1002, "quantity", "40", err));
    BS_CHECK(!bs_save_edit_item_set(&e, 1002, "number", "4", err));  /* arrows have none */
    BS_CHECK(!bs_save_edit_item_set(&e, 1002, "colour", "red", err));

    /* Taking the sack takes what's in it, and the hot key that named it. */
    BS_CHECK(bs_save_edit_remove(&e, 1001, err));
    BS_CHECK(bs_save_edit_find(&e, 1001) < 0 && bs_save_edit_find(&e, 1002) < 0 &&
             bs_save_edit_find(&e, BS_SAVE_PLAYER_ID + 2) < 0);
    BS_CHECK_EQ_INT(get32(p + 0x2E7 + 4), 0);
    BS_CHECK_EQ_INT(p[0x1FE + 3], 0);
    BS_CHECK(bs_save_edit_remove(&e, id, err));
    BS_CHECK_EQ_INT(get32(p + 0x260), 0);
    BS_CHECK(bs_save_edit_remove(&e, 577, err));  /* the monster, its sack, and its leaving */
    BS_CHECK(bs_save_edit_find(&e, 1004) < 0);
    BS_CHECK_EQ_INT(get32(e.vars + 15136), 0);
    BS_CHECK(!bs_save_edit_remove(&e, BS_SAVE_PLAYER_ID, err));

    /* Variables: changed, or added. */
    BS_CHECK(bs_save_edit_set_var(&e, "PCMale", 0, err));
    BS_CHECK(bs_save_edit_set_var(&e, "ScampOath", 1, err));
    BS_CHECK_EQ_INT(get32(e.vars + 15233), 2);
    BS_CHECK_EQ_INT(get32(e.vars + 15241), 0);
    BS_CHECK_EQ_INT(get32(e.vars + 15245), bs_save_variable_hash("ScampOath"));
    BS_CHECK(bs_save_edit_set(&e, "level_number", "2", err));
    BS_CHECK_EQ_INT(get32(e.vars + 1051), 2);

    /* What's written reads back. */
    BS_CHECK_EQ_INT(bs_save_edit_write(&e, &out, &size), BS_OK);
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_save_tree t;
    BS_CHECK_EQ_INT(bs_save_tree_parse(out, size, &a, &t), BS_OK);
    BS_CHECK_EQ_INT(t.count, 3);
    BS_CHECK_EQ_INT(t.controls_size, 5);
    BS_CHECK(memcmp(e.vars, p, 0x260) == 0);
    bs_arena_free(&a);
    free(out);
    bs_save_edit_free(&e);
}

int main(void)
{
    BS_RUN(test_edit);
    return BS_TEST_RESULT();
}
