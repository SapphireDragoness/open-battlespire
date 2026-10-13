#include "bs/formats/lzss.h"

#include <string.h>

#define RING_MASK (BS_LZSS_RING_SIZE - 1)
#define RING_START (BS_LZSS_RING_SIZE - BS_LZSS_MAX_MATCH)

/*
 * Shared decoder. With dst == NULL only counts output bytes. The ring buffer
 * is only needed when producing output, since the count does not depend on
 * the data.
 */
static size_t decode(const uint8_t *src, size_t n, uint8_t *dst)
{
    uint8_t ring[BS_LZSS_RING_SIZE];
    if (dst)
        memset(ring, BS_LZSS_FILL, sizeof ring);

    size_t in = 0, out = 0;
    unsigned r = RING_START;
    unsigned flags = 0;

    while (in < n) {
        flags >>= 1;
        if (!(flags & 0x100)) {
            flags = src[in++] | 0xFF00u; /* high byte counts the remaining 8 bits */
            if (in >= n)
                break;
        }
        if (flags & 1) {
            if (dst) {
                uint8_t c = src[in];
                dst[out] = c;
                ring[r] = c;
                r = (r + 1) & RING_MASK;
            }
            in++;
            out++;
        } else {
            if (n - in < 2)
                break; /* incomplete reference at end of input */
            unsigned pos = src[in] | ((src[in + 1] & 0xF0u) << 4);
            unsigned len = (src[in + 1] & 0x0Fu) + BS_LZSS_THRESHOLD + 1;
            in += 2;
            if (dst) {
                for (unsigned k = 0; k < len; k++) {
                    uint8_t c = ring[(pos + k) & RING_MASK];
                    dst[out + k] = c;
                    ring[r] = c;
                    r = (r + 1) & RING_MASK;
                }
            }
            out += len;
        }
    }
    return out;
}

size_t bs_lzss_decompressed_size(const uint8_t *src, size_t n)
{
    return decode(src, n, NULL);
}

bs_result bs_lzss_decompress(const uint8_t *src, size_t n, bs_arena *arena,
                             uint8_t **out, size_t *out_size)
{
    *out = NULL;
    *out_size = 0;
    size_t size = decode(src, n, NULL);
    if (size == SIZE_MAX)
        return BS_ERR_NOMEM;
    uint8_t *buf = bs_arena_alloc(arena, size + 1, 16);
    if (!buf)
        return BS_ERR_NOMEM;
    decode(src, n, buf);
    buf[size] = 0;
    *out = buf;
    *out_size = size;
    return BS_OK;
}
