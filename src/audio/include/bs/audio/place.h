#ifndef BS_AUDIO_PLACE_H
#define BS_AUDIO_PLACE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Where a positional sound plays, by the original game's rule (RE notes:
 * docs/re/README.md, "Audio"). The game sets volume and pan when a sound
 * starts (0x1C33F) and again on every frame while it plays (0x1C54E), from
 * the source's position then.
 *
 * Positions are in level units (+y down). yaw is the listener's heading in the
 * game's angle units: 2048 per turn, 0 facing +z, increasing towards +x.
 *
 * - Distance: the game's integer approximation (octagonal in x/z, then
 *   combined with y), not the Euclidean distance.
 * - Volume falls linearly from full (within 25 units) to silent at
 *   max_distance. A non-looping sound at or beyond max_distance is not
 *   started, and one that moves beyond it is stopped.
 * - Pan depends only on direction: hard left or right at 90 degrees to the
 *   heading, centred straight ahead or behind (front and back sound the same).
 */

/* The play functions' ranges (0x1CBD4, 0x1CC68), and how far away from the player they don't start a
 * sound at all, whatever the max distance. */
#define BS_SOUND_RANGE_NEAR 127      /* nearly every sound */
#define BS_SOUND_RANGE_FAR 1280
#define BS_SOUND_CUTOFF_NEAR 768
#define BS_SOUND_CUTOFF_FAR 8192

/* The max distances: range * 768 / 127. */
#define BS_SOUND_DISTANCE_NEAR 768
#define BS_SOUND_DISTANCE_FAR 7740

typedef struct bs_sound_place {
    float gain;  /* 0..1 (the game's volume 0..127) */
    float pan;   /* -1 left .. +1 right (the game's pan 1..127, centre 64) */
} bs_sound_place;

/* The max distance for a range: range * 768 / 127, or range * 1024 / 127 for a player with Acute Hearing
 * (+0x194). */
int32_t bs_sound_max_distance(int32_t range, bool acute_hearing);

/* The game's distance approximation between two points, in level units. */
int32_t bs_sound_distance(const float a[3], const float b[3]);

/* A sound starting. Returns false if it would not be started at all. */
bool bs_sound_place_at(const float listener[3], int32_t yaw, const float source[3], int32_t max_distance,
                       bool looping, bs_sound_place *out);

/* A sound playing, on each frame. Within 25 units it is centred. Returns false if a non-looping sound has
 * moved beyond max_distance and stops. */
bool bs_sound_follow(const float listener[3], int32_t yaw, const float source[3], int32_t max_distance,
                     bool looping, bs_sound_place *out);

#endif
