#ifndef BSTOOL_GLTF_H
#define BSTOOL_GLTF_H

#include "bs/assets/scene.h"
#include "bs/core/result.h"

/*
 * Writes a scene as glTF 2.0: <dir>/<stem>.gltf, <dir>/<stem>.bin and one
 * indexed PNG per texture in <dir>/textures/.
 *
 * Game axes (+y down) become glTF axes (+y up) by a 180-degree turn about x,
 * so nothing is mirrored. `scale` converts level units to glTF units.
 * Flats become empty nodes named "flat:<name>" (markers are included only
 * if `markers` is set).
 */

#define GLTF_DEFAULT_SCALE (1.0f / 64.0f) /* roughly metres: a door is about 2.4 m */

bs_result gltf_write_scene(const char *dir, const char *stem, const bs_scene *scene, float scale, bool markers);

#endif
