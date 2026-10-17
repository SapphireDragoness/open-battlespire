/* Fuzzes the saved game readers: the record tree with its controllers, SAVEVARS.DAT, class files, and the
 * import of a tree into the engine's pieces. */
#include <string.h>

#include "bs/formats/save.h"
#include "bs/formats/scl.h"
#include "bs/game/save_import.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_save_tree t;
    if (bs_save_tree_parse(data, size, &a, &t) == BS_OK) {
        volatile uint32_t sink = 0;
        for (uint32_t i = 0; i < t.count; i++) {
            const bs_save_record *r = &t.records[i];
            sink += r->parent_index >= 0 ? t.records[r->parent_index].id : 0;
            if (r->type == BS_SAVE_PLAYER || r->type == BS_SAVE_MONSTER) {
                static bs_save_character c;
                bs_save_character_parse(r->body, r->body_size, &c);
                sink += (uint32_t)c.wounds;
            } else if (r->type == BS_SAVE_ITEM) {
                bs_scl_item it;
                bs_save_item_parse(r, &it);
                sink += it.base;
            }
        }
        /* Controllers in chains of 1..4, as many as the first byte says. */
        uint32_t chains[64];
        uint32_t n = t.controls_size ? t.controls[0] % 64 : 0;
        for (uint32_t c = 0; c < n; c++)
            chains[c] = 1 + c % 4;
        bs_save_controller *ctl;
        uint32_t nc, na;
        uint8_t *active;
        if (t.controls_size > 1 &&
            bs_save_controllers_read(t.controls + 1, t.controls_size - 1, chains, n, &a, &ctl, &nc, &active, &na) ==
                BS_OK) {
            for (uint32_t i = 0; i < nc; i++)
                sink += ctl[i].start;
            for (uint32_t i = 0; i < na; i++)
                sink += active[i];
        }
        /* With empty variables: the records alone decide the rest. */
        static bs_save_vars none;
        static bs_saved_game g;
        if (bs_save_import(&t, &none, NULL, &a, &g) == BS_OK) {
            for (uint32_t i = 0; i < g.item_count; i++)
                if (g.items[i].place == BS_SAVED_IN_ITEM)
                    sink += g.items[g.items[i].ref].id;
                else if (g.items[i].place == BS_SAVED_MONSTER)
                    sink += g.monsters[g.items[i].ref].id;
        }
        (void)sink;
    }
    static bs_save_vars v;
    if (bs_save_vars_parse(data, size, &v) == BS_OK)
        (void)bs_save_variable_hash(v.log);
    static bs_class k;
    (void)bs_class_parse(data, size, &k);
    bs_arena_free(&a);
    return 0;
}
