#ifndef BS_TEST_H
#define BS_TEST_H

/*
 * Minimal test harness. Each test executable defines test functions and
 * runs them with BS_RUN(fn) from main(), then returns BS_TEST_RESULT().
 *
 * Tests that need the original game data return BS_TEST_SKIP (77) when
 * BS_DATA_DIR is not set; CTest reports them as skipped.
 */

#include <stdio.h>

#define BS_TEST_SKIP 77

static int bs_test_failures;

#define BS_CHECK(cond)                                                            \
    do {                                                                          \
        if (!(cond)) {                                                            \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            bs_test_failures++;                                                   \
        }                                                                         \
    } while (0)

#define BS_CHECK_EQ_INT(a, b)                                                     \
    do {                                                                          \
        long long bs_a_ = (long long)(a), bs_b_ = (long long)(b);                 \
        if (bs_a_ != bs_b_) {                                                     \
            fprintf(stderr, "%s:%d: %s == %s failed (%lld != %lld)\n", __FILE__,  \
                    __LINE__, #a, #b, bs_a_, bs_b_);                              \
            bs_test_failures++;                                                   \
        }                                                                         \
    } while (0)

#define BS_RUN(fn)                                  \
    do {                                            \
        int bs_before_ = bs_test_failures;          \
        fn();                                       \
        fprintf(stderr, "%-40s %s\n", #fn,          \
                bs_test_failures == bs_before_ ? "ok" : "FAILED"); \
    } while (0)

#define BS_TEST_RESULT() (bs_test_failures ? 1 : 0)

#endif
