#ifndef OPENBATTLESPIRE_TEXT_H
#define OPENBATTLESPIRE_TEXT_H

#include <stddef.h>
#include <stdint.h>

#include "bs/formats/font.h"
#include "bs/formats/rsc.h"
#include "bs/render/overlay.h"

/* Text on the game's screens. */

/* Windows-1252 to the fonts' code page 437, where there is an equivalent. */
void ui_cp437(const char *in, char *out, size_t cap);

/* A TEXT.RSC record's first version as text, '$' for its line breaks. */
void ui_rsc_text(const bs_rsc *rsc, int32_t id, char *out, size_t cap);

/* Text wrapped at a width, each line centred on cx (0x597DB): a line breaks at the last space before the
 * text gets too wide, or at a '$'. Returns the y of the last line drawn (y if none). */
int ui_text_box(bs_overlay *o, const bs_font *f, const char *s, int cx, int y, int width);

#endif
