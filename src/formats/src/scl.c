#include "bs/formats/scl.h"

#include "bs/formats/save.h"

#include <stdlib.h>
#include <string.h>

#define PLAYER_MIN 0x2E2              /* what is read of it; CLASS002.SCL's is shorter than the rest (743 bytes) */
#define ITEM_MIN 0x2E6                /* to the enchantment */

static int32_t i32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static void put32(uint8_t *p, int32_t v)
{
    for (int k = 0; k < 4; k++)
        p[k] = (uint8_t)((uint32_t)v >> (8 * k));
}

static void copy_str(char *dst, size_t cap, const uint8_t *src, size_t max)
{
    size_t n = 0;
    while (n < max && n + 1 < cap && src[n])
        n++;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void bs_class_player_parse(const uint8_t b[BS_SAVE_PLAYER_BODY], bs_class *c)
{
    memset(c, 0, sizeof *c);
    copy_str(c->class_name, sizeof c->class_name, b + 0x17A, 24);
    c->level = b[0x29F] ? b[0x29F] : 1;
    c->points_left = i32(b + 0x20A);
    c->points_hundreds = b[0x2E1];
    for (int k = 0; k < 8; k++)
        c->attributes[k] = i32(b + 0x40 + 4 * k);
    c->wounds = i32(b + 0x6E);
    for (int k = 0; k < BS_SCL_SKILLS; k++)
        c->skills[k] = i32(b + 0x7A + 12 * k);
    memcpy(c->class_skills, b + 0x1A6, BS_SCL_CLASS_SKILLS);
    c->resists = b[0x192];
    c->immune = b[0x193];
    c->acute_hearing = b[0x194] != 0;
    c->spell_absorption = b[0x195] != 0;
    c->rapid_healing = b[0x196] != 0;
    c->regenerate_health = b[0x197] != 0;
    c->regenerate_sp = b[0x198] != 0;
    c->athleticism = b[0x199] != 0;
    c->magery = i32(b + 0x19A);
    c->adrenaline_rush = b[0x19E] != 0;
    c->low_tolerance = b[0x19F];
    c->critical_weakness = b[0x1A0];
    c->no_sp_regen = (b[0x1A1] & 1) != 0;
    c->forbidden_weapons = b[0x1A2];
    c->max_armour = b[0x1A3];
    c->forbidden_shield = b[0x1A4] != 0;
    c->max_material = b[0x1A5];
}

void bs_scl_item_parse(uint8_t flags, const uint8_t *body, size_t size, bs_scl_item *it)
{
    uint8_t b[BS_SAVE_ITEM_BODY] = {0};
    memcpy(b, body, size < sizeof b ? size : sizeof b);
    memset(it, 0, sizeof *it);
    copy_str(it->name, sizeof it->name, b, 32);
    it->base = u16(b + 0x20);
    copy_str(it->picture, sizeof it->picture, b + 0x22, 9);
    it->condition = i32(b + 0x2B);
    it->condition_max = i32(b + 0x2F);
    it->material = u16(b + 0x33);
    it->number = b[0x39] == 0xFF ? -1 : b[0x39];
    it->quantity = i32(b + 0x3E);
    it->container = u16(b + 0x42) != 0;
    it->charges = i32(b + 0x2D2);
    it->charges_max = i32(b + 0x2D6);
    it->magic = i32(b + 0x2E2);
    it->flags = flags;
}

bs_result bs_class_parse(const uint8_t *data, size_t size, bs_class *out)
{
    memset(out, 0, sizeof *out);
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_save_record *r;
    uint32_t n;
    bs_result res = bs_save_records_read(data, size, &a, &r, &n, NULL);
    if (res == BS_OK && (n == 0 || r[0].type != BS_SAVE_PLAYER || r[0].body_size < PLAYER_MIN))
        res = BS_ERR_FORMAT;
    if (res == BS_OK) {
        uint8_t padded[BS_SAVE_PLAYER_BODY] = {0};
        memcpy(padded, r[0].body, r[0].body_size < sizeof padded ? r[0].body_size : sizeof padded);
        bs_class_player_parse(padded, out);
        for (uint32_t i = 1; i < n; i++) {
            if (r[i].type == BS_SAVE_SPELL && r[i].body_size >= BS_SPELL_RECORD) {
                if (out->spell_count < BS_SCL_MAX_SPELLS)
                    bs_spell_parse(r[i].body, &out->spells[out->spell_count++]);
            } else if (r[i].type == BS_SAVE_ITEM && r[i].body_size >= ITEM_MIN) {
                if (out->item_count < BS_SCL_MAX_ITEMS)
                    bs_save_item_parse(&r[i], &out->items[out->item_count++]);
            }
        }
    }
    bs_arena_free(&a);
    return res;
}

/* ---- writing ---- */

#define AUTOMAP_BODY 0x4B5
#define AUTOMAP_W 40
#define AUTOMAP_H 30
#define FIRST_ID (1u << 16 | 10000u)   /* new objects' ids (0x21DE4, 0x22ECC) */

static void class_fields(uint8_t *at, const bs_class *c)
{
    memset(at, 0, 0x38);
    for (size_t n = 0; n < sizeof c->class_name - 1 && c->class_name[n]; n++)
        at[n] = (uint8_t)c->class_name[n];
    at[0x18] = c->resists;
    at[0x19] = c->immune;
    at[0x1A] = c->acute_hearing;
    at[0x1B] = c->spell_absorption;
    at[0x1C] = c->rapid_healing;
    at[0x1D] = c->regenerate_health;
    at[0x1E] = c->regenerate_sp;
    at[0x1F] = c->athleticism;
    put32(at + 0x20, c->magery);
    at[0x24] = c->adrenaline_rush;
    at[0x25] = c->low_tolerance;
    at[0x26] = c->critical_weakness;
    at[0x27] = c->no_sp_regen;
    at[0x28] = c->forbidden_weapons;
    at[0x29] = c->max_armour;
    at[0x2A] = c->forbidden_shield;
    at[0x2B] = c->max_material;
    memcpy(at + 0x2C, c->class_skills, BS_SCL_CLASS_SKILLS);
}

static void player_body(uint8_t b[BS_SAVE_PLAYER_BODY], const bs_class_out *in)
{
    const bs_class *c = in->cls;
    memset(b, 0, BS_SAVE_PLAYER_BODY);
    for (size_t n = 0; n < sizeof in->name - 1 && in->name[n]; n++)
        b[n] = (uint8_t)in->name[n];
    for (int k = 0; k < 8; k++) {
        put32(b + 0x20 + 4 * k, 50);
        put32(b + 0x40 + 4 * k, c->attributes[k]);
    }
    put32(b + 0x6E, c->wounds);
    for (int k = 0; k < BS_SCL_SKILLS; k++) {
        put32(b + 0x72 + 12 * k, 5);
        put32(b + 0x7A + 12 * k, c->skills[k]);
    }
    class_fields(b + 0x17A, c);
    memcpy(b + 0x1B2, b + 0x17A, 0x38);
    put32(b + 0x20A, c->points_left);
    put32(b + 0x226, in->female ? 8 : 0);
    b[0x23E] = in->hair;
    b[0x23F] = in->eyes;
    b[0x240] = in->mouth;
    b[0x25D] = in->race;
    b[0x29F] = (uint8_t)(c->level > 0 ? c->level : 1);
    b[0x2E1] = c->points_hundreds;
}

/* A record: its length, then the object's header and body. */
static uint8_t *record(uint8_t *at, uint8_t type, uint8_t flags, uint32_t id, uint32_t parent, size_t body)
{
    put32(at, (int32_t)(BS_SAVE_HEADER + body));
    uint8_t *h = at + 4;
    memset(h, 0, BS_SAVE_HEADER);
    h[0] = type;
    h[0x14] = flags;
    put32(h + 0x1D, (int32_t)id);
    put32(h + 0x25, -1); /* no links */
    put32(h + 0x39, (int32_t)parent);
    return h + BS_SAVE_HEADER;
}

bs_result bs_class_write(const bs_class_out *in, uint8_t **out, size_t *size)
{
    *out = NULL;
    *size = 0;
    if (!in->cls || (in->spell_count && !in->spells) || (in->item_count && (!in->items || !in->item_flags)))
        return BS_ERR_ARGS;
    size_t n = 4 + BS_SAVE_HEADER + BS_SAVE_PLAYER_BODY + (size_t)in->spell_count * (4 + BS_SAVE_HEADER + BS_SPELL_RECORD) +
               4 + BS_SAVE_HEADER + AUTOMAP_BODY + (size_t)in->item_count * (4 + BS_SAVE_HEADER + BS_SAVE_ITEM_BODY);
    uint8_t *f = malloc(n);
    if (!f)
        return BS_ERR_NOMEM;
    uint8_t *p = f;
    uint32_t id = FIRST_ID;

    uint8_t *b = record(p, BS_SAVE_PLAYER, 0, BS_SAVE_PLAYER_ID, BS_SAVE_PARENT_ROOT, BS_SAVE_PLAYER_BODY);
    p[4 + 0x13] = 1; /* as the game's player has it */
    put32(p + 4 + 0x29, (int32_t)in->clock);
    player_body(b, in);
    p = b + BS_SAVE_PLAYER_BODY;

    for (uint32_t i = 0; i < in->spell_count; i++) {
        b = record(p, BS_SAVE_SPELL, 2, id++, BS_SAVE_PLAYER_ID, BS_SPELL_RECORD);
        memcpy(b, in->spells[i], BS_SPELL_RECORD);
        p = b + BS_SPELL_RECORD;
    }

    b = record(p, BS_SAVE_AUTOMAP, 0, id++, BS_SAVE_PLAYER_ID, AUTOMAP_BODY);
    p[4 + 1] = AUTOMAP_W; /* the grid's size in the angles */
    p[4 + 3] = AUTOMAP_H;
    memset(b, 0, AUTOMAP_BODY);
    p = b + AUTOMAP_BODY;

    for (uint32_t i = 0; i < in->item_count; i++) {
        b = record(p, BS_SAVE_ITEM, in->item_flags[i], id++, BS_SAVE_PLAYER_ID, BS_SAVE_ITEM_BODY);
        memcpy(b, in->items[i], BS_SAVE_ITEM_BODY);
        p = b + BS_SAVE_ITEM_BODY;
    }

    *out = f;
    *size = (size_t)(p - f);
    return BS_OK;
}
