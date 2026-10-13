#ifndef BS_CORE_STRMAP_H
#define BS_CORE_STRMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"

/*
 * Map from case-insensitive ASCII strings to pointers (open addressing).
 * Keys are copied into the map's arena; values are borrowed. There is no
 * removal. NULL is a valid value, so lookups report presence separately.
 */

typedef struct bs_strmap_slot {
    const char *key;  /* NULL = empty */
    uint32_t hash;
    void *value;
} bs_strmap_slot;

typedef struct bs_strmap {
    bs_strmap_slot *slots;
    size_t cap;    /* power of two, or 0 */
    size_t count;
    bs_arena arena;
} bs_strmap;

void bs_strmap_init(bs_strmap *m);
void bs_strmap_free(bs_strmap *m);

/* Inserts or replaces. */
bs_result bs_strmap_set(bs_strmap *m, const char *key, void *value);

/* Returns true and sets *value if key is present. value may be NULL. */
bool bs_strmap_get(const bs_strmap *m, const char *key, void **value);

/* Case-insensitive FNV-1a. */
uint32_t bs_strhash_ci(const char *s);

#endif
