#include "bs/formats/automap.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#define RECORD 0x42
#define NAME 13

static int32_t i32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

static void copy_name(char *dst, const uint8_t *src)
{
    memcpy(dst, src, NAME);
    dst[NAME - 1] = '\0';
}

static bool same(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
    return *a == *b;
}

bs_result bs_automap_table_read(const uint8_t *exe, size_t size, bs_automap_table *out)
{
    memset(out, 0, sizeof *out);
    /* The first record: "l1.bs6" with "map000.bsi" after it. */
    static const char first[] = "l1.bs6";
    for (size_t at = 0; at + RECORD * BS_AUTOMAP_MAPS <= size; at++) {
        if (memcmp(exe + at, first, sizeof first) != 0 || memcmp(exe + at + NAME, "map000.bsi", 11) != 0)
            continue;
        for (uint32_t i = 0; i < BS_AUTOMAP_MAPS; i++) {
            const uint8_t *r = exe + at + (size_t)i * RECORD;
            bs_automap_entry *e = &out->maps[i];
            copy_name(e->level, r);
            copy_name(e->image, r + NAME);
            for (int k = 0; k < 4; k++) {
                e->world[k] = i32(r + 2 * NAME + 4 * k);
                e->screen[k] = i32(r + 2 * NAME + 16 + 4 * k);
            }
            e->shift[0] = i32(r + 2 * NAME + 32);
            e->shift[1] = i32(r + 2 * NAME + 36);
            if (!strstr(e->level, ".bs6") || strncmp(e->image, "map", 3) != 0)
                return BS_ERR_FORMAT;
            out->count++;
        }
        return BS_OK;
    }
    return BS_ERR_NOT_FOUND;
}

int bs_automap_find(const bs_automap_table *t, const char *level, float y)
{
    for (uint32_t i = 0; i < t->count; i++) {
        if (!same(t->maps[i].level, level))
            continue;
        /* Level 3's lower part has its own map, the next record (0x57F45). */
        if (same(level, "l3.bs6") && y > 700.0f && i + 1 < t->count)
            return (int)i + 1;
        return (int)i;
    }
    return -1;
}

void bs_automap_point(const bs_automap_entry *m, float x, float z, int *sx, int *sy)
{
    int32_t wx = m->world[2] - m->world[0], wz = m->world[1] - m->world[3];
    int32_t px = (int32_t)(x - (float)m->world[0]), pz = (int32_t)(z - (float)m->world[3]);
    int32_t fx = wx ? px * 100 / wx : 0, fz = 100 - (wz ? pz * 100 / wz : 0);
    int32_t rx = (m->screen[2] - m->screen[0]) * fx / 100 + m->shift[0];
    int32_t ry = (m->screen[3] - m->screen[1]) * fz / 100 + m->shift[1];
    *sx = rx + m->screen[0] + m->shift[0];
    *sy = ry + m->screen[1] + m->shift[1];
}
