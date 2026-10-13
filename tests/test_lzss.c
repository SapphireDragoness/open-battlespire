#include <stdlib.h>
#include <string.h>

#include "bs/formats/lzss.h"
#include "bs_test.h"

/*
 * Reference encoder for tests: greedy, brute-force search over the whole
 * ring buffer, simulating the decoder exactly (including the space-filled
 * ring and overlapping copies). Slow, but correct by construction.
 */
static size_t encode(const uint8_t *src, size_t n, uint8_t *dst)
{
    uint8_t ring[BS_LZSS_RING_SIZE];
    memset(ring, BS_LZSS_FILL, sizeof ring);
    unsigned r = BS_LZSS_RING_SIZE - BS_LZSS_MAX_MATCH;
    size_t in = 0, out = 0;

    while (in < n) {
        size_t flag_pos = out++;
        uint8_t flags = 0;
        for (int bit = 0; bit < 8 && in < n; bit++) {
            unsigned best_len = 0, best_pos = 0;
            for (unsigned p = 0; p < BS_LZSS_RING_SIZE; p++) {
                /* Simulate copying from p while writing at r. */
                uint8_t sim[BS_LZSS_MAX_MATCH];
                unsigned len = 0;
                while (len < BS_LZSS_MAX_MATCH && in + len < n) {
                    unsigned src_pos = (p + len) & (BS_LZSS_RING_SIZE - 1);
                    unsigned written = (src_pos - r) & (BS_LZSS_RING_SIZE - 1);
                    uint8_t c = written < len ? sim[written] : ring[src_pos];
                    if (c != src[in + len])
                        break;
                    sim[len++] = c;
                }
                if (len > best_len) {
                    best_len = len;
                    best_pos = p;
                }
            }
            unsigned take;
            if (best_len > BS_LZSS_THRESHOLD) {
                dst[out++] = (uint8_t)(best_pos & 0xFF);
                dst[out++] = (uint8_t)(((best_pos >> 4) & 0xF0) | (best_len - BS_LZSS_THRESHOLD - 1));
                take = best_len;
            } else {
                flags |= (uint8_t)(1u << bit);
                dst[out++] = src[in];
                take = 1;
            }
            for (unsigned k = 0; k < take; k++) {
                ring[r] = src[in++];
                r = (r + 1) & (BS_LZSS_RING_SIZE - 1);
            }
        }
        dst[flag_pos] = flags;
    }
    return out;
}

static void check_round_trip(const uint8_t *src, size_t n)
{
    uint8_t *packed = malloc(n + n / 8 + 2);
    size_t packed_n = encode(src, n, packed);
    bs_arena a;
    bs_arena_init(&a, 0);
    uint8_t *out;
    size_t out_n;
    BS_CHECK_EQ_INT(bs_lzss_decompressed_size(packed, packed_n), n);
    BS_CHECK_EQ_INT(bs_lzss_decompress(packed, packed_n, &a, &out, &out_n), BS_OK);
    BS_CHECK_EQ_INT(out_n, n);
    BS_CHECK(memcmp(out, src, n) == 0);
    BS_CHECK(out[out_n] == 0);
    bs_arena_free(&a);
    free(packed);
}

static void test_round_trips(void)
{
    static const char text[] = "the spire, the spire, the battlespire! the spire, the spire...";
    check_round_trip((const uint8_t *)text, sizeof text - 1);

    uint8_t zeros[3000] = { 0 };
    check_round_trip(zeros, sizeof zeros);

    /* Pseudo-random bytes (incompressible) and a mix that wraps the ring buffer. */
    uint8_t mixed[9000];
    uint32_t seed = 12345;
    for (size_t i = 0; i < sizeof mixed; i++) {
        seed = seed * 1103515245u + 12345u;
        mixed[i] = (i / 700) % 2 ? (uint8_t)(seed >> 24) : (uint8_t)(i % 37);
    }
    check_round_trip(mixed, sizeof mixed);
    check_round_trip(mixed, 1);
}

/* Hand-built streams pinning down exact semantics. */
static void test_known_streams(void)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    uint8_t *out;
    size_t n;

    /* Empty input. */
    BS_CHECK_EQ_INT(bs_lzss_decompress(NULL, 0, &a, &out, &n), BS_OK);
    BS_CHECK_EQ_INT(n, 0);

    /* All literals: flag 0xFF, 3 bytes (fewer than 8 items is fine). */
    static const uint8_t lits[] = { 0xFF, 'a', 'b', 'c' };
    BS_CHECK_EQ_INT(bs_lzss_decompress(lits, sizeof lits, &a, &out, &n), BS_OK);
    BS_CHECK(n == 3 && memcmp(out, "abc", 3) == 0);

    /* A reference into the untouched ring yields the space fill: pos 0, len 5. */
    static const uint8_t fill[] = { 0x00, 0x00, 0x02 };
    BS_CHECK_EQ_INT(bs_lzss_decompress(fill, sizeof fill, &a, &out, &n), BS_OK);
    BS_CHECK(n == 5 && memcmp(out, "     ", 5) == 0);

    /* Overlapping copy: literal 'x' at ring 0xFEE, then copy from 0xFEE, len 4 -> "xxxxx". */
    static const uint8_t overlap[] = { 0x01, 'x', 0xEE, 0xF1 };
    BS_CHECK_EQ_INT(bs_lzss_decompress(overlap, sizeof overlap, &a, &out, &n), BS_OK);
    BS_CHECK(n == 5 && memcmp(out, "xxxxx", 5) == 0);

    /* Trailing half reference is ignored. */
    static const uint8_t trunc[] = { 0x01, 'q', 0xEE };
    BS_CHECK_EQ_INT(bs_lzss_decompress(trunc, sizeof trunc, &a, &out, &n), BS_OK);
    BS_CHECK(n == 1 && out[0] == 'q');

    bs_arena_free(&a);
}

int main(void)
{
    BS_RUN(test_known_streams);
    BS_RUN(test_round_trips);
    return BS_TEST_RESULT();
}
