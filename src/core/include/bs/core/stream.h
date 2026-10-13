#ifndef BS_CORE_STREAM_H
#define BS_CORE_STREAM_H

#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"

/*
 * Read-only random-access byte source. Archives are read through streams so
 * that large files (FLC.BSA, or WAVES.BSA inside the CD image) never have to
 * be loaded whole.
 *
 * Streams are not thread-safe.
 */
typedef struct bs_stream bs_stream;

struct bs_stream {
    uint64_t size;
    /* Reads exactly n bytes at offset; BS_ERR_FORMAT if the range exceeds size. */
    bs_result (*read_at)(bs_stream *s, uint64_t offset, void *dst, size_t n);
    void (*close)(bs_stream *s);
};

/* Opens a host file. */
bs_result bs_stream_open_file(const char *path, bs_stream **out);

/* Wraps a memory buffer (not copied; must outlive the stream). */
bs_result bs_stream_open_memory(const void *data, size_t size, bs_stream **out);

bs_result bs_stream_read_at(bs_stream *s, uint64_t offset, void *dst, size_t n);

/*
 * Reads [offset, offset + n) into memory allocated from arena, with a NUL
 * byte appended (not counted).
 */
bs_result bs_stream_read_range(bs_stream *s, uint64_t offset, size_t n, bs_arena *arena, uint8_t **out);

/* Reads the whole stream (NUL-terminated as above). */
bs_result bs_stream_read_all(bs_stream *s, bs_arena *arena, uint8_t **out, size_t *out_size);

/* Safe to call with NULL. */
void bs_stream_close(bs_stream *s);

#endif
