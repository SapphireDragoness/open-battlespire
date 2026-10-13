#ifndef BS_FORMATS_BSA_H
#define BS_FORMATS_BSA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"
#include "bs/core/stream.h"
#include "bs/core/vfs.h"

/*
 * BSA archives (3D.BSA, BSI.BSA, BS6.BSA, TXT.BSA, FLC.BSA, WAVES.BSA, 3D.BS6,
 * SPIRE.SND). See docs/formats/bsa.md.
 *
 *   u16 record_count
 *   u16 type                0x0100 = named records, 0x0200 = numbered records
 *   record data...          back to back, starting at offset 4
 *   directory               at the end of the file, one entry per record:
 *     named:    char name[12]; u16 flags; u32 size     (18 bytes)
 *     numbered: u16 id;        u16 flags; u32 size     (8 bytes)
 *
 * flags & 0x0100 marks LZSS-compressed records (see lzss.h).
 */

#define BS_BSA_TYPE_NAMED    0x0100
#define BS_BSA_TYPE_NUMBERED 0x0200
#define BS_BSA_FLAG_COMPRESSED 0x0100

typedef struct bs_bsa_record {
    char name[13];      /* named archives; "" for numbered */
    uint16_t id;        /* numbered archives; 0 for named */
    uint16_t flags;
    uint32_t size;      /* stored (possibly compressed) size */
    uint64_t offset;    /* offset of the stored data in the archive */
} bs_bsa_record;

typedef struct bs_bsa {
    bs_stream *stream;  /* owned */
    uint16_t type;
    uint32_t count;
    bs_bsa_record *records;
    bs_arena arena;
} bs_bsa;

/* Opens an archive from a stream; takes ownership of the stream even on failure. */
bs_result bs_bsa_open(bs_stream *stream, bs_bsa **out);

/* Opens an archive by VFS name, e.g. "GAMEDATA/3D.BSA" or "WAVES.BSA". */
bs_result bs_bsa_open_vfs(const bs_vfs *vfs, const char *name, bs_bsa **out);

/* Safe to call with NULL. */
void bs_bsa_close(bs_bsa *bsa);

/* Case-insensitive name lookup; NULL if absent. */
const bs_bsa_record *bs_bsa_find(const bs_bsa *bsa, const char *name);

/* Lookup in numbered archives; NULL if absent. */
const bs_bsa_record *bs_bsa_find_id(const bs_bsa *bsa, uint16_t id);

static inline bool bs_bsa_record_compressed(const bs_bsa_record *rec)
{
    return (rec->flags & BS_BSA_FLAG_COMPRESSED) != 0;
}

/* Reads a record as stored (compressed records stay compressed). NUL-terminated. */
bs_result bs_bsa_read_raw(const bs_bsa *bsa, const bs_bsa_record *rec, bs_arena *arena,
                          uint8_t **out, size_t *out_size);

/* Reads a record, decompressing if needed. NUL-terminated. */
bs_result bs_bsa_read(const bs_bsa *bsa, const bs_bsa_record *rec, bs_arena *arena,
                      uint8_t **out, size_t *out_size);

#endif
