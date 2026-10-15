/* Fuzzes MG*.TXT enchantment parsing and the GAME.EXE item table search. */
#include <string.h>

#include "bs/formats/items.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_magic_table t;
    if (bs_magic_parse(data, size, &a, &t) == BS_OK) {
        for (size_t i = 0; i < t.count; i++) {
            const bs_magic *m = &t.records[i];
            if (!memchr(m->name, 0, sizeof m->name) || m->app_count > BS_MAGIC_LIST_MAX ||
                m->adv_count > BS_MAGIC_LIST_MAX || bs_magic_find(&t, m->id) == NULL)
                __builtin_trap();
        }
    }
    static bs_item_tables tables;
    if (bs_item_tables_read(data, size, &tables) == BS_OK)
        for (int i = 0; i < BS_ITEM_BASE_COUNT; i++)
            if (!memchr(tables.bases[i].name, 0, sizeof tables.bases[i].name))
                __builtin_trap();
    bs_arena_free(&a);
    return 0;
}
