#ifndef BS_CORE_ARENA_H
#define BS_CORE_ARENA_H

#include <stddef.h>
#include <stdint.h>

/*
 * Growable bump allocator. Memory is released all at once with
 * bs_arena_reset() or bs_arena_free(), or back to a saved mark with
 * bs_arena_rewind(). Allocations are zero-initialized.
 */

typedef struct bs_arena_block bs_arena_block;

typedef struct bs_arena {
    bs_arena_block *head;  /* current (most recent) block */
    size_t block_size;     /* default size for new blocks */
} bs_arena;

typedef struct bs_arena_mark {
    bs_arena_block *block;
    size_t used;
} bs_arena_mark;

#define BS_ARENA_DEFAULT_BLOCK ((size_t)1 << 20)

/* block_size of 0 selects BS_ARENA_DEFAULT_BLOCK. No memory is allocated until first use. */
void bs_arena_init(bs_arena *a, size_t block_size);
void bs_arena_free(bs_arena *a);

/* Returns NULL on allocation failure or size overflow. align must be a power of two. */
void *bs_arena_alloc(bs_arena *a, size_t size, size_t align);

/* Copies of memory and NUL-terminated strings. */
void *bs_arena_dup(bs_arena *a, const void *src, size_t size);
char *bs_arena_strdup(bs_arena *a, const char *s);

bs_arena_mark bs_arena_get_mark(const bs_arena *a);
void bs_arena_rewind(bs_arena *a, bs_arena_mark mark);

/* Frees all but the first block and empties it. */
void bs_arena_reset(bs_arena *a);

#define BS_ARENA_NEW(a, T) ((T *)bs_arena_alloc((a), sizeof(T), _Alignof(T)))
/* n elements of size bytes, or NULL if n * size overflows. */
static inline void *bs_arena_alloc_array(bs_arena *a, size_t n, size_t size, size_t align)
{
    if (size != 0 && n > SIZE_MAX / size)
        return NULL;
    return bs_arena_alloc(a, n * size, align);
}

#define BS_ARENA_ARRAY(a, T, n) ((T *)bs_arena_alloc_array((a), (size_t)(n), sizeof(T), _Alignof(T)))

#endif
