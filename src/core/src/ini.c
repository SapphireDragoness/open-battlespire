#include "bs/core/ini.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INI_MAX_LINES 10000000u

static bool blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/* One line's text, its ends trimmed: [b, e). */
static void trim(const char **b, const char **e)
{
    while (*b < *e && blank(**b))
        (*b)++;
    while (*e > *b && blank((*e)[-1]))
        (*e)--;
}

static char *copy(bs_arena *a, const char *b, const char *e)
{
    char *s = bs_arena_alloc(a, (size_t)(e - b) + 1, 1);
    if (s) {
        memcpy(s, b, (size_t)(e - b));
        s[e - b] = '\0';
    }
    return s;
}

/* A value: quoted, or up to a '#' after a blank. NULL if a quote isn't closed or text follows it. */
static char *value(bs_arena *a, const char *b, const char *e)
{
    trim(&b, &e);
    if (b < e && *b == '"') {
        char *s = bs_arena_alloc(a, (size_t)(e - b), 1);
        if (!s)
            return NULL;
        size_t n = 0;
        const char *p = b + 1;
        for (; p < e && *p != '"'; p++) {
            if (*p == '\\' && p + 1 < e && (p[1] == '"' || p[1] == '\\'))
                p++;
            s[n++] = *p;
        }
        if (p >= e)
            return NULL;
        s[n] = '\0';
        for (p++; p < e && blank(*p); p++)
            ;
        return p == e || *p == '#' ? s : NULL;
    }
    for (const char *p = b; p < e; p++)
        if (*p == '#' && (p == b || blank(p[-1]))) {
            e = p;
            break;
        }
    trim(&b, &e);
    return copy(a, b, e);
}

typedef struct growing {
    void *items;
    uint32_t count, cap;
} growing;

static void *grow(growing *g, size_t size)
{
    if (g->count == g->cap) {
        uint32_t cap = g->cap * 2 + 16;
        void *more = realloc(g->items, size * cap);
        if (!more)
            return NULL;
        g->items = more;
        g->cap = cap;
    }
    return (char *)g->items + size * g->count++;
}

bs_result bs_ini_parse(const char *text, size_t size, bs_arena *arena, bs_ini *out, uint32_t *error_line)
{
    memset(out, 0, sizeof *out);
    if (error_line)
        *error_line = 0;
    growing sections = {0}, entries = {0};
    uint32_t nsections = 0;
    bs_result r = BS_OK;
    /* Every section's entries go in one list in order; a section remembers where its entries start. */
    typedef struct pending {
        const char *name;
        uint32_t line, first, count;
    } pending;
    pending *top = grow(&sections, sizeof(pending));
    if (!top)
        return BS_ERR_NOMEM;
    *top = (pending){"", 0, 0, 0};
    nsections = 1;
    uint32_t line = 0;
    for (size_t at = 0; at < size && r == BS_OK;) {
        line++;
        size_t end = at;
        while (end < size && text[end] != '\n' && text[end] != '\0')
            end++;
        const char *b = text + at, *e = text + end;
        at = end + 1;
        if (line > INI_MAX_LINES) {
            r = BS_ERR_FORMAT;
            break;
        }
        trim(&b, &e);
        if (b == e || *b == '#' || *b == ';')
            continue;
        if (*b == '[') {
            if (e[-1] != ']' || e - b < 2) {
                r = BS_ERR_FORMAT;
                break;
            }
            const char *nb = b + 1, *ne = e - 1;
            trim(&nb, &ne);
            pending *s = grow(&sections, sizeof(pending));
            char *name = copy(arena, nb, ne);
            if (!s || !name) {
                r = BS_ERR_NOMEM;
                break;
            }
            *s = (pending){name, line, entries.count, 0};
            nsections++;
            continue;
        }
        const char *eq = memchr(b, '=', (size_t)(e - b));
        if (!eq || eq == b) {
            r = BS_ERR_FORMAT;
            break;
        }
        const char *kb = b, *ke = eq;
        trim(&kb, &ke);
        bs_ini_entry *en = grow(&entries, sizeof(bs_ini_entry));
        if (!en) {
            r = BS_ERR_NOMEM;
            break;
        }
        en->key = copy(arena, kb, ke);
        en->value = value(arena, eq + 1, e);
        en->line = line;
        if (!en->key) {
            r = BS_ERR_NOMEM;
            break;
        }
        if (!en->value) {
            r = BS_ERR_FORMAT;
            break;
        }
        ((pending *)sections.items)[nsections - 1].count++;
    }
    if (r == BS_OK) {
        out->sections = BS_ARENA_ARRAY(arena, bs_ini_section, nsections);
        bs_ini_entry *all = entries.count ? BS_ARENA_ARRAY(arena, bs_ini_entry, entries.count) : NULL;
        if (!out->sections || (entries.count && !all)) {
            r = BS_ERR_NOMEM;
        } else {
            if (entries.count)
                memcpy(all, entries.items, sizeof *all * entries.count);
            for (uint32_t i = 0; i < nsections; i++) {
                const pending *p = &((pending *)sections.items)[i];
                out->sections[i] = (bs_ini_section){p->name, p->line, all ? all + p->first : NULL, p->count};
            }
            out->count = nsections;
        }
    } else if (error_line) {
        *error_line = line;
    }
    free(sections.items);
    free(entries.items);
    return r;
}

const char *bs_ini_get(const bs_ini_section *s, const char *key)
{
    for (uint32_t i = 0; i < s->count; i++)
        if (!strcmp(s->entries[i].key, key))
            return s->entries[i].value;
    return NULL;
}

/* ---- writing ---- */

void bs_ini_writer_init(bs_ini_writer *w)
{
    memset(w, 0, sizeof *w);
}

void bs_ini_writer_free(bs_ini_writer *w)
{
    free(w->text);
    memset(w, 0, sizeof *w);
}

static void vappend(bs_ini_writer *w, const char *fmt, va_list ap) BS_PRINTF_FMT(2, 0);

static void vappend(bs_ini_writer *w, const char *fmt, va_list ap)
{
    if (w->failed)
        return;
    va_list again;
    va_copy(again, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    if (n < 0) {
        va_end(again);
        w->failed = true;
        return;
    }
    if (w->len + (size_t)n + 1 > w->cap) {
        size_t cap = (w->cap + (size_t)n + 1) * 2;
        char *more = realloc(w->text, cap);
        if (!more) {
            va_end(again);
            w->failed = true;
            return;
        }
        w->text = more;
        w->cap = cap;
    }
    vsnprintf(w->text + w->len, w->cap - w->len, fmt, again);
    va_end(again);
    w->len += (size_t)n;
}

void bs_ini_printf(bs_ini_writer *w, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vappend(w, fmt, ap);
    va_end(ap);
}

void bs_ini_section_begin(bs_ini_writer *w, const char *name)
{
    bs_ini_printf(w, "%s[%s]\n", w->len ? "\n" : "", name);
}

void bs_ini_put(bs_ini_writer *w, const char *key, const char *comment, const char *fmt, ...)
{
    bs_ini_printf(w, "%s = ", key);
    va_list ap;
    va_start(ap, fmt);
    vappend(w, fmt, ap);
    va_end(ap);
    if (comment && comment[0])
        bs_ini_printf(w, "  # %s", comment);
    bs_ini_printf(w, "\n");
}

void bs_ini_put_str(bs_ini_writer *w, const char *key, const char *value)
{
    size_t n = strlen(value);
    bool quote = n && (blank(value[0]) || blank(value[n - 1]) || value[0] == '"');
    for (size_t i = 0; i < n && !quote; i++)
        quote = value[i] == '#' || value[i] == '\\' || value[i] == '\n';
    if (!quote) {
        bs_ini_printf(w, "%s = %s\n", key, value);
        return;
    }
    bs_ini_printf(w, "%s = \"", key);
    for (size_t i = 0; i < n; i++) {
        char c = value[i] == '\n' ? ' ' : value[i];
        if (c == '"' || c == '\\')
            bs_ini_printf(w, "\\%c", c);
        else
            bs_ini_printf(w, "%c", c);
    }
    bs_ini_printf(w, "\"\n");
}
