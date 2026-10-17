/* Fuzzes our saved games' reader (and the sectioned text under it), then writes what it read and reads that
 * back: it must read. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/game/game_save.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    static bs_saved_game g, again;
    char err[BS_GAME_SAVE_ERROR];
    if (bs_game_save_read((const char *)data, size, NULL, NULL, &a, &g, err) == BS_OK) {
        char *text;
        size_t n;
        if (bs_game_save_write(&g, NULL, &text, &n) == BS_OK) {
            if (bs_game_save_read(text, n, NULL, NULL, &a, &again, err) != BS_OK) {
                fprintf(stderr, "what was written doesn't read: %s\n%s", err, text);
                abort();
            }
            free(text);
        }
    }
    bs_arena_free(&a);
    return 0;
}
