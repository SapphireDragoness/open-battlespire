/* fseeko/off_t are POSIX; 64-bit offsets for files over 2 GB on 32-bit hosts. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#endif

#include "bs/core/stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/core/fs.h"

static bool range_ok(const bs_stream *s, uint64_t offset, size_t n)
{
    return offset <= s->size && n <= s->size - offset;
}

bs_result bs_stream_read_at(bs_stream *s, uint64_t offset, void *dst, size_t n)
{
    if (!range_ok(s, offset, n))
        return BS_ERR_FORMAT;
    if (n == 0)
        return BS_OK;
    return s->read_at(s, offset, dst, n);
}

bs_result bs_stream_read_range(bs_stream *s, uint64_t offset, size_t n, bs_arena *arena, uint8_t **out)
{
    *out = NULL;
    if (n == SIZE_MAX)
        return BS_ERR_NOMEM;
    uint8_t *buf = bs_arena_alloc(arena, n + 1, 16);
    if (!buf)
        return BS_ERR_NOMEM;
    bs_result r = bs_stream_read_at(s, offset, buf, n);
    if (r != BS_OK)
        return r;
    buf[n] = 0;
    *out = buf;
    return BS_OK;
}

bs_result bs_stream_read_all(bs_stream *s, bs_arena *arena, uint8_t **out, size_t *out_size)
{
    *out = NULL;
    *out_size = 0;
    if (s->size >= SIZE_MAX)
        return BS_ERR_NOMEM;
    bs_result r = bs_stream_read_range(s, 0, (size_t)s->size, arena, out);
    if (r == BS_OK)
        *out_size = (size_t)s->size;
    return r;
}

void bs_stream_close(bs_stream *s)
{
    if (s)
        s->close(s);
}

/* ---- host file ---- */

typedef struct file_stream {
    bs_stream base;
    FILE *f;
} file_stream;

static int seek64(FILE *f, uint64_t offset)
{
#ifdef _WIN32
    return _fseeki64(f, (long long)offset, SEEK_SET);
#else
    return fseeko(f, (off_t)offset, SEEK_SET);
#endif
}

static bs_result file_read_at(bs_stream *s, uint64_t offset, void *dst, size_t n)
{
    file_stream *fs = (file_stream *)s;
    if (seek64(fs->f, offset) != 0 || fread(dst, 1, n, fs->f) != n)
        return BS_ERR_IO;
    return BS_OK;
}

static void file_close(bs_stream *s)
{
    file_stream *fs = (file_stream *)s;
    fclose(fs->f);
    free(fs);
}

bs_result bs_stream_open_file(const char *path, bs_stream **out)
{
    *out = NULL;
    uint64_t size;
    if (bs_fs_file_size(path, &size) != BS_OK)
        return BS_ERR_NOT_FOUND;
    FILE *f = fopen(path, "rb");
    if (!f)
        return BS_ERR_NOT_FOUND;
    file_stream *fs = calloc(1, sizeof *fs);
    if (!fs) {
        fclose(f);
        return BS_ERR_NOMEM;
    }
    fs->base.size = size;
    fs->base.read_at = file_read_at;
    fs->base.close = file_close;
    fs->f = f;
    *out = &fs->base;
    return BS_OK;
}

/* ---- memory ---- */

typedef struct mem_stream {
    bs_stream base;
    const uint8_t *data;
} mem_stream;

static bs_result mem_read_at(bs_stream *s, uint64_t offset, void *dst, size_t n)
{
    mem_stream *ms = (mem_stream *)s;
    memcpy(dst, ms->data + (size_t)offset, n);
    return BS_OK;
}

static void mem_close(bs_stream *s)
{
    free(s);
}

bs_result bs_stream_open_memory(const void *data, size_t size, bs_stream **out)
{
    *out = NULL;
    if (!data && size)
        return BS_ERR_ARGS;
    mem_stream *ms = calloc(1, sizeof *ms);
    if (!ms)
        return BS_ERR_NOMEM;
    ms->base.size = size;
    ms->base.read_at = mem_read_at;
    ms->base.close = mem_close;
    ms->data = data;
    *out = &ms->base;
    return BS_OK;
}
