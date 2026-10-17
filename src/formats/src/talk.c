#include "bs/formats/talk.h"

#include <stdlib.h>
#include <string.h>

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

bool bs_talk_prefix(const char *s, const char *key, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (lower((unsigned char)s[i]) != lower((unsigned char)key[i]))
            return false;
        if (!s[i])
            return true;
    }
    return true;
}

bs_result bs_talk_parse(const uint8_t *data, size_t size, bs_arena *arena, bs_talk_file *out)
{
    memset(out, 0, sizeof *out);
    char *text = BS_ARENA_ARRAY(arena, char, size + 1);
    if (!text)
        return BS_ERR_NOMEM;
    uint32_t lines = 1;
    for (size_t i = 0; i < size; i++) {
        uint8_t c = data[i];
        if (c == '\n')
            lines++;
        text[i] = c == 0x92 ? '\'' : (char)c; /* (0x584C6) */
    }
    text[size] = '\0';
    bs_talk_row *rows = BS_ARENA_ARRAY(arena, bs_talk_row, lines);
    if (!rows)
        return BS_ERR_NOMEM;
    uint32_t n = 0;
    char *p = text, *end = text + size;
    while (p < end && n < lines) {
        char *eol = p;
        while (eol < end && *eol != '\n')
            eol++;
        char *next = eol < end ? eol + 1 : end;
        if (eol > p && eol[-1] == '\r')
            eol--;
        *eol = '\0';
        bs_talk_row *r = &rows[n++];
        for (int k = 0; k < BS_TALK_COLUMNS; k++)
            r->cells[k] = "";
        int col = 0;
        char *cell = p;
        for (char *q = p;; q++) {
            if (*q == '\t' || *q == '\0') {
                bool last = *q == '\0';
                *q = '\0';
                if (col < BS_TALK_COLUMNS)
                    r->cells[col] = cell;
                col++;
                cell = q + 1;
                if (last)
                    break;
            }
        }
        p = next;
    }
    out->rows = rows;
    out->count = n;
    return BS_OK;
}

const char *bs_talk_cell(const bs_talk_file *f, uint32_t row, uint32_t column)
{
    if (row >= f->count || column >= BS_TALK_COLUMNS)
        return "";
    return f->rows[row].cells[column];
}

int32_t bs_talk_find(const bs_talk_file *f, uint32_t from, uint32_t column, const char *key)
{
    char k[64];
    size_t n = 0;
    for (const char *s = key; (unsigned char)*s > ' ' && n + 1 < sizeof k; s++)
        if (*s != '"')
            k[n++] = *s;
    k[n] = '\0';
    if (!n)
        return -1;
    for (uint32_t r = from; r < f->count; r++)
        if (bs_talk_prefix(bs_talk_cell(f, r, column), k, n))
            return (int32_t)r;
    return -1;
}

bool bs_talk_in_range(const char *cell, int32_t roll)
{
    while (*cell == ' ')
        cell++;
    char a[3] = {0}, b[3] = {0};
    for (int i = 0; i < 2 && cell[i]; i++)
        a[i] = cell[i];
    const char *dash = strchr(cell, '-');
    if (!dash)
        return false;
    for (int i = 0; i < 2 && dash[1 + i]; i++)
        b[i] = dash[1 + i];
    int32_t lo = atoi(a), hi = atoi(b);
    if (hi == 0)
        hi = 100;
    return roll >= lo && roll <= hi;
}
