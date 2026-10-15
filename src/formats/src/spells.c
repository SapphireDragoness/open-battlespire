#include "bs/formats/spells.h"

#include <string.h>

#define RECORD 0x77
#define SCHOOLS 6                    /* the schools' skills, 10 bytes before the table (0xDA60A) */
#define SCHOOLS_BEFORE 10

static int32_t i32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

bs_result bs_spells_read(const uint8_t *exe, size_t size, bs_spell out[BS_SPELL_COUNT])
{
    static const char first[] = "Monster Summoning", second[] = "Detect Spell";
    const size_t need = (size_t)BS_SPELL_COUNT * RECORD;
    for (size_t off = 0; off + need <= size; off++) {
        const uint8_t *t = exe + off;
        if (memcmp(t, first, sizeof first) != 0 || memcmp(t + RECORD, second, sizeof second) != 0)
            continue;
        for (uint32_t i = 0; i < BS_SPELL_COUNT; i++) {
            const uint8_t *r = t + i * RECORD;
            bs_spell *s = &out[i];
            memset(s, 0, sizeof *s);
            size_t n = 0;
            while (n < sizeof s->name - 1 && r[n])
                n++;
            memcpy(s->name, r, n);
            s->effect = r[0x20];
            s->target = r[0x21];
            s->icon = r[0x22];
            s->duration_base = i32(r + 0x23);
            s->duration_increase = i32(r + 0x27);
            s->duration_step = i32(r + 0x2B);
            s->magnitude_min = i32(r + 0x2F);
            s->magnitude_max = i32(r + 0x33);
            s->increase_min = i32(r + 0x37);
            s->increase_max = i32(r + 0x3B);
            s->magnitude_step = i32(r + 0x3F);
            s->element = r[0x4D];
            s->delivery = r[0x4E];
            s->cost = i32(r + 0x5F);
            s->school = i32(r + 0x67);
            s->detonation = i32(r + 0x6B);
            s->skill = -1;
            if (off >= SCHOOLS_BEFORE && s->school >= 0 && s->school < SCHOOLS && t[s->school - SCHOOLS_BEFORE] < 21)
                s->skill = t[s->school - SCHOOLS_BEFORE];
            if (s->effect != i + 1)
                return BS_ERR_FORMAT;
        }
        return BS_OK;
    }
    return BS_ERR_NOT_FOUND;
}
