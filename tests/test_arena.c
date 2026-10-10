#include <stdint.h>
#include <string.h>

#include "bs/core/arena.h"
#include "bs_test.h"

static void test_alignment_and_zeroing(void)
{
    bs_arena a;
    bs_arena_init(&a, 256);
    char *c = bs_arena_alloc(&a, 1, 1);
    uint64_t *u = BS_ARENA_NEW(&a, uint64_t);
    BS_CHECK(c && u);
    BS_CHECK(((uintptr_t)u % _Alignof(uint64_t)) == 0);
    BS_CHECK(*u == 0);
    BS_CHECK(bs_arena_alloc(&a, 8, 3) == NULL); /* non power of two */
    bs_arena_free(&a);
}

static void test_grows_beyond_block(void)
{
    bs_arena a;
    bs_arena_init(&a, 64);
    unsigned char *big = bs_arena_alloc(&a, 1000, 1);
    BS_CHECK(big != NULL);
    memset(big, 0xAB, 1000);
    for (int i = 0; i < 100; i++)
        BS_CHECK(bs_arena_alloc(&a, 48, 8) != NULL);
    bs_arena_free(&a);
    BS_CHECK(a.head == NULL);
}

static void test_mark_rewind(void)
{
    bs_arena a;
    bs_arena_init(&a, 128);
    char *keep = bs_arena_strdup(&a, "keep me");
    bs_arena_mark m = bs_arena_get_mark(&a);
    for (int i = 0; i < 50; i++)
        bs_arena_alloc(&a, 100, 1); /* forces several new blocks */
    bs_arena_rewind(&a, m);
    BS_CHECK(strcmp(keep, "keep me") == 0);
    char *again = bs_arena_alloc(&a, 4, 1);
    BS_CHECK(again == keep + strlen("keep me") + 1); /* space after mark is reused */
    bs_arena_free(&a);
}

static void test_reset(void)
{
    bs_arena a;
    bs_arena_init(&a, 64);
    void *first = bs_arena_alloc(&a, 8, 8);
    for (int i = 0; i < 20; i++)
        bs_arena_alloc(&a, 60, 1);
    bs_arena_reset(&a);
    /* After reset, allocation starts at the beginning of the first block. */
    BS_CHECK(bs_arena_alloc(&a, 8, 8) == first);
    bs_arena_free(&a);
}

static void test_array_overflow(void)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    BS_CHECK(BS_ARENA_ARRAY(&a, uint64_t, SIZE_MAX / 4) == NULL);
    bs_arena_free(&a);
}

int main(void)
{
    BS_RUN(test_alignment_and_zeroing);
    BS_RUN(test_grows_beyond_block);
    BS_RUN(test_mark_rewind);
    BS_RUN(test_reset);
    BS_RUN(test_array_overflow);
    return BS_TEST_RESULT();
}
