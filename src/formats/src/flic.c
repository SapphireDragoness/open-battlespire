#include "bs/formats/flic.h"

#include <string.h>

#include "bs/core/reader.h"

#define HEADER_SIZE 128u
#define CHUNK_PREFIX 0xF100u
#define CHUNK_FRAME 0xF1FAu
#define MAX_DIM 4096u

enum {
    SUB_DELTA_FLC = 7,
    SUB_COLOR_256 = 4,
    SUB_COLOR_64 = 11,
    SUB_DELTA_FLI = 12,
    SUB_BLACK = 13,
    SUB_BYTE_RUN = 15,
    SUB_COPY = 16,
    SUB_PSTAMP = 18
};

bs_result bs_flic_open(const uint8_t *data, size_t size, bs_flic *out)
{
    memset(out, 0, sizeof *out);
    bs_reader r = bs_reader_make(data, size);
    bs_read_u32le(&r); /* file size; not trusted */
    out->magic = bs_read_u16le(&r);
    out->frames = bs_read_u16le(&r);
    out->width = bs_read_u16le(&r);
    out->height = bs_read_u16le(&r);
    uint16_t depth = bs_read_u16le(&r);
    bs_read_u16le(&r); /* flags */
    out->speed = bs_read_u32le(&r);
    if (!bs_reader_ok(&r) || size < HEADER_SIZE)
        return BS_ERR_FORMAT;
    if (out->magic != BS_FLIC_MAGIC_FLC && out->magic != BS_FLIC_MAGIC_FLI)
        return BS_ERR_FORMAT;
    if (depth != 8 && depth != 0)
        return BS_ERR_UNSUPPORTED;
    if (out->width == 0 || out->height == 0 || out->width > MAX_DIM || out->height > MAX_DIM)
        return BS_ERR_FORMAT;

    /* FLC stores the first frame's offset at 80; FLI frames start right after the header. */
    size_t first = HEADER_SIZE;
    if (out->magic == BS_FLIC_MAGIC_FLC) {
        bs_reader_seek(&r, 80);
        uint32_t oframe1 = bs_read_u32le(&r);
        if (oframe1 >= HEADER_SIZE && oframe1 < size)
            first = oframe1;
    }
    /* Skip a prefix chunk if present. */
    bs_reader_seek(&r, first);
    uint32_t csize = bs_read_u32le(&r);
    uint16_t ctype = bs_read_u16le(&r);
    if (bs_reader_ok(&r) && ctype == CHUNK_PREFIX && csize >= 6 && csize <= size - first)
        first += csize;

    out->data = data;
    out->size = size;
    out->first_frame = first;
    return BS_OK;
}

bs_result bs_flic_player_init(bs_flic_player *p, const bs_flic *flic, bs_arena *arena)
{
    memset(p, 0, sizeof *p);
    p->flic = flic;
    p->pos = flic->first_frame;
    p->pixels = bs_arena_alloc(arena, (size_t)flic->width * flic->height, 16);
    return p->pixels ? BS_OK : BS_ERR_NOMEM;
}

static bs_result decode_color(bs_flic_player *p, bs_reader *r, int shift)
{
    uint16_t packets = bs_read_u16le(r);
    unsigned idx = 0;
    for (uint16_t k = 0; k < packets; k++) {
        idx += bs_read_u8(r);
        unsigned count = bs_read_u8(r);
        if (count == 0)
            count = 256;
        if (idx + count > 256)
            return BS_ERR_FORMAT;
        const uint8_t *rgb = bs_read_span(r, (size_t)count * 3);
        if (!rgb)
            return BS_ERR_FORMAT;
        for (unsigned i = 0; i < count * 3; i++)
            p->palette[idx * 3 + i] = (uint8_t)(shift ? (rgb[i] & 63) * 255 / 63 : rgb[i]);
        idx += count;
    }
    return bs_reader_ok(r) ? BS_OK : BS_ERR_FORMAT;
}

static bs_result decode_byte_run(bs_flic_player *p, bs_reader *r)
{
    uint32_t w = p->flic->width;
    for (uint32_t y = 0; y < p->flic->height; y++) {
        uint8_t *line = p->pixels + (size_t)y * w;
        bs_read_u8(r); /* packet count: unreliable for wide images, decode by width instead */
        uint32_t x = 0;
        while (x < w) {
            int8_t count = bs_read_i8(r);
            if (!bs_reader_ok(r))
                return BS_ERR_FORMAT;
            if (count > 0) {
                uint8_t v = bs_read_u8(r);
                if ((uint32_t)count > w - x)
                    return BS_ERR_FORMAT;
                memset(line + x, v, (size_t)count);
                x += (uint32_t)count;
            } else if (count < 0) {
                uint32_t n = (uint32_t)(-(int)count);
                if (n > w - x || !bs_read_bytes(r, line + x, n))
                    return BS_ERR_FORMAT;
                x += n;
            }
        }
    }
    return bs_reader_ok(r) ? BS_OK : BS_ERR_FORMAT;
}

static bs_result decode_delta_fli(bs_flic_player *p, bs_reader *r)
{
    uint32_t w = p->flic->width, h = p->flic->height;
    uint32_t y = bs_read_u16le(r);
    uint32_t lines = bs_read_u16le(r);
    if (y > h || lines > h - y)
        return BS_ERR_FORMAT;
    for (uint32_t l = 0; l < lines; l++, y++) {
        uint8_t *line = p->pixels + (size_t)y * w;
        uint8_t packets = bs_read_u8(r);
        uint32_t x = 0;
        for (uint8_t k = 0; k < packets; k++) {
            x += bs_read_u8(r);
            int8_t count = bs_read_i8(r);
            if (!bs_reader_ok(r) || x > w)
                return BS_ERR_FORMAT;
            if (count > 0) {
                if ((uint32_t)count > w - x || !bs_read_bytes(r, line + x, (size_t)count))
                    return BS_ERR_FORMAT;
                x += (uint32_t)count;
            } else if (count < 0) {
                uint32_t n = (uint32_t)(-(int)count);
                uint8_t v = bs_read_u8(r);
                if (n > w - x)
                    return BS_ERR_FORMAT;
                memset(line + x, v, n);
                x += n;
            }
        }
    }
    return bs_reader_ok(r) ? BS_OK : BS_ERR_FORMAT;
}

static bs_result decode_delta_flc(bs_flic_player *p, bs_reader *r)
{
    uint32_t w = p->flic->width, h = p->flic->height;
    uint32_t lines = bs_read_u16le(r);
    uint32_t y = 0;
    for (uint32_t l = 0; l < lines; l++) {
        /* Opcode words: skip lines (0xC000), set last pixel (0x8000), or packet count. */
        uint16_t op;
        for (;;) {
            op = bs_read_u16le(r);
            if (!bs_reader_ok(r))
                return BS_ERR_FORMAT;
            if ((op & 0xC000) == 0xC000) {
                uint32_t skip = 0x10000u - op;
                if (skip > h - y)
                    return BS_ERR_FORMAT;
                y += skip;
            } else if ((op & 0xC000) == 0x8000) {
                if (y >= h)
                    return BS_ERR_FORMAT;
                p->pixels[(size_t)y * w + w - 1] = (uint8_t)op;
            } else {
                break;
            }
        }
        if (y >= h)
            return BS_ERR_FORMAT;
        uint8_t *line = p->pixels + (size_t)y * w;
        uint32_t x = 0;
        for (uint16_t k = 0; k < op; k++) {
            x += bs_read_u8(r);
            int8_t count = bs_read_i8(r);
            if (!bs_reader_ok(r) || x > w)
                return BS_ERR_FORMAT;
            if (count > 0) {
                uint32_t n = 2u * (uint32_t)count;
                if (n > w - x || !bs_read_bytes(r, line + x, n))
                    return BS_ERR_FORMAT;
                x += n;
            } else if (count < 0) {
                uint32_t n = 2u * (uint32_t)(-(int)count);
                uint8_t a = bs_read_u8(r), b = bs_read_u8(r);
                if (n > w - x)
                    return BS_ERR_FORMAT;
                for (uint32_t i = 0; i < n; i += 2) {
                    line[x + i] = a;
                    line[x + i + 1] = b;
                }
                x += n;
            }
        }
        y++;
    }
    return bs_reader_ok(r) ? BS_OK : BS_ERR_FORMAT;
}

static bs_result decode_frame(bs_flic_player *p, bs_reader *frame)
{
    uint16_t subchunks = bs_read_u16le(frame);
    bs_reader_skip(frame, 8); /* delay, reserved, width/height overrides */
    size_t npix = (size_t)p->flic->width * p->flic->height;
    for (uint16_t i = 0; i < subchunks; i++) {
        uint32_t size = bs_read_u32le(frame);
        uint16_t type = bs_read_u16le(frame);
        if (!bs_reader_ok(frame) || size < 6)
            return BS_ERR_FORMAT;
        bs_reader body = bs_reader_sub(frame, size - 6);
        if (!bs_reader_ok(frame))
            return BS_ERR_FORMAT;
        bs_result res = BS_OK;
        switch (type) {
        case SUB_COLOR_256: res = decode_color(p, &body, 0); break;
        case SUB_COLOR_64:  res = decode_color(p, &body, 1); break;
        case SUB_BYTE_RUN:  res = decode_byte_run(p, &body); break;
        case SUB_DELTA_FLI: res = decode_delta_fli(p, &body); break;
        case SUB_DELTA_FLC: res = decode_delta_flc(p, &body); break;
        case SUB_BLACK:     memset(p->pixels, 0, npix); break;
        case SUB_COPY:
            if (!bs_read_bytes(&body, p->pixels, npix))
                res = BS_ERR_FORMAT;
            break;
        default: break; /* PSTAMP and unknown chunks */
        }
        if (res != BS_OK)
            return res;
    }
    return BS_OK;
}

bs_result bs_flic_next_frame(bs_flic_player *p, bool *ended)
{
    const bs_flic *f = p->flic;
    *ended = false;
    for (;;) {
        if (f->size - p->pos < 6) {
            *ended = true;
            return BS_OK;
        }
        bs_reader r = bs_reader_make(f->data + p->pos, f->size - p->pos);
        uint32_t size = bs_read_u32le(&r);
        uint16_t type = bs_read_u16le(&r);
        if (size < 6 || size > f->size - p->pos)
            return BS_ERR_FORMAT;
        bs_reader body = bs_reader_sub(&r, size - 6);
        p->pos += size;
        if (type != CHUNK_FRAME)
            continue; /* skip non-frame chunks */
        bs_result res = decode_frame(p, &body);
        if (res == BS_OK)
            p->frame++;
        return res;
    }
}
