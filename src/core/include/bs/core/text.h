#ifndef BS_CORE_TEXT_H
#define BS_CORE_TEXT_H

#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"

/*
 * Text encoding helpers.
 *
 * Battlespire's text data (TEXT.RSC, TXT.BSA) is Windows-1252: the dialogue
 * was authored in Excel, and it contains smart quotes (0x92-0x94), ellipses
 * (0x85) and accented letters (0xE9 "é"). Bytes undefined in Windows-1252 are
 * mapped to the C1 control code points with the same value.
 */

/* Unicode code point for a Windows-1252 byte. */
uint32_t bs_cp1252_to_unicode(uint8_t c);

/* Encodes a code point as UTF-8 into out (at least 4 bytes). Returns the byte count. */
size_t bs_utf8_encode(uint32_t cp, char out[4]);

/* Converts n bytes of Windows-1252 to a NUL-terminated UTF-8 string. Returns NULL on allocation failure. */
char *bs_cp1252_to_utf8(bs_arena *arena, const uint8_t *src, size_t n);

#endif
