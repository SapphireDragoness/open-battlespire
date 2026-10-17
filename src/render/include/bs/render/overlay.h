#ifndef BS_RENDER_OVERLAY_H
#define BS_RENDER_OVERLAY_H

#include <stdint.h>

#include "bs/core/result.h"
#include "bs/formats/bsi.h"
#include "bs/formats/font.h"

/*
 * A 2D layer over the 3D view, in the game's 640x480 screen pixels, scaled
 * to the window like bs_screen (nearest neighbour, letterboxed) and blended
 * by its alpha. Drawing goes to an RGBA buffer on the CPU; bs_overlay_draw
 * uploads it when it changed. Needs a current OpenGL 3.3 context.
 */

#define BS_OVERLAY_WIDTH 640
#define BS_OVERLAY_HEIGHT 480

typedef struct bs_overlay bs_overlay;

bs_result bs_overlay_create(bs_overlay **out);
void bs_overlay_destroy(bs_overlay *o);

/* Makes every pixel transparent. */
void bs_overlay_clear(bs_overlay *o);

/* Limits drawing to a rectangle; w or h 0 for the whole layer again. */
void bs_overlay_clip(bs_overlay *o, int x, int y, int w, int h);

/* Fills a rectangle (clipped) with an RGBA colour, replacing what is there. */
void bs_overlay_fill(bs_overlay *o, int x, int y, int w, int h, uint32_t rgba);

/* Copies a BSI image (frame 0) at its own placement (img->x, img->y) plus (dx, dy); pixel 0 is
 * transparent in images marked so. */
void bs_overlay_image(bs_overlay *o, const bs_bsi_image *img, int dx, int dy);

/* As bs_overlay_image, frame `frame` of an animated image. */
void bs_overlay_image_frame(bs_overlay *o, const bs_bsi_image *img, uint32_t frame, int dx, int dy);

/* An opaque w x h RGB picture (3 bytes a pixel, top row first), top-left at (x, y). */
void bs_overlay_rgb(bs_overlay *o, const uint8_t *rgb, int w, int h, int x, int y);

/* A one-pixel rectangle outline. */
void bs_overlay_frame(bs_overlay *o, int x, int y, int w, int h, uint32_t rgba);

/*
 * Text in a game font, top-left at (x, y). Bytes are code page 437, as the
 * fonts are; glyphs advance their width + 1, a space 5 (0x13A81). Returns the width drawn.
 */
int bs_overlay_text(bs_overlay *o, const bs_font *font, int x, int y, const char *text);

/* The width text would take. */
int bs_text_width(const bs_font *font, const char *text);

/* How far one character moves the pen. */
int bs_text_advance(const bs_font *font, uint8_t c);

/* The height of a line of text. */
int bs_text_height(const bs_font *font);

/* Draws the layer into the default framebuffer (window size width x height). */
void bs_overlay_draw(bs_overlay *o, int width, int height);

#endif
