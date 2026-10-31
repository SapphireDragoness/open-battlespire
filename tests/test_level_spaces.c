/* A campaign level's spaces: the interiors its doors load, in the original's order, with their id bases and
 * their controller chains in the save's table. */

#include <string.h>

#include "bs/core/fs.h"
#include "bs/game/level_spaces.h"
#include "bs_test.h"
#include "synth.h"

typedef struct file {
    const char *name;
    uint8_t data[4096];
    size_t size;
} file;

static file files[4];

static bs_result read_file(void *ctx, const char *name, bs_arena *a, bs_bs6_level *out)
{
    (void)ctx;
    for (int i = 0; i < 4; i++)
        if (bs_strcasecmp(files[i].name, name) == 0)
            return bs_bs6_parse(files[i].data, files[i].size, a, out);
    return BS_ERR_NOT_FOUND;
}

static void test_spaces(void)
{
    /* top (highest id 20, a chain of 2): inner1, a door without a name, inner1 again, itself, a file that
     * isn't there, inner2. inner1 (9, 1 controller) leads on to nested and back; inner2 (15, 3) has no doors;
     * nested (12, 1) leads back. */
    static const synth_door top[] = { {5, "inner1"}, {6, ""}, {7, "INNER1"}, {8, "top"}, {9, "missing"}, {10, "inner2"} };
    static const synth_door inner1[] = { {3, "nested"}, {4, "top"} };
    static const synth_door nested[] = { {2, "top"} };
    files[0] = (file){"top.bs6", {0}, 0};
    files[0].size = synth_bs6_doors(files[0].data, sizeof files[0].data, top, 6, 20, 2);
    files[1] = (file){"inner1.bs6", {0}, 0};
    files[1].size = synth_bs6_doors(files[1].data, sizeof files[1].data, inner1, 2, 9, 1);
    files[2] = (file){"inner2.bs6", {0}, 0};
    files[2].size = synth_bs6_doors(files[2].data, sizeof files[2].data, NULL, 0, 15, 3);
    files[3] = (file){"nested.bs6", {0}, 0};
    files[3].size = synth_bs6_doors(files[3].data, sizeof files[3].data, nested, 1, 12, 1);
    for (int i = 0; i < 4; i++)
        BS_CHECK(files[i].size > 0);

    bs_arena a;
    bs_arena_init(&a, 0);
    bs_bs6_level level;
    BS_CHECK_EQ_INT(read_file(NULL, "top.bs6", &a, &level), BS_OK);
    static bs_level_spaces s;
    BS_CHECK_EQ_INT(bs_level_spaces_build("top.bs6", &level, read_file, NULL, &a, &s), BS_OK);

    /* The counter starts at 20 + 100; each door adds 100, and a file it loads its highest id. */
    BS_CHECK_EQ_INT(s.count, 4);
    BS_CHECK(strcmp(s.spaces[0].file, "top.bs6") == 0);
    BS_CHECK_EQ_INT(s.spaces[0].door, 0);
    BS_CHECK_EQ_INT(s.spaces[0].base, 0);
    BS_CHECK_EQ_INT(s.spaces[0].last_id, 20);
    BS_CHECK(strcmp(s.spaces[1].file, "inner1.bs6") == 0);
    BS_CHECK_EQ_INT(s.spaces[1].door, 5);
    BS_CHECK_EQ_INT(s.spaces[1].base, 120);
    BS_CHECK(strcmp(s.spaces[2].file, "inner2.bs6") == 0);
    BS_CHECK_EQ_INT(s.spaces[2].door, 10);
    BS_CHECK_EQ_INT(s.spaces[2].base, 120 + 109 + 400);
    /* The nested interior comes after all of the level's, its door by inner1's base. */
    BS_CHECK(strcmp(s.spaces[3].file, "nested.bs6") == 0);
    BS_CHECK_EQ_INT(s.spaces[3].door, 123);
    BS_CHECK_EQ_INT(s.spaces[3].base, 629 + 115);

    /* Chains: the level's, then each interior's in load order. */
    BS_CHECK_EQ_INT(s.chain_count, 4);
    BS_CHECK_EQ_INT(s.chain_lengths[0], 2);
    BS_CHECK_EQ_INT(s.chain_lengths[1], 1);
    BS_CHECK_EQ_INT(s.chain_lengths[2], 3);
    BS_CHECK_EQ_INT(s.chain_lengths[3], 1);
    /* Each chain's entity, by its own file's id: each level's last object. */
    BS_CHECK(s.chain_ids[0] == 20 && s.chain_ids[1] == 9 && s.chain_ids[2] == 15 && s.chain_ids[3] == 12);
    BS_CHECK_EQ_INT(s.chain_kinds[2], BS_LOGIC_OBJECT);
    for (uint32_t k = 0; k < 4; k++) {
        BS_CHECK_EQ_INT(s.spaces[k].first_chain, k);
        BS_CHECK_EQ_INT(s.spaces[k].chain_count, 1);
    }

    BS_CHECK_EQ_INT(bs_level_spaces_by_door(&s, 0), 0);
    BS_CHECK_EQ_INT(bs_level_spaces_by_door(&s, 123), 3);
    BS_CHECK_EQ_INT(bs_level_spaces_by_door(&s, 7), -1);
    uint32_t local = 0;
    BS_CHECK_EQ_INT(bs_level_spaces_of_id(&s, 7, &local), 0);
    BS_CHECK_EQ_INT(local, 7);
    BS_CHECK_EQ_INT(bs_level_spaces_of_id(&s, 129, &local), 1);
    BS_CHECK_EQ_INT(local, 9);
    BS_CHECK_EQ_INT(bs_level_spaces_of_id(&s, 130, &local), -1);
    BS_CHECK_EQ_INT(bs_level_spaces_of_id(&s, 744 + 12, &local), 3);
    BS_CHECK_EQ_INT(local, 12);
    bs_arena_free(&a);
}

int main(void)
{
    BS_RUN(test_spaces);
    return BS_TEST_RESULT();
}
