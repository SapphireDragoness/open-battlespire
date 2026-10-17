#include "bs/formats/save.h"

#include <string.h>

/* RE references are GAME.EXE addresses; see docs/formats/save.md. */

#define TREE_RECORDS_MAX 65536

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int32_t i32(const uint8_t *p)
{
    return (int32_t)u32(p);
}

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static float f32(const uint8_t *p)
{
    uint32_t v = u32(p);
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

static void copy_str(char *dst, size_t cap, const uint8_t *src, size_t max)
{
    size_t n = 0;
    while (n < max && n + 1 < cap && src[n])
        n++;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* ---- records ---- */

static void header(bs_save_record *r, const uint8_t *o, uint32_t size)
{
    memset(r, 0, sizeof *r);
    r->type = o[0];
    for (int k = 0; k < 3; k++) {
        r->angles[k] = u16(o + 1 + 2 * k);
        r->pos[k] = f32(o + 7 + 4 * k);
    }
    r->flags = u16(o + 0x13);
    r->file_id = u32(o + 0x15);
    r->id = u32(o + 0x1D);
    r->time = u32(o + 0x21);
    r->link = (int16_t)u16(o + 0x25);
    r->link2 = (int16_t)u16(o + 0x27);
    r->parent = u32(o + 0x39);
    r->parent_index = -1;
    r->reserve = r->type == BS_SAVE_MONSTER && (o[0x14] & 2);
    r->object = o;
    r->size = size;
    r->body = o + BS_SAVE_HEADER;
    r->body_size = size - BS_SAVE_HEADER;
}

bs_result bs_save_records_read(const uint8_t *data, size_t size, bs_arena *arena, bs_save_record **out,
                               uint32_t *count, size_t *used)
{
    /* Count first, then fill. */
    uint32_t n = 0;
    size_t at = 0;
    while (at + 4 <= size) {
        uint32_t len = u32(data + at);
        if (len == 0) {
            at += 4;
            break;
        }
        if (len < BS_SAVE_HEADER || len > size - at - 4 || n == TREE_RECORDS_MAX)
            return BS_ERR_FORMAT;
        at += 4 + len;
        n++;
    }
    bs_save_record *r = n ? BS_ARENA_ARRAY(arena, bs_save_record, n) : NULL;
    if (n && !r)
        return BS_ERR_NOMEM;
    size_t p = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t len = u32(data + p);
        header(&r[i], data + p + 4, len);
        p += 4 + len;
    }
    /* Parents by id; the game writes a parent before its children (0x2FBC4), but don't count on it. */
    for (uint32_t i = 0; i < n; i++) {
        if (r[i].parent == BS_SAVE_PARENT_ROOT || r[i].parent == BS_SAVE_PARENT_LEVEL || r[i].reserve)
            continue;
        for (uint32_t j = 0; j < n; j++)
            if (j != i && r[j].id == r[i].parent) {
                r[i].parent_index = (int32_t)j;
                break;
            }
    }
    *out = r;
    *count = n;
    if (used)
        *used = at;
    return BS_OK;
}

bs_result bs_save_tree_parse(const uint8_t *data, size_t size, bs_arena *arena, bs_save_tree *out)
{
    memset(out, 0, sizeof *out);
    if (size < 8)
        return BS_ERR_FORMAT;
    out->version = u32(data);
    if (out->version < 0x100) /* "Invalid save game format." (0x303FE) */
        return BS_ERR_FORMAT;
    size_t used;
    bs_result r = bs_save_records_read(data + 4, size - 4, arena, &out->records, &out->count, &used);
    if (r != BS_OK)
        return r;
    out->controls = data + 4 + used;
    out->controls_size = size - 4 - used;
    return BS_OK;
}

int32_t bs_save_find(const bs_save_tree *t, uint32_t id)
{
    for (uint32_t i = 0; i < t->count; i++)
        if (t->records[i].id == id)
            return (int32_t)i;
    return -1;
}

bs_result bs_save_controllers_read(const uint8_t *data, size_t size, const uint32_t *chain_lengths, uint32_t chains,
                                   bs_arena *arena, bs_save_controller **out, uint32_t *count, uint8_t **active,
                                   uint32_t *active_count)
{
    uint32_t n = 0;
    for (uint32_t c = 0; c < chains; c++)
        n += chain_lengths[c];
    bs_save_controller *ctl = n ? BS_ARENA_ARRAY(arena, bs_save_controller, n) : NULL;
    if (n && !ctl)
        return BS_ERR_NOMEM;
    size_t at = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (at + 1 > size)
            return BS_ERR_FORMAT;
        memset(&ctl[i], 0, sizeof ctl[i]);
        ctl[i].flags = data[at++];
        if (ctl[i].flags & 1) { /* running (0x4C354) */
            if (at + 44 > size)
                return BS_ERR_FORMAT;
            ctl[i].object = u32(data + at);
            memcpy(ctl[i].values, data + at + 4, sizeof ctl[i].values);
            ctl[i].start = u32(data + at + 40);
            at += 44;
        }
    }
    if (at + 4 > size)
        return BS_ERR_FORMAT;
    uint32_t na = u32(data + at);
    at += 4;
    if (na > size - at)
        return BS_ERR_FORMAT;
    uint8_t *a = na ? BS_ARENA_ARRAY(arena, uint8_t, na) : NULL;
    if (na && !a)
        return BS_ERR_NOMEM;
    if (na)
        memcpy(a, data + at, na);
    *out = ctl;
    *count = n;
    *active = a;
    *active_count = na;
    return BS_OK;
}

/* ---- typed records ---- */

void bs_save_character_parse(const uint8_t *body, size_t size, bs_save_character *out)
{
    uint8_t b[BS_SAVE_PLAYER_BODY] = {0};
    memcpy(b, body, size < sizeof b ? size : sizeof b);
    memset(out, 0, sizeof *out);
    copy_str(out->name, sizeof out->name, b, 32);
    for (int k = 0; k < 8; k++) {
        out->attributes[k] = i32(b + 0x20 + 4 * k);
        out->attributes_base[k] = i32(b + 0x40 + 4 * k);
    }
    out->spell_points = u16(b + 0x60);
    out->spell_points_max = u16(b + 0x62);
    out->spell_points_base = u16(b + 0x64);
    out->wounds = i32(b + 0x66);
    out->wounds_max = i32(b + 0x6A);
    out->wounds_base = i32(b + 0x6E);
    for (int k = 0; k < BS_SCL_SKILLS; k++) {
        out->skills[k] = i32(b + 0x72 + 12 * k);
        out->skill_counts[k] = i32(b + 0x76 + 12 * k);
        out->skills_base[k] = i32(b + 0x7A + 12 * k);
    }
    out->shield = i32(b + 0x16E);
    out->effects = u32(b + 0x172);
    out->monster_level = i32(b + 0x176);
    memcpy(out->hotkeys, b + 0x1FE, sizeof out->hotkeys);
    out->points = i32(b + 0x20A);
    out->flags = u32(b + 0x226);
    out->music_volume = i32(b + 0x236);
    out->hair = b[0x23E];
    out->eyes = b[0x23F];
    out->mouth = b[0x240];
    out->sound_volume = i32(b + 0x24A);
    out->creature = i32(b + 0x24E);
    out->race = b[0x25D];
    out->weapon = u32(b + 0x260);
    out->secondary = u32(b + 0x264);
    for (int k = 0; k < 7; k++)
        out->armour[k] = i32(b + 0x268 + 4 * k);
    out->level = b[0x29F];
    for (int k = 0; k < 3; k++)
        out->anchor[k] = f32(b + 0x2B0 + 4 * k);
    out->water_breathing_until = u32(b + 0x2C0);
    out->turn_rate = b[0x2E0];
    for (int k = 0; k < 8; k++)
        out->hotkey_items[k] = u32(b + 0x2E7 + 4 * k);
    out->confused_until = u32(b + 0x30B);
    bs_class_player_parse(b, &out->cls);
}

void bs_save_item_parse(const bs_save_record *r, bs_scl_item *out)
{
    bs_scl_item_parse(r->object[0x14], r->body, r->body_size, out);
}

/* ---- SAVEVARS.DAT (0x30A7D, 0x5AA46) ---- */

enum {
    VARS_PLAYER = 0,
    VARS_MISC = 1051,                 /* after 0x104 bytes of 0x16C088 */
    VARS_LOG = 1075,
    VARS_VOLUMES = 2423,              /* after 0x40 bytes of 0x112724 and 0x16BF9C */
    VARS_LINKS = 3464,                /* after a 0x400-byte table and its count (0x100FE4) */
    VARS_CONVERSATIONS = 4492,
    VARS_MONSTERS = 5520,
    VARS_GEMS = 12692,
    VARS_LIST_100984 = 13848,
    VARS_SIGILS = 14364,
    VARS_LEAVING = 15136,
    VARS_GLOBAL_COUNT = 15233,        /* after a byte */
    VARS_GLOBALS = 15237,
    VARS_LOCALS = 17285,
    VARS_CREATURES = 34693,
};

static uint32_t clamp_count(uint32_t n, uint32_t max)
{
    return n > max ? max : n;
}

bs_result bs_save_vars_parse(const uint8_t *d, size_t size, bs_save_vars *out)
{
    memset(out, 0, sizeof *out);
    if (size < BS_SAVE_VARS_SIZE)
        return BS_ERR_FORMAT;
    out->data = d;
    bs_save_character_parse(d + VARS_PLAYER, BS_SAVE_PLAYER_BODY, &out->player);
    out->level_number = i32(d + VARS_MISC);
    out->clock = u32(d + VARS_MISC + 4);
    for (int k = 0; k < 4; k++)
        out->misc[k] = i32(d + VARS_MISC + 8 + 4 * k);
    memcpy(out->log, d + VARS_LOG, BS_SAVE_LOG_SIZE);
    out->music_volume = i32(d + VARS_VOLUMES);
    out->sound_volume = i32(d + VARS_VOLUMES + 4);

    out->link_count = clamp_count(u32(d + VARS_LINKS + BS_SAVE_TABLE * 8), BS_SAVE_TABLE);
    for (int i = 0; i < BS_SAVE_TABLE; i++) {
        out->links[i].id = u32(d + VARS_LINKS + 8 * i);
        out->links[i].link = i32(d + VARS_LINKS + 8 * i + 4);
    }
    out->conversation_count = clamp_count(u32(d + VARS_CONVERSATIONS + BS_SAVE_TABLE * 8), BS_SAVE_TABLE);
    for (int i = 0; i < BS_SAVE_TABLE; i++) {
        const uint8_t *p = d + VARS_CONVERSATIONS + 8 * i;
        out->conversations[i].id = u32(p);
        copy_str(out->conversations[i].file, sizeof out->conversations[i].file, p + 4, 4);
    }
    out->monster_count = clamp_count(u32(d + VARS_MONSTERS + BS_SAVE_TABLE * 56), BS_SAVE_TABLE);
    for (int i = 0; i < BS_SAVE_TABLE; i++) {
        const uint8_t *p = d + VARS_MONSTERS + 56 * i;
        out->monsters[i].id = u32(p);
        out->monsters[i].creature = i32(p + 4);
        out->monsters[i].speed = i32(p + 8);
        out->monsters[i].strength = i32(p + 12);
        out->monsters[i].spell_points = i32(p + 16);
        out->monsters[i].health = i32(p + 20);
        out->monsters[i].skill = i32(p + 24);
        out->monsters[i].goal = i32(p + 28);
        out->monsters[i].effects = u32(p + 32);
        for (int k = 0; k < 5; k++)
            out->monsters[i].spells[k] = i32(p + 36 + 4 * k);
    }
    out->gem_count = clamp_count(u32(d + VARS_GEMS + BS_SAVE_TABLE * 9), BS_SAVE_TABLE);
    for (int i = 0; i < BS_SAVE_TABLE; i++) {
        const uint8_t *p = d + VARS_GEMS + 9 * i;
        out->gems[i].id = u32(p);
        out->gems[i].amount = i32(p + 4);
        out->gems[i].spell_points = p[8] != 0;
    }
    out->list_100984_count = clamp_count(u32(d + VARS_LIST_100984 + BS_SAVE_TABLE * 4), BS_SAVE_TABLE);
    for (int i = 0; i < BS_SAVE_TABLE; i++)
        out->list_100984[i] = u32(d + VARS_LIST_100984 + 4 * i);
    out->sigil_count = clamp_count(u32(d + VARS_SIGILS + BS_SAVE_SIGILS * 12), BS_SAVE_SIGILS);
    for (int i = 0; i < BS_SAVE_SIGILS; i++) {
        const uint8_t *p = d + VARS_SIGILS + 12 * i;
        out->sigils[i].id = u32(p);
        out->sigils[i].y = i32(p + 4);
        out->sigils[i].unknown = u32(p + 8);
    }
    for (int i = 0; i < 8; i++) {
        out->leaving[i].id = u32(d + VARS_LEAVING + 12 * i);
        out->leaving[i].at = u32(d + VARS_LEAVING + 12 * i + 4);
        out->leaving[i].start = u32(d + VARS_LEAVING + 12 * i + 8);
    }
    out->global_count = clamp_count(u32(d + VARS_GLOBAL_COUNT), BS_SAVE_GLOBALS);
    for (int i = 0; i < BS_SAVE_GLOBALS; i++) {
        out->globals[i].hash = u32(d + VARS_GLOBALS + 8 * i);
        out->globals[i].value = i32(d + VARS_GLOBALS + 8 * i + 4);
    }
    for (int i = 0; i < BS_SAVE_LOCALS; i++) {
        const uint8_t *p = d + VARS_LOCALS + 68 * i;
        out->locals[i].id = u32(p);
        for (int k = 0; k < BS_SAVE_LOCAL_VARS; k++) {
            out->locals[i].vars[k].hash = u32(p + 4 + 8 * k);
            out->locals[i].vars[k].value = i32(p + 8 + 8 * k);
        }
    }
    memcpy(out->visits, d + VARS_CREATURES, BS_SAVE_CREATURES);
    return BS_OK;
}

uint32_t bs_save_variable_hash(const char *name)
{
    const uint8_t *p = (const uint8_t *)name;
    while (*p && *p <= ' ')
        p++;
    if (*p == '&')
        p++;
    uint32_t h = 0;
    for (; *p > ' '; p++) {
        uint32_t c = *p >= 'a' && *p <= 'z' ? (uint32_t)(*p - 0x20) : *p;
        h = ((h << 4) | (h >> 28)) + c;
    }
    return h;
}

void bs_save_image_rgb(const uint8_t raw[BS_SAVE_IMAGE_SIZE], uint8_t *rgb)
{
    for (int i = 0; i < BS_SAVE_IMAGE_WIDTH * BS_SAVE_IMAGE_HEIGHT; i++) {
        uint16_t v = u16(raw + 2 * i);
        int r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
        rgb[3 * i] = (uint8_t)(r << 3 | r >> 2);
        rgb[3 * i + 1] = (uint8_t)(g << 3 | g >> 2);
        rgb[3 * i + 2] = (uint8_t)(b << 3 | b >> 2);
    }
}
