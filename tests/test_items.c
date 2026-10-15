#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/formats/bs6.h"
#include "bs/formats/items.h"
#include "bs/game/items.h"
#include "bs_test.h"
#include "synth.h"

static uint8_t exe[64 * 1024];
static bs_item_tables tables;
static bs_magic_table magic;
static bs_arena arena;
static bs_item_db db;

static const char mg_text[] =
    "Name\t\x93" "Ayem Sigil of Entry\x94\r\n"
    "ID#\t9001\r\n"
    "Item\t3\r\n"
    "Spell\t-1, -1, -1, -1, -1\r\n"
    "App\t100\r\n"
    "Adv\t-1\r\n"
    "Uses\t0\r\n"
    "Level\t8\r\n"
    "\r\n\r\n"
    "Name\t\"of the Test\"\r\n"
    "ID#\t7001\r\n"
    "Item\t2\r\n"
    "Spell\t-1, 28, -1, 8, 4\r\n"
    "App\t15, 30, 100\r\n"
    "Adv\t36, 37\r\n"
    "Uses\t10\r\n"
    "Level\t0\r\n";

static void setup(void)
{
    size_t n = synth_item_exe(exe, sizeof exe);
    BS_CHECK(n > 0);
    BS_CHECK_EQ_INT(bs_item_tables_read(exe, n, &tables), BS_OK);
    bs_arena_init(&arena, 0);
    BS_CHECK_EQ_INT(bs_magic_parse((const uint8_t *)mg_text, sizeof mg_text - 1, &arena, &magic), BS_OK);
    db = (bs_item_db){.tables = &tables, .magic = {&magic, NULL}};
}

static void test_tables(void)
{
    BS_CHECK(strcmp(tables.bases[0].name, "Dagger") == 0);
    BS_CHECK(strcmp(tables.bases[3].name, "Broad Sword") == 0);
    BS_CHECK(strcmp(tables.bases[42].name, "Item 42") == 0);
    BS_CHECK(strcmp(tables.bases[5].icon, "mwpns605") == 0);
    BS_CHECK(strcmp(tables.bases[19].icon, "x") == 0 && tables.bases[15].icon[0] == '\0');
    BS_CHECK_EQ_INT(tables.bases[5].condition, 105);
    BS_CHECK_EQ_INT(tables.bases[5].weight, 6);
    BS_CHECK_EQ_INT(tables.bases[5].image, 7);
    BS_CHECK(!tables.bases[15].container);
    BS_CHECK(tables.bases[16].container && tables.bases[18].container && tables.bases[42].container);
    BS_CHECK_EQ_INT(tables.weapons[3].damage_min, 4);
    BS_CHECK_EQ_INT(tables.weapons[3].damage_max, 9);
    BS_CHECK_EQ_INT(tables.weapons[4].hands, 2);
    BS_CHECK_EQ_INT(tables.weapons[15].skill, 16);
    BS_CHECK(strcmp(tables.materials[0], "Iron") == 0);
    BS_CHECK(strcmp(tables.materials[9], "Daedric") == 0);

    /* Not an executable with the tables. */
    uint8_t junk[4096];
    memset(junk, 'x', sizeof junk);
    bs_item_tables t;
    BS_CHECK_EQ_INT(bs_item_tables_read(junk, sizeof junk, &t), BS_ERR_NOT_FOUND);
    /* Truncated right after the bases: no materials. */
    BS_CHECK_EQ_INT(bs_item_tables_read(exe, 100 + 16 * 24 + 43 * 0x2F2, &t), BS_ERR_NOT_FOUND);
}

static void test_magic(void)
{
    BS_CHECK_EQ_INT(magic.count, 2);
    const bs_magic *m = bs_magic_find(&magic, 9001);
    BS_CHECK(m && strcmp(m->name, "Ayem Sigil of Entry") == 0);
    BS_CHECK(m && m->item == 3 && m->level == 8 && m->app_count == 1 && m->app[0] == 100);
    m = bs_magic_find(&magic, 7001);
    BS_CHECK(m && strcmp(m->name, "of the Test") == 0);
    BS_CHECK(m && m->spell[1] == 28 && m->spell[4] == 4 && m->uses == 10);
    BS_CHECK(m && m->app_count == 3 && m->app[2] == 100 && m->adv_count == 2 && m->adv[1] == 37);
    BS_CHECK(bs_magic_find(&magic, 1234) == NULL);

    bs_magic_table t;
    static const char missing[] = "Name\t\"x\"\nID#\t1\n\n";
    BS_CHECK_EQ_INT(bs_magic_parse((const uint8_t *)missing, sizeof missing - 1, &arena, &t), BS_ERR_FORMAT);
    static const char unknown[] = "Name\t\"x\"\nColour\t1\n";
    BS_CHECK_EQ_INT(bs_magic_parse((const uint8_t *)unknown, sizeof unknown - 1, &arena, &t), BS_ERR_FORMAT);
    static const char bad_list[] = "Name\t\"x\"\nID#\t1\nItem\t0\nSpell\t1, 2\nApp\t1\nAdv\t1\nUses\t0\nLevel\t0\n";
    BS_CHECK_EQ_INT(bs_magic_parse((const uint8_t *)bad_list, sizeof bad_list - 1, &arena, &t), BS_ERR_FORMAT);
    BS_CHECK_EQ_INT(bs_magic_parse((const uint8_t *)"", 0, &arena, &t), BS_OK);
    BS_CHECK_EQ_INT(t.count, 0);
}

static void test_material(void)
{
    BS_CHECK_EQ_INT(bs_item_material_for_roll(-50), 0);
    BS_CHECK_EQ_INT(bs_item_material_for_roll(40), 0);
    BS_CHECK_EQ_INT(bs_item_material_for_roll(41), 1);
    BS_CHECK_EQ_INT(bs_item_material_for_roll(65), 1);
    BS_CHECK_EQ_INT(bs_item_material_for_roll(66), 2);
    BS_CHECK_EQ_INT(bs_item_material_for_roll(99), 8);
    BS_CHECK_EQ_INT(bs_item_material_for_roll(100), 9);
    BS_CHECK_EQ_INT(bs_item_material_for_roll(5000), 9);
    /* Level 1 rolls -23..76: never past Elven. */
    bs_rng r = {1};
    for (int i = 0; i < 1000; i++)
        BS_CHECK(bs_item_roll_material(1, false, &r) <= 3);
}

static void test_make(void)
{
    bs_rng r = {7};
    bs_item it;
    /* A placed arrow bundle: 4-8 arrows per two levels. */
    BS_CHECK(bs_item_make(&db, BS_ITEM_SPEC_PLACED, (const int32_t[]){22, 0, 1, 10000}, 4, false, &r, &it));
    BS_CHECK_EQ_INT(it.base, BS_ITEM_ARROW);
    BS_CHECK(it.quantity >= 12 && it.quantity <= 24 && it.quantity % 3 == 0);
    BS_CHECK_EQ_INT(it.magic, 0);
    BS_CHECK_EQ_INT(it.condition, 122);
    /* Contents: a plain item of a base, a magic one, a sigil, a potion, a lantern. */
    BS_CHECK(bs_item_make(&db, BS_ITEM_SPEC_CONTENT, (const int32_t[]){10000, 19, 0, 0}, 1, false, &r, &it));
    BS_CHECK(it.base == BS_ITEM_CLOTHES && it.magic == 0);
    BS_CHECK(bs_item_make(&db, BS_ITEM_SPEC_CONTENT, (const int32_t[]){7001, 3, 0, 1}, 1, false, &r, &it));
    BS_CHECK(it.base == 3 && it.magic == 7001);
    BS_CHECK(bs_item_make(&db, BS_ITEM_SPEC_CONTENT, (const int32_t[]){9001, 0, 0, 0}, 1, false, &r, &it));
    BS_CHECK(it.base == BS_ITEM_SIGIL_AMULET && it.magic == 9001 && it.number == 9001);
    BS_CHECK(bs_item_make(&db, BS_ITEM_SPEC_CONTENT, (const int32_t[]){21, 15, 0, 0}, 1, false, &r, &it));
    BS_CHECK(it.base == BS_ITEM_POTION && it.number == 21);
    BS_CHECK(bs_item_make(&db, BS_ITEM_SPEC_CONTENT, (const int32_t[]){14, 15, 0, 0}, 1, false, &r, &it));
    BS_CHECK(it.base == BS_ITEM_POTION && it.number == 21); /* type 14 is made as 21 */
    BS_CHECK(bs_item_make(&db, BS_ITEM_SPEC_CONTENT, (const int32_t[]){0, 23, 0, 0}, 1, false, &r, &it));
    BS_CHECK(it.base == BS_ITEM_LANTERN);
    /* Placed: a potion of restoration on any base, a book, a magic item on a base. */
    BS_CHECK(bs_item_make(&db, BS_ITEM_SPEC_PLACED, (const int32_t[]){3, 0, 0, 40}, 1, false, &r, &it));
    BS_CHECK(it.base == BS_ITEM_POTION && it.number == 40);
    BS_CHECK(bs_item_make(&db, BS_ITEM_SPEC_PLACED, (const int32_t[]){32, 0, 0, 993}, 1, false, &r, &it));
    BS_CHECK(it.base == BS_ITEM_BOOK && it.number == 993);
    BS_CHECK(bs_item_make(&db, BS_ITEM_SPEC_PLACED, (const int32_t[]){1, 0, 1, 7001}, 1, false, &r, &it));
    BS_CHECK(it.base == 1 && it.magic == 7001);
    /* A random base for an enchantment of item kind 2: clothes or a weapon, never a sling. */
    for (int i = 0; i < 200; i++) {
        BS_CHECK(bs_item_make(&db, BS_ITEM_SPEC_CONTENT, (const int32_t[]){7001, 10000, 0, 0}, 1, false, &r, &it));
        BS_CHECK(it.magic == 7001 && (it.base == BS_ITEM_CLOTHES || (it.base <= 14 && it.base != 12)));
    }
    /* Unknown enchantments and bases fail (the game stops). */
    BS_CHECK(!bs_item_make(&db, BS_ITEM_SPEC_CONTENT, (const int32_t[]){1234, 3, 0, 0}, 1, false, &r, &it));
    BS_CHECK(!bs_item_make(&db, BS_ITEM_SPEC_CONTENT, (const int32_t[]){10000, 99, 0, 0}, 1, false, &r, &it));
}

static void test_name(void)
{
    char buf[96];
    bs_item it = {.base = 0, .material = 1, .magic = 7001, .quantity = 1};
    bs_item_name(&db, &it, buf, sizeof buf);
    BS_CHECK(strcmp(buf, "Steel Dagger of the Test") == 0);
    it = (bs_item){.base = BS_ITEM_HELMET, .material = 9, .quantity = 1};
    bs_item_name(&db, &it, buf, sizeof buf);
    BS_CHECK(strcmp(buf, "Daedric Helmet") == 0);
    it = (bs_item){.base = BS_ITEM_ARROW, .quantity = 12};
    bs_item_name(&db, &it, buf, sizeof buf);
    BS_CHECK(strcmp(buf, "Iron Arrow (12)") == 0);
    it = (bs_item){.base = BS_ITEM_SIGIL_AMULET, .magic = 9001, .quantity = 1};
    bs_item_name(&db, &it, buf, sizeof buf);
    BS_CHECK(strcmp(buf, "Sigil Amulet Ayem Sigil of Entry") == 0);
    it = (bs_item){.base = BS_ITEM_SACK, .material = 5, .quantity = 1};
    bs_item_name(&db, &it, buf, sizeof buf);
    BS_CHECK(strcmp(buf, "Sack") == 0); /* no material on other items */
    /* Truncation keeps it terminated. */
    char small[8];
    it = (bs_item){.base = 0, .material = 1, .magic = 7001, .quantity = 1};
    BS_CHECK_EQ_INT(bs_item_name(&db, &it, small, sizeof small), 7);
    BS_CHECK(strcmp(small, "Steel D") == 0);
}

static void test_model_name(void)
{
    static bs_item_tables t;
    memcpy(&t, &tables, sizeof t);
    snprintf(t.models[7], sizeof t.models[7], "%s", "seven.3d");
    char buf[16];
    BS_CHECK(strcmp(bs_item_model_name(&t, 3, 0, buf), "seven.3d") == 0); /* image 7 */
    BS_CHECK(strcmp(bs_item_model_name(&t, BS_ITEM_SIGIL_AMULET, 9001, buf), "sigila.3d") == 0);
    BS_CHECK(strcmp(bs_item_model_name(&t, BS_ITEM_SIGIL_AMULET, 9026, buf), "sigilz.3d") == 0);
    BS_CHECK(strcmp(bs_item_model_name(&t, BS_ITEM_SIGIL_AMULET, 9024, buf), "sigilz.3d") == 0); /* 'x' */
    BS_CHECK(strcmp(bs_item_model_name(&t, BS_ITEM_SIGIL_AMULET, 9027, buf), "sigila.3d") == 0); /* 26 on */
    BS_CHECK(strcmp(bs_item_model_name(&t, BS_ITEM_SIGIL_AMULET, 9052, buf), "sigilz.3d") == 0);
    t.bases[3].image = 99;
    BS_CHECK(strcmp(bs_item_model_name(&t, 3, 0, buf), "") == 0);
}

static void test_store(void)
{
    bs_items it = {.grid_cell = 1000};
    /* Cells: 64 x 64 of 1000 units centred on 0, truncating towards zero. */
    BS_CHECK_EQ_INT(bs_items_cell(1000, (const float[3]){0, 0, 0}), 32 * 64 + 32);
    BS_CHECK_EQ_INT(bs_items_cell(1000, (const float[3]){-999, 0, 1500}), 32 * 64 + 33);
    BS_CHECK_EQ_INT(bs_items_cell(1000, (const float[3]){-1000, 0, 0}), 31 * 64 + 32);
    BS_CHECK_EQ_INT(bs_items_cell(1000, (const float[3]){40000, 0, 0}), BS_ITEM_CELL_OUTSIDE);
    BS_CHECK_EQ_INT(bs_items_cell(0, (const float[3]){0, 0, 0}), BS_ITEM_CELL_OUTSIDE);

    bs_item dagger = {.base = 0, .quantity = 1}, sack = {.base = BS_ITEM_SACK, .quantity = 1};
    bs_item helmet = {.base = BS_ITEM_HELMET, .quantity = 1};
    const bs_item_place carried = {BS_ITEM_CARRIED, 0}, floor0 = {BS_ITEM_FLOOR, bs_items_cell(1000, (const float[3]){0, 0, 0})};
    uint32_t d = bs_items_add(&it, &dagger, carried, -1, NULL);
    uint32_t sk = bs_items_add(&it, &sack, floor0, -1, (const float[3]){10, 0, 10});
    uint32_t h = bs_items_add(&it, &helmet, (bs_item_place){BS_ITEM_IN_ITEM, sk}, -1, NULL);
    uint32_t list[8];
    BS_CHECK_EQ_INT(bs_items_list(&it, carried, list, 8), 1);
    BS_CHECK_EQ_INT(bs_items_list(&it, floor0, list, 8), 1);
    BS_CHECK(list[0] == sk);
    /* Weights: dagger 1, sack 17, helmet 25 (in the sack). */
    BS_CHECK_EQ_INT(bs_items_weight(&db, &it, sk), 17 + 25);
    BS_CHECK_EQ_INT(bs_items_carried_weight(&db, &it), 1);
    BS_CHECK(!bs_items_place_carried(&it, (bs_item_place){BS_ITEM_IN_ITEM, sk}));

    /* Picking the sack up brings the helmet: 1 + 42 is over 4 x 10 but within 4 x 11. */
    BS_CHECK_EQ_INT(bs_items_move(&it, &db, sk, carried, NULL, 10), BS_MOVE_TOO_HEAVY);
    BS_CHECK_EQ_INT(bs_items_move(&it, &db, sk, carried, NULL, 11), BS_MOVE_OK);
    BS_CHECK_EQ_INT(bs_items_carried_weight(&db, &it), 43);
    BS_CHECK(bs_items_place_carried(&it, (bs_item_place){BS_ITEM_IN_ITEM, sk}));
    BS_CHECK(bs_items_encumbered(&db, &it, 14) && !bs_items_encumbered(&db, &it, 15));
    /* Moving within the inventory isn't weighed again. */
    BS_CHECK_EQ_INT(bs_items_move(&it, &db, d, (bs_item_place){BS_ITEM_IN_ITEM, sk}, NULL, 1), BS_MOVE_OK);
    BS_CHECK_EQ_INT(bs_items_list(&it, (bs_item_place){BS_ITEM_IN_ITEM, sk}, list, 8), 2);
    BS_CHECK(list[0] == h && list[1] == d);
    /* Not into itself, not into a non-container. */
    BS_CHECK_EQ_INT(bs_items_move(&it, &db, sk, (bs_item_place){BS_ITEM_IN_ITEM, sk}, NULL, 99), BS_MOVE_INTO_ITSELF);
    BS_CHECK_EQ_INT(bs_items_move(&it, &db, sk, (bs_item_place){BS_ITEM_IN_ITEM, h}, NULL, 99), BS_MOVE_INVALID);
    /* Dropped on the floor: at the given place, in its cell, no longer shown by an object. */
    BS_CHECK_EQ_INT(bs_items_move(&it, &db, h, (bs_item_place){BS_ITEM_FLOOR, 0}, (const float[3]){-1500, 0, 0}, 99),
                    BS_MOVE_OK);
    BS_CHECK_EQ_INT(it.items[h].place.ref, 31 * 64 + 32); /* -1500 / 1000 truncates to -1 */
    BS_CHECK(it.items[h].object == -1 && it.items[h].pos[0] == -1500);

    /* Arrows and potions stack. */
    bs_item arrows = {.base = BS_ITEM_ARROW, .material = 2, .quantity = 10};
    bs_item potion = {.base = BS_ITEM_POTION, .number = 21, .quantity = 1};
    uint32_t a1 = bs_items_add(&it, &arrows, carried, -1, NULL);
    uint32_t a2 = bs_items_add(&it, &arrows, floor0, -1, NULL);
    arrows.material = 3;
    uint32_t a3 = bs_items_add(&it, &arrows, floor0, -1, NULL);
    uint32_t p1 = bs_items_add(&it, &potion, carried, -1, NULL);
    uint32_t p2 = bs_items_add(&it, &potion, floor0, -1, NULL);
    BS_CHECK_EQ_INT(bs_items_move(&it, &db, a2, carried, NULL, 99), BS_MOVE_JOINED);
    BS_CHECK(it.items[a1].item.quantity == 20 && it.items[a2].place.where == BS_ITEM_GONE);
    BS_CHECK_EQ_INT(bs_items_move(&it, &db, a3, carried, NULL, 99), BS_MOVE_OK); /* other material */
    BS_CHECK_EQ_INT(bs_items_move(&it, &db, p2, carried, NULL, 99), BS_MOVE_JOINED);
    BS_CHECK_EQ_INT(it.items[p1].item.quantity, 2);
    BS_CHECK_EQ_INT(bs_items_move(&it, &db, a2, carried, NULL, 99), BS_MOVE_INVALID); /* gone */

    /* To the next level: only what is carried, containers' contents remapped. */
    bs_items_keep_carried(&it);
    BS_CHECK_EQ_INT(it.count, 5); /* sack, dagger in it, arrows x2, potions */
    uint32_t n = bs_items_list(&it, carried, list, 8);
    BS_CHECK_EQ_INT(n, 4);
    uint32_t snew = UINT32_MAX;
    for (uint32_t i = 0; i < n; i++)
        if (it.items[list[i]].item.base == BS_ITEM_SACK)
            snew = list[i];
    BS_CHECK(snew != UINT32_MAX);
    BS_CHECK_EQ_INT(bs_items_list(&it, (bs_item_place){BS_ITEM_IN_ITEM, snew}, list, 8), 1);
    BS_CHECK_EQ_INT(it.items[list[0]].item.base, 0);
    bs_items_free(&it);
}

static const char *const pictures[] = {"MHLMT500", "MHLMT503", "FHLMT501", "MBRST260", "MPNTS100",
                                       "MSHRT200", "MARMB300", "MCAPE500", "FPNTS101"};

static void test_pictures(void)
{
    bs_item_db pdb = db;
    pdb.pictures = pictures;
    pdb.picture_count = sizeof pictures / sizeof pictures[0];
    bs_rng r = {11};
    bs_item it;
    for (int k = 0; k < 50; k++) {
        bs_item_make_plain(&pdb, BS_ITEM_HELMET, 1, false, &r, &it);
        BS_CHECK(strcmp(it.picture, "MHLMT500") == 0 || strcmp(it.picture, "MHLMT503") == 0);
        bs_item_make_plain(&pdb, BS_ITEM_CLOTHES, 1, false, &r, &it);
        BS_CHECK(strncmp(it.picture, "MARMB", 5) == 0 || strncmp(it.picture, "MCAPE", 5) == 0 ||
                 strncmp(it.picture, "MPNTS", 5) == 0 || strncmp(it.picture, "MSHRT", 5) == 0);
    }
    bs_item_make_plain(&pdb, 3, 1, false, &r, &it);
    BS_CHECK(strcmp(it.picture, "mwpns603") == 0);
    pdb.female = true;
    bs_item_make_plain(&pdb, 3, 1, false, &r, &it);
    BS_CHECK(strcmp(it.picture, "fwpns603") == 0);
    bs_item_make_plain(&pdb, BS_ITEM_HELMET, 1, false, &r, &it);
    BS_CHECK(strcmp(it.picture, "FHLMT501") == 0);
    bs_item_make_plain(&pdb, BS_ITEM_POTION, 1, false, &r, &it);
    BS_CHECK(it.picture[0] == '\0');
    bs_item_make_plain(&db, BS_ITEM_HELMET, 1, false, &r, &it); /* no pictures known */
    BS_CHECK(it.picture[0] == '\0');

    /* Armour values: the last digit mod 4, + 1, + the material less 1 (halved above 1); at least 1. */
    bs_item h = {.base = BS_ITEM_HELMET, .material = 0, .picture = "MHLMT503"};
    BS_CHECK_EQ_INT(bs_item_armour_value(&h), 3);
    h.material = 5;
    BS_CHECK_EQ_INT(bs_item_armour_value(&h), 6);
    h.material = 2;
    BS_CHECK_EQ_INT(bs_item_armour_value(&h), 5);
    snprintf(h.picture, sizeof h.picture, "%s", "MHLMT500");
    h.material = 0;
    BS_CHECK_EQ_INT(bs_item_armour_value(&h), 1);
    h.picture[0] = '\0';
    BS_CHECK_EQ_INT(bs_item_armour_value(&h), 1);
}

static void test_may_equip(void)
{
    bs_equip_rules any = bs_equip_rules_any();
    bs_item dagger = {.base = 0, .material = 3};
    BS_CHECK(bs_item_may_equip(&db, &dagger, &any));
    /* Weapons by their skill's group: the dagger is a short blade (skill 14, group bit 0). */
    bs_equip_rules no_blades = {1, 3, 9};
    BS_CHECK(!bs_item_may_equip(&db, &dagger, &no_blades));
    bs_equip_rules iron_only = {0, 3, 0};
    BS_CHECK(!bs_item_may_equip(&db, &dagger, &iron_only));
    /* The Moon Reiver (base 3, 8011) anyone may wield. */
    bs_item reiver = {.base = 3, .material = 9, .magic = 8011};
    BS_CHECK(bs_item_may_equip(&db, &reiver, &iron_only));
    /* Armour: none at all, or heavier than allowed (value / 2, at most 3). */
    bs_item helmet = {.base = BS_ITEM_HELMET, .material = 5, .picture = "MHLMT503"}; /* value 6: type 3 */
    BS_CHECK(bs_item_may_equip(&db, &helmet, &any));
    BS_CHECK(!bs_item_may_equip(&db, &helmet, &(bs_equip_rules){0, 0, 9}));
    BS_CHECK(!bs_item_may_equip(&db, &helmet, &(bs_equip_rules){0, 2, 9}));
    helmet.material = 0; /* value 3: type 1 */
    BS_CHECK(bs_item_may_equip(&db, &helmet, &(bs_equip_rules){0, 1, 9}));
    bs_item sack = {.base = BS_ITEM_SACK};
    BS_CHECK(bs_item_may_equip(&db, &sack, &any)); /* not a weapon or armour; bs_items_equip refuses it */
}

static void test_equip(void)
{
    bs_items it = {.grid_cell = 1000};
    bs_equip_rules any = bs_equip_rules_any();
    const bs_item_place carried = {BS_ITEM_CARRIED, 0}, floor0 = {BS_ITEM_FLOOR, 32 * 64 + 32};
    bs_item h1 = {.base = BS_ITEM_HELMET, .quantity = 1, .picture = "MHLMT500"};
    bs_item h2 = {.base = BS_ITEM_HELMET, .quantity = 1, .picture = "MHLMT503"};
    bs_item cu = {.base = 25, .quantity = 1, .picture = "MBRST260"};
    bs_item dagger = {.base = 0, .quantity = 1, .picture = "mwpns600"};
    bs_item bow = {.base = 8, .quantity = 1, .picture = "mwpns608"};
    bs_item arrows = {.base = BS_ITEM_ARROW, .quantity = 10};
    bs_item sack = {.base = BS_ITEM_SACK, .quantity = 1};
    uint32_t a = bs_items_add(&it, &h1, floor0, -1, NULL);
    uint32_t b = bs_items_add(&it, &h2, carried, -1, NULL);
    uint32_t c = bs_items_add(&it, &cu, carried, -1, NULL);
    uint32_t d = bs_items_add(&it, &dagger, carried, -1, NULL);
    uint32_t w = bs_items_add(&it, &bow, carried, -1, NULL);
    uint32_t ar = bs_items_add(&it, &arrows, floor0, -1, NULL);
    uint32_t sk = bs_items_add(&it, &sack, carried, -1, NULL);

    /* From the floor: picked up and worn; weighed first. */
    BS_CHECK_EQ_INT(bs_items_equip(&it, &db, a, &any, 10), BS_EQUIP_TOO_HEAVY);
    BS_CHECK_EQ_INT(bs_items_equip(&it, &db, a, &any, 99), BS_EQUIP_OK);
    BS_CHECK(it.items[a].equipped && it.items[a].place.where == BS_ITEM_CARRIED);
    BS_CHECK_EQ_INT(bs_items_worn(&it, BS_ITEM_HELMET), a);
    /* Another helmet takes its place; the cuirass goes beside them. */
    BS_CHECK_EQ_INT(bs_items_equip(&it, &db, b, &any, 99), BS_EQUIP_OK);
    BS_CHECK(!it.items[a].equipped && it.items[b].equipped);
    BS_CHECK_EQ_INT(bs_items_equip(&it, &db, c, &any, 99), BS_EQUIP_OK);
    BS_CHECK(it.items[b].equipped && it.items[c].equipped);
    int32_t av[BS_ARMOUR_PLACES];
    bs_items_armour_values(&it, av);
    BS_CHECK(av[0] == 3 && av[1] == 0 && av[2] == 1 && av[5] == 0); /* helmet, pauldrons, cuirass .. boots */
    /* Not wearable. */
    BS_CHECK_EQ_INT(bs_items_equip(&it, &db, sk, &any, 99), BS_EQUIP_NOT_ALLOWED);
    BS_CHECK_EQ_INT(bs_items_equip(&it, &db, d, &(bs_equip_rules){1, 3, 9}, 99), BS_EQUIP_NOT_ALLOWED);

    /* Weapons: one at a time; a bow needs arrows ready. */
    BS_CHECK_EQ_INT(bs_items_equip(&it, &db, d, &any, 99), BS_EQUIP_OK);
    BS_CHECK_EQ_INT(bs_items_weapon(&it), d);
    BS_CHECK_EQ_INT(bs_items_equip(&it, &db, w, &any, 99), BS_EQUIP_NO_ARROWS);
    BS_CHECK_EQ_INT(bs_items_equip(&it, &db, ar, &any, 99), BS_EQUIP_OK);
    BS_CHECK_EQ_INT(bs_items_arrows(&it), ar);
    BS_CHECK_EQ_INT(bs_items_equip(&it, &db, w, &any, 99), BS_EQUIP_OK);
    BS_CHECK(bs_items_weapon(&it) == w && !it.items[d].equipped);
    BS_CHECK(it.items[b].equipped); /* armour stays */
    /* More arrows from the floor join the ones ready, which stay ready. */
    uint32_t ar2 = bs_items_add(&it, &arrows, floor0, -1, NULL);
    BS_CHECK_EQ_INT(bs_items_equip(&it, &db, ar2, &any, 99), BS_EQUIP_OK);
    BS_CHECK(bs_items_arrows(&it) == ar && it.items[ar].item.quantity == 20);
    /* Moving an item takes it off: without arrows the bow is put away. */
    BS_CHECK_EQ_INT(bs_items_move(&it, &db, ar, floor0, (const float[3]){0, 0, 0}, 99), BS_MOVE_OK);
    BS_CHECK(!it.items[ar].equipped);
    BS_CHECK(bs_items_check_bow(&it));
    BS_CHECK_EQ_INT(bs_items_weapon(&it), UINT32_MAX);
    BS_CHECK(!bs_items_check_bow(&it));

    /* The secondary weapon: one, carried, not wielded. */
    BS_CHECK_EQ_INT(bs_items_set_secondary(&it, &db, d, &any, 99), BS_EQUIP_OK);
    BS_CHECK_EQ_INT(bs_items_secondary(&it), d);
    BS_CHECK_EQ_INT(bs_items_set_secondary(&it, &db, w, &any, 99), BS_EQUIP_OK);
    BS_CHECK(bs_items_secondary(&it) == w && !it.items[d].secondary);
    BS_CHECK_EQ_INT(bs_items_set_secondary(&it, &db, c, &any, 99), BS_EQUIP_INVALID); /* not a weapon */
    BS_CHECK_EQ_INT(bs_items_equip(&it, &db, d, &any, 99), BS_EQUIP_OK);
    BS_CHECK(it.items[d].equipped && !it.items[d].secondary);
    bs_items_unequip(&it, d);
    BS_CHECK_EQ_INT(bs_items_weapon(&it), UINT32_MAX);
    bs_items_free(&it);
}

static void test_take_and_put_level(void)
{
    bs_items it = {.grid_cell = 1000};
    const bs_item_place carried = {BS_ITEM_CARRIED, 0}, floor0 = {BS_ITEM_FLOOR, 2080};
    bs_item dagger = {.base = 0, .quantity = 1}, sack = {.base = BS_ITEM_SACK, .quantity = 1};
    uint32_t d = bs_items_add(&it, &dagger, carried, -1, NULL);
    uint32_t sk = bs_items_add(&it, &sack, floor0, 7, (const float[3]){1, 2, 3});
    bs_items_add(&it, &dagger, (bs_item_place){BS_ITEM_IN_ITEM, sk}, -1, NULL);
    bs_items_add(&it, &dagger, (bs_item_place){BS_ITEM_IN_OBJECT, 4}, -1, NULL);
    uint32_t cs[1] = {4};
    it.containers = malloc(sizeof cs);
    memcpy(it.containers, cs, sizeof cs);
    it.container_count = 1;
    bs_items lv;
    BS_CHECK_EQ_INT(bs_items_take_level(&it, &lv), BS_OK);
    BS_CHECK(it.count == 1 && it.items[0].place.where == BS_ITEM_CARRIED);
    BS_CHECK(lv.count == 3 && lv.container_count == 1);
    /* In another level, then back: the sack still lies where it was with the dagger in it. */
    BS_CHECK_EQ_INT(bs_items_put_level(&it, &lv), BS_OK);
    BS_CHECK_EQ_INT(it.count, 4);
    uint32_t list[4];
    BS_CHECK_EQ_INT(bs_items_list(&it, floor0, list, 4), 1);
    BS_CHECK(it.items[list[0]].object == 7 && it.items[list[0]].pos[2] == 3);
    BS_CHECK_EQ_INT(bs_items_list(&it, (bs_item_place){BS_ITEM_IN_ITEM, list[0]}, list, 4), 1);
    BS_CHECK(bs_items_is_container_object(&it, 4));
    BS_CHECK_EQ_INT(bs_items_list(&it, carried, list, 4), 1);
    BS_CHECK_EQ_INT(list[0], d);
    bs_items_free(&lv);
    bs_items_free(&it);
}

static void test_level_items(void)
{
    static uint8_t buf[16384];
    size_t n = synth_bs6_items(buf, sizeof buf);
    BS_CHECK(n > 0);
    bs_bs6_level lv;
    BS_CHECK_EQ_INT(bs_bs6_parse(buf, n, &arena, &lv), BS_OK);
    bs_items it = {0};
    bs_rng r = {3};
    BS_CHECK_EQ_INT(bs_items_add_level(&it, &lv, &db, 1, false, 4096, &r), BS_OK);
    BS_CHECK_EQ_INT(it.count, 5);
    /* #30: arrows on the floor, shown by the object. */
    uint32_t a = bs_items_on_object(&it, 0);
    BS_CHECK(a != UINT32_MAX && it.items[a].item.base == BS_ITEM_ARROW);
    BS_CHECK(a != UINT32_MAX && it.items[a].place.where == BS_ITEM_FLOOR && it.items[a].place.ref == 32 * 64 + 32);
    /* #31: a chest with two items. */
    BS_CHECK(bs_items_is_container_object(&it, 1) && !bs_items_is_container_object(&it, 0));
    uint32_t list[8];
    BS_CHECK_EQ_INT(bs_items_list(&it, (bs_item_place){BS_ITEM_IN_OBJECT, 1}, list, 8), 2);
    BS_CHECK(it.items[list[0]].item.base == BS_ITEM_CLOTHES && it.items[list[1]].item.magic == 7001);
    BS_CHECK_EQ_INT(bs_items_on_object(&it, 2), UINT32_MAX);
    uint32_t p = bs_items_on_object(&it, 3);
    BS_CHECK(p != UINT32_MAX && it.items[p].item.base == BS_ITEM_POTION && it.items[p].item.number == 21);
    p = bs_items_on_object(&it, 4);
    BS_CHECK(p != UINT32_MAX && it.items[p].item.base == BS_ITEM_BOOK && it.items[p].item.number == 993);
    /* The three on the floor in x 0..400 share the cell. */
    BS_CHECK_EQ_INT(bs_items_list(&it, (bs_item_place){BS_ITEM_FLOOR, 32 * 64 + 32}, list, 8), 3);
    bs_items_free(&it);
}

int main(void)
{
    setup();
    BS_RUN(test_tables);
    BS_RUN(test_magic);
    BS_RUN(test_material);
    BS_RUN(test_make);
    BS_RUN(test_name);
    BS_RUN(test_model_name);
    BS_RUN(test_store);
    BS_RUN(test_pictures);
    BS_RUN(test_may_equip);
    BS_RUN(test_equip);
    BS_RUN(test_take_and_put_level);
    BS_RUN(test_level_items);
    bs_arena_free(&arena);
    return BS_TEST_RESULT();
}
