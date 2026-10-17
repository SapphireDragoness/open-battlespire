#ifndef BS_AUDIO_AUDIO_H
#define BS_AUDIO_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bs/core/result.h"

/*
 * Audio output on SDL3. Every voice is an SDL audio stream bound to one
 * playback device, which mixes them; SDL converts each stream's format and
 * rate to the device's.
 *
 * - Sounds: PCM copied at play time (the game's are 8-bit mono 11025 Hz),
 *   with gain, stereo pan and optional looping.
 * - Music: Ogg Vorbis decoded while it plays (MUSIC/TrackNN.ogg).
 * - Streams: PCM pushed by the caller, e.g. a video's soundtrack; the queued
 *   amount gives the audio clock for synchronising video.
 *
 * SDL must be initialised with SDL_INIT_AUDIO before bs_audio_open().
 */

typedef struct bs_audio bs_audio;
typedef int bs_voice;  /* 0 = none */

typedef struct bs_pcm_format {
    int bits;        /* 8 (unsigned) or 16 (signed little-endian) */
    int channels;    /* 1 or 2 */
    uint32_t rate;   /* samples per second */
} bs_pcm_format;

bs_result bs_audio_open(bs_audio **out);
void bs_audio_close(bs_audio *a);

/* Plays PCM (copied). pan: -1 left .. 0 centre .. +1 right (mono sources).
 * Returns 0 if no voice is free or the format is invalid. */
bs_voice bs_audio_play(bs_audio *a, const void *pcm, size_t bytes, bs_pcm_format fmt, float gain, float pan,
                       bool loop);

/* Changes a playing sound's gain and pan. */
void bs_audio_set(bs_audio *a, bs_voice v, float gain, float pan);
void bs_audio_stop(bs_audio *a, bs_voice v);
bool bs_audio_playing(bs_audio *a, bs_voice v);

/* Music: plays an Ogg Vorbis file from memory (which must outlive playback). */
bs_result bs_audio_play_music(bs_audio *a, const uint8_t *ogg, size_t size, bool loop, float gain);
void bs_audio_stop_music(bs_audio *a);
/* Changes the playing music's gain. */
void bs_audio_set_music_gain(bs_audio *a, float gain);
bool bs_audio_music_playing(bs_audio *a);

/* Push streams. */
bs_voice bs_audio_open_stream(bs_audio *a, bs_pcm_format fmt);
bs_result bs_audio_push(bs_audio *a, bs_voice v, const void *pcm, size_t bytes);
/* No more data will be pushed: lets the last samples play out. */
void bs_audio_end_stream(bs_audio *a, bs_voice v);
/* Bytes pushed but not yet played (in the stream's own format). */
size_t bs_audio_queued(bs_audio *a, bs_voice v);
void bs_audio_pause(bs_audio *a, bool paused);

#endif
