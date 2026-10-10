#ifndef BS_CORE_READER_H
#define BS_CORE_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Bounds-checked cursor over an in-memory buffer. All binary parsing goes
 * through this; never cast file data to structs.
 *
 * Errors are sticky: a read past the end sets `err`, returns zero, and moves
 * the cursor to the end so later reads also fail. Parsers can read a whole
 * header and check bs_reader_ok() once at the end.
 */
typedef struct bs_reader {
    const uint8_t *data;
    size_t size;
    size_t pos;
    bool err;
} bs_reader;

bs_reader bs_reader_make(const void *data, size_t size);

static inline bool bs_reader_ok(const bs_reader *r) { return !r->err; }
static inline size_t bs_reader_remaining(const bs_reader *r) { return r->size - r->pos; }
static inline bool bs_reader_eof(const bs_reader *r) { return r->pos >= r->size; }

/* Seek to an absolute offset. Seeking to `size` is allowed (end of buffer). */
bool bs_reader_seek(bs_reader *r, size_t pos);
bool bs_reader_skip(bs_reader *r, size_t n);

uint8_t  bs_read_u8(bs_reader *r);
uint16_t bs_read_u16le(bs_reader *r);
uint32_t bs_read_u32le(bs_reader *r);
uint16_t bs_read_u16be(bs_reader *r);
uint32_t bs_read_u32be(bs_reader *r);
int8_t   bs_read_i8(bs_reader *r);
int16_t  bs_read_i16le(bs_reader *r);
int32_t  bs_read_i32le(bs_reader *r);
int16_t  bs_read_i16be(bs_reader *r);
int32_t  bs_read_i32be(bs_reader *r);

/* Copies n bytes into dst. On failure dst is zero-filled. */
bool bs_read_bytes(bs_reader *r, void *dst, size_t n);

/* Returns a pointer to the next n bytes (no copy), or NULL on failure. */
const uint8_t *bs_read_span(bs_reader *r, size_t n);

/*
 * Reads a fixed-size, NUL-padded string field of `field_len` bytes into dst
 * (capacity dst_cap, always NUL-terminated, truncated if needed).
 */
bool bs_read_fixed_str(bs_reader *r, size_t field_len, char *dst, size_t dst_cap);

/* Returns a reader over the next n bytes and advances past them. */
bs_reader bs_reader_sub(bs_reader *r, size_t n);

#endif
