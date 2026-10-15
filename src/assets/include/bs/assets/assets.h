#ifndef BS_ASSETS_ASSETS_H
#define BS_ASSETS_ASSETS_H

#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"
#include "bs/core/vfs.h"
#include "bs/formats/bsi.h"
#include "bs/formats/model.h"

/*
 * Game assets by name, with caching. A file is looked up as a loose file in
 * GAMEDATA/ first, then in the archive for its extension:
 *
 *   .3d  -> GAMEDATA/3D.BSA      .bsi -> GAMEDATA/BSI.BSA      .bs6 -> GAMEDATA/BS6.BSA
 *
 * Loose files either duplicate archive records byte for byte or add new
 * ones (the v1.5 patch's deathmatch levels and their models), so the order
 * doesn't change any shipped asset.
 *
 * Parsed models and images live until bs_assets_close(). A missing or
 * malformed asset is logged once and then returns NULL.
 */

typedef struct bs_assets bs_assets;

/* The VFS must outlive the assets. */
bs_result bs_assets_open(const bs_vfs *vfs, bs_assets **out);
void bs_assets_close(bs_assets *a);

/* Reads a file by name, e.g. "l1.bs6", "WALL96.BSI" or "MG0_GEN.TXT": loose in GAMEDATA/, else from the
 * archive for its extension (3D.BSA, BSI.BSA, BS6.BSA, TXT.BSA). */
bs_result bs_assets_read(bs_assets *a, const char *name, bs_arena *arena, uint8_t **out, size_t *out_size);

/* A model by name, with or without ".3d". */
const bs_model *bs_assets_model(bs_assets *a, const char *name);

/* A BSI file by name, with or without ".bsi". */
const bs_bsi *bs_assets_bsi(bs_assets *a, const char *name);

/* First image of a BSI file, or NULL. */
const bs_bsi_image *bs_assets_image(bs_assets *a, const char *name);

/*
 * The image for a model face texture name (at most 6 characters, see
 * bs_model_texture_name()). A few faces name textures whose file name is
 * longer than 6 characters or lacks the frame digits, so if the exact name
 * is missing this tries, in order (inferred, not confirmed in GAME.EXE):
 *   1. the first BSI.BSA record that starts with the name ("STRUT0" -> STRUT01)
 *   2. the name without its trailing digits ("HAND00" -> HAND)
 */
const bs_bsi_image *bs_assets_texture(bs_assets *a, const char *name);

#endif
