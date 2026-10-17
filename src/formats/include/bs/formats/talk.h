#ifndef BS_FORMATS_TALK_H
#define BS_FORMATS_TALK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"

/*
 * The dialogue tables in TXT.BSA (docs/formats/txt_bsa.md): tab-separated text exported from Excel, read
 * by the game as rows of cells (0x584C6 turns the tabs into NULs; 0x58F86 finds a row's n-th cell).
 *
 * - <stem>T.TXT, the talk table: column 1 a say code, 2 the speaker's line, 3 a reply code, 4-6 the
 *   reply's checks, 7 the player's reply, 8 where it goes, 9 the actions, 10 a variable to set.
 * - <stem>M.TXT, <stem>F.TXT, <stem>B.TXT, the group tables for a male, a female or any player:
 *   "talkcheck" rows (column 1 a condition, 2 a say code), "talktable" rows (column 2 a say code per
 *   visit), and "random" rows (column 1 a range of d100, 2 a say code).
 *
 * The text is Windows-1252, with 0x92 turned into an apostrophe as the game does.
 */

#define BS_TALK_COLUMNS 16

typedef struct bs_talk_row {
    const char *cells[BS_TALK_COLUMNS];   /* missing cells are "" */
} bs_talk_row;

typedef struct bs_talk_file {
    bs_talk_row *rows;
    uint32_t count;
} bs_talk_file;

/* Splits a table into rows (CR LF) and cells (tabs). */
bs_result bs_talk_parse(const uint8_t *data, size_t size, bs_arena *arena, bs_talk_file *out);

/* A cell, "" if out of range. */
const char *bs_talk_cell(const bs_talk_file *f, uint32_t row, uint32_t column);

/*
 * The first row at or after `from` whose cell in `column` starts with `key`, ignoring case (0x590C0): the
 * key ends at its first space or control character, its quotes left out. -1 if none.
 */
int32_t bs_talk_find(const bs_talk_file *f, uint32_t from, uint32_t column, const char *key);

/* Whether a d100 roll falls in a range cell "01-10" ("00" is 100) (0x59168). */
bool bs_talk_in_range(const char *cell, int32_t roll);

/* Whether two strings match for their first n characters, ignoring case (the game's strnicmp, 0x870A9). */
bool bs_talk_prefix(const char *s, const char *key, size_t n);

#endif
