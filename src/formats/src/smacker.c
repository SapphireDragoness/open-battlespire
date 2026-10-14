#include "bs/formats/smacker.h"

#include <stdlib.h>
#include <string.h>

#include "bs/core/reader.h"

#define HEADER_SIZE 0x68
#define MAX_FRAMES 100000u
#define MAX_DIM 4096u
#define MAX_TREE_DEPTH 32
#define MAX_AUDIO_BYTES (1u << 24)

#define FLAG_RING_FRAME 0x01
#define FLAG_Y_INTERLACED 0x02
#define FLAG_Y_DOUBLED 0x04

#define TRACK_COMPRESSED 0x80000000u
#define TRACK_PRESENT 0x40000000u
#define TRACK_16BIT 0x20000000u
#define TRACK_STEREO 0x10000000u
#define TRACK_RATE_MASK 0x00FFFFFFu

enum { BLOCK_MONO, BLOCK_FULL, BLOCK_SKIP, BLOCK_SOLID };

/* ---- LSB-first bit reader ---- */

typedef struct bits {
    const uint8_t *p;
    size_t size;  /* bytes */
    size_t pos;   /* bits */
    bool err;
} bits;

static bits bits_make(const uint8_t *p, size_t size)
{
    bits b = {p, size, 0, false};
    return b;
}

static uint32_t get_bit(bits *b)
{
    if (b->pos >= b->size * 8) {
        b->err = true;
        return 0;
    }
    uint32_t v = (b->p[b->pos >> 3] >> (b->pos & 7)) & 1u;
    b->pos++;
    return v;
}

static uint32_t get_bits(bits *b, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++)
        v |= get_bit(b) << i;
    return v;
}

/* ---- Huffman trees ----
 * Nodes are stored in an array; a leaf has child[0] < 0. A "big" tree has
 * 16-bit leaves built from two byte trees, plus three escape leaves that act
 * as a cache of recently decoded values. */

typedef struct node {
    int32_t child[2];
    uint32_t value;
} node;

struct bs_smk_tree {
    node *nodes;
    uint32_t count, cap;
    int32_t root;      /* -1: tree absent, every code decodes as 0 */
    int32_t last[3];   /* indices of the cache leaves (big trees) */
};

static int32_t new_node(bs_smk_tree *t)
{
    if (t->count >= t->cap)
        return -1;
    node *n = &t->nodes[t->count];
    n->child[0] = n->child[1] = -1;
    n->value = 0;
    return (int32_t)t->count++;
}

/* Byte tree: bit 1 = branch (first child on bit 0), bit 0 = leaf with an 8-bit value. */
static int32_t read_byte_tree(bits *b, bs_smk_tree *t, int depth)
{
    if (depth > MAX_TREE_DEPTH || b->err)
        return -1;
    int32_t i = new_node(t);
    if (i < 0)
        return -1;
    if (!get_bit(b)) {
        t->nodes[i].value = get_bits(b, 8);
        return i;
    }
    int32_t c0 = read_byte_tree(b, t, depth + 1);
    int32_t c1 = c0 < 0 ? -1 : read_byte_tree(b, t, depth + 1);
    if (c1 < 0)
        return -1;
    t->nodes[i].child[0] = c0;
    t->nodes[i].child[1] = c1;
    return i;
}

static bool alloc_tree(bs_smk_tree *t, uint32_t cap, bs_arena *arena)
{
    memset(t, 0, sizeof *t);
    t->root = -1;
    t->last[0] = t->last[1] = t->last[2] = -1;
    t->cap = cap;
    t->nodes = BS_ARENA_ARRAY(arena, node, cap);
    return t->nodes != NULL;
}

/* A byte tree preceded by its "present" bit and followed by a terminating bit. */
static bool read_optional_byte_tree(bits *b, bs_smk_tree *t, bs_arena *arena)
{
    if (!alloc_tree(t, 512, arena))
        return false;
    if (!get_bit(b))
        return !b->err;
    t->root = read_byte_tree(b, t, 0);
    get_bit(b);
    return t->root >= 0 && !b->err;
}

static uint32_t decode(bits *b, const bs_smk_tree *t)
{
    if (t->root < 0)
        return 0;
    int32_t i = t->root;
    int guard = 0;
    while (t->nodes[i].child[0] >= 0 && guard++ <= MAX_TREE_DEPTH)
        i = t->nodes[i].child[get_bit(b)];
    return t->nodes[i].value;
}

typedef struct big_ctx {
    const bs_smk_tree *lo, *hi;
    uint32_t escape[3];
} big_ctx;

static int32_t read_big_tree(bits *b, bs_smk_tree *t, const big_ctx *c, int depth)
{
    if (depth > MAX_TREE_DEPTH || b->err)
        return -1;
    int32_t i = new_node(t);
    if (i < 0)
        return -1;
    if (!get_bit(b)) {
        uint32_t v = decode(b, c->lo) | decode(b, c->hi) << 8;
        for (int k = 0; k < 3; k++) {
            if (v == c->escape[k]) {
                t->last[k] = i;
                v = 0;
                break;
            }
        }
        t->nodes[i].value = v;
        return i;
    }
    int32_t c0 = read_big_tree(b, t, c, depth + 1);
    int32_t c1 = c0 < 0 ? -1 : read_big_tree(b, t, c, depth + 1);
    if (c1 < 0)
        return -1;
    t->nodes[i].child[0] = c0;
    t->nodes[i].child[1] = c1;
    return i;
}

static bs_smk_tree *read_header_tree(bits *b, uint32_t size_bytes, bs_arena *arena)
{
    bs_smk_tree *t = BS_ARENA_NEW(arena, bs_smk_tree);
    if (!t)
        return NULL;
    /* The header gives the tree size in bytes, 4 per entry; 3 cache slots on top. */
    uint32_t cap = size_bytes / 4 + 4;
    if (cap > (1u << 20))
        return NULL;
    if (!alloc_tree(t, cap, arena))
        return NULL;
    if (!get_bit(b))
        return b->err ? NULL : t; /* absent: decodes as 0 */

    bs_smk_tree lo, hi;
    if (!read_optional_byte_tree(b, &lo, arena) || !read_optional_byte_tree(b, &hi, arena))
        return NULL;
    big_ctx c = {&lo, &hi, {0, 0, 0}};
    for (int k = 0; k < 3; k++)
        c.escape[k] = get_bits(b, 16);
    t->root = read_big_tree(b, t, &c, 0);
    get_bit(b);
    if (t->root < 0 || b->err)
        return NULL;
    for (int k = 0; k < 3; k++) {
        if (t->last[k] < 0 && (t->last[k] = new_node(t)) < 0) /* escape never used: a spare slot */
            return NULL;
    }
    return t;
}

/* Decodes a 16-bit value and updates the recent-value cache. */
static uint32_t decode_big(bits *b, bs_smk_tree *t)
{
    if (t->root < 0)
        return 0;
    uint32_t v = decode(b, t);
    node *n = t->nodes;
    if (v != n[t->last[0]].value) {
        n[t->last[2]].value = n[t->last[1]].value;
        n[t->last[1]].value = n[t->last[0]].value;
        n[t->last[0]].value = v;
    }
    return v;
}

static void reset_cache(bs_smk_tree *t)
{
    if (t->root < 0)
        return;
    for (int k = 0; k < 3; k++)
        t->nodes[t->last[k]].value = 0;
}

/* ---- header ---- */

bs_result bs_smk_open(const uint8_t *data, size_t size, bs_arena *arena, bs_smacker *out)
{
    memset(out, 0, sizeof *out);
    bs_reader r = bs_reader_make(data, size);
    const uint8_t *sig = bs_read_span(&r, 4);
    if (!sig || memcmp(sig, "SMK", 3) != 0)
        return BS_ERR_FORMAT;
    if (sig[3] != '2')
        return BS_ERR_UNSUPPORTED; /* SMK4 has extra block modes; none shipped */
    out->width = bs_read_u32le(&r);
    out->height = bs_read_u32le(&r);
    out->frames = bs_read_u32le(&r);
    int32_t rate = bs_read_i32le(&r);
    out->flags = bs_read_u32le(&r);
    uint32_t audio_size[BS_SMK_TRACKS];
    for (int t = 0; t < BS_SMK_TRACKS; t++)
        audio_size[t] = bs_read_u32le(&r);
    uint32_t trees_size = bs_read_u32le(&r);
    uint32_t mmap_size = bs_read_u32le(&r);
    uint32_t mclr_size = bs_read_u32le(&r);
    uint32_t full_size = bs_read_u32le(&r);
    uint32_t type_size = bs_read_u32le(&r);
    for (int t = 0; t < BS_SMK_TRACKS; t++) {
        uint32_t v = bs_read_u32le(&r);
        bs_smk_track *tr = &out->tracks[t];
        tr->present = (v & TRACK_PRESENT) != 0;
        tr->compressed = (v & TRACK_COMPRESSED) != 0;
        tr->bits16 = (v & TRACK_16BIT) != 0;
        tr->stereo = (v & TRACK_STEREO) != 0;
        tr->rate = v & TRACK_RATE_MASK;
    }
    bs_read_u32le(&r); /* reserved */
    if (!bs_reader_ok(&r))
        return BS_ERR_FORMAT;
    if (out->width == 0 || out->height == 0 || out->width > MAX_DIM || out->height > MAX_DIM ||
        out->width % 4 || out->height % 4 || out->frames == 0 || out->frames > MAX_FRAMES)
        return BS_ERR_FORMAT;
    out->y_mode = (out->flags & FLAG_Y_DOUBLED)      ? BS_SMK_Y_DOUBLED
                  : (out->flags & FLAG_Y_INTERLACED) ? BS_SMK_Y_INTERLACED
                                                     : BS_SMK_Y_NORMAL;
    /* Positive: milliseconds per frame; negative: units of 10 microseconds; 0: 10 fps. */
    out->frame_ms = rate > 0 ? (double)rate : rate < 0 ? -(double)rate / 100.0 : 100.0;

    out->total_frames = out->frames + ((out->flags & FLAG_RING_FRAME) ? 1u : 0u);
    uint32_t n = out->total_frames;
    out->frame_offset = BS_ARENA_ARRAY(arena, uint32_t, n);
    out->frame_size = BS_ARENA_ARRAY(arena, uint32_t, n);
    out->frame_type = BS_ARENA_ARRAY(arena, uint8_t, n);
    if (!out->frame_offset || !out->frame_size || !out->frame_type)
        return BS_ERR_NOMEM;
    for (uint32_t i = 0; i < n; i++)
        out->frame_size[i] = bs_read_u32le(&r) & ~3u; /* low bits: keyframe flags */
    for (uint32_t i = 0; i < n; i++)
        out->frame_type[i] = bs_read_u8(&r);
    const uint8_t *trees = bs_read_span(&r, trees_size);
    if (!trees)
        return BS_ERR_FORMAT;

    bits b = bits_make(trees, trees_size);
    out->mmap = read_header_tree(&b, mmap_size, arena);
    out->mclr = out->mmap ? read_header_tree(&b, mclr_size, arena) : NULL;
    out->full = out->mclr ? read_header_tree(&b, full_size, arena) : NULL;
    out->type = out->full ? read_header_tree(&b, type_size, arena) : NULL;
    if (!out->type)
        return BS_ERR_FORMAT;

    size_t off = r.pos;
    for (uint32_t i = 0; i < n; i++) {
        if (out->frame_size[i] > size - off)
            return BS_ERR_FORMAT;
        out->frame_offset[i] = (uint32_t)off;
        off += out->frame_size[i];
    }

    out->pixels = bs_arena_alloc(arena, (size_t)out->width * out->height, 16);
    if (!out->pixels)
        return BS_ERR_NOMEM;
    /* The header's largest-chunk sizes are only a hint for the first allocation. */
    for (int t = 0; t < BS_SMK_TRACKS; t++)
        out->audio_cap[t] = audio_size[t] < MAX_AUDIO_BYTES ? audio_size[t] : MAX_AUDIO_BYTES;
    out->data = data;
    out->size = size;
    return BS_OK;
}

void bs_smk_close(bs_smacker *s)
{
    for (int t = 0; t < BS_SMK_TRACKS; t++) {
        free(s->audio[t]);
        s->audio[t] = NULL;
    }
}

void bs_smk_rewind(bs_smacker *s)
{
    s->next = 0;
    memset(s->pixels, 0, (size_t)s->width * s->height);
    memset(s->palette, 0, sizeof s->palette);
}

/* ---- frame parts ---- */

static uint8_t pal6(uint8_t v)
{
    v &= 63;
    return (uint8_t)((v << 2) | (v >> 4));
}

static bs_result decode_palette(bs_smacker *s, const uint8_t *p, size_t len)
{
    uint8_t old[768];
    memcpy(old, s->palette, sizeof old);
    uint8_t *pal = s->palette;
    size_t i = 0;
    uint32_t entry = 0;
    while (entry < 256 && i < len) {
        uint8_t c = p[i++];
        if (c & 0x80) {
            entry += (c & 0x7Fu) + 1; /* keep */
        } else if (c & 0x40) {
            if (i >= len)
                return BS_ERR_FORMAT;
            uint32_t src = p[i++], cnt = (c & 0x3Fu) + 1;
            if (src + cnt > 256 || entry + cnt > 256)
                return BS_ERR_FORMAT;
            memcpy(pal + entry * 3, old + src * 3, cnt * 3);
            entry += cnt;
        } else {
            if (i + 2 > len)
                return BS_ERR_FORMAT;
            pal[entry * 3] = pal6(c);
            pal[entry * 3 + 1] = pal6(p[i++]);
            pal[entry * 3 + 2] = pal6(p[i++]);
            entry++;
        }
    }
    return BS_OK;
}

static bool grow_audio(bs_smacker *s, int t, uint32_t bytes)
{
    if (bytes <= s->audio_cap[t] && s->audio[t])
        return true;
    uint32_t cap = bytes > s->audio_cap[t] ? bytes : s->audio_cap[t];
    uint8_t *p = realloc(s->audio[t], cap ? cap : 1);
    if (!p)
        return false;
    s->audio[t] = p;
    s->audio_cap[t] = cap;
    return true;
}

static bs_result decode_audio(bs_smacker *s, int t, const uint8_t *p, size_t len)
{
    const bs_smk_track *tr = &s->tracks[t];
    s->audio_bytes[t] = 0;
    if (!tr->compressed) {
        if (len > MAX_AUDIO_BYTES || !grow_audio(s, t, (uint32_t)len))
            return len > MAX_AUDIO_BYTES ? BS_ERR_FORMAT : BS_ERR_NOMEM;
        memcpy(s->audio[t], p, len);
        s->audio_bytes[t] = (uint32_t)len;
        return BS_OK;
    }
    if (len < 4)
        return BS_ERR_FORMAT;
    uint32_t unpacked = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    if (unpacked > MAX_AUDIO_BYTES)
        return BS_ERR_FORMAT;
    bits b = bits_make(p + 4, len - 4);
    if (!get_bit(&b))
        return BS_OK; /* no data in this frame */
    bool stereo = get_bit(&b), bits16 = get_bit(&b);
    if (stereo != tr->stereo || bits16 != tr->bits16)
        return BS_ERR_FORMAT;
    int ntrees = 1 << ((int)stereo + (int)bits16);

    /* Trees are small; build them on the stack-backed arena-free storage. */
    node storage[4][512];
    bs_smk_tree trees[4];
    for (int k = 0; k < ntrees; k++) {
        bs_smk_tree *h = &trees[k];
        memset(h, 0, sizeof *h);
        h->nodes = storage[k];
        h->cap = 512;
        h->root = -1;
        if (get_bit(&b)) {
            h->root = read_byte_tree(&b, h, 0);
            get_bit(&b);
            if (h->root < 0)
                return BS_ERR_FORMAT;
        }
    }
    if (b.err)
        return BS_ERR_FORMAT;
    if (!grow_audio(s, t, unpacked))
        return BS_ERR_NOMEM;
    uint8_t *out = s->audio[t];
    int ch = stereo ? 2 : 1;

    if (bits16) {
        int32_t pred[2] = {0, 0};
        for (int c = ch - 1; c >= 0; c--) {
            uint32_t v = get_bits(&b, 16);
            v = ((v & 0xFFu) << 8) | (v >> 8); /* stored big-endian */
            pred[c] = (int16_t)v;
        }
        uint32_t count = unpacked / 2, i = 0;
        for (; i < (uint32_t)ch && i < count; i++) {
            out[i * 2] = (uint8_t)pred[i];
            out[i * 2 + 1] = (uint8_t)((uint32_t)pred[i] >> 8);
        }
        for (; i < count && !b.err; i++) {
            int c = stereo ? (int)(i & 1u) : 0;
            uint32_t lo = decode(&b, &trees[c * 2]), hi = decode(&b, &trees[c * 2 + 1]);
            pred[c] = (int16_t)(uint16_t)(pred[c] + (int16_t)(uint16_t)(lo | hi << 8));
            out[i * 2] = (uint8_t)pred[c];
            out[i * 2 + 1] = (uint8_t)((uint32_t)pred[c] >> 8);
        }
    } else {
        int32_t pred[2] = {0, 0};
        for (int c = ch - 1; c >= 0; c--)
            pred[c] = (int32_t)get_bits(&b, 8);
        uint32_t i = 0;
        for (; i < (uint32_t)ch && i < unpacked; i++)
            out[i] = (uint8_t)pred[i];
        for (; i < unpacked && !b.err; i++) {
            int c = stereo ? (int)(i & 1u) : 0;
            pred[c] = (pred[c] + (int8_t)(uint8_t)decode(&b, &trees[c])) & 0xFF;
            out[i] = (uint8_t)pred[c];
        }
    }
    if (b.err)
        return BS_ERR_FORMAT;
    s->audio_bytes[t] = unpacked;
    return BS_OK;
}

static const uint16_t block_runs[64] = {
    1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,  17,  18,  19,   20,   21, 22,
    23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,  39,  40,  41,   42,   43, 44,
    45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 128, 256, 512, 1024, 2048,
};

static bs_result decode_video(bs_smacker *s, const uint8_t *p, size_t len)
{
    bits b = bits_make(p, len);
    reset_cache(s->mmap);
    reset_cache(s->mclr);
    reset_cache(s->full);
    reset_cache(s->type);
    uint32_t bw = s->width / 4, bh = s->height / 4, total = bw * bh, blk = 0;
    size_t stride = s->width;

    while (blk < total) {
        uint32_t type = decode_big(&b, s->type);
        uint32_t run = block_runs[(type >> 2) & 0x3F];
        if (b.err)
            return BS_ERR_FORMAT;
        for (; run > 0 && blk < total; run--, blk++) {
            uint8_t *out = s->pixels + (size_t)(blk / bw) * 4 * stride + (size_t)(blk % bw) * 4;
            switch (type & 3) {
            case BLOCK_MONO: {
                uint32_t clr = decode_big(&b, s->mclr), map = decode_big(&b, s->mmap);
                uint8_t hi = (uint8_t)(clr >> 8), lo = (uint8_t)clr;
                for (int y = 0; y < 4; y++, out += stride)
                    for (int x = 0; x < 4; x++, map >>= 1)
                        out[x] = (map & 1) ? hi : lo;
                break;
            }
            case BLOCK_FULL:
                for (int y = 0; y < 4; y++, out += stride) {
                    uint32_t pix = decode_big(&b, s->full);
                    out[2] = (uint8_t)pix;
                    out[3] = (uint8_t)(pix >> 8);
                    pix = decode_big(&b, s->full);
                    out[0] = (uint8_t)pix;
                    out[1] = (uint8_t)(pix >> 8);
                }
                break;
            case BLOCK_SKIP:
                break;
            case BLOCK_SOLID: {
                uint8_t c = (uint8_t)(type >> 8);
                for (int y = 0; y < 4; y++, out += stride)
                    memset(out, c, 4);
                break;
            }
            }
        }
    }
    return b.err ? BS_ERR_FORMAT : BS_OK;
}

bs_result bs_smk_next_frame(bs_smacker *s)
{
    if (s->next >= s->frames)
        return BS_ERR_NOT_FOUND;
    uint32_t i = s->next;
    const uint8_t *p = s->data + s->frame_offset[i];
    size_t len = s->frame_size[i];
    bs_reader r = bs_reader_make(p, len);
    uint8_t type = s->frame_type[i];
    for (int t = 0; t < BS_SMK_TRACKS; t++)
        s->audio_bytes[t] = 0;

    if (type & 1) {
        size_t plen = (size_t)bs_read_u8(&r) * 4;
        if (plen == 0)
            return BS_ERR_FORMAT;
        const uint8_t *pal = bs_read_span(&r, plen - 1);
        if (!pal)
            return BS_ERR_FORMAT;
        bs_result res = decode_palette(s, pal, plen - 1);
        if (res != BS_OK)
            return res;
    }
    for (int t = 0; t < BS_SMK_TRACKS; t++) {
        if (!(type & (2 << t)))
            continue;
        uint32_t alen = bs_read_u32le(&r);
        if (alen < 4)
            return BS_ERR_FORMAT;
        const uint8_t *audio = bs_read_span(&r, alen - 4);
        if (!audio)
            return BS_ERR_FORMAT;
        bs_result res = decode_audio(s, t, audio, alen - 4);
        if (res != BS_OK)
            return res;
    }
    if (!bs_reader_ok(&r))
        return BS_ERR_FORMAT;
    bs_result res = decode_video(s, p + r.pos, len - r.pos);
    if (res != BS_OK)
        return res;
    s->next++;
    return BS_OK;
}
