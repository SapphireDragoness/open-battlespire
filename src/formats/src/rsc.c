#include "bs/formats/rsc.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "bs/core/reader.h"
#include "bs/core/text.h"

#define ENTRY_SIZE 6u
#define SENTINEL_ID 0xFFFFu

bs_result bs_rsc_parse(const uint8_t *data, size_t size, bs_arena *arena, bs_rsc *out)
{
    memset(out, 0, sizeof *out);
    bs_reader r = bs_reader_make(data, size);
    uint16_t table_size = bs_read_u16le(&r);
    if (!bs_reader_ok(&r) || table_size % ENTRY_SIZE != 0 || table_size < ENTRY_SIZE)
        return BS_ERR_FORMAT;

    size_t entries = table_size / ENTRY_SIZE;
    size_t data_start = 2u + table_size;
    bs_rsc_record *recs = BS_ARENA_ARRAY(arena, bs_rsc_record, entries);
    if (!recs)
        return BS_ERR_NOMEM;

    size_t count = 0;
    uint32_t prev_offset = (uint32_t)data_start;
    int prev_id = -1;
    bool found_sentinel = false;
    for (size_t i = 0; i < entries; i++) {
        uint16_t id = bs_read_u16le(&r);
        uint32_t offset = bs_read_u32le(&r);
        if (!bs_reader_ok(&r) || offset < prev_offset || offset > size || (int)id <= prev_id)
            return BS_ERR_FORMAT;
        /* The previous record ends where this one starts. */
        if (count > 0)
            recs[count - 1].size = offset - (size_t)(recs[count - 1].data - data);
        if (id == SENTINEL_ID) {
            found_sentinel = true;
            break;
        }
        recs[count].id = id;
        recs[count].data = data + offset;
        count++;
        prev_offset = offset;
        prev_id = id;
    }
    if (!found_sentinel)
        return BS_ERR_FORMAT;

    for (size_t i = 0; i < count; i++) {
        if (recs[i].size > 0 && recs[i].data[recs[i].size - 1] == BS_RSC_END)
            recs[i].size--;
    }
    out->records = recs;
    out->count = count;
    return BS_OK;
}

const bs_rsc_record *bs_rsc_find(const bs_rsc *rsc, uint16_t id)
{
    size_t lo = 0, hi = rsc->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (rsc->records[mid].id < id)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < rsc->count && rsc->records[lo].id == id ? &rsc->records[lo] : NULL;
}

char *bs_rsc_to_utf8(bs_arena *arena, const bs_rsc_record *rec)
{
    /* Worst case per input byte: "{XX:YY}" = 7 bytes for two bytes, or 3 UTF-8 bytes. */
    size_t cap = rec->size * 4 + 1;
    char *out = bs_arena_alloc(arena, cap, 1);
    if (!out)
        return NULL;
    size_t len = 0;
    for (size_t i = 0; i < rec->size; i++) {
        uint8_t c = rec->data[i];
        if (c == BS_RSC_NEWLINE || c == BS_RSC_NEWLINE_C) {
            out[len++] = '\n';
        } else if (c == BS_RSC_CODE && i + 1 < rec->size) {
            len += (size_t)snprintf(out + len, cap - len, "{%02X:%02X}", c, rec->data[i + 1]);
            i++;
        } else if (c >= 0xF0 || (c < 0x20 && c != '\t' && c != '\r' && c != '\n')) {
            len += (size_t)snprintf(out + len, cap - len, "{%02X}", c);
        } else {
            len += bs_utf8_encode(bs_cp1252_to_unicode(c), out + len);
        }
    }
    out[len] = '\0';
    return out;
}
