#include "bs/core/arena.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct bs_arena_block {
    bs_arena_block *prev;  /* older block */
    size_t size;           /* capacity of data[] */
    size_t used;
    _Alignas(max_align_t) unsigned char data[];
};

void bs_arena_init(bs_arena *a, size_t block_size)
{
    a->head = NULL;
    a->block_size = block_size ? block_size : BS_ARENA_DEFAULT_BLOCK;
}

void bs_arena_free(bs_arena *a)
{
    bs_arena_block *b = a->head;
    while (b) {
        bs_arena_block *prev = b->prev;
        free(b);
        b = prev;
    }
    a->head = NULL;
}

static size_t align_up(size_t v, size_t align)
{
    return (v + (align - 1)) & ~(align - 1);
}

void *bs_arena_alloc(bs_arena *a, size_t size, size_t align)
{
    if (align == 0 || (align & (align - 1)) != 0 || align > _Alignof(max_align_t))
        return NULL;

    bs_arena_block *b = a->head;
    if (b) {
        size_t start = align_up(b->used, align);
        if (start >= b->used && start <= b->size && size <= b->size - start) {
            b->used = start + size;
            void *p = b->data + start;
            memset(p, 0, size);
            return p;
        }
    }

    size_t cap = a->block_size;
    if (size > cap)
        cap = size;
    if (cap > SIZE_MAX - sizeof(bs_arena_block))
        return NULL;

    bs_arena_block *nb = malloc(sizeof(bs_arena_block) + cap);
    if (!nb)
        return NULL;
    nb->prev = a->head;
    nb->size = cap;
    nb->used = size;
    a->head = nb;
    memset(nb->data, 0, size);
    return nb->data;
}

void *bs_arena_dup(bs_arena *a, const void *src, size_t size)
{
    void *p = bs_arena_alloc(a, size ? size : 1, 1);
    if (p && size)
        memcpy(p, src, size);
    return p;
}

char *bs_arena_strdup(bs_arena *a, const char *s)
{
    size_t n = strlen(s) + 1;
    return bs_arena_dup(a, s, n);
}

bs_arena_mark bs_arena_get_mark(const bs_arena *a)
{
    bs_arena_mark m;
    m.block = a->head;
    m.used = a->head ? a->head->used : 0;
    return m;
}

void bs_arena_rewind(bs_arena *a, bs_arena_mark mark)
{
    while (a->head && a->head != mark.block) {
        bs_arena_block *prev = a->head->prev;
        free(a->head);
        a->head = prev;
    }
    if (a->head)
        a->head->used = mark.used;
}

void bs_arena_reset(bs_arena *a)
{
    if (!a->head)
        return;
    while (a->head->prev) {
        bs_arena_block *prev = a->head->prev;
        free(a->head);
        a->head = prev;
    }
    a->head->used = 0;
}
