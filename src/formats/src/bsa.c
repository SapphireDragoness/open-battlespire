#include "bs/formats/bsa.h"

#include <stdlib.h>
#include <string.h>

#include "bs/core/fs.h"
#include "bs/core/log.h"
#include "bs/core/reader.h"
#include "bs/formats/lzss.h"

#define HEADER_SIZE 4u
#define NAMED_ENTRY_SIZE 18u
#define NUMBERED_ENTRY_SIZE 8u

static bs_result parse_directory(bs_bsa *bsa)
{
    uint8_t header[HEADER_SIZE];
    bs_result r = bs_stream_read_at(bsa->stream, 0, header, sizeof header);
    if (r != BS_OK)
        return r;
    bs_reader hr = bs_reader_make(header, sizeof header);
    bsa->count = bs_read_u16le(&hr);
    bsa->type = bs_read_u16le(&hr);

    uint32_t entry_size;
    if (bsa->type == BS_BSA_TYPE_NAMED)
        entry_size = NAMED_ENTRY_SIZE;
    else if (bsa->type == BS_BSA_TYPE_NUMBERED)
        entry_size = NUMBERED_ENTRY_SIZE;
    else
        return BS_ERR_UNSUPPORTED;

    uint64_t dir_size = (uint64_t)bsa->count * entry_size;
    uint64_t file_size = bsa->stream->size;
    if (file_size < HEADER_SIZE || dir_size > file_size - HEADER_SIZE)
        return BS_ERR_FORMAT;
    uint64_t dir_start = file_size - dir_size;

    uint8_t *dir;
    r = bs_stream_read_range(bsa->stream, dir_start, (size_t)dir_size, &bsa->arena, &dir);
    if (r != BS_OK)
        return r;
    bsa->records = BS_ARENA_ARRAY(&bsa->arena, bs_bsa_record, bsa->count ? bsa->count : 1);
    if (!bsa->records)
        return BS_ERR_NOMEM;

    bs_reader rd = bs_reader_make(dir, (size_t)dir_size);
    uint64_t offset = HEADER_SIZE;
    for (uint32_t i = 0; i < bsa->count; i++) {
        bs_bsa_record *rec = &bsa->records[i];
        if (bsa->type == BS_BSA_TYPE_NAMED)
            bs_read_fixed_str(&rd, 12, rec->name, sizeof rec->name);
        else
            rec->id = bs_read_u16le(&rd);
        rec->flags = bs_read_u16le(&rd);
        rec->size = bs_read_u32le(&rd);
        rec->offset = offset;
        if (rec->size > dir_start - offset)
            return BS_ERR_FORMAT;
        offset += rec->size;
    }
    if (!bs_reader_ok(&rd))
        return BS_ERR_FORMAT;
    if (offset != dir_start)
        BS_WARN("BSA: %llu unaccounted bytes before directory",
                (unsigned long long)(dir_start - offset));
    return BS_OK;
}

bs_result bs_bsa_open(bs_stream *stream, bs_bsa **out)
{
    *out = NULL;
    bs_bsa *bsa = calloc(1, sizeof *bsa);
    if (!bsa) {
        bs_stream_close(stream);
        return BS_ERR_NOMEM;
    }
    bsa->stream = stream;
    bs_arena_init(&bsa->arena, 64 * 1024);

    bs_result r = parse_directory(bsa);
    if (r != BS_OK) {
        bs_bsa_close(bsa);
        return r;
    }
    *out = bsa;
    return BS_OK;
}

bs_result bs_bsa_open_vfs(const bs_vfs *vfs, const char *name, bs_bsa **out)
{
    *out = NULL;
    bs_stream *s;
    bs_result r = bs_vfs_open(vfs, name, &s);
    if (r != BS_OK)
        return r;
    return bs_bsa_open(s, out);
}

void bs_bsa_close(bs_bsa *bsa)
{
    if (!bsa)
        return;
    bs_stream_close(bsa->stream);
    bs_arena_free(&bsa->arena);
    free(bsa);
}

const bs_bsa_record *bs_bsa_find(const bs_bsa *bsa, const char *name)
{
    for (uint32_t i = 0; i < bsa->count; i++) {
        if (bs_strcasecmp(bsa->records[i].name, name) == 0)
            return &bsa->records[i];
    }
    return NULL;
}

const bs_bsa_record *bs_bsa_find_id(const bs_bsa *bsa, uint16_t id)
{
    if (bsa->type != BS_BSA_TYPE_NUMBERED)
        return NULL;
    for (uint32_t i = 0; i < bsa->count; i++) {
        if (bsa->records[i].id == id)
            return &bsa->records[i];
    }
    return NULL;
}

bs_result bs_bsa_read_raw(const bs_bsa *bsa, const bs_bsa_record *rec, bs_arena *arena,
                          uint8_t **out, size_t *out_size)
{
    *out_size = 0;
    bs_result r = bs_stream_read_range(bsa->stream, rec->offset, rec->size, arena, out);
    if (r == BS_OK)
        *out_size = rec->size;
    return r;
}

bs_result bs_bsa_read(const bs_bsa *bsa, const bs_bsa_record *rec, bs_arena *arena,
                      uint8_t **out, size_t *out_size)
{
    if (!bs_bsa_record_compressed(rec))
        return bs_bsa_read_raw(bsa, rec, arena, out, out_size);

    *out = NULL;
    *out_size = 0;
    uint8_t *packed = malloc(rec->size ? rec->size : 1);
    if (!packed)
        return BS_ERR_NOMEM;
    bs_result r = bs_stream_read_at(bsa->stream, rec->offset, packed, rec->size);
    if (r == BS_OK)
        r = bs_lzss_decompress(packed, rec->size, arena, out, out_size);
    free(packed);
    return r;
}
