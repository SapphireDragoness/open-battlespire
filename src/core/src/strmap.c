#include "bs/core/strmap.h"

#include <stdlib.h>
#include <string.h>

#include "bs/core/fs.h"

static unsigned char lower(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + 32) : c;
}

uint32_t bs_strhash_ci(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h ^= lower((unsigned char)*s);
        h *= 16777619u;
    }
    return h;
}

void bs_strmap_init(bs_strmap *m)
{
    memset(m, 0, sizeof *m);
    bs_arena_init(&m->arena, 64 * 1024);
}

void bs_strmap_free(bs_strmap *m)
{
    free(m->slots);
    bs_arena_free(&m->arena);
    memset(m, 0, sizeof *m);
}

static bs_strmap_slot *find_slot(bs_strmap_slot *slots, size_t cap, const char *key, uint32_t hash)
{
    size_t i = hash & (cap - 1);
    for (;;) {
        bs_strmap_slot *s = &slots[i];
        if (!s->key || (s->hash == hash && bs_strcasecmp(s->key, key) == 0))
            return s;
        i = (i + 1) & (cap - 1);
    }
}

static bs_result grow(bs_strmap *m)
{
    size_t cap = m->cap ? m->cap * 2 : 64;
    bs_strmap_slot *slots = calloc(cap, sizeof *slots);
    if (!slots)
        return BS_ERR_NOMEM;
    for (size_t i = 0; i < m->cap; i++) {
        bs_strmap_slot *old = &m->slots[i];
        if (old->key)
            *find_slot(slots, cap, old->key, old->hash) = *old;
    }
    free(m->slots);
    m->slots = slots;
    m->cap = cap;
    return BS_OK;
}

bs_result bs_strmap_set(bs_strmap *m, const char *key, void *value)
{
    if ((m->count + 1) * 4 > m->cap * 3) {
        bs_result r = grow(m);
        if (r != BS_OK)
            return r;
    }
    uint32_t hash = bs_strhash_ci(key);
    bs_strmap_slot *s = find_slot(m->slots, m->cap, key, hash);
    if (!s->key) {
        char *copy = bs_arena_strdup(&m->arena, key);
        if (!copy)
            return BS_ERR_NOMEM;
        s->key = copy;
        s->hash = hash;
        m->count++;
    }
    s->value = value;
    return BS_OK;
}

bool bs_strmap_get(const bs_strmap *m, const char *key, void **value)
{
    if (!m->cap)
        return false;
    bs_strmap_slot *s = find_slot(m->slots, m->cap, key, bs_strhash_ci(key));
    if (!s->key)
        return false;
    if (value)
        *value = s->value;
    return true;
}
