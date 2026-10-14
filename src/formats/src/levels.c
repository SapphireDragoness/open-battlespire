#include "bs/formats/levels.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "bs/core/fs.h"

#define NUM_FIELDS 11

typedef struct span {
    const char *p;
    size_t n;
} span;

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static bool copy_field(span s, char *dst, size_t cap)
{
    if (s.n >= cap)
        return false;
    memcpy(dst, s.p, s.n);
    dst[s.n] = '\0';
    return true;
}

/* Like copy_field, but "xxxx" becomes "". */
static bool copy_optional(span s, char *dst, size_t cap)
{
    if (!copy_field(s, dst, cap))
        return false;
    if (bs_strcasecmp(dst, "xxxx") == 0)
        dst[0] = '\0';
    return true;
}

static bool parse_int(span s, int32_t *out)
{
    char buf[16];
    if (!copy_field(s, buf, sizeof buf) || s.n == 0)
        return false;
    char *end;
    errno = 0;
    long v = strtol(buf, &end, 10);
    if (*end != '\0' || errno != 0 || v < INT32_MIN || v > INT32_MAX)
        return false;
    *out = (int32_t)v;
    return true;
}

static bs_result parse_line(const char *line, size_t len, bs_level_info *out)
{
    span f[NUM_FIELDS];
    size_t i = 0;
    for (int k = 0; k < NUM_FIELDS; k++) {
        while (i < len && is_space(line[i]))
            i++;
        size_t start = i;
        while (i < len && !is_space(line[i]))
            i++;
        if (i == start)
            return BS_ERR_FORMAT;
        f[k].p = line + start;
        f[k].n = i - start;
    }
    while (i < len && is_space(line[i]))
        i++;
    size_t name_end = len;
    while (name_end > i && is_space(line[name_end - 1]))
        name_end--;
    span name = { line + i, name_end - i };

    memset(out, 0, sizeof *out);
    bool ok = copy_field(f[0], out->file, sizeof out->file) &&
              copy_field(f[1], out->id, sizeof out->id) &&
              parse_int(f[2], &out->view_distance) &&
              f[3].n == 3 && copy_field(f[3], out->mode, sizeof out->mode) &&
              copy_optional(f[4], out->sky, sizeof out->sky) &&
              copy_field(f[5], out->haze, sizeof out->haze) &&
              parse_int(f[6], &out->ambient) &&
              copy_optional(f[7], out->nodes, sizeof out->nodes) &&
              parse_int(f[8], &out->unk_c) &&
              parse_int(f[9], &out->grid_cell) &&
              parse_int(f[10], &out->monster_percent) &&
              copy_field(name, out->name, sizeof out->name);
    return ok ? BS_OK : BS_ERR_FORMAT;
}

bs_result bs_levels_parse(const uint8_t *text, size_t size, bs_arena *arena, bs_level_table *out)
{
    memset(out, 0, sizeof *out);
    const char *s = (const char *)text;

    size_t lines = 1;
    for (size_t i = 0; i < size; i++)
        lines += s[i] == '\n';
    bs_level_info *levels = BS_ARENA_ARRAY(arena, bs_level_info, lines);
    if (!levels)
        return BS_ERR_NOMEM;

    size_t count = 0, pos = 0;
    while (pos < size) {
        const char *nl = memchr(s + pos, '\n', size - pos);
        size_t len = nl ? (size_t)(nl - (s + pos)) : size - pos;
        const char *line = s + pos;
        pos += len + (nl ? 1 : 0);

        bool blank = true;
        for (size_t k = 0; k < len; k++)
            blank = blank && (is_space(line[k]) || line[k] == '\x1a'); /* DOS EOF */
        if (blank)
            continue;

        bs_result r = parse_line(line, len, &levels[count]);
        if (r != BS_OK)
            return r;
        count++;
    }
    out->levels = levels;
    out->count = count;
    return BS_OK;
}

int bs_level_number(const bs_level_info *l)
{
    if (bs_level_game_type(l) != BS_GAME_CAMPAIGN)
        return 4;
    char c = l->file[1]; /* "l1.bs6" */
    if (c < '0' || c > '9')
        return 0;
    int n = c - '0';
    return n == 2 ? 3 : n == 3 ? 2 : n == 7 ? 8 : n;
}
