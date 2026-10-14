#ifndef BS_RENDER_SCREEN_H
#define BS_RENDER_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

#include "bs/core/result.h"
#include "bs/formats/bsi.h"

/*
 * Full-screen 2D images: video frames, menu screens. The image is scaled to
 * the window with nearest-neighbour filtering, keeping its aspect ratio, with
 * black bars. Needs a current OpenGL 3.3 context.
 */

typedef struct bs_screen bs_screen;

bs_result bs_screen_create(bs_screen **out);
void bs_screen_destroy(bs_screen *s);

/* Replaces the image: width x height pixels, RGBA8 bytes, top row first. */
bs_result bs_screen_set(bs_screen *s, const uint8_t *rgba, uint32_t width, uint32_t height);

/* Converts a BSI image (frame 0) and shows it. */
bs_result bs_screen_set_bsi(bs_screen *s, const bs_bsi_image *img);

/* Draws the image into the default framebuffer of size width x height. */
void bs_screen_draw(const bs_screen *s, int width, int height);

/* Maps a window pixel to image coordinates; false if outside the image. */
bool bs_screen_to_image(const bs_screen *s, int width, int height, float wx, float wy, int *ix, int *iy);

#endif
