#ifndef BS_CORE_INI_H
#define BS_CORE_INI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/log.h"
#include "bs/core/result.h"

/*
 * Sectioned text files, for what people may edit by hand (saved games):
 *
 *     # a comment
 *     [section]
 *     key = value          # a comment after a value
 *     name = "a #quoted value"
 *
 * A section may repeat ([item] for each item); keys may too. Leading and trailing blanks don't count. A value
 * that starts with a double quote runs to the closing one, with \" and \\ inside; otherwise it ends at a '#'
 * that follows a blank. Keys before the first section are in a section named "".
 */

typedef struct bs_ini_entry {
    const char *key, *value;
    uint32_t line;
} bs_ini_entry;

typedef struct bs_ini_section {
    const char *name;
    uint32_t line;
    bs_ini_entry *entries;
    uint32_t count;
} bs_ini_section;

typedef struct bs_ini {
    bs_ini_section *sections;
    uint32_t count;
} bs_ini;

/* Parses text; on BS_ERR_FORMAT *error_line is the line that isn't a section, a key = value or a comment. */
bs_result bs_ini_parse(const char *text, size_t size, bs_arena *arena, bs_ini *out, uint32_t *error_line);

/* A key's value in a section (the first if it repeats), or NULL. */
const char *bs_ini_get(const bs_ini_section *s, const char *key);

/* ---- writing ---- */

typedef struct bs_ini_writer {
    char *text;                       /* malloc'd, NUL-terminated */
    size_t len, cap;
    bool failed;                      /* out of memory */
} bs_ini_writer;

void bs_ini_writer_init(bs_ini_writer *w);
void bs_ini_writer_free(bs_ini_writer *w);

/* Raw text (a comment line, a blank line). */
void bs_ini_printf(bs_ini_writer *w, const char *fmt, ...) BS_PRINTF_FMT(2, 3);

/* "\n[name]\n" (no blank line first at the start). */
void bs_ini_section_begin(bs_ini_writer *w, const char *name);

/* key = value, the value formatted; comment (may be NULL) after it. */
void bs_ini_put(bs_ini_writer *w, const char *key, const char *comment, const char *fmt, ...) BS_PRINTF_FMT(4, 5);

/* key = a string, quoted when it needs to be (blanks at its ends, a '#', a quote, a backslash). */
void bs_ini_put_str(bs_ini_writer *w, const char *key, const char *value);

#endif
