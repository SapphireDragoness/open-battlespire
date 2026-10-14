#ifndef BS_AUDIO_PLACE_H
#define BS_AUDIO_PLACE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Where a positional sound plays, by the original game's rule (RE notes:
 * docs/re/README.md, "Audio"). The game sets volume and pan once, when the sound
 * starts, and never updates them while it plays.
 *
 * Positions are in level units (+y down). yaw is the listener's heading in the
 * game's angle units: 2048 per turn, 0 facing +z, increasing towards +x.
 *
 * - Distance: the game's integer approximation (octagonal in x/z, then
 *   combined with y), not the Euclidean distance.
 * - Volume falls linearly from full (within 25 units) to silent at
 *   max_distance. A non-looping sound at or beyond max_distance is not played.
 * - Pan depends only on direction: hard left or right at 90 degrees to the
 *   heading, centred straight ahead or behind (front and back sound the same).
 */

/* The game's range values: max distance = range * 768 / 127. */
#define BS_SOUND_DISTANCE_NEAR 768   /* range 127: nearly every sound */
#define BS_SOUND_DISTANCE_FAR 7740   /* range 1280 */

typedef struct bs_sound_place {
    float gain;  /* 0..1 (the game's volume 0..127) */
    float pan;   /* -1 left .. +1 right (the game's pan 1..127, centre 64) */
} bs_sound_place;

/* The game's distance approximation between two points, in level units. */
int32_t bs_sound_distance(const float a[3], const float b[3]);

/* Returns false if the sound would not be started at all. */
bool bs_sound_place_at(const float listener[3], int32_t yaw, const float source[3], int32_t max_distance,
                       bool looping, bs_sound_place *out);

#endif
