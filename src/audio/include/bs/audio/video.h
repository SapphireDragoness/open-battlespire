#ifndef BS_AUDIO_VIDEO_H
#define BS_AUDIO_VIDEO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bs/audio/audio.h"
#include "bs/core/result.h"

/*
 * Smacker video playback timed by its soundtrack: frames are decoded a few
 * ahead, each frame's audio is pushed to an audio stream as it is decoded,
 * and the frame due is the one matching the audio played so far (wall time
 * for silent videos). Drawing is up to the caller.
 *
 * It lives in bs_audio because the soundtrack is the clock.
 */

typedef struct bs_video bs_video;

/* data must outlive the player. audio may be NULL (silent, wall-clock timing). */
bs_result bs_video_open(bs_audio *audio, const uint8_t *data, size_t size, bs_video **out);
void bs_video_close(bs_video *v);

/* Display size: doubled and interlaced videos are expanded to full height. */
uint32_t bs_video_width(const bs_video *v);
uint32_t bs_video_height(const bs_video *v);
uint32_t bs_video_frame_count(const bs_video *v);

/*
 * Decodes ahead and returns the frame due now (RGBA8, top row first) if it
 * differs from the last one returned, otherwise NULL. Sets *done once every
 * frame has been shown and the soundtrack has played out, or on a decoding
 * error (then bs_video_error() says which).
 */
const uint8_t *bs_video_update(bs_video *v, bool *done);
bs_result bs_video_error(const bs_video *v);

/* Index of the frame last returned. */
uint32_t bs_video_position(const bs_video *v);

/* Pauses the clock (and the whole audio device). */
void bs_video_pause(bs_video *v, bool paused);

#endif
