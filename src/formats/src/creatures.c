#include "bs/formats/creatures.h"

#include <string.h>

#define RECORD 0xCE
#define IMAGE_RECORD 0x31F
#define IMAGE_NAME 13
/* From the creature table, in v1.5's data object (0xE80A1 and 0xEF25F against 0xE3559). */
#define LEVELS_FROM_TABLE 0x4B48
#define SOUNDS_FROM_TABLE 0xBD06

static int32_t i32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

static void copy_name(char *dst, size_t cap, const uint8_t *src, size_t max)
{
    size_t n = 0;
    while (n < cap - 1 && n < max && src[n])
        n++;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/*
 * The spell lists (+0x74, +0x78) are pointers into a block of 0-terminated
 * byte lists right before the table, the last ending where the table starts.
 * Without the data object's address the pointers are placed by that last
 * list: the highest pointer is its first byte. Lists that don't fit are left
 * empty.
 */
static void read_spell_lists(const uint8_t *exe, size_t off, bs_creature_tables *out)
{
    uint32_t top = 0;
    for (uint32_t i = 0; i < BS_CREATURE_COUNT; i++)
        for (int k = 0; k < 2; k++) {
            uint32_t p = (uint32_t)i32(exe + off + (size_t)i * RECORD + 0x74 + 4 * k);
            top = p > top ? p : top;
        }
    if (!top || off < 2 || exe[off - 1] != 0)
        return;
    size_t start = off - 1;
    while (start > 0 && off - start < 0x40 && exe[start - 1] != 0)
        start--;
    for (uint32_t i = 0; i < BS_CREATURE_COUNT; i++)
        for (int k = 0; k < 2; k++) {
            uint32_t p = (uint32_t)i32(exe + off + (size_t)i * RECORD + 0x74 + 4 * k);
            if (!p || top - p > start || top - p > 0x40)
                continue;
            size_t at = start - (top - p);
            uint8_t *list = out->creatures[i].spells[k];
            for (uint32_t n = 0; n < BS_CREATURE_SPELLS - 1 && at + n < off && exe[at + n]; n++) {
                if (exe[at + n] > 31) {
                    memset(list, 0, BS_CREATURE_SPELLS);
                    break;
                }
                list[n] = exe[at + n];
            }
            out->creatures[i].spell_caster |= list[0] != 0;
        }
}

static bool read_at(const uint8_t *exe, size_t size, size_t off, bs_creature_tables *out)
{
    if (off + SOUNDS_FROM_TABLE + sizeof out->sounds > size ||
        off + (size_t)BS_CREATURE_COUNT * (RECORD + IMAGE_RECORD) > size)
        return false;
    const uint8_t *images = exe + off + (size_t)BS_CREATURE_COUNT * RECORD;
    if (memcmp(images, "smpw00.bsi", 11) != 0)
        return false;
    for (uint32_t i = 0; i < BS_CREATURE_COUNT; i++) {
        const uint8_t *r = exe + off + (size_t)i * RECORD;
        bs_creature *c = &out->creatures[i];
        memset(c, 0, sizeof *c);
        copy_name(c->name, sizeof c->name, r, 0x20);
        for (int k = 0; k < 8; k++)
            c->attributes[k] = i32(r + 0x20 + 4 * k);
        c->spell_points = (int32_t)(r[0x40] | r[0x41] << 8);
        c->health = i32(r + 0x44);
        c->skill = i32(r + 0x48);
        c->flags = (uint32_t)i32(r + 0x4C);
        c->level = i32(r + 0x6C);
        c->armour = i32(r + 0x70);
        c->resists = r[0xAE];
        c->immune = r[0xAF];
        c->low_tolerance = r[0xBB];
        c->critical_weakness = r[0xBC];
        c->unk_7c = (uint16_t)(r[0x7C] | r[0x7D] << 8);
        const uint8_t *w = r + 0x7E;
        c->weapon = (bs_weapon_stats){i32(w), i32(w + 4), i32(w + 8), i32(w + 12), i32(w + 16), i32(w + 20)};
        if (!c->name[0] || c->health <= 0 || c->weapon.damage_min > c->weapon.damage_max)
            return false;
        const uint8_t *im = images + (size_t)i * IMAGE_RECORD;
        for (uint32_t s = 0; s < BS_CREATURE_IMAGES; s++)
            copy_name(c->images[s], sizeof c->images[s], im + s * IMAGE_NAME, IMAGE_NAME);
    }
    read_spell_lists(exe, off, out);
    const uint8_t *lv = exe + off + LEVELS_FROM_TABLE;
    for (uint32_t i = 0; i < BS_CREATURE_LEVELS * 4; i++)
        if (lv[i] >= BS_CREATURE_COUNT)
            return false;
    memcpy(out->level_creatures, lv, sizeof out->level_creatures);
    const uint8_t *snd = exe + off + SOUNDS_FROM_TABLE;
    for (uint32_t i = 0; i < BS_CREATURE_COUNT; i++)
        for (uint32_t k = 0; k < BS_CREATURE_SOUNDS; k++) {
            int32_t id = i32(snd + 16 * i + 4 * k);
            if (id <= 0 || id > 4096)
                return false;
            out->sounds[i][k] = id;
        }
    return true;
}

bs_result bs_creature_tables_read(const uint8_t *exe, size_t size, bs_creature_tables *out)
{
    static const char first[] = "Scamp", second[] = "Vermai";
    for (size_t off = 0; off + RECORD + sizeof second <= size; off++) {
        if (memcmp(exe + off, first, sizeof first) != 0 || memcmp(exe + off + RECORD, second, sizeof second) != 0)
            continue;
        return read_at(exe, size, off, out) ? BS_OK : BS_ERR_FORMAT;
    }
    return BS_ERR_NOT_FOUND;
}
