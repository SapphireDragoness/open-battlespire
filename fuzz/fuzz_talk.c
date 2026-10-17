/* Fuzzes the dialogue tables and a conversation run on them (the input as both opening and script). */
#include <string.h>

#include "bs/formats/talk.h"
#include "bs/game/dialogue.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_talk_file f;
    if (bs_talk_parse(data, size, &a, &f) == BS_OK) {
        static bs_dialogue_state s;
        static bs_dialogue d;
        bs_dialogue_state_init(&s);
        bs_dialogue_update_vars(&s, size & 1, NULL, NULL);
        bs_rng r = {(uint32_t)size | 1u};
        char msg[256];
        for (int visit = 0; visit < 5; visit++) {
            if (bs_dialogue_start(&d, &s, &f, &f, 77, 0, &r, msg, sizeof msg) != BS_DIALOGUE_TALK)
                continue;
            for (int step = 0; step < 16 && !bs_dialogue_over(&d) && d.reply_count; step++) {
                bs_dialogue_action act[BS_DIALOGUE_ACTIONS];
                bs_dialogue_choose(&d, (uint32_t)step % d.reply_count, &r, act, BS_DIALOGUE_ACTIONS);
            }
        }
    }
    bs_arena_free(&a);
    return 0;
}
