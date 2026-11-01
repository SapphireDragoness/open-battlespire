#ifndef BS_APP_LAUNCHER_H
#define BS_APP_LAUNCHER_H

#include <stdbool.h>
#include <stddef.h>

#include <SDL3/SDL.h>

#include "bs/render/screen.h"

/*
 * The startup screen, shown before the game's files are opened when neither -d nor BS_DATA_DIR names them:
 * the Battlespire folder (the one remembered, or chosen with Browse, dropped on the window, typed or pasted),
 * checked as it changes, then Play or Quit. It draws with SDL's built-in 8x8 font, as the game's fonts are
 * in the folder it asks for, into a 640x480 picture shown with `screen`.
 */

typedef struct launcher_testing {
    const char *screenshot;                              /* save the screen there after `after_ms` and quit */
    unsigned after_ms;
    void (*save)(const char *path, int width, int height); /* writes the window's pixels */
} launcher_testing;

/* Runs the screen in `window` until Play (true: `dir` is a Battlespire install) or Quit / closing the window
 * (false). `dir` (cap bytes) comes in as the folder to show first, "" for none. testing may be NULL. */
bool launcher_run(SDL_Window *window, bs_screen *screen, char *dir, size_t cap, const launcher_testing *testing);

#endif
