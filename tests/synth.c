#include "synth.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Little-endian writer over a fixed buffer; sets ok = 0 on overflow. */
typedef struct w {
    uint8_t *p;
    size_t len, cap;
    int ok;
} w;

static void put(w *b, const void *src, size_t n)
{
    if (!b->ok || b->len + n > b->cap) {
        b->ok = 0;
        return;
    }
    memcpy(b->p + b->len, src, n);
    b->len += n;
}

static void u8(w *b, uint8_t v) { put(b, &v, 1); }
static void u16(w *b, uint16_t v) { u8(b, (uint8_t)v); u8(b, (uint8_t)(v >> 8)); }
static void u32(w *b, uint32_t v) { u16(b, (uint16_t)v); u16(b, (uint16_t)(v >> 16)); }

static void fill(w *b, uint8_t v, size_t n)
{
    for (size_t i = 0; i < n; i++)
        u8(b, v);
}

static void pad_to(w *b, size_t off)
{
    if (off > b->len)
        fill(b, 0, off - b->len);
}

static void name12(w *b, const char *name)
{
    char field[12] = { 0 };
    memcpy(field, name, strlen(name));
    put(b, field, 12);
}

static size_t done(const w *b)
{
    return b->ok ? b->len : 0;
}

/* ---- BSA ---- */

const uint8_t synth_comp_data[4] = { 0x01, 'a', 0x00, 0x02 }; /* literal 'a', ref pos 0 len 5 */

size_t synth_bsa(uint8_t *out, size_t cap)
{
    w b = { out, 0, cap, 1 };
    u16(&b, 2);
    u16(&b, 0x0100);
    put(&b, "hello", 5);
    put(&b, synth_comp_data, sizeof synth_comp_data);
    name12(&b, "HELLO.TXT");
    u16(&b, 0);
    u32(&b, 5);
    name12(&b, "COMP.3D");
    u16(&b, 0x0100);
    u32(&b, sizeof synth_comp_data);
    return done(&b);
}

size_t synth_bsa_numbered(uint8_t *out, size_t cap)
{
    w b = { out, 0, cap, 1 };
    u16(&b, 2);
    u16(&b, 0x0200);
    put(&b, "RIFFxyz", 7);
    u16(&b, 102);
    u16(&b, 0);
    u32(&b, 4);
    u16(&b, 507);
    u16(&b, 0);
    u32(&b, 3);
    return done(&b);
}

/* ---- ISO 9660 ---- */

static void put32_both(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
        p[7 - i] = (uint8_t)(v >> (8 * i));
    }
}

static size_t dir_record(uint8_t *p, uint32_t lba, uint32_t size, int is_dir, const char *name, size_t name_len)
{
    size_t len = 33 + name_len;
    len += len & 1;
    memset(p, 0, len);
    p[0] = (uint8_t)len;
    put32_both(p + 2, lba);
    put32_both(p + 10, size);
    p[25] = is_dir ? 0x02 : 0x00;
    p[32] = (uint8_t)name_len;
    memcpy(p + 33, name, name_len);
    return len;
}

size_t synth_iso(uint8_t *iso, size_t cap)
{
    if (cap < SYNTH_ISO_SIZE)
        return 0;
    memset(iso, 0, SYNTH_ISO_SIZE);
    uint8_t *pvd = iso + 16 * 2048;
    pvd[0] = 1;
    memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    memset(pvd + 40, ' ', 32);
    memcpy(pvd + 40, "TESTVOL", 7);
    dir_record(pvd + 156, 18, 2048, 1, "\0", 1);

    uint8_t *root = iso + 18 * 2048;
    size_t p = 0;
    p += dir_record(root + p, 18, 2048, 1, "\0", 1);
    p += dir_record(root + p, 18, 2048, 1, "\1", 1);
    p += dir_record(root + p, 20, 11, 0, "HELLO.TXT;1", 11);
    dir_record(root + p, 19, 2048, 1, "SUBDIR", 6);

    uint8_t *sub = iso + 19 * 2048;
    p = 0;
    p += dir_record(sub + p, 19, 2048, 1, "\0", 1);
    p += dir_record(sub + p, 18, 2048, 1, "\1", 1);
    dir_record(sub + p, 21, SYNTH_ISO_BIG_SIZE, 0, "BIG.BIN;1", 9);

    memcpy(iso + 20 * 2048, "hello world", 11);
    for (int i = 0; i < SYNTH_ISO_BIG_SIZE; i++)
        iso[21 * 2048 + i] = (uint8_t)(i * 7);
    return SYNTH_ISO_SIZE;
}

size_t synth_raw_mode2(const uint8_t *iso, size_t iso_size, uint8_t *out, size_t cap)
{
    size_t sectors = iso_size / 2048;
    if (cap < sectors * 2352)
        return 0;
    for (size_t s = 0; s < sectors; s++) {
        uint8_t *sector = out + s * 2352;
        memset(sector, 0xEE, 2352); /* junk where EDC/ECC would be */
        memset(sector, 0xFF, 12);
        sector[0] = 0x00;
        sector[11] = 0x00;
        sector[12] = 0x00;
        sector[13] = 0x02;
        sector[14] = (uint8_t)s;
        sector[15] = 0x02; /* mode 2 */
        memset(sector + 16, 0, 8);
        memcpy(sector + 24, iso + s * 2048, 2048);
    }
    return sectors * 2352;
}

/* ---- TEXT.RSC ---- */

size_t synth_rsc(uint8_t *out, size_t cap)
{
    static const uint8_t strings[] = {
        'H', 'i', 0xFD, 't', 'h', 'e', 'r', 'e', 0xFE,
        'a', 0xFA, 0x00, 'b', 0xFE,
    };
    static const uint16_t ids[] = { 5, 9, 12, 0xFFFF };
    static const uint32_t rel[] = { 0, 9, 9, sizeof strings };
    const uint16_t table = (uint16_t)(sizeof ids / sizeof ids[0] * 6);
    w b = { out, 0, cap, 1 };
    u16(&b, table);
    for (size_t i = 0; i < 4; i++) {
        u16(&b, ids[i]);
        u32(&b, 2u + table + rel[i]);
    }
    put(&b, strings, sizeof strings);
    return done(&b);
}

/* ---- BSI ---- */

static void be32(w *b, uint32_t v)
{
    u8(b, (uint8_t)(v >> 24));
    u8(b, (uint8_t)(v >> 16));
    u8(b, (uint8_t)(v >> 8));
    u8(b, (uint8_t)v);
}

static void chunk(w *b, const char *tag, uint32_t len)
{
    put(b, tag, 4);
    be32(b, len);
}

static void bsi_header(w *b, uint16_t width, uint16_t height, uint16_t frames, uint16_t enc, int16_t f4)
{
    chunk(b, "IFHD", 44);
    fill(b, 0, 44);
    chunk(b, "BHDR", 26);
    const int16_t f[13] = { 0, 0, (int16_t)width, (int16_t)height, f4, 0, 0, (int16_t)frames, 71, 0, 0, 256,
                            (int16_t)enc };
    for (int i = 0; i < 13; i++)
        u16(b, (uint16_t)f[i]);
}

static void bsi_tables(w *b, uint16_t hicl1)
{
    chunk(b, "HICL", 256);
    u16(b, 0);
    u16(b, hicl1);
    fill(b, 0, 252);
    chunk(b, "CMAP", 768);
    fill(b, 0x3F, 768);
}

size_t synth_bsi_single(uint8_t *out, size_t cap)
{
    w b = { out, 0, cap, 1 };
    chunk(&b, "IFHD", 44);
    fill(&b, 0, 44);
    chunk(&b, "NAME", 4);
    put(&b, "ONE", 4);
    chunk(&b, "BHDR", 26);
    const int16_t f[13] = { 5, 7, 2, 2, 3, 0, 0, 1, 71, 0, 0, 256, 0 };
    for (int i = 0; i < 13; i++)
        u16(&b, (uint16_t)f[i]);
    bsi_tables(&b, 0xF800);
    chunk(&b, "DATA", 4);
    put(&b, "\x00\x02\x02\x00", 4);
    chunk(&b, "END ", 0);
    return done(&b);
}

size_t synth_bsi_container(uint8_t *out, size_t cap)
{
    w b = { out, 0, cap, 1 };
    put(&b, "BSIF", 4);
    put(&b, "\x28\xe0\x46\x00", 4); /* junk, as in the shipped files */

    bsi_header(&b, 1, 1, 1, 0, 2);
    bsi_tables(&b, 0x07E0);
    chunk(&b, "DATA", 1);
    u8(&b, 4);

    /* Offsets: table 4 rows * 4 = 16; RLE row A at 16 (2 bytes); raw row at 18 (4 bytes);
       RLE row B at 22 (5 bytes); total 27. */
    bsi_header(&b, 4, 2, 2, 6, 2);
    bsi_tables(&b, 0x001F);
    chunk(&b, "DATA", 27);
    u32(&b, 0x80000000u | 16);
    u32(&b, 18);
    u32(&b, 0x80000000u | 22);
    u32(&b, 18);
    put(&b, "\x84\x02", 2);
    put(&b, "\x00\x02\x04\x06", 4);
    put(&b, "\x02\x08\x0a\x82\x00", 5);

    chunk(&b, "END ", 0x454E4420u); /* garbage length */
    return done(&b);
}

/* ---- Font ---- */

size_t synth_font(uint8_t *out, size_t cap)
{
    w b = { out, 0, cap, 1 };
    const uint32_t pixels = 4 + 3 * 4 + 3 * 4, size = pixels + 4;
    u32(&b, 3);
    u16(&b, 2); u16(&b, 1);
    u16(&b, 1); u16(&b, 1);
    u16(&b, 1); u16(&b, 1);
    u32(&b, pixels);
    u32(&b, pixels);
    u32(&b, size);
    u16(&b, 0x7FFF);
    u16(&b, 0x0000);
    return done(&b);
}

/* ---- FLIC ---- */

static void flc_chunk(w *b, uint32_t size, uint16_t type)
{
    u32(b, size);
    u16(b, type);
}

size_t synth_flc(uint8_t *out, size_t cap)
{
    w b = { out, 0, cap, 1 };
    /* Header (128 bytes). */
    u32(&b, 0); /* file size: patched below */
    u16(&b, 0xAF12);
    u16(&b, 2);   /* frames */
    u16(&b, 4);
    u16(&b, 2);
    u16(&b, 8);   /* depth */
    u16(&b, 3);   /* flags */
    u32(&b, 70);  /* ms per frame */
    pad_to(&b, 80);
    u32(&b, 128); /* first frame offset */
    pad_to(&b, 128);

    /* Frame 1: COLOR_256 (1 packet: skip 0, 3 colours) + BYTE_RUN. */
    const uint32_t color = 6 + 2 + 2 + 9, brun = 6 + (1 + 2) + (1 + 1 + 4);
    flc_chunk(&b, 16 + color + brun, 0xF1FA);
    u16(&b, 2);
    fill(&b, 0, 8);
    flc_chunk(&b, color, 4);
    u16(&b, 1);
    u8(&b, 0);
    u8(&b, 3);
    put(&b, "\x00\x00\x00\xff\x00\x00\x00\xff\x00", 9);
    flc_chunk(&b, brun, 15);
    put(&b, "\x01\x04\x01", 3);          /* row 0: run of 4 x 1 */
    put(&b, "\x01\xfc\x00\x01\x00\x01", 6); /* row 1: 4 literals */

    /* Frame 2: DELTA_FLC: 1 line, skip 1 line (0xFFFF), 1 packet: column 2, 1 word {2,2}. */
    const uint32_t d2 = 6 + 2 + 2 + 2 + 2 + 2;
    flc_chunk(&b, 16 + d2, 0xF1FA);
    u16(&b, 1);
    fill(&b, 0, 8);
    flc_chunk(&b, d2, 7);
    u16(&b, 1);
    u16(&b, 0xFFFF);
    u16(&b, 1);
    put(&b, "\x02\x01\x02\x02", 4);

    /* Ring frame: same shape, repeat word {0,1} once at column 2. */
    flc_chunk(&b, 16 + d2, 0xF1FA);
    u16(&b, 1);
    fill(&b, 0, 8);
    flc_chunk(&b, d2, 7);
    u16(&b, 1);
    u16(&b, 0xFFFF);
    u16(&b, 1);
    put(&b, "\x02\xff\x00\x01", 4);

    if (b.ok) {
        uint32_t n = (uint32_t)b.len;
        out[0] = (uint8_t)n;
        out[1] = (uint8_t)(n >> 8);
        out[2] = (uint8_t)(n >> 16);
        out[3] = (uint8_t)(n >> 24);
    }
    return done(&b);
}

/* ---- LE executable ---- */

#define LE_OFF 0x40u
#define PAGE 512u

/* Fixup record: internal target, 32-bit target offset unless noted. */
static void fix_off32(w *b, int16_t srcoff, uint8_t obj, uint32_t target)
{
    u8(b, 0x07);
    u8(b, 0x10);
    u16(b, (uint16_t)srcoff);
    u8(b, obj);
    u32(b, target);
}

size_t synth_le(uint8_t *out, size_t cap)
{
    w b = { out, 0, cap, 1 };

    /* MZ stub. */
    put(&b, "MZ", 2);
    pad_to(&b, 0x3C);
    u32(&b, LE_OFF);
    pad_to(&b, LE_OFF);

    /* Table offsets, relative to the LE header. */
    const uint32_t obj_table = 0xB0, page_map = obj_table + 2 * 24, fix_pages = page_map + 3 * 4;
    const uint32_t fix_recs = fix_pages + 4 * 4;

    /* Fixup records, built first to know their offsets per page. */
    uint8_t recs[256];
    w r = { recs, 0, sizeof recs, 1 };
    uint32_t page_start[4];
    page_start[0] = 0;
    fix_off32(&r, 0x10, 2, 0x20);
    u8(&r, 0x08);                 /* rel32, 16-bit target offset */
    u8(&r, 0x00);
    u16(&r, 0x20);
    u8(&r, 1);
    u16(&r, 0x100);
    u8(&r, 0x27);                 /* offset32 with source list */
    u8(&r, 0x14);                 /* 32-bit target, additive (16-bit) */
    u8(&r, 2);                    /* count */
    u8(&r, 2);                    /* object 2 */
    u32(&r, 0);
    u16(&r, 5);                   /* additive */
    u16(&r, 0x30);
    u16(&r, 0x40);
    fix_off32(&r, 0x1FE, 1, 0x44);
    page_start[1] = (uint32_t)r.len;
    fix_off32(&r, -2, 1, 0x44);   /* same fixup, from the second page */
    u8(&r, 0x02);                 /* selector */
    u8(&r, 0x00);
    u16(&r, 0x10);
    u8(&r, 2);
    page_start[2] = (uint32_t)r.len;
    fix_off32(&r, 0x00, 1, 0x1);
    page_start[3] = (uint32_t)r.len;
    if (!r.ok)
        return 0;

    const uint32_t data_pages = (uint32_t)((LE_OFF + fix_recs + r.len + 15) & ~15u); /* file-relative */

    /* LE header. */
    put(&b, "LE\0\0", 4);
    pad_to(&b, LE_OFF + 0x08);
    u16(&b, 2);                   /* cpu 386 */
    u16(&b, 1);                   /* os */
    pad_to(&b, LE_OFF + 0x14);
    u32(&b, 3);                   /* pages */
    u32(&b, 1);                   /* eip object */
    u32(&b, 8);                   /* eip */
    u32(&b, 2);                   /* esp object */
    u32(&b, 0x1000);              /* esp */
    u32(&b, PAGE);
    u32(&b, 0x100);               /* last page size */
    pad_to(&b, LE_OFF + 0x40);
    u32(&b, obj_table);
    u32(&b, 2);
    u32(&b, page_map);
    pad_to(&b, LE_OFF + 0x68);
    u32(&b, fix_pages);
    u32(&b, fix_recs);
    pad_to(&b, LE_OFF + 0x80);
    u32(&b, data_pages);
    pad_to(&b, LE_OFF + obj_table);

    /* Object table: virtual size, base, flags, page index, page count, reserved. */
    u32(&b, 2 * PAGE); u32(&b, 0x10000); u32(&b, 0x2005); u32(&b, 1); u32(&b, 2); u32(&b, 0);
    u32(&b, 0x1000);   u32(&b, 0x20000); u32(&b, 0x2003); u32(&b, 3); u32(&b, 1); u32(&b, 0);

    /* Page map: 24-bit big-endian page number, type 0. */
    for (uint8_t i = 1; i <= 3; i++) {
        u8(&b, 0);
        u8(&b, 0);
        u8(&b, i);
        u8(&b, 0);
    }
    for (int i = 0; i < 4; i++)
        u32(&b, page_start[i]);
    put(&b, recs, r.len);

    pad_to(&b, data_pages);
    fill(&b, 0xA1, PAGE);
    fill(&b, 0xA2, PAGE);
    fill(&b, 0xB0, 0x100);
    return done(&b);
}

/* ---- 3D models ---- */

uint32_t synth_base40(const char *name)
{
    uint32_t v = 0;
    size_t len = strlen(name);
    for (size_t i = 0; i < 6; i++) {
        char c = i < len ? name[i] : '\0';
        uint32_t d = 39; /* padding */
        if (c >= '0' && c <= '9')
            d = (uint32_t)(c - '0');
        else if (c >= 'A' && c <= 'Z')
            d = (uint32_t)(c - 'A') + 10;
        else if (c == '_')
            d = 37;
        v = v * 40 + d;
    }
    return v;
}

static void model_vertex(w *b, uint32_t point, int16_t u, int16_t v)
{
    u32(b, point * 12);
    u16(b, (uint16_t)u);
    u16(b, (uint16_t)v);
}

size_t synth_model(uint8_t *out, size_t cap)
{
    enum { POINTS = 64, FACES = POINTS + 48, NORMALS = FACES + 110, PLANE_DATA = NORMALS + 36,
           OBJECT_DATA = PLANE_DATA + 72 };
    w b = { out, 0, cap, 1 };
    put(&b, "v2.7", 4);
    u32(&b, 4);     /* points */
    u32(&b, 3);     /* faces */
    u32(&b, 100);   /* radius */
    fill(&b, 0, 8);
    u32(&b, PLANE_DATA);
    u32(&b, OBJECT_DATA);
    u32(&b, 0);     /* object data count */
    u32(&b, 0);
    fill(&b, 0, 8);
    u32(&b, POINTS);
    u32(&b, NORMALS);
    u32(&b, 0);
    u32(&b, FACES);

    static const int32_t pts[4][3] = { { 0, 0, 0 }, { 512, 0, 0 }, { 512, 512, 0 }, { 0, 512, 0 } };
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 3; k++)
            u32(&b, (uint32_t)pts[i][k]);

    /* Face 0: textured quad; the 4th vertex's stored UV is ignored. */
    u8(&b, 4);
    u8(&b, 6);
    u32(&b, synth_base40("WALL96"));
    u32(&b, 0);
    model_vertex(&b, 0, 16, 32);
    model_vertex(&b, 1, 160, 0);
    model_vertex(&b, 2, 0, 160);
    model_vertex(&b, 3, 999, 999);
    /* Face 1: solid red triangle. */
    u8(&b, 3);
    u8(&b, 0);
    u32(&b, 0xFFFFF800u);
    u32(&b, 0);
    model_vertex(&b, 0, 0, 0);
    model_vertex(&b, 1, 0, 0);
    model_vertex(&b, 2, 0, 0);
    /* Face 2: invisible triangle. */
    u8(&b, 3);
    u8(&b, 0);
    u32(&b, 0xFFFFFFFEu);
    u32(&b, 0);
    model_vertex(&b, 0, 0, 0);
    model_vertex(&b, 2, 0, 0);
    model_vertex(&b, 3, 0, 0);

    for (int i = 0; i < 3; i++) {
        u32(&b, 0);
        u32(&b, 0);
        u32(&b, 256);
    }
    fill(&b, 0, 72); /* plane data */
    return done(&b);
}

/* ---- BS6 levels ---- */

static size_t le_chunk_begin(w *b, const char *tag)
{
    put(b, tag, 4);
    u32(b, 0);
    return b->len;
}

static void le_chunk_end(w *b, size_t body)
{
    if (!b->ok)
        return;
    uint32_t len = (uint32_t)(b->len - body);
    for (int i = 0; i < 4; i++)
        b->p[body - 4 + (size_t)i] = (uint8_t)(len >> (8 * i));
}

static void le_i32s(w *b, const char *tag, const int32_t *v, int n)
{
    put(b, tag, 4);
    u32(b, (uint32_t)(n * 4));
    for (int i = 0; i < n; i++)
        u32(b, (uint32_t)v[i]);
}

static void le_i32(w *b, const char *tag, int32_t v)
{
    le_i32s(b, tag, &v, 1);
}

static void le_name(w *b, const char *tag, const char *name)
{
    put(b, tag, 4);
    u32(b, 260);
    size_t n = strlen(name);
    put(b, name, n);
    fill(b, 0, 260 - n);
}

size_t synth_bs6(uint8_t *out, size_t cap)
{
    w b = { out, 0, cap, 1 };
    size_t gnrl = le_chunk_begin(&b, "GNRL");
    static const int32_t bbox[6] = { -100, -50, -100, 100, 50, 100 };
    le_i32s(&b, "BBOX", bbox, 6);
    le_i32(&b, "WATR", 91);

    size_t view = le_chunk_begin(&b, "VIEW");
    le_i32(&b, "IDNB", 0);
    le_i32(&b, "IDTY", 6);
    static const int32_t vpos[3] = { 1, 2, 3 }, vang[3] = { 256, 0, 0 };
    le_i32s(&b, "POSI", vpos, 3);
    le_i32s(&b, "ANGS", vang, 3);
    le_i32(&b, "AMBI", 16000);
    le_chunk_end(&b, view);

    size_t objs = le_chunk_begin(&b, "OBJS");
    le_name(&b, "DIRN", "e:\\projects\\batspire\\art\\3d_objs");
    put(&b, "LFIL", 4);
    u32(&b, 520);
    put(&b, "box", 3);
    fill(&b, 0, 257);
    put(&b, "rug", 3);
    fill(&b, 0, 257);
    size_t obj = le_chunk_begin(&b, "OBJD");
    le_i32(&b, "IDNB", 7);
    le_i32(&b, "IDFI", 1);
    static const int32_t opos[3] = { 10, 20, 30 }, oang[3] = { 512, 0, 0 };
    le_i32s(&b, "POSI", opos, 3);
    le_i32s(&b, "ANGS", oang, 3);
    le_i32(&b, "SELE", 0);
    size_t ctrl = le_chunk_begin(&b, "CTRL");
    le_i32(&b, "IDNB", 5);
    put(&b, "RAWD", 4);
    u32(&b, 8);
    fill(&b, 0xAB, 8);
    le_chunk_end(&b, ctrl);
    le_chunk_end(&b, obj);
    obj = le_chunk_begin(&b, "OBJD");
    le_i32(&b, "IDNB", 8);
    le_i32(&b, "IDFI", 0);
    static const int32_t opos2[3] = { -5, 0, 5 }, oang2[3] = { 0, 1024, 0 };
    le_i32s(&b, "POSI", opos2, 3);
    le_i32s(&b, "ANGS", oang2, 3);
    le_chunk_end(&b, obj);
    le_chunk_end(&b, objs);

    size_t lits = le_chunk_begin(&b, "LITS");
    size_t lit = le_chunk_begin(&b, "LITD");
    le_i32(&b, "IDNB", 1);
    le_i32(&b, "IDTY", 0);
    static const int32_t lpos[3] = { 0, -40, 0 };
    le_i32s(&b, "POSI", lpos, 3);
    le_i32(&b, "RADI", 300);
    le_i32(&b, "BRIT", 40);
    le_i32(&b, "SELE", 0);
    le_chunk_end(&b, lit);
    le_chunk_end(&b, lits);

    size_t flas = le_chunk_begin(&b, "FLAS");
    size_t flat = le_chunk_begin(&b, "FLAD");
    le_i32(&b, "IDNB", 2);
    le_name(&b, "FILN", "start");
    static const int32_t fpos[3] = { 4, 5, 6 };
    le_i32s(&b, "POSI", fpos, 3);
    le_i32(&b, "SCAL", 0);
    le_i32(&b, "AMBI", 0);
    le_i32(&b, "SELE", 0);
    le_chunk_end(&b, flat);
    le_chunk_end(&b, flas);

    le_chunk_end(&b, gnrl);
    return done(&b);
}

static void le_ctrl(w *b, int32_t type, const int32_t *v, int n, const char *opts)
{
    size_t c = le_chunk_begin(b, "CTRL");
    le_i32(b, "IDNB", type);
    put(b, "RAWD", 4);
    bool move = type == 261 || type == 512;
    u32(b, move ? 32u : 24u);
    for (int i = 0; i < (move ? 4 : 6); i++)
        u32(b, (uint32_t)(i < n ? v[i] : 0));
    if (move) {
        char text[16] = {0};
        if (opts)
            memcpy(text, opts, strlen(opts));
        put(b, text, 16);
    }
    le_chunk_end(b, c);
}

static void le_link(w *b, int32_t type, int32_t target)
{
    size_t c = le_chunk_begin(b, "LINK");
    le_i32(b, "IDTY", type);
    le_i32(b, "IDNB", target);
    le_chunk_end(b, c);
}

static size_t le_object_begin(w *b, int32_t id, int32_t x)
{
    size_t o = le_chunk_begin(b, "OBJD");
    le_i32(b, "IDNB", id);
    le_i32(b, "IDFI", 0);
    const int32_t pos[3] = { x, 0, 0 }, ang[3] = { 0, 0, 0 };
    le_i32s(b, "POSI", pos, 3);
    le_i32s(b, "ANGS", ang, 3);
    le_i32(b, "SELE", 0);
    return o;
}

size_t synth_bs6_logic(uint8_t *out, size_t cap)
{
    w b = { out, 0, cap, 1 };
    size_t gnrl = le_chunk_begin(&b, "GNRL");
    size_t objs = le_chunk_begin(&b, "OBJS");
    put(&b, "LFIL", 4);
    u32(&b, 520);
    put(&b, "box", 3);
    fill(&b, 0, 257);
    put(&b, "rug", 3);
    fill(&b, 0, 257);

    size_t o = le_object_begin(&b, 10, 0);
    le_ctrl(&b, 16, NULL, 0, NULL);
    size_t st = le_chunk_begin(&b, "STRU");
    le_i32(&b, "IDNB", 121);
    put(&b, "RAWD", 4);
    u32(&b, 8);
    u32(&b, 72);
    u32(&b, 1);
    le_chunk_end(&b, st);
    le_link(&b, 0, 11);
    le_chunk_end(&b, o);

    static const int32_t door[] = { 0, -512, 0, 1000 }, lift[] = { 0, -500, 0, 2000 },
                         wheel[] = { 0, 2047, 0, 500 }, a[] = { 10, 0, 0, 100 }, bb[] = { 0, 10, 0, 100 },
                         once[] = { 0, 0, 50, 100 }, timer[] = { 0, 300 }, tele[] = { 50, -20, 30, 0 };
    o = le_object_begin(&b, 11, 100);
    le_ctrl(&b, 512, door, 4, "r");
    le_chunk_end(&b, o);
    o = le_object_begin(&b, 12, 200);
    le_ctrl(&b, 5, NULL, 0, NULL);
    le_ctrl(&b, 261, lift, 4, "r");
    le_chunk_end(&b, o);
    o = le_object_begin(&b, 13, 300);
    le_ctrl(&b, 16, NULL, 0, NULL);
    le_ctrl(&b, 512, wheel, 4, "c");
    le_chunk_end(&b, o);
    o = le_object_begin(&b, 14, 400);
    le_ctrl(&b, 5, NULL, 0, NULL);
    le_link(&b, 2, 20);
    le_chunk_end(&b, o);
    o = le_object_begin(&b, 15, 500);
    le_ctrl(&b, 261, a, 4, "");
    le_ctrl(&b, 149, timer, 2, NULL);
    le_link(&b, 0, 16);
    le_chunk_end(&b, o);
    o = le_object_begin(&b, 16, 600);
    le_ctrl(&b, 261, bb, 4, "");
    le_ctrl(&b, 149, timer, 2, NULL);
    le_link(&b, 0, 15);
    le_chunk_end(&b, o);
    o = le_object_begin(&b, 17, 700);
    le_ctrl(&b, 16, NULL, 0, NULL);
    le_ctrl(&b, 261, once, 4, "");
    le_chunk_end(&b, o);
    le_chunk_end(&b, objs);

    size_t flas = le_chunk_begin(&b, "FLAS");
    size_t flat = le_chunk_begin(&b, "FLAD");
    le_i32(&b, "IDNB", 20);
    le_name(&b, "FILN", "user");
    static const int32_t upos[3] = { 1000, 0, 0 }, spos[3] = { 0, -100, 0 };
    le_i32s(&b, "POSI", upos, 3);
    le_i32(&b, "SCAL", 0);
    le_i32(&b, "AMBI", 0);
    le_i32(&b, "SELE", 0);
    le_ctrl(&b, 261, tele, 4, "");
    le_chunk_end(&b, flat);
    flat = le_chunk_begin(&b, "FLAD");
    le_i32(&b, "IDNB", 21);
    le_name(&b, "FILN", "start");
    le_i32s(&b, "POSI", spos, 3);
    le_i32(&b, "SCAL", 0);
    le_i32(&b, "AMBI", 0);
    le_i32(&b, "SELE", 0);
    le_chunk_end(&b, flat);
    le_chunk_end(&b, flas);
    le_chunk_end(&b, gnrl);
    return done(&b);
}

/* ---- WAV ---- */

size_t synth_wav(uint8_t *out, size_t cap)
{
    w b = { out, 0, cap, 1 };
    put(&b, "RIFF", 4);
    u32(&b, 4 + 8 + 16 + 8 + 10 + 8 + 2);
    put(&b, "WAVE", 4);
    put(&b, "fmt ", 4);
    u32(&b, 16);
    u16(&b, 1);      /* PCM */
    u16(&b, 1);      /* mono */
    u32(&b, 11025);
    u32(&b, 11025);
    u16(&b, 1);
    u16(&b, 8);
    put(&b, "LIST", 4); /* an extra chunk to skip */
    u32(&b, 2);
    put(&b, "xx", 2);
    put(&b, "data", 4);
    u32(&b, 10);
    for (int i = 0; i < 10; i++)
        u8(&b, (uint8_t)(128 + i));
    return done(&b);
}

/* ---- Smacker ---- */

typedef struct bitw {
    uint8_t buf[64];
    size_t bits;
} bitw;

static void putb(bitw *bw, uint32_t v, int n)
{
    for (int i = 0; i < n; i++, bw->bits++)
        if ((v >> i) & 1u)
            bw->buf[bw->bits >> 3] |= (uint8_t)(1u << (bw->bits & 7));
}

size_t synth_smk(uint8_t *out, size_t cap)
{
    /* Trees: MMAP, MCLR, FULL absent; TYPE = one leaf 0x5503 (solid block, colour 0x55, run 1). */
    bitw t = { {0}, 0 };
    putb(&t, 0, 1);
    putb(&t, 0, 1);
    putb(&t, 0, 1);
    putb(&t, 1, 1);                     /* TYPE present */
    putb(&t, 1, 1); putb(&t, 0, 1); putb(&t, 0x03, 8); putb(&t, 0, 1);  /* low byte tree */
    putb(&t, 1, 1); putb(&t, 0, 1); putb(&t, 0x55, 8); putb(&t, 0, 1);  /* high byte tree */
    putb(&t, 0x1111, 16); putb(&t, 0x2222, 16); putb(&t, 0x3333, 16);    /* escapes */
    putb(&t, 0, 1);                     /* big tree: a single leaf */
    putb(&t, 0, 1);
    uint32_t trees = (uint32_t)((t.bits + 7) / 8);

    w b = { out, 0, cap, 1 };
    put(&b, "SMK2", 4);
    u32(&b, 4);       /* width */
    u32(&b, 4);       /* height */
    u32(&b, 2);       /* frames */
    u32(&b, 100);     /* ms per frame */
    u32(&b, 0);       /* flags */
    for (int i = 0; i < 7; i++)
        u32(&b, i == 0 ? 4 : 0);   /* max audio bytes per track */
    u32(&b, trees);
    u32(&b, 16);      /* MMAP size */
    u32(&b, 16);      /* MCLR size */
    u32(&b, 16);      /* FULL size */
    u32(&b, 16);      /* TYPE size */
    u32(&b, 0x40000000u | 11025);  /* track 0: present, uncompressed 8-bit mono */
    for (int i = 1; i < 7; i++)
        u32(&b, 0);
    u32(&b, 0);
    u32(&b, 8);       /* frame 0: palette (8 bytes), empty video bitstream */
    u32(&b, 8);       /* frame 1: audio (8 bytes) */
    u8(&b, 1);        /* frame 0 type: palette */
    u8(&b, 2);        /* frame 1 type: audio track 0 */
    put(&b, t.buf, trees);
    /* frame 0 palette: skip 85 entries, entry 0x55 = (63, 0, 0), skip the rest */
    static const uint8_t pal[8] = { 2, 0x80 | 84, 63, 0, 0, 0xFF, 0x80 | 41, 0 };
    put(&b, pal, sizeof pal);
    /* frame 1 audio: length (incl. itself) + 4 bytes */
    u32(&b, 8);
    put(&b, "\x01\x02\x03\x04", 4);
    return done(&b);
}

static void f32(w *b, float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    u32(b, u);
}

static void nav_node(w *b, uint16_t index, float x, float y, float z, uint16_t group, int links,
                     const uint16_t *pairs)
{
    u32(b, 0x25u + 4u * (uint32_t)links);
    u32(b, 0x01D5B028);   /* saved pointers */
    u32(b, 0x01D5B028);
    f32(b, x);
    f32(b, y);
    f32(b, z);
    u32(b, 123);
    u32(b, 0x5E5E5E5E);
    u32(b, 0);
    u16(b, group);
    u16(b, index);
    u8(b, (uint8_t)links);
    for (int i = 0; i < links * 2; i++)
        u16(b, pairs[i]);
}

size_t synth_nodes(uint8_t *out, size_t cap)
{
    w b = { out, 0, cap, 1 };
    static const uint16_t l0[] = { 10, 1, 20, 2 }, l2[] = { 20, 0 };
    u32(&b, 3);
    nav_node(&b, 0, 1, -2, 3, 7, 2, l0);
    nav_node(&b, 1, 11, -2, 3, 7, 0, NULL);
    nav_node(&b, 2, 1, -2, 23, 9, 1, l2);
    return done(&b);
}

size_t synth_item_exe(uint8_t *out, size_t cap)
{
    w b = { out, 0, cap, 1 };
    fill(&b, 0xCC, 100);
    for (uint32_t i = 0; i < 16; i++) {
        uint32_t skill = i == 0 ? 14 : i == 15 ? 16 : 15 + i % 5;
        u32(&b, i);
        u32(&b, 1 + (i >= 4));
        u32(&b, 0);
        u32(&b, i + 1);
        u32(&b, i + 6);
        u32(&b, skill);
    }
    for (uint32_t i = 0; i < 43; i++) {
        uint8_t r[0x2F2];
        memset(r, 0, sizeof r);
        const char *name = i == 0 ? "Dagger" : i == 1 ? "Short Sword" : i == 3 ? "Broad Sword" : i == 15 ? "Potion"
                         : i == 16 ? "Sack" : i == 19 ? "Clothes" : i == 22 ? "Arrow" : i == 24 ? "Helmet"
                         : i == 31 ? "Sigil Amulet" : i == 32 ? "Book" : NULL;
        if (name)
            memcpy(r, name, strlen(name));
        else
            r[0] = 'I', r[1] = 't', r[2] = 'e', r[3] = 'm', r[4] = ' ', r[5] = (uint8_t)('0' + i / 10),
            r[6] = (uint8_t)('0' + i % 10);
        r[0x20] = (uint8_t)i;
        /* Icons: weapons "mwpns6NN", Clothes "x", helmet "hlmt", cuirass "brst", none for the rest. */
        if (i <= 14) {
            memcpy(r + 0x22, "mwpns6", 6);
            r[0x28] = (uint8_t)('0' + i / 10);
            r[0x29] = (uint8_t)('0' + i % 10);
        } else if (i == 19) {
            r[0x22] = 'x';
        } else if (i == 24) {
            memcpy(r + 0x22, "hlmt", 4);
        } else if (i == 25) {
            memcpy(r + 0x22, "brst", 4);
        }
        r[0x2B] = r[0x2F] = (uint8_t)(100 + i);
        r[0x33] = 7;
        r[0x35] = (uint8_t)(i + 1);
        r[0x42] = (i >= 16 && i <= 18) || i == 42;
        put(&b, r, sizeof r);
    }
    static const char materials[] = "\0Iron\0Steel\0Silver\0Elven\0Dwarven\0Mithril\0Adamantium\0Ebony\0Orcish\0Daedric";
    put(&b, materials, sizeof materials);
    return done(&b);
}

static void le_stru(w *b, int32_t id, const int32_t *v, int n)
{
    size_t st = le_chunk_begin(b, "STRU");
    le_i32(b, "IDNB", id);
    put(b, "RAWD", 4);
    u32(b, (uint32_t)(n * 4));
    for (int i = 0; i < n; i++)
        u32(b, (uint32_t)v[i]);
    le_chunk_end(b, st);
}

size_t synth_bs6_items(uint8_t *out, size_t cap)
{
    w b = { out, 0, cap, 1 };
    size_t gnrl = le_chunk_begin(&b, "GNRL");
    size_t objs = le_chunk_begin(&b, "OBJS");
    put(&b, "LFIL", 4);
    u32(&b, 260);
    put(&b, "box", 3);
    fill(&b, 0, 257);
    static const int32_t arrows[] = { 22, 0, 1, 10000 }, clothes[] = { 10000, 19, 0, 0 }, sound[] = { 362, 1 },
                         magic[] = { 7001, 3, 0, 1 }, potion[] = { 21 }, book[] = { 993 };
    size_t o = le_object_begin(&b, 30, 0);
    le_stru(&b, 105, arrows, 4);
    le_chunk_end(&b, o);
    o = le_object_begin(&b, 31, 100);
    le_stru(&b, 116, clothes, 4);
    le_stru(&b, 121, sound, 2);
    le_stru(&b, 116, magic, 4);
    le_chunk_end(&b, o);
    o = le_object_begin(&b, 32, 200);
    le_ctrl(&b, 16, NULL, 0, NULL);
    le_chunk_end(&b, o);
    o = le_object_begin(&b, 33, 300);
    le_stru(&b, 115, potion, 1);
    le_chunk_end(&b, o);
    o = le_object_begin(&b, 34, 400);
    le_stru(&b, 107, book, 1);
    le_chunk_end(&b, o);
    le_chunk_end(&b, objs);
    le_chunk_end(&b, gnrl);
    return done(&b);
}

size_t synth_creature_exe(uint8_t *out, size_t cap)
{
    w b = { out, 0, cap, 1 };
    /* The spell lists right before the table, as in GAME.EXE: [18] then [9, 12]. */
    fill(&b, 0, 95);
    static const uint8_t lists[5] = { 18, 0, 9, 12, 0 };
    put(&b, lists, sizeof lists);
    size_t table = b.len;
    const uint32_t table_va = 0xE3559;
    for (int i = 0; i < 16; i++) {
        uint8_t r[0xCE];
        memset(r, 0, sizeof r);
        char name[32];
        if (i == 0)
            snprintf(name, sizeof name, "Scamp");
        else if (i == 1)
            snprintf(name, sizeof name, "Vermai");
        else
            snprintf(name, sizeof name, "Creature %d", i);
        memcpy(r, name, strlen(name));
        static const int32_t attrs[8] = { 20, 40, 10, 60, 20, 10, 65, 30 };
        for (int k = 0; k < 8; k++) {
            int32_t v = attrs[k] + (k == 0 ? i : 0);
            memcpy(r + 0x20 + 4 * k, &v, 4);
        }
        int32_t health = 30 + 10 * i, skill = 20, flags = i == 13 ? 0x18000 : 0, level = 1, armour = 2;
        memcpy(r + 0x44, &health, 4);
        memcpy(r + 0x48, &skill, 4);
        memcpy(r + 0x4C, &flags, 4);
        memcpy(r + 0x6C, &level, 4);
        memcpy(r + 0x70, &armour, 4);
        if (i == 2 || i == 13) {
            uint32_t a = table_va - 5, c = table_va - 3;
            memcpy(r + 0x74, &a, 4);
            if (i == 2)
                memcpy(r + 0x78, &c, 4);
        }
        if (i == 8) {
            r[0xAF] = 2; /* immune to fire */
            r[0xBC] = 4; /* never resists cold */
        }
        r[0x7C] = 20;
        static const int32_t weapon[6] = { 0, 0, 0, 1, 6, 16 };
        memcpy(r + 0x7E, weapon, sizeof weapon);
        put(&b, r, sizeof r);
    }
    for (int i = 0; i < 16; i++) {
        uint8_t r[0x31F];
        memset(r, 0, sizeof r);
        char code[4];
        snprintf(code, sizeof code, "%s", i == 0 ? "smp" : i == 1 ? "ver" : "cxx");
        if (i >= 2) {
            code[1] = (char)('0' + i / 10);
            code[2] = (char)('0' + i % 10);
        }
        for (int v = 0; v < 5; v++) {
            snprintf((char *)r + 13 * v, 13, "%sw%02d.bsi", code, v);
            snprintf((char *)r + 13 * (5 + v), 13, "%sa%02d.bsi", code, v);
            snprintf((char *)r + 13 * (40 + v), 13, "%ss%02d.bsi", code, v);
        }
        snprintf((char *)r + 13 * 45, 13, "%sd00.bsi", code);
        put(&b, r, sizeof r);
    }
    pad_to(&b, table + 0x4B48);
    for (int l = 0; l < 9; l++)
        for (int k = 0; k < 4; k++)
            u8(&b, (uint8_t)((l + k) % 16));
    pad_to(&b, table + 0xBD06);
    for (int i = 0; i < 16; i++)
        for (int k = 0; k < 4; k++)
            u32(&b, (uint32_t)(400 + 4 * i + k));
    fill(&b, 0, 16);
    return done(&b);
}

static size_t le_flat_begin(w *b, int32_t id, const char *name, int32_t x, int32_t z)
{
    size_t f = le_chunk_begin(b, "FLAD");
    le_i32(b, "IDNB", id);
    le_name(b, "FILN", name);
    const int32_t pos[3] = { x, -10, z };
    le_i32s(b, "POSI", pos, 3);
    le_i32(b, "SCAL", 0);
    le_i32(b, "AMBI", 0);
    le_i32(b, "SELE", 0);
    return f;
}

static void le_code(w *b, const char *code)
{
    size_t st = le_chunk_begin(b, "STRU");
    le_i32(b, "IDNB", 108);
    put(b, "RAWD", 4);
    u32(b, 4);
    put(b, code, 3);
    u8(b, 0);
    le_chunk_end(b, st);
}

size_t synth_bs6_monsters(uint8_t *out, size_t cap)
{
    w b = { out, 0, cap, 1 };
    size_t gnrl = le_chunk_begin(&b, "GNRL");
    size_t objs = le_chunk_begin(&b, "OBJS");
    put(&b, "LFIL", 4);
    u32(&b, 260);
    put(&b, "box", 3);
    fill(&b, 0, 257);
    le_chunk_end(&b, objs);
    size_t flas = le_chunk_begin(&b, "FLAS");
    static const int32_t dremora[13] = { 3, 0, 0, 0, 77, 0, 0, 0x8000 }, loot[4] = { 10000, 19, 0, 0 },
                         invalid[1] = { 0 }, talker[13] = { 12, 0, 0, 0, 0, 0, 13 };
    size_t f = le_flat_begin(&b, 40, "monster1", 0, 0);
    le_stru(&b, 106, dremora, 13);
    le_chunk_end(&b, f);
    f = le_flat_begin(&b, 41, "monster1", 300, 0);
    le_chunk_end(&b, f);
    f = le_flat_begin(&b, 42, "monster2", 600, 0);
    le_code(&b, "ab1");
    le_chunk_end(&b, f);
    f = le_flat_begin(&b, 43, "monster1", 900, 0);
    le_stru(&b, 116, loot, 4);
    le_chunk_end(&b, f);
    f = le_flat_begin(&b, 44, "monster1", 1200, 0);
    le_stru(&b, 106, invalid, 1);
    le_chunk_end(&b, f);
    f = le_flat_begin(&b, 45, "start", 0, 500);
    le_chunk_end(&b, f);
    f = le_flat_begin(&b, 46, "MONSTER1", 1500, 0);
    le_stru(&b, 106, talker, 13);
    le_code(&b, "DS5");
    le_chunk_end(&b, f);
    le_chunk_end(&b, flas);
    le_chunk_end(&b, gnrl);
    return done(&b);
}

