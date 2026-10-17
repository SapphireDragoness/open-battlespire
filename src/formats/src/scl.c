#include "bs/formats/scl.h"

#include "bs/formats/save.h"

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
