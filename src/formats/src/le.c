#include "bs/formats/le.h"

#include <string.h>

#include "bs/core/reader.h"

#define MAX_OBJECTS 256u
#define MAX_TOTAL_MEMORY (64u << 20) /* GAME.EXE needs ~8.6 MB */

/* Fixup source types (low nibble of the source byte). */
enum {
    SRC_BYTE = 0x0,
    SRC_SELECTOR16 = 0x2,
    SRC_POINTER1616 = 0x3,
    SRC_OFFSET16 = 0x5,
    SRC_POINTER1632 = 0x6,
    SRC_OFFSET32 = 0x7,
    SRC_RELATIVE32 = 0x8
};
#define SRC_TYPE_MASK 0x0F
#define SRC_LIST 0x20

/* Fixup target flags. */
#define TGT_TYPE_MASK 0x03
#define TGT_INTERNAL 0x00
#define TGT_IMPORT_ORDINAL 0x01
#define TGT_IMPORT_NAME 0x02
#define TGT_ENTRY 0x03
#define TGT_ADDITIVE 0x04
#define TGT_OFFSET32 0x10
#define TGT_ADDITIVE32 0x20
#define TGT_OBJECT16 0x40
#define TGT_ORDINAL8 0x80

/* Page map entry types. */
#define PAGE_LEGAL 0
#define PAGE_ITERATED 1
#define PAGE_INVALID 2
#define PAGE_ZERO 3

typedef struct le_header {
    uint32_t page_count, eip_object, eip, esp_object, esp, page_size, last_page_size;
    uint32_t object_table, object_count, page_map, fixup_page_table, fixup_records;
    uint32_t data_pages;
} le_header;

static bs_result read_header(const uint8_t *exe, size_t size, uint32_t le_off, le_header *h)
{
    bs_reader r = bs_reader_make(exe, size);
    if (!bs_reader_seek(&r, le_off))
        return BS_ERR_FORMAT;
    const uint8_t *sig = bs_read_span(&r, 4);
    if (!sig || sig[0] != 'L' || sig[1] != 'E' || sig[2] != 0 || sig[3] != 0)
        return BS_ERR_FORMAT; /* only little-endian byte and word order */

    bs_reader_seek(&r, le_off + 0x14u);
    h->page_count = bs_read_u32le(&r);
    h->eip_object = bs_read_u32le(&r);
    h->eip = bs_read_u32le(&r);
    h->esp_object = bs_read_u32le(&r);
    h->esp = bs_read_u32le(&r);
    h->page_size = bs_read_u32le(&r);
    h->last_page_size = bs_read_u32le(&r);
    bs_reader_seek(&r, le_off + 0x40u);
    h->object_table = bs_read_u32le(&r);
    h->object_count = bs_read_u32le(&r);
    h->page_map = bs_read_u32le(&r);
    bs_reader_seek(&r, le_off + 0x68u);
    h->fixup_page_table = bs_read_u32le(&r);
    h->fixup_records = bs_read_u32le(&r);
    bs_reader_seek(&r, le_off + 0x80u);
    h->data_pages = bs_read_u32le(&r);
    if (!bs_reader_ok(&r))
        return BS_ERR_FORMAT;

    if (h->page_size < 512 || h->page_size > 65536 || (h->page_size & (h->page_size - 1)))
        return BS_ERR_FORMAT;
    if (h->object_count == 0 || h->object_count > MAX_OBJECTS)
        return BS_ERR_FORMAT;
    if (h->last_page_size > h->page_size)
        return BS_ERR_FORMAT;
    return BS_OK;
}

/* Finds the LE header: via the MZ stub's e_lfanew, or at offset 0. */
static bs_result find_le(const uint8_t *exe, size_t size, uint32_t *out)
{
    if (size >= 0x40 && exe[0] == 'M' && exe[1] == 'Z') {
        bs_reader r = bs_reader_make(exe, size);
        bs_reader_seek(&r, 0x3C);
        *out = bs_read_u32le(&r);
        return BS_OK;
    }
    if (size >= 2 && exe[0] == 'L' && exe[1] == 'E') {
        *out = 0;
        return BS_OK;
    }
    return BS_ERR_FORMAT;
}

static bs_result load_objects(const uint8_t *exe, size_t size, uint32_t le_off, const le_header *h,
                              bs_arena *arena, bs_le_image *img)
{
    img->objects = BS_ARENA_ARRAY(arena, bs_le_object, h->object_count);
    if (!img->objects)
        return BS_ERR_NOMEM;

    bs_reader objs = bs_reader_make(exe, size);
    if (!bs_reader_seek(&objs, (size_t)le_off + h->object_table))
        return BS_ERR_FORMAT;
    bs_reader map = bs_reader_make(exe, size);
    if (!bs_reader_seek(&map, (size_t)le_off + h->page_map) ||
        bs_reader_remaining(&map) / 4 < h->page_count)
        return BS_ERR_FORMAT;
    const uint8_t *page_map = bs_read_span(&map, (size_t)h->page_count * 4);

    uint64_t total = 0;
    for (uint32_t i = 0; i < h->object_count; i++) {
        bs_le_object *o = &img->objects[i];
        o->virtual_size = bs_read_u32le(&objs);
        o->base = bs_read_u32le(&objs);
        o->flags = bs_read_u32le(&objs);
        o->page_index = bs_read_u32le(&objs);
        o->page_count = bs_read_u32le(&objs);
        bs_reader_skip(&objs, 4);
        if (!bs_reader_ok(&objs))
            return BS_ERR_FORMAT;
        if (o->page_count > 0 &&
            (o->page_index == 0 || o->page_index > h->page_count ||
             o->page_count > h->page_count - o->page_index + 1))
            return BS_ERR_FORMAT;

        uint64_t file_size = (uint64_t)o->page_count * h->page_size;
        uint64_t mem_size = file_size > o->virtual_size ? file_size : o->virtual_size;
        total += mem_size;
        if (total > MAX_TOTAL_MEMORY)
            return BS_ERR_UNSUPPORTED;
        o->file_size = (uint32_t)file_size;
        o->mem_size = (uint32_t)mem_size;
        o->data = bs_arena_alloc(arena, o->mem_size ? o->mem_size : 1, 16);
        if (!o->data)
            return BS_ERR_NOMEM;

        for (uint32_t k = 0; k < o->page_count; k++) {
            uint32_t mod_page = o->page_index + k; /* 1-based */
            const uint8_t *e = page_map + (size_t)(mod_page - 1) * 4;
            uint32_t number = ((uint32_t)e[0] << 16) | ((uint32_t)e[1] << 8) | e[2];
            uint8_t type = e[3];
            if (type == PAGE_ZERO || type == PAGE_INVALID)
                continue;
            if (type != PAGE_LEGAL || number == 0)
                return BS_ERR_UNSUPPORTED;

            uint32_t len = h->page_size;
            if (number == h->page_count && h->last_page_size)
                len = h->last_page_size;
            uint64_t src = (uint64_t)h->data_pages + (uint64_t)(number - 1) * h->page_size;
            if (src > size || len > size - src)
                return BS_ERR_FORMAT;
            memcpy(o->data + (size_t)k * h->page_size, exe + src, len);
        }
    }
    return BS_OK;
}

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* Applies one internal fixup at `offset` within object `o`. */
static bs_result apply(bs_le_image *img, bs_le_object *o, int64_t offset, uint8_t src_type, uint32_t target)
{
    static const uint8_t width[16] = {
        [SRC_BYTE] = 1, [SRC_SELECTOR16] = 2, [SRC_POINTER1616] = 4, [SRC_OFFSET16] = 2,
        [SRC_POINTER1632] = 6, [SRC_OFFSET32] = 4, [SRC_RELATIVE32] = 4,
    };
    uint8_t w = width[src_type];
    if (w == 0) {
        img->stats.unsupported++;
        return BS_OK;
    }
    if (offset < 0 || (uint64_t)offset + w > o->mem_size)
        return BS_ERR_FORMAT;
    uint8_t *p = o->data + offset;
    uint32_t src_va = o->base + (uint32_t)offset;

    switch (src_type) {
    case SRC_BYTE:        p[0] = (uint8_t)target; img->stats.byte++; break;
    case SRC_SELECTOR16:  img->stats.selector++; break;
    case SRC_POINTER1616: put16(p, target); img->stats.pointer1616++; break;
    case SRC_OFFSET16:    put16(p, target); img->stats.offset16++; break;
    case SRC_POINTER1632: put32(p, target); img->stats.pointer1632++; break;
    case SRC_OFFSET32:    put32(p, target); img->stats.offset32++; break;
    case SRC_RELATIVE32:  put32(p, target - (src_va + 4)); img->stats.relative32++; break;
    default: break;
    }
    return BS_OK;
}

/* Parses and applies the fixup records of one module page. */
static bs_result fixup_page(bs_le_image *img, bs_le_object *o, uint32_t page_in_object,
                            bs_reader *r)
{
    while (!bs_reader_eof(r)) {
        uint8_t src = bs_read_u8(r);
        uint8_t flags = bs_read_u8(r);
        uint8_t src_type = src & SRC_TYPE_MASK;
        uint32_t count = 1;
        int16_t single_off = 0;
        if (src & SRC_LIST)
            count = bs_read_u8(r);
        else
            single_off = bs_read_i16le(r);

        bool internal = (flags & TGT_TYPE_MASK) == TGT_INTERNAL;
        uint32_t target_obj = 0, target_off = 0;
        switch (flags & TGT_TYPE_MASK) {
        case TGT_INTERNAL:
            target_obj = (flags & TGT_OBJECT16) ? bs_read_u16le(r) : bs_read_u8(r);
            if (src_type != SRC_SELECTOR16)
                target_off = (flags & TGT_OFFSET32) ? bs_read_u32le(r) : bs_read_u16le(r);
            break;
        case TGT_IMPORT_ORDINAL:
            bs_reader_skip(r, (flags & TGT_OBJECT16) ? 2 : 1);
            bs_reader_skip(r, (flags & TGT_ORDINAL8) ? 1 : (flags & TGT_OFFSET32) ? 4 : 2);
            break;
        case TGT_IMPORT_NAME:
            bs_reader_skip(r, (flags & TGT_OBJECT16) ? 2 : 1);
            bs_reader_skip(r, (flags & TGT_OFFSET32) ? 4 : 2);
            break;
        case TGT_ENTRY:
            bs_reader_skip(r, (flags & TGT_OBJECT16) ? 2 : 1);
            break;
        default:
            break;
        }
        uint32_t additive = 0;
        if (flags & TGT_ADDITIVE)
            additive = (flags & TGT_ADDITIVE32) ? bs_read_u32le(r) : bs_read_u16le(r);
        if (!bs_reader_ok(r))
            return BS_ERR_FORMAT;

        uint32_t target = 0;
        if (internal) {
            if (target_obj == 0 || target_obj > img->object_count)
                return BS_ERR_FORMAT;
            target = img->objects[target_obj - 1].base + target_off + additive;
        }

        int64_t page_base = (int64_t)page_in_object * img->page_size;
        for (uint32_t i = 0; i < count; i++) {
            int16_t off = (src & SRC_LIST) ? bs_read_i16le(r) : single_off;
            if (!bs_reader_ok(r))
                return BS_ERR_FORMAT;
            if (!internal) {
                img->stats.unsupported++;
                continue;
            }
            bs_result res = apply(img, o, page_base + off, src_type, target);
            if (res != BS_OK)
                return res;
        }
    }
    return BS_OK;
}

static bs_result apply_fixups(const uint8_t *exe, size_t size, uint32_t le_off, const le_header *h,
                              bs_le_image *img)
{
    bs_reader fpt = bs_reader_make(exe, size);
    if (!bs_reader_seek(&fpt, (size_t)le_off + h->fixup_page_table) ||
        bs_reader_remaining(&fpt) / 4 <= h->page_count)
        return BS_ERR_FORMAT;
    size_t rec_base = (size_t)le_off + h->fixup_records;
    if (rec_base > size)
        return BS_ERR_FORMAT;

    for (uint32_t i = 0; i < img->object_count; i++) {
        bs_le_object *o = &img->objects[i];
        for (uint32_t k = 0; k < o->page_count; k++) {
            uint32_t mod_page = o->page_index + k;
            bs_reader_seek(&fpt, (size_t)le_off + h->fixup_page_table + (size_t)(mod_page - 1) * 4);
            uint32_t start = bs_read_u32le(&fpt);
            uint32_t end = bs_read_u32le(&fpt);
            if (!bs_reader_ok(&fpt) || end < start || end > size - rec_base)
                return BS_ERR_FORMAT;
            bs_reader recs = bs_reader_make(exe + rec_base + start, end - start);
            bs_result r = fixup_page(img, o, k, &recs);
            if (r != BS_OK)
                return r;
        }
    }
    return BS_OK;
}

bs_result bs_le_load(const uint8_t *exe, size_t size, bs_arena *arena, bs_le_image *out)
{
    memset(out, 0, sizeof *out);
    uint32_t le_off;
    bs_result r = find_le(exe, size, &le_off);
    if (r != BS_OK)
        return r;
    le_header h;
    r = read_header(exe, size, le_off, &h);
    if (r != BS_OK)
        return r;

    out->le_offset = le_off;
    out->page_size = h.page_size;
    out->page_count = h.page_count;
    out->object_count = h.object_count;
    out->entry_object = h.eip_object;
    out->entry_offset = h.eip;
    out->stack_object = h.esp_object;
    out->stack_offset = h.esp;

    r = load_objects(exe, size, le_off, &h, arena, out);
    if (r == BS_OK)
        r = apply_fixups(exe, size, le_off, &h, out);
    if (r != BS_OK)
        return r;

    if (h.eip_object >= 1 && h.eip_object <= h.object_count)
        out->entry_va = out->objects[h.eip_object - 1].base + h.eip;
    return BS_OK;
}
