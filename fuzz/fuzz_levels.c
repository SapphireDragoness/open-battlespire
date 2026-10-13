/* Fuzzes LEVELS.TXT parsing. */
#include <string.h>

#include "bs/formats/levels.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_level_table t;
    if (bs_levels_parse(data, size, &a, &t) == BS_OK) {
        for (size_t i = 0; i < t.count; i++) {
            /* All string fields must be terminated within their arrays. */
            const bs_level_info *l = &t.levels[i];
            if (!memchr(l->name, 0, sizeof l->name) || !memchr(l->file, 0, sizeof l->file))
                __builtin_trap();
        }
    }
    bs_arena_free(&a);
    return 0;
}
