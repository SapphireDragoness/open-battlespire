#ifndef BS_AUDIO_LEVEL_AUDIO_H
#define BS_AUDIO_LEVEL_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#include "bs/audio/audio.h"
#include "bs/core/vfs.h"
#include "bs/formats/bsa.h"
#include "bs/formats/levels.h"
#include "bs/formats/nodes.h"
#include "bs/formats/wav.h"

/*
 * A level's music and ambient sounds, as GAME.EXE plays them (RE notes:
 * docs/re/README.md, "Audio").
 *
 * - Music: CD track (level number + 1), looped.
 * - Ambience: on each frame, with chance 201 in 32768, one of 32 sound effects
 *   plays at a navigation node 64..600 units away, placed by the game's
 *   volume/pan rule (bs/audio/place.h).
 * - Effects: placed when they start and again on each update while they play,
 *   following their source (0x1C54E).
 */

#define BS_LEVEL_AUDIO_AMBIENT_SOUNDS 32
#define BS_LEVEL_AUDIO_EFFECT_CACHE 64
#define BS_LEVEL_AUDIO_PLACED 32

/* Where a playing sound's source is now, by the tag the caller gave it (not 0). Returns false if it's
 * gone; the sound then stays where it last was. */
typedef bool (*bs_sound_locate_fn)(void *ctx, uint32_t source, float pos[3]);

typedef struct bs_level_audio {
    bs_audio *audio;          /* borrowed; NULL: silent */
    bs_vfs *vfs;
    bs_bsa *effects;          /* GAMEDATA/SPIRE.SND */
    bool music;               /* play level music */
    bool fixed_ambience;      /* pick a random node in range, not the original's (see level_audio.c) */
    float music_gain, effects_gain;
    bool acute_hearing;       /* the player's advantage: effects carry 1024 / 768 as far */
    bs_sound_locate_fn locate;
    void *locate_ctx;

    bs_arena arena;           /* this level's music file, nodes and sounds */
    bs_nav_graph nodes;
    bool have_nodes;
    bs_wav sounds[BS_LEVEL_AUDIO_AMBIENT_SOUNDS];
    uint8_t sound_state[BS_LEVEL_AUDIO_AMBIENT_SOUNDS]; /* 0 not loaded, 1 loaded, 2 missing */
    uint32_t rand_state;
    double tick_time;
    struct {
        int32_t id;
        bool ok;
        bs_wav wav;
    } effects_cache[BS_LEVEL_AUDIO_EFFECT_CACHE];  /* other effects, loaded on demand */
    uint32_t effects_cached;
    struct {
        bs_voice voice;
        uint32_t source;      /* 0: a fixed place */
        float pos[3];
    } placed[BS_LEVEL_AUDIO_PLACED];  /* effects playing at a place, oldest first */
    uint32_t placed_count;
} bs_level_audio;

/* audio (may be NULL: silent) must outlive la. */
void bs_level_audio_open(bs_level_audio *la, bs_audio *audio, bs_vfs *vfs);
void bs_level_audio_close(bs_level_audio *la);

/* Stops the level's music and forgets the level (e.g. leaving it for a menu). */
void bs_level_audio_stop(bs_level_audio *la);

/* Starts a level's music and loads its ambience. info may be NULL (no music, no ambience). */
void bs_level_audio_start(bs_level_audio *la, const bs_level_info *info);

/* Plays a SPIRE.SND effect at a position, placed for a listener at `listener`
 * facing `yaw` (radians) by the game's rule for near sounds (0x1CBD4): not
 * started 768 units or more away. pos NULL: the player's own sound, centred
 * at full volume. source: 0 for a sound that stays at pos, else a tag that
 * la->locate follows while it plays. Returns its voice, or 0 if it didn't start. */
bs_voice bs_level_audio_play_at(bs_level_audio *la, int32_t id, uint32_t source, const float pos[3],
                                const float listener[3], float yaw);

/* Advances by dt seconds with the listener at pos (level units), facing yaw (radians): places the
 * effects playing, and rolls for ambience. */
void bs_level_audio_update(bs_level_audio *la, const float pos[3], float yaw, double dt);

#endif
