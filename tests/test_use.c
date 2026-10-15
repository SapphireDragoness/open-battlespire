/* Using items: spell scrolls, books, potions; spell costs and the spell book's deliveries. */

#include <string.h>

#include "bs/game/use.h"
#include "bs_test.h"
#include "synth.h"

static uint8_t exe[64 * 1024];
static bs_item_tables tables;
static bs_item_db db;
static bs_spell spells[BS_SPELL_COUNT];

static void setup(void)
{
    size_t n = synth_item_exe(exe, sizeof exe);
    BS_CHECK_EQ_INT(bs_item_tables_read(exe, n, &tables), BS_OK);
    db = (bs_item_db){.tables = &tables};
    /* Spells: 21 Cure Health heals 2-20 + 1-6 per level; 8 Slow Fall lasts (5 + level / 2 x 5) x 5 s. */
    memset(spells, 0, sizeof spells);
    for (int i = 0; i < BS_SPELL_COUNT; i++)
        spells[i].effect = (uint8_t)(i + 1);
    spells[20] = (bs_spell){.name = "Cure Health", .effect = 21, .magnitude_min = 2, .magnitude_max = 20,
                            .increase_min = 1, .increase_max = 6, .magnitude_step = 1};
    spells[17] = (bs_spell){.name = "Cause Damage", .effect = 18, .magnitude_min = 2, .magnitude_max = 20,
                            .increase_min = 1, .increase_max = 6, .magnitude_step = 1};
    spells[7] = (bs_spell){.name = "Slow Fall", .effect = 8, .duration_base = 5, .duration_increase = 5,
                           .duration_step = 2};
}

static void test_spell_rolls(void)
{
    BS_CHECK_EQ_INT(bs_spell_duration(&spells[7], 1), 25000);
    BS_CHECK_EQ_INT(bs_spell_duration(&spells[7], 4), 75000);
    BS_CHECK_EQ_INT(bs_spell_duration(&spells[20], 9), 0);
    bs_rng r = {5};
    for (int k = 0; k < 200; k++) {
        int32_t m = bs_spell_magnitude(&spells[20], 3, &r);
        BS_CHECK(m >= 2 + 3 && m <= 20 + 18);
    }
    BS_CHECK_EQ_INT(bs_spell_magnitude(&spells[7], 3, &r), 0);
}

static void test_scrolls_and_books(void)
{
    bs_items it = {.grid_cell = 1000};
    bs_character c;
    bs_character_default(&c);
    bs_rng r = {1};
    const bs_item_place carried = {BS_ITEM_CARRIED, 0};
    bs_item scroll = {.base = BS_ITEM_SCROLL, .number = 10008, .quantity = 1};
    bs_item book = {.base = BS_ITEM_BOOK, .number = 993, .quantity = 1};
    bs_item note = {.base = BS_ITEM_SCROLL, .number = 994, .quantity = 1};
    uint32_t s = bs_items_add(&it, &scroll, carried, -1, NULL);
    uint32_t s2 = bs_items_add(&it, &scroll, carried, -1, NULL);
    uint32_t b = bs_items_add(&it, &book, carried, -1, NULL);
    uint32_t n = bs_items_add(&it, &note, carried, -1, NULL);
    bs_use_result u = bs_items_use(&it, &db, s, &c, spells, 1, 0, &r);
    BS_CHECK(u.kind == BS_USE_DONE && u.message_count == 1 && u.messages[0].id == 244);
    BS_CHECK(c.spells_known == 1u << 7 && it.items[s].place.where == BS_ITEM_GONE);
    BS_CHECK(strcmp(c.book[7].name, "Slow Fall") == 0); /* the book has the table's record */
    u = bs_items_use(&it, &db, s2, &c, spells, 1, 0, &r);
    BS_CHECK(u.messages[0].id == 243 && it.items[s2].place.where == BS_ITEM_CARRIED); /* kept */
    u = bs_items_use(&it, &db, b, &c, spells, 1, 0, &r);
    BS_CHECK(u.kind == BS_USE_READ && u.book == 993 && it.items[b].place.where == BS_ITEM_CARRIED);
    u = bs_items_use(&it, &db, n, &c, spells, 1, 0, &r);
    BS_CHECK(u.kind == BS_USE_READ && u.book == 994);
    bs_item dagger = {.base = 0, .quantity = 1};
    uint32_t d = bs_items_add(&it, &dagger, carried, -1, NULL);
    u = bs_items_use(&it, &db, d, &c, spells, 1, 0, &r);
    BS_CHECK(u.kind == BS_USE_NOTHING && u.message_count == 0);
    bs_items_free(&it);
}

static void test_potions(void)
{
    bs_items it = {.grid_cell = 1000};
    bs_character c;
    bs_character_default(&c);
    bs_rng r = {2};
    const bs_item_place carried = {BS_ITEM_CARRIED, 0};
    bs_item restore = {.base = BS_ITEM_POTION, .number = 40, .quantity = 2};
    bs_item water = {.base = BS_ITEM_POTION, .number = 39, .quantity = 1};
    bs_item heal = {.base = BS_ITEM_POTION, .number = 21, .quantity = 1};
    bs_item fall = {.base = BS_ITEM_POTION, .number = 8, .quantity = 1};
    uint32_t p = bs_items_add(&it, &restore, carried, -1, NULL);
    /* Restoration: already full, then restored; one used up each time. */
    bs_use_result u = bs_items_use(&it, &db, p, &c, spells, 1, 0, &r);
    BS_CHECK(u.messages[0].id == 246 && it.items[p].item.quantity == 1);
    c.spell_points = 3;
    u = bs_items_use(&it, &db, p, &c, spells, 1, 0, &r);
    BS_CHECK(u.messages[0].id == 247 && c.spell_points == 50 && it.items[p].place.where == BS_ITEM_GONE);
    /* Water breathing for 5 minutes. */
    uint32_t w = bs_items_add(&it, &water, carried, -1, NULL);
    u = bs_items_use(&it, &db, w, &c, spells, 1, 1000, &r);
    BS_CHECK(u.messages[0].id == 432 && c.water_breathing_until == 301000);
    /* Other potions are drunk and their spell is to be cast on the drinker (bs/game/magic.h). */
    uint32_t h = bs_items_add(&it, &heal, carried, -1, NULL);
    u = bs_items_use(&it, &db, h, &c, spells, 1, 0, &r);
    BS_CHECK(u.kind == BS_USE_DONE && u.message_count == 1 && u.messages[0].id == 433 && u.spell == 21);
    BS_CHECK_EQ_INT(it.items[h].place.where, BS_ITEM_GONE);
    uint32_t f = bs_items_add(&it, &fall, carried, -1, NULL);
    u = bs_items_use(&it, &db, f, &c, spells, 1, 2000, &r);
    BS_CHECK_EQ_INT(u.spell, 8);
    bs_items_free(&it);
}

static void test_costs_and_deliveries(void)
{
    /* base x (110 - skill) / 100 x the delivery's 80-120 %, at least 5 */
    bs_spell s = {.effect = 18, .cost = 60, .delivery = BS_DELIVERY_RANGE, .target = BS_TARGET_OTHER};
    BS_CHECK_EQ_INT(bs_spell_cost(&s, 50), 36);
    s.delivery = BS_DELIVERY_TOUCH;
    BS_CHECK_EQ_INT(bs_spell_cost(&s, 50), 32);          /* 36 x 90 / 100 */
    s.delivery = BS_DELIVERY_AREA_RANGE;
    BS_CHECK_EQ_INT(bs_spell_cost(&s, 50), 43);
    BS_CHECK_EQ_INT(bs_spell_cost(&s, 109), 5);          /* 0, at least 5 */
    /* The delivery button: spells for others skip the caster, self-only spells stay. */
    BS_CHECK_EQ_INT(bs_spell_next_delivery(&s, BS_DELIVERY_AREA_RANGE), BS_DELIVERY_TOUCH);
    s.target = BS_TARGET_EITHER;
    BS_CHECK_EQ_INT(bs_spell_next_delivery(&s, BS_DELIVERY_AREA_RANGE), BS_DELIVERY_CASTER);
    s.target = BS_TARGET_SELF;
    BS_CHECK_EQ_INT(bs_spell_next_delivery(&s, BS_DELIVERY_CASTER), BS_DELIVERY_CASTER);
}

int main(void)
{
    setup();
    BS_RUN(test_costs_and_deliveries);
    BS_RUN(test_spell_rolls);
    BS_RUN(test_scrolls_and_books);
    BS_RUN(test_potions);
    return BS_TEST_RESULT();
}
