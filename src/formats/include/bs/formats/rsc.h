#ifndef BS_FORMATS_RSC_H
#define BS_FORMATS_RSC_H

#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"

/*
 * TEXT.RSC: the string table for spell, item and UI text. Same layout as
 * Daggerfall's TEXT.RSC. See docs/formats/text_rsc.md.
 *
 *   u16 table_size                  bytes of entry table that follow
 *   { u16 id; u32 offset; } ...     sorted by id; last is a sentinel with
 *                                   id 0xFFFF and offset = file size
 *   string data                     Windows-1252 with control bytes
 *
 * A record runs from its offset to the next entry's offset and normally ends
 * with 0xFE.
 */

#define BS_RSC_END       0xFE  /* end of record */
#define BS_RSC_NEWLINE   0xFD  /* line break */
#define BS_RSC_NEWLINE_C 0xFC  /* line break; Daggerfall: centred line (unverified here) */
#define BS_RSC_CODE      0xFA  /* followed by one argument byte; meaning unknown */

typedef struct bs_rsc_record {
    uint16_t id;
    const uint8_t *data;  /* points into the parsed buffer; excludes the trailing 0xFE */
    size_t size;
} bs_rsc_record;

typedef struct bs_rsc {
    bs_rsc_record *records;  /* sorted by id, sentinel excluded */
    size_t count;
} bs_rsc;

/* Parses TEXT.RSC. Records reference `data`, which must outlive `out`. */
bs_result bs_rsc_parse(const uint8_t *data, size_t size, bs_arena *arena, bs_rsc *out);

const bs_rsc_record *bs_rsc_find(const bs_rsc *rsc, uint16_t id);

/*
 * Renders a record as UTF-8 for display and debugging: newlines become '\n',
 * other control bytes become "{XX}" (or "{XX:YY}" with an argument).
 */
char *bs_rsc_to_utf8(bs_arena *arena, const bs_rsc_record *rec);

#endif
