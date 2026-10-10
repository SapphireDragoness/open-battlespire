#include "bs/core/reader.h"

#include <string.h>

bs_reader bs_reader_make(const void *data, size_t size)
{
    static const uint8_t empty[1];
    bs_reader r;
    r.data = data ? data : empty; /* never NULL, so data + 0 is well-defined */
    r.size = data ? size : 0;
    r.pos = 0;
    r.err = false;
    return r;
}

static void fail(bs_reader *r)
{
    r->err = true;
    r->pos = r->size;
}

/* Returns a pointer to n bytes and advances, or NULL (and sets err). */
static const uint8_t *take(bs_reader *r, size_t n)
{
    if (r->err || n > r->size - r->pos) {
        fail(r);
        return NULL;
    }
    const uint8_t *p = r->data + r->pos;
    r->pos += n;
    return p;
}

bool bs_reader_seek(bs_reader *r, size_t pos)
{
    if (r->err || pos > r->size) {
        fail(r);
        return false;
    }
    r->pos = pos;
    return true;
}

bool bs_reader_skip(bs_reader *r, size_t n)
{
    return take(r, n) != NULL;
}

uint8_t bs_read_u8(bs_reader *r)
{
    const uint8_t *p = take(r, 1);
    return p ? p[0] : 0;
}

uint16_t bs_read_u16le(bs_reader *r)
{
    const uint8_t *p = take(r, 2);
    if (!p)
        return 0;
    return (uint16_t)(p[0] | (p[1] << 8));
}

uint32_t bs_read_u32le(bs_reader *r)
{
    const uint8_t *p = take(r, 4);
    return p ? (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24) : 0;
}

uint16_t bs_read_u16be(bs_reader *r)
{
    const uint8_t *p = take(r, 2);
    if (!p)
        return 0;
    return (uint16_t)((p[0] << 8) | p[1]);
}

uint32_t bs_read_u32be(bs_reader *r)
{
    const uint8_t *p = take(r, 4);
    return p ? ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3] : 0;
}

/* Two's-complement reinterpretation without implementation-defined conversions. */
static int16_t as_i16(uint16_t v) { int16_t s; memcpy(&s, &v, sizeof s); return s; }
static int32_t as_i32(uint32_t v) { int32_t s; memcpy(&s, &v, sizeof s); return s; }

int8_t bs_read_i8(bs_reader *r)
{
    uint8_t v = bs_read_u8(r);
    int8_t s;
    memcpy(&s, &v, 1);
    return s;
}

int16_t bs_read_i16le(bs_reader *r) { return as_i16(bs_read_u16le(r)); }
int32_t bs_read_i32le(bs_reader *r) { return as_i32(bs_read_u32le(r)); }
int16_t bs_read_i16be(bs_reader *r) { return as_i16(bs_read_u16be(r)); }
int32_t bs_read_i32be(bs_reader *r) { return as_i32(bs_read_u32be(r)); }

bool bs_read_bytes(bs_reader *r, void *dst, size_t n)
{
    const uint8_t *p = take(r, n);
    if (!p) {
        if (n)
            memset(dst, 0, n);
        return false;
    }
    memcpy(dst, p, n);
    return true;
}

const uint8_t *bs_read_span(bs_reader *r, size_t n)
{
    return take(r, n);
}

bool bs_read_fixed_str(bs_reader *r, size_t field_len, char *dst, size_t dst_cap)
{
    if (dst_cap == 0) {
        fail(r);
        return false;
    }
    const uint8_t *p = take(r, field_len);
    if (!p) {
        dst[0] = '\0';
        return false;
    }
    size_t n = 0;
    while (n < field_len && p[n] != 0 && n + 1 < dst_cap) {
        dst[n] = (char)p[n];
        n++;
    }
    dst[n] = '\0';
    return true;
}

bs_reader bs_reader_sub(bs_reader *r, size_t n)
{
    const uint8_t *p = take(r, n);
    bs_reader sub = bs_reader_make(p, p ? n : 0);
    if (!p)
        sub.err = true;
    return sub;
}
