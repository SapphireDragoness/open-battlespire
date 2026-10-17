#include "bs/game/save_edit.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/core/log.h"
#include "bs/game/use.h"

/* RE references are GAME.EXE addresses; see docs/formats/save.md. */

#define PLAYER_WEAPON 0x260
#define PLAYER_SECONDARY 0x264
#define PLAYER_HOTKEYS 0x1FE
#define PLAYER_HOTKEY_ITEMS 0x2E7
#define HOTKEY_ITEM 64
#define ITEM_WORN 0x20                /* header byte +0x14 */
#define VARS_LEAVING 15136
#define VARS_LEAVING_COPY 34709
#define VARS_GLOBAL_COUNT 15233
#define VARS_GLOBALS 15237

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int k = 0; k < 4; k++)
        p[k] = (uint8_t)(v >> (8 * k));
}

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void fail(char err[BS_SAVE_EDIT_ERROR], const char *fmt, ...) BS_PRINTF_FMT(2, 3);

static void fail(char err[BS_SAVE_EDIT_ERROR], const char *fmt, ...)
{
    if (!err)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, BS_SAVE_EDIT_ERROR, fmt, ap);
    va_end(ap);
}

/* ---- the file ---- */

bs_result bs_save_edit_open(bs_save_edit *e, const uint8_t *tree, size_t tree_size, const uint8_t *vars,
                            size_t vars_size)
{
    memset(e, 0, sizeof *e);
    if (vars_size < BS_SAVE_VARS_SIZE)
        return BS_ERR_FORMAT;
    memcpy(e->vars, vars, BS_SAVE_VARS_SIZE);
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_save_tree t;
    bs_result r = bs_save_tree_parse(tree, tree_size, &a, &t);
    if (r == BS_OK) {
        e->version = t.version;
        e->cap = t.count + 16;
        e->records = calloc(e->cap, sizeof *e->records);
        e->controls = malloc(t.controls_size + 1);
        if (!e->records || !e->controls)
            r = BS_ERR_NOMEM;
        for (uint32_t i = 0; r == BS_OK && i < t.count; i++) {
            e->records[i].object = malloc(t.records[i].size);
            if (!e->records[i].object) {
                r = BS_ERR_NOMEM;
                break;
            }
            memcpy(e->records[i].object, t.records[i].object, t.records[i].size);
            e->records[i].size = t.records[i].size;
            e->count++;
        }
        if (r == BS_OK) {
            memcpy(e->controls, t.controls, t.controls_size);
            e->controls_size = t.controls_size;
        }
    }
    bs_arena_free(&a);
    if (r != BS_OK)
        bs_save_edit_free(e);
    return r;
}

void bs_save_edit_free(bs_save_edit *e)
{
    for (uint32_t i = 0; i < e->count; i++)
        free(e->records[i].object);
    free(e->records);
    free(e->controls);
    e->records = NULL;
    e->controls = NULL;
    e->count = e->cap = 0;
}

int32_t bs_save_edit_find(const bs_save_edit *e, uint32_t id)
{
    for (uint32_t i = 0; i < e->count; i++)
        if (u32(e->records[i].object + 0x1D) == id)
            return (int32_t)i;
    return -1;
}

static uint8_t *player_body(const bs_save_edit *e)
{
    int32_t p = bs_save_edit_find(e, BS_SAVE_PLAYER_ID);
    if (p < 0 || e->records[p].object[0] != BS_SAVE_PLAYER || e->records[p].size < BS_SAVE_HEADER + 0x317)
        return NULL;
    return e->records[p].object + BS_SAVE_HEADER;
}

bs_result bs_save_edit_write(bs_save_edit *e, uint8_t **tree, size_t *tree_size)
{
    size_t n = 4 + 4 + e->controls_size;
    for (uint32_t i = 0; i < e->count; i++)
        n += 4 + e->records[i].size;
    uint8_t *out = malloc(n);
    if (!out)
        return BS_ERR_NOMEM;
    size_t at = 0;
    put32(out, e->version);
    at += 4;
    for (uint32_t i = 0; i < e->count; i++) {
        put32(out + at, e->records[i].size);
        memcpy(out + at + 4, e->records[i].object, e->records[i].size);
        at += 4 + e->records[i].size;
    }
    put32(out + at, 0);
    at += 4;
    memcpy(out + at, e->controls, e->controls_size);
    /* SAVEVARS.DAT's copy of the player (0x3078C reads it first, then the record replaces it). Its weapon and
     * secondary weapon are the pointers the game had in memory (it turns them into ids only for the tree,
     * 0x30F05): those stay as they were. */
    const uint8_t *pb = player_body(e);
    if (pb) {
        memcpy(e->vars, pb, PLAYER_WEAPON);
        memcpy(e->vars + PLAYER_SECONDARY + 4, pb + PLAYER_SECONDARY + 4, BS_SAVE_PLAYER_BODY - PLAYER_SECONDARY - 4);
    }
    *tree = out;
    *tree_size = n;
    return BS_OK;
}

/* ---- values ---- */

static bool parse_int(const char *s, long lo, long hi, long *out)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 0);
    if (errno || end == s || *end || v < lo || v > hi)
        return false;
    *out = v;
    return true;
}

static bool parse_float(const char *s, float *out)
{
    char *end;
    errno = 0;
    float v = strtof(s, &end);
    if (errno || end == s || *end)
        return false;
    *out = v;
    return true;
}

static bool parse_bool(const char *s, bool *out)
{
    if (!strcmp(s, "yes") || !strcmp(s, "true") || !strcmp(s, "1")) {
        *out = true;
        return true;
    }
    if (!strcmp(s, "no") || !strcmp(s, "false") || !strcmp(s, "0")) {
        *out = false;
        return true;
    }
    return false;
}

static const char *const race_codes[6] = {"rd", "br", "no", "hi", "dk", "wd"};

/* ---- the player's fields ---- */

typedef enum field_kind {
    F_STR,        /* a NUL-padded string of n bytes */
    F_PAIR,       /* an i32 with effects and its base: off, off2 */
    F_I32,        /* off, and off2 too if not 0 */
    F_U16,
    F_U8,
    F_LEVEL,      /* +0x176 and the class's byte +0x29F */
    F_FLAG,       /* off & n */
    F_RACE,
    F_POS,        /* the header's position, axis n */
    F_FACING,     /* the header's yaw */
    F_VARS,       /* an i32 in SAVEVARS.DAT */
} field_kind;

typedef struct field {
    const char *key;
    field_kind kind;
    uint32_t off, off2, n;
} field;

#define FIELDS (19 + 8 + BS_SKILL_COUNT)  /* 13 first, the attributes and skills, 6 last */

static void fields(field out[FIELDS])
{
    uint32_t n = 0;
    out[n++] = (field){"name", F_STR, 0x00, 0, 32};
    out[n++] = (field){"class", F_STR, 0x17A, 0, 24};
    out[n++] = (field){"female", F_FLAG, 0x226, 0, 8};
    out[n++] = (field){"race", F_RACE, 0x25D, 0, 0};
    out[n++] = (field){"hair", F_U8, 0x23E, 0, 0};
    out[n++] = (field){"eyes", F_U8, 0x23F, 0, 0};
    out[n++] = (field){"mouth", F_U8, 0x240, 0, 0};
    out[n++] = (field){"level", F_LEVEL, 0x176, 0x29F, 0};
    out[n++] = (field){"points", F_I32, 0x20A, 0, 0};
    out[n++] = (field){"health", F_I32, 0x66, 0, 0};
    out[n++] = (field){"health_max", F_I32, 0x6A, 0x6E, 0};
    out[n++] = (field){"spell_points", F_U16, 0x60, 0, 0};
    out[n++] = (field){"spell_points_max", F_U16, 0x62, 0x64, 0};
    for (uint32_t k = 0; k < 8; k++)
        out[n++] = (field){bs_attribute_keys[k], F_PAIR, 0x20 + 4 * k, 0x40 + 4 * k, 0};
    for (uint32_t k = 0; k < BS_SKILL_COUNT; k++)
        out[n++] = (field){bs_skill_keys[k], F_PAIR, 0x72 + 12 * k, 0x7A + 12 * k, 0};
    out[n++] = (field){"x", F_POS, 0, 0, 0};
    out[n++] = (field){"y", F_POS, 0, 0, 1};
    out[n++] = (field){"z", F_POS, 0, 0, 2};
    out[n++] = (field){"facing", F_FACING, 0, 0, 0};
    out[n++] = (field){"progress", F_U8, 0x25E, 0, 0};          /* the level script's (cogs placed ...) */
    out[n++] = (field){"level_number", F_VARS, 1051, 0, 0};
    (void)n; /* n == FIELDS */
}

const char *bs_save_edit_field(uint32_t i)
{
    field f[FIELDS];
    fields(f);
    return i < FIELDS ? f[i].key : NULL;
}

static bool find_field(const char *key, field *out)
{
    field f[FIELDS];
    fields(f);
    for (uint32_t i = 0; i < FIELDS; i++)
        if (!strcmp(f[i].key, key)) {
            *out = f[i];
            return true;
        }
    return false;
}

bool bs_save_edit_get(const bs_save_edit *e, const char *key, char *out, size_t cap)
{
    field f;
    uint8_t *b = player_body(e);
    if (!find_field(key, &f) || !b)
        return false;
    const uint8_t *hdr = b - BS_SAVE_HEADER;
    switch (f.kind) {
    case F_STR: {
        size_t n = 0;
        while (n < f.n && b[f.off + n])
            n++;
        snprintf(out, cap, "%.*s", (int)n, (const char *)b + f.off);
        break;
    }
    case F_PAIR:
        if ((int32_t)u32(b + f.off) == (int32_t)u32(b + f.off2))
            snprintf(out, cap, "%d", (int)(int32_t)u32(b + f.off2));
        else /* an effect on it now */
            snprintf(out, cap, "%d (now %d)", (int)(int32_t)u32(b + f.off2), (int)(int32_t)u32(b + f.off));
        break;
    case F_I32:
        snprintf(out, cap, "%d", (int)(int32_t)u32(b + f.off));
        break;
    case F_LEVEL: /* the player's is the class's byte; +0x176 is a monster's */
        snprintf(out, cap, "%u", (unsigned)(b[f.off2] ? b[f.off2] : u32(b + f.off)));
        break;
    case F_U16:
        snprintf(out, cap, "%u", (unsigned)u16(b + f.off));
        break;
    case F_U8:
        snprintf(out, cap, "%u", (unsigned)b[f.off]);
        break;
    case F_FLAG:
        snprintf(out, cap, "%s", (b[f.off] & f.n) ? "yes" : "no");
        break;
    case F_RACE:
        snprintf(out, cap, "%s", b[f.off] < 6 ? race_codes[b[f.off]] : "?");
        break;
    case F_POS: {
        uint32_t v = u32(hdr + 7 + 4 * f.n);
        float x;
        memcpy(&x, &v, sizeof x);
        snprintf(out, cap, "%g", (double)x);
        break;
    }
    case F_FACING:
        snprintf(out, cap, "%u", (unsigned)(u16(hdr + 3) & 0x7FF));
        break;
    case F_VARS:
        snprintf(out, cap, "%d", (int)(int32_t)u32(e->vars + f.off));
        break;
    }
    return true;
}

/* Whether a record is the player's, directly or in a container it carries. */
static bool carried(const bs_save_edit *e, uint32_t i)
{
    for (uint32_t guard = 0; guard <= e->count; guard++) {
        uint32_t parent = u32(e->records[i].object + 0x39);
        if (parent == BS_SAVE_PLAYER_ID)
            return true;
        int32_t p = bs_save_edit_find(e, parent);
        if (p < 0 || e->records[p].object[0] != BS_SAVE_ITEM)
            return false;
        i = (uint32_t)p;
    }
    return false;
}

/* The player's weapons, clothes and armour show the pictures of the player's sex (0x54219, 0x54522). */
static void sex_pictures(bs_save_edit *e, bool female)
{
    for (uint32_t i = 0; i < e->count; i++) {
        uint8_t *o = e->records[i].object;
        if (o[0] != BS_SAVE_ITEM || e->records[i].size < BS_SAVE_HEADER + 0x2C || !carried(e, i))
            continue;
        uint16_t base = u16(o + BS_SAVE_HEADER + 0x20);
        uint8_t *pic = o + BS_SAVE_HEADER + 0x22;
        if (pic[0] && (bs_item_is_weapon(base) || base == BS_ITEM_CLOTHES || bs_item_is_armour(base)))
            pic[0] = female ? 'F' : 'M';
    }
}

bool bs_save_edit_set(bs_save_edit *e, const char *key, const char *value, char err[BS_SAVE_EDIT_ERROR])
{
    field f;
    if (!find_field(key, &f)) {
        fail(err, "no field '%s'", key);
        return false;
    }
    uint8_t *b = player_body(e);
    if (!b) {
        fail(err, "%s", "no player record");
        return false;
    }
    uint8_t *hdr = b - BS_SAVE_HEADER;
    long v;
    switch (f.kind) {
    case F_STR: {
        size_t n = strlen(value);
        if (n >= f.n) {
            fail(err, "'%s' is too long", value);
            return false;
        }
        memset(b + f.off, 0, f.n);
        memcpy(b + f.off, value, n);
        return true;
    }
    case F_PAIR:
    case F_I32:
    case F_LEVEL:
    case F_VARS:
        if (!parse_int(value, -1000000, 1000000, &v))
            break;
        if (f.kind == F_VARS) {
            put32(e->vars + f.off, (uint32_t)v);
            return true;
        }
        put32(b + f.off, (uint32_t)v);
        if (f.kind == F_LEVEL)
            b[f.off2] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        else if (f.off2)
            put32(b + f.off2, (uint32_t)v);
        return true;
    case F_U16:
        if (!parse_int(value, 0, 65535, &v))
            break;
        put16(b + f.off, (uint16_t)v);
        if (f.off2)
            put16(b + f.off2, (uint16_t)v);
        return true;
    case F_U8:
        if (!parse_int(value, 0, 255, &v))
            break;
        b[f.off] = (uint8_t)v;
        return true;
    case F_FLAG: {
        bool on;
        if (!parse_bool(value, &on))
            break;
        b[f.off] = (uint8_t)(on ? b[f.off] | f.n : b[f.off] & ~f.n);
        if (!strcmp(f.key, "female"))
            sex_pictures(e, on);
        return true;
    }
    case F_RACE:
        for (int r = 0; r < 6; r++)
            if (!strcmp(value, race_codes[r])) {
                b[f.off] = (uint8_t)r;
                return true;
            }
        if (!parse_int(value, 0, 5, &v))
            break;
        b[f.off] = (uint8_t)v;
        return true;
    case F_POS: {
        float x;
        if (!parse_float(value, &x))
            break;
        uint32_t bits;
        memcpy(&bits, &x, sizeof bits);
        put32(hdr + 7 + 4 * f.n, bits);
        return true;
    }
    case F_FACING:
        if (!parse_int(value, 0, 2047, &v))
            break;
        put16(hdr + 3, (uint16_t)v);
        return true;
    }
    fail(err, "bad value '%s'", value);
    return false;
}

/* ---- items ---- */

void bs_save_item_body(const uint8_t base_record[0x2F2], const bs_item *it, const uint8_t *magic,
                       uint8_t out[BS_SAVE_ITEM_BODY])
{
    memset(out, 0, BS_SAVE_ITEM_BODY);
    memcpy(out, base_record, 0x2F2);
    put32(out + 0x2B, (uint32_t)it->condition);
    put32(out + 0x2F, (uint32_t)it->condition_max);
    bool numbered = it->base == BS_ITEM_SCROLL || it->base == BS_ITEM_BOOK || it->base == BS_ITEM_SIGIL_AMULET;
    put16(out + 0x33, (uint16_t)(numbered ? it->number : it->material));
    out[0x39] = it->base == BS_ITEM_POTION ? (uint8_t)it->number : 0xFF;
    if (it->base == BS_ITEM_ARROW)
        put32(out + 0x3E, (uint32_t)it->quantity);
    memset(out + 0x22, 0, 9);
    size_t pn = 0;
    while (pn < 9 && it->picture[pn])
        pn++;
    memcpy(out + 0x22, it->picture, pn);
    if (magic) {
        memcpy(out + BS_SAVE_MAGIC_AT, magic, BS_SAVE_MAGIC_SIZE);
        put32(out + 0x2E2, (uint32_t)it->magic);
    }
    out[0x2C3] = 0xFF; /* (0x521AB) */
}

const uint8_t *bs_save_edit_find_magic(const bs_save_edit *e, int32_t magic)
{
    for (uint32_t i = 0; i < e->count; i++) {
        const bs_save_edit_record *r = &e->records[i];
        if (r->object[0] == BS_SAVE_ITEM && r->size >= BS_SAVE_HEADER + 0x2E6 &&
            (int32_t)u32(r->object + BS_SAVE_HEADER + 0x2E2) == magic)
            return r->object + BS_SAVE_HEADER + BS_SAVE_MAGIC_AT;
    }
    return NULL;
}

static uint32_t next_id(const bs_save_edit *e)
{
    uint32_t max = 0;
    for (uint32_t i = 0; i < e->count; i++) {
        uint32_t id = u32(e->records[i].object + 0x1D);
        max = id > max ? id : max;
    }
    return max + 1;
}

uint32_t bs_save_edit_give(bs_save_edit *e, const uint8_t body[BS_SAVE_ITEM_BODY], uint32_t parent)
{
    int32_t p = bs_save_edit_find(e, parent);
    if (p < 0 || (e->records[p].object[0] != BS_SAVE_PLAYER && e->records[p].object[0] != BS_SAVE_ITEM))
        return 0;
    if (e->count == e->cap) {
        uint32_t cap = e->cap * 2 + 16;
        bs_save_edit_record *more = realloc(e->records, sizeof *more * cap);
        if (!more)
            return 0;
        e->records = more;
        e->cap = cap;
    }
    uint32_t size = BS_SAVE_HEADER + BS_SAVE_ITEM_BODY;
    uint8_t *o = calloc(1, size);
    if (!o)
        return 0;
    uint32_t id = next_id(e);
    o[0] = BS_SAVE_ITEM;
    memcpy(o + 7, e->records[p].object + 7, 12); /* the parent's place (0x521AB) */
    o[0x13] = 1;
    put32(o + 0x1D, id);
    put16(o + 0x25, 0xFFFF);
    put16(o + 0x27, 0xFFFF);
    put32(o + 0x39, parent);
    memcpy(o + BS_SAVE_HEADER, body, BS_SAVE_ITEM_BODY);
    /* Right after the parent: the game writes a parent before its children. */
    uint32_t at = (uint32_t)p + 1;
    memmove(e->records + at + 1, e->records + at, sizeof *e->records * (e->count - at));
    e->records[at] = (bs_save_edit_record){o, size};
    e->count++;
    return id;
}

bool bs_save_edit_remove(bs_save_edit *e, uint32_t id, char err[BS_SAVE_EDIT_ERROR])
{
    int32_t i = bs_save_edit_find(e, id);
    if (i < 0 || (e->records[i].object[0] != BS_SAVE_ITEM && e->records[i].object[0] != BS_SAVE_MONSTER)) {
        char s[16];
        snprintf(s, sizeof s, "%u", (unsigned)id);
        fail(err, "no item or monster %s", s);
        return false;
    }
    /* It and everything under it. */
    bool *gone = calloc(e->count, sizeof *gone);
    if (!gone) {
        fail(err, "%s", "out of memory");
        return false;
    }
    gone[i] = true;
    for (bool more = true; more;) {
        more = false;
        for (uint32_t k = 0; k < e->count; k++) {
            if (gone[k])
                continue;
            int32_t p = bs_save_edit_find(e, u32(e->records[k].object + 0x39));
            if (p >= 0 && gone[p] && !(e->records[k].object[0] == BS_SAVE_MONSTER && (e->records[k].object[0x14] & 2))) {
                gone[k] = more = true;
            }
        }
    }
    uint8_t *pb = player_body(e);
    uint32_t n = 0;
    for (uint32_t k = 0; k < e->count; k++) {
        if (!gone[k]) {
            e->records[n++] = e->records[k];
            continue;
        }
        uint32_t rid = u32(e->records[k].object + 0x1D);
        /* What pointed at it: the weapons, the hot keys, a speaker leaving. */
        if (pb) {
            if (u32(pb + PLAYER_WEAPON) == rid)
                put32(pb + PLAYER_WEAPON, 0);
            if (u32(pb + PLAYER_SECONDARY) == rid)
                put32(pb + PLAYER_SECONDARY, 0);
            for (int h = 0; h < 8; h++)
                if (u32(pb + PLAYER_HOTKEY_ITEMS + 4 * h) == rid) {
                    put32(pb + PLAYER_HOTKEY_ITEMS + 4 * h, 0);
                    for (int f = 0; f < 8; f++)
                        if (pb[PLAYER_HOTKEYS + f] == HOTKEY_ITEM + h)
                            pb[PLAYER_HOTKEYS + f] = 0;
                }
        }
        for (int l = 0; l < 8; l++)
            if (u32(e->vars + VARS_LEAVING + 12 * l) == rid) {
                memset(e->vars + VARS_LEAVING + 12 * l, 0, 12);
                memset(e->vars + VARS_LEAVING_COPY + 12 * l, 0, 12);
            }
        free(e->records[k].object);
    }
    e->count = n;
    free(gone);
    return true;
}

bool bs_save_edit_item_set(bs_save_edit *e, uint32_t id, const char *key, const char *value,
                           char err[BS_SAVE_EDIT_ERROR])
{
    int32_t i = bs_save_edit_find(e, id);
    if (i < 0 || e->records[i].object[0] != BS_SAVE_ITEM || e->records[i].size < BS_SAVE_HEADER + BS_SAVE_ITEM_BODY) {
        char s[16];
        snprintf(s, sizeof s, "%u", (unsigned)id);
        fail(err, "no item %s", s);
        return false;
    }
    uint8_t *o = e->records[i].object, *b = o + BS_SAVE_HEADER;
    uint16_t base = u16(b + 0x20);
    long v;
    bool on;
    if (!strcmp(key, "condition") || !strcmp(key, "condition_max") || !strcmp(key, "quantity")) {
        if (!parse_int(value, 0, 1000000, &v)) {
            fail(err, "bad value '%s'", value);
            return false;
        }
        put32(b + (!strcmp(key, "condition") ? 0x2B : !strcmp(key, "condition_max") ? 0x2F : 0x3E), (uint32_t)v);
        return true;
    }
    if (!strcmp(key, "material") || !strcmp(key, "number")) {
        bool numbered = base == BS_ITEM_SCROLL || base == BS_ITEM_BOOK || base == BS_ITEM_SIGIL_AMULET;
        if (!parse_int(value, 0, 65535, &v)) {
            fail(err, "bad value '%s'", value);
            return false;
        }
        if (!strcmp(key, "number") && base == BS_ITEM_POTION && v < 256) {
            b[0x39] = (uint8_t)v;
            return true;
        }
        if ((strcmp(key, "number") == 0) != numbered) {
            fail(err, "this item has no %s", key);
            return false;
        }
        put16(b + 0x33, (uint16_t)v);
        return true;
    }
    if (!strcmp(key, "worn") || !strcmp(key, "secondary")) {
        uint8_t *pb = player_body(e);
        if (!parse_bool(value, &on)) {
            fail(err, "bad value '%s'", value);
            return false;
        }
        if (!pb || u32(o + 0x39) != BS_SAVE_PLAYER_ID) {
            fail(err, "%s", "the player doesn't carry it");
            return false;
        }
        if (!strcmp(key, "secondary")) {
            if (on && !bs_item_is_weapon(base)) {
                fail(err, "%s", "only a weapon can be the secondary one");
                return false;
            }
            put32(pb + PLAYER_SECONDARY, on ? id : u32(pb + PLAYER_SECONDARY) == id ? 0 : u32(pb + PLAYER_SECONDARY));
            return true;
        }
        if (on && bs_item_is_weapon(base)) {
            /* The weapon in hand (+0x260): the one there before is put away. */
            int32_t old = bs_save_edit_find(e, u32(pb + PLAYER_WEAPON));
            if (old >= 0 && old != i)
                e->records[old].object[0x14] &= (uint8_t)~ITEM_WORN;
            put32(pb + PLAYER_WEAPON, id);
        } else if (!on && u32(pb + PLAYER_WEAPON) == id) {
            put32(pb + PLAYER_WEAPON, 0);
        }
        o[0x14] = (uint8_t)(on ? o[0x14] | ITEM_WORN : o[0x14] & ~ITEM_WORN);
        return true;
    }
    fail(err, "no item field '%s'", key);
    return false;
}

/* ---- variables ---- */

bool bs_save_edit_set_var(bs_save_edit *e, const char *name, int32_t value, char err[BS_SAVE_EDIT_ERROR])
{
    long h;
    uint32_t hash = parse_int(name, 0, 0x7FFFFFFF, &h) ? (uint32_t)h : bs_save_variable_hash(name);
    if (!name[0] || !hash) {
        fail(err, "bad variable '%s'", name);
        return false;
    }
    uint32_t count = u32(e->vars + VARS_GLOBAL_COUNT);
    count = count > BS_SAVE_GLOBALS ? BS_SAVE_GLOBALS : count;
    for (uint32_t i = 0; i < count; i++)
        if (u32(e->vars + VARS_GLOBALS + 8 * i) == hash) {
            put32(e->vars + VARS_GLOBALS + 8 * i + 4, (uint32_t)value);
            return true;
        }
    if (count == BS_SAVE_GLOBALS) {
        fail(err, "no room for '%s'", name);
        return false;
    }
    put32(e->vars + VARS_GLOBALS + 8 * count, hash);
    put32(e->vars + VARS_GLOBALS + 8 * count + 4, (uint32_t)value);
    put32(e->vars + VARS_GLOBAL_COUNT, count + 1);
    return true;
}
