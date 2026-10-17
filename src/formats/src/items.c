#include "bs/formats/items.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bs/formats/exe_text.h"

#define BASE_SIZE 0x2F2
#define WEAPON_SIZE 0x18

static int32_t i32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static void copy_str(char *dst, size_t cap, const uint8_t *src, size_t max)
{
    size_t n = 0;
    while (n < max && n + 1 < cap && src[n])
        n++;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static const uint8_t *find(const uint8_t *hay, size_t size, const void *needle, size_t n, const uint8_t *from)
{
    for (const uint8_t *p = from; p + n <= hay + size; p++)
        if (*p == *(const uint8_t *)needle && memcmp(p, needle, n) == 0)
            return p;
    return NULL;
}

/* The base table: "Dagger" with index 0, "Short Sword" one record on, and room for all 43. */
static const uint8_t *find_bases(const uint8_t *exe, size_t size)
{
    for (const uint8_t *p = find(exe, size, "Dagger", 7, exe); p; p = find(exe, size, "Dagger", 7, p + 1)) {
        size_t off = (size_t)(p - exe);
        if (off >= (size_t)BS_ITEM_WEAPON_COUNT * WEAPON_SIZE && size - off >= (size_t)BS_ITEM_BASE_COUNT * BASE_SIZE &&
            u16(p + 0x20) == 0 && memcmp(p + BASE_SIZE, "Short Sword", 12) == 0 && u16(p + BASE_SIZE + 0x20) == 1)
            return p;
    }
    return NULL;
}

bs_result bs_item_tables_read(const uint8_t *exe, size_t size, bs_item_tables *out)
{
    memset(out, 0, sizeof *out);
    const uint8_t *t = find_bases(exe, size);
    if (!t)
        return BS_ERR_NOT_FOUND;
    for (uint32_t i = 0; i < BS_ITEM_BASE_COUNT; i++) {
        const uint8_t *r = t + i * BASE_SIZE;
        if (u16(r + 0x20) != i)
            return BS_ERR_FORMAT;
        bs_item_base *b = &out->bases[i];
        copy_str(b->name, sizeof b->name, r, 32);
        copy_str(b->icon, sizeof b->icon, r + 0x22, 9);
        b->condition = i32(r + 0x2B);
        b->image = u16(r + 0x33);
        b->weight = u16(r + 0x35);
        b->container = r[0x42] & 1;
    }
    /* The weapon stats are the 16 records of 24 bytes right before. */
    const uint8_t *w = t - BS_ITEM_WEAPON_COUNT * WEAPON_SIZE;
    for (uint32_t i = 0; i < BS_ITEM_WEAPON_COUNT; i++) {
        const uint8_t *r = w + i * WEAPON_SIZE;
        out->weapons[i] = (bs_weapon_stats){i32(r), i32(r + 4), i32(r + 8), i32(r + 12), i32(r + 16), i32(r + 20)};
    }
    if (out->weapons[0].skill != 14 || out->weapons[BS_ITEM_WEAPON_COUNT - 1].skill != 16)
        return BS_ERR_FORMAT;
    /* The material names: ten strings in a row from "Iron". */
    const uint8_t *m = find(exe, size, "\0Iron\0Steel\0", 12, exe);
    if (!m)
        return BS_ERR_NOT_FOUND;
    m++;
    for (uint32_t i = 0; i < BS_ITEM_MATERIAL_COUNT; i++) {
        size_t left = size - (size_t)(m - exe);
        const uint8_t *nul = memchr(m, 0, left);
        if (!nul || nul == m)
            return BS_ERR_FORMAT;
        copy_str(out->materials[i], sizeof out->materials[i], m, (size_t)(nul - m));
        m = nul + 1;
    }
    return BS_OK;
}

const uint8_t *bs_item_base_record(const uint8_t *exe, size_t size, uint32_t base)
{
    const uint8_t *t = base < BS_ITEM_BASE_COUNT ? find_bases(exe, size) : NULL;
    return t ? t + (size_t)base * BASE_SIZE : NULL;
}

bs_result bs_item_models_read(const bs_le_image *img, bs_item_tables *out)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_exe_text t;
    bs_result r = bs_exe_string_table(img, "bataxe.3d", BS_ITEM_MODEL_COUNT, &a, &t);
    if (r == BS_OK && t.count < BS_ITEM_MODEL_COUNT)
        r = BS_ERR_FORMAT;
    for (uint32_t i = 0; r == BS_OK && i < BS_ITEM_MODEL_COUNT; i++)
        snprintf(out->models[i], sizeof out->models[i], "%s", t.messages[i]);
    bs_arena_free(&a);
    return r;
}

bs_result bs_item_hand_models_read(const bs_le_image *img, bs_item_tables *out)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_exe_text t;
    bs_result r = bs_exe_string_table(img, "edagger.3d", BS_ITEM_LAST_WEAPON + 1, &a, &t);
    if (r == BS_OK && t.count < BS_ITEM_LAST_WEAPON + 1)
        r = BS_ERR_FORMAT;
    for (uint32_t i = 0; r == BS_OK && i <= BS_ITEM_LAST_WEAPON; i++)
        snprintf(out->hand_models[i], sizeof out->hand_models[i], "%s", t.messages[i]);
    bs_arena_free(&a);
    return r;
}

const char *bs_item_model_name(const bs_item_tables *t, uint32_t base, int32_t sigil_id, char buf[16])
{
    buf[0] = '\0';
    if (base == BS_ITEM_SIGIL_AMULET && sigil_id >= 9001) {
        int32_t i = sigil_id - 9001;
        if (i > 25)
            i -= 26;
        if (i >= 0 && i <= 25) {
            char c = (char)('a' + i);
            if (c == 'x' || c == 'y')
                c = 'z';
            snprintf(buf, 16, "sigil%c.3d", c);
            return buf;
        }
    }
    if (base < BS_ITEM_BASE_COUNT && t->bases[base].image < BS_ITEM_MODEL_COUNT)
        snprintf(buf, 16, "%s", t->models[t->bases[base].image]);
    return buf;
}

/* ---- MG*.TXT ---- */

typedef struct span {
    const char *p;
    size_t n;
} span;

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static span trim(span s)
{
    while (s.n && is_space(s.p[0])) {
        s.p++;
        s.n--;
    }
    while (s.n && is_space(s.p[s.n - 1]))
        s.n--;
    return s;
}

static bool key_is(span k, const char *name)
{
    return k.n == strlen(name) && memcmp(k.p, name, k.n) == 0;
}

/* A comma-separated list of integers; returns the count, or -1 if malformed or too long. */
static int parse_list(span s, int32_t *out, int max)
{
    int n = 0;
    size_t i = 0;
    while (i < s.n) {
        size_t start = i;
        while (i < s.n && s.p[i] != ',')
            i++;
        span item = trim((span){s.p + start, i - start});
        char buf[16];
        if (item.n == 0 || item.n >= sizeof buf || n >= max)
            return -1;
        memcpy(buf, item.p, item.n);
        buf[item.n] = '\0';
        char *end;
        errno = 0;
        long v = strtol(buf, &end, 10);
        if (*end || errno || v < INT32_MIN || v > INT32_MAX)
            return -1;
        out[n++] = (int32_t)v;
        i++; /* the comma */
    }
    return n;
}

static bool parse_one(span s, int32_t *out)
{
    return parse_list(s, out, 1) == 1;
}

/* The name without its quotes: plain (") or curly (0x93, 0x94). */
static bool parse_name(span s, char *dst, size_t cap)
{
    size_t n = 0;
    for (size_t i = 0; i < s.n; i++) {
        unsigned char c = (unsigned char)s.p[i];
        if (c == '"' || c == 0x93 || c == 0x94)
            continue;
        if (n + 1 >= cap)
            return false;
        dst[n++] = (char)c;
    }
    dst[n] = '\0';
    return true;
}

enum { F_NAME = 1, F_ID = 2, F_ITEM = 4, F_SPELL = 8, F_APP = 16, F_ADV = 32, F_USES = 64, F_LEVEL = 128, F_ALL = 255 };

static bs_result parse_field(span key, span val, bs_magic *m, unsigned *seen)
{
    int n;
    if (key_is(key, "Name")) {
        *seen |= F_NAME;
        return parse_name(val, m->name, sizeof m->name) ? BS_OK : BS_ERR_FORMAT;
    } else if (key_is(key, "ID#")) {
        *seen |= F_ID;
        return parse_one(val, &m->id) ? BS_OK : BS_ERR_FORMAT;
    } else if (key_is(key, "Item")) {
        *seen |= F_ITEM;
        return parse_one(val, &m->item) ? BS_OK : BS_ERR_FORMAT;
    } else if (key_is(key, "Spell")) {
        *seen |= F_SPELL;
        return parse_list(val, m->spell, 5) == 5 ? BS_OK : BS_ERR_FORMAT;
    } else if (key_is(key, "App")) {
        *seen |= F_APP;
        n = parse_list(val, m->app, BS_MAGIC_LIST_MAX);
        m->app_count = (uint8_t)(n > 0 ? n : 0);
        return n > 0 ? BS_OK : BS_ERR_FORMAT;
    } else if (key_is(key, "Adv")) {
        *seen |= F_ADV;
        n = parse_list(val, m->adv, BS_MAGIC_LIST_MAX);
        m->adv_count = (uint8_t)(n > 0 ? n : 0);
        return n > 0 ? BS_OK : BS_ERR_FORMAT;
    } else if (key_is(key, "Uses")) {
        *seen |= F_USES;
        return parse_one(val, &m->uses) ? BS_OK : BS_ERR_FORMAT;
    } else if (key_is(key, "Level")) {
        *seen |= F_LEVEL;
        return parse_one(val, &m->level) ? BS_OK : BS_ERR_FORMAT;
    }
    return BS_ERR_FORMAT;
}

bs_result bs_magic_parse(const uint8_t *text, size_t size, bs_arena *arena, bs_magic_table *out)
{
    memset(out, 0, sizeof *out);
    const char *s = (const char *)text;
    size_t names = 0;
    for (size_t i = 0; i + 4 <= size; i++)
        names += (i == 0 || s[i - 1] == '\n') && memcmp(s + i, "Name", 4) == 0;
    bs_magic *recs = BS_ARENA_ARRAY(arena, bs_magic, names ? names : 1);
    if (!recs)
        return BS_ERR_NOMEM;

    /* Records are runs of "Key<TAB>value" lines; blank lines end them. */
    size_t count = 0, pos = 0;
    unsigned seen = 0;
    while (pos <= size) {
        const char *nl = pos < size ? memchr(s + pos, '\n', size - pos) : NULL;
        size_t len = nl ? (size_t)(nl - (s + pos)) : size - pos;
        span line = trim((span){s + pos, len});
        pos += len + 1;
        if (line.n == 1 && line.p[0] == '\x1a')
            line.n = 0; /* DOS EOF */
        if (line.n == 0) {
            if (seen) {
                if (seen != F_ALL)
                    return BS_ERR_FORMAT;
                count++;
                seen = 0;
            }
            continue;
        }
        const char *tab = memchr(line.p, '\t', line.n);
        if (!tab)
            return BS_ERR_FORMAT;
        span key = trim((span){line.p, (size_t)(tab - line.p)});
        span val = trim((span){tab + 1, line.n - (size_t)(tab + 1 - line.p)});
        if (!seen) {
            if (count >= names)
                return BS_ERR_FORMAT;
            memset(&recs[count], 0, sizeof recs[count]);
        }
        unsigned before = seen;
        bs_result r = parse_field(key, val, &recs[count], &seen);
        if (r != BS_OK || seen == before)
            return BS_ERR_FORMAT; /* unknown or repeated field */
    }
    out->records = recs;
    out->count = count;
    return BS_OK;
}

const bs_magic *bs_magic_find(const bs_magic_table *t, int32_t id)
{
    for (size_t i = 0; i < t->count; i++)
        if (t->records[i].id == id)
            return &t->records[i];
    return NULL;
}

bool bs_bs6_logic_has_items(const bs_bs6_logic *logic)
{
    return bs_bs6_logic_prop(logic, BS_BS6_PROP_ITEM) || bs_bs6_logic_prop(logic, BS_BS6_PROP_BOOK) ||
           bs_bs6_logic_prop(logic, BS_BS6_PROP_POTION) || bs_bs6_logic_prop(logic, BS_BS6_PROP_CONTENTS);
}
