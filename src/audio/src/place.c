#include "bs/audio/place.h"

#include <math.h>

#define ANGLE_MASK 0x7FF  /* 2048 per turn */
#define FULL_VOLUME_DISTANCE 25
#define MAX_VOLUME 127
#define PAN_CENTRE 64
#define PAN_SPAN 63

/* The game converts with truncation; clamped here so huge values stay defined. */
static int32_t to_int(float v)
{
    if (!(v > -1e9f))
        return v < 0 ? -1000000000 : 0; /* also catches NaN */
    if (v > 1e9f)
        return 1000000000;
    return (int32_t)v;
}

static int32_t iabs(int32_t v)
{
    return v < 0 ? -v : v;
}

int32_t bs_sound_distance(const float a[3], const float b[3])
{
    /* Octagonal estimate in the x/z plane: long + short - short/2. */
    int32_t dx = iabs(to_int(a[0]) - to_int(b[0]));
    int32_t dz = iabs(to_int(a[2]) - to_int(b[2]));
    int32_t lo = dx < dz ? dx : dz;
    int32_t h = dx + dz - lo / 2;
    /* Then with height: long + short/4. */
    int32_t dy = iabs(to_int(a[1] - b[1]));
    return h > dy ? h + dy / 4 : dy + h / 4;
}

/* Heading from the listener to the source, in game angle units. */
static int32_t angle_to(const float from[3], const float to[3], bool *ok)
{
    float dx = to[0] - from[0], dz = to[2] - from[2];
    float r = sqrtf(dx * dx + dz * dz);
    *ok = r > 0;
    if (!*ok)
        return 0;
    /* The game's own approximation of pi. */
    int32_t a = to_int((float)(acos((double)(dz / r)) * 1024.0 / 3.14));
    return dx > 0 ? a : ANGLE_MASK - a;
}

bool bs_sound_place_at(const float listener[3], int32_t yaw, const float source[3], int32_t max_distance,
                       bool looping, bs_sound_place *out)
{
    int32_t dist = bs_sound_distance(listener, source);
    if (max_distance < 1)
        max_distance = 1;
    if (dist >= max_distance && !looping)
        return false;

    int32_t vol = MAX_VOLUME;
    if (dist >= FULL_VOLUME_DISTANCE) {
        vol = MAX_VOLUME - (int32_t)((int64_t)dist * MAX_VOLUME / max_distance);
        vol = vol < 0 ? 0 : vol > MAX_VOLUME ? MAX_VOLUME : vol;
    }

    int32_t pan = PAN_CENTRE;
    bool ok;
    int32_t to = angle_to(listener, source, &ok) & ANGLE_MASK, facing = yaw & ANGLE_MASK;
    if (ok) {
        /* Angle between heading and source (0..1024) and which side it is on. */
        int32_t d = to - facing, rel, side;
        if (d > 1024) {
            rel = facing + 2048 - to;
            side = -1;
        } else if (d > 0) {
            rel = d;
            side = 1;
        } else if (d < -1024) {
            rel = to + 2048 - facing;
            side = 1;
        } else {
            rel = -d;
            side = -1;
        }
        if (rel > 512)
            rel = 1024 - rel; /* behind mirrors in front */
        pan = PAN_CENTRE + side * (rel * PAN_SPAN / 512);
    }

    out->gain = (float)vol / MAX_VOLUME;
    out->pan = (float)(pan - PAN_CENTRE) / PAN_SPAN;
    return true;
}
