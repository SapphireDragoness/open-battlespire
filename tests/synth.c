#include "synth.h"

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
