#include "bs/audio/video.h"

#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "bs/formats/smacker.h"

#define LOOKAHEAD 8  /* decoded frames kept ahead of the clock */

struct bs_video {
    bs_audio *audio;
    bs_arena arena;
    bs_smacker smk;
    uint32_t width, height;    /* display size */
    uint8_t *slots[LOOKAHEAD]; /* RGBA frames; frame i lives in slot i % LOOKAHEAD */
    bs_voice voice;
    double bytes_per_sec;
    size_t pushed;
    uint32_t decoded, shown;   /* shown: frames up to and including this one are due */
    bool any_shown;
    uint64_t start, pause_start, paused_ns;
    bool paused;
    bs_result error;
};

bs_result bs_video_open(bs_audio *audio, const uint8_t *data, size_t size, bs_video **out)
{
    *out = NULL;
    bs_video *v = calloc(1, sizeof *v);
    if (!v)
        return BS_ERR_NOMEM;
    bs_arena_init(&v->arena, 0);
    bs_result r = bs_smk_open(data, size, &v->arena, &v->smk);
    if (r != BS_OK) {
        bs_arena_free(&v->arena);
        free(v);
        return r;
    }
    v->width = v->smk.width;
    v->height = v->smk.y_mode == BS_SMK_Y_NORMAL ? v->smk.height : v->smk.height * 2;
    for (int i = 0; i < LOOKAHEAD; i++) {
        v->slots[i] = BS_ARENA_ARRAY(&v->arena, uint8_t, (size_t)v->width * v->height * 4);
        if (!v->slots[i]) {
            bs_video_close(v);
            return BS_ERR_NOMEM;
        }
    }
    /* The first track is the soundtrack. */
    const bs_smk_track *tr = &v->smk.tracks[0];
    v->audio = audio;
    if (audio && tr->present) {
        bs_pcm_format f = {tr->bits16 ? 16 : 8, tr->stereo ? 2 : 1, tr->rate};
        v->voice = bs_audio_open_stream(audio, f);
        v->bytes_per_sec = (double)tr->rate * f.channels * f.bits / 8;
    }
    v->start = SDL_GetTicksNS();
    *out = v;
    return BS_OK;
}

void bs_video_close(bs_video *v)
{
    if (!v)
        return;
    if (v->voice)
        bs_audio_stop(v->audio, v->voice);
    if (v->paused && v->audio)
        bs_audio_pause(v->audio, false);
    bs_smk_close(&v->smk);
    bs_arena_free(&v->arena);
    free(v);
}

uint32_t bs_video_width(const bs_video *v) { return v->width; }
uint32_t bs_video_height(const bs_video *v) { return v->height; }
uint32_t bs_video_frame_count(const bs_video *v) { return v->smk.frames; }
uint32_t bs_video_position(const bs_video *v) { return v->shown; }
bs_result bs_video_error(const bs_video *v) { return v->error; }

static void convert_frame(const bs_smacker *s, uint8_t *dst, uint32_t height)
{
    for (uint32_t y = 0; y < height; y++) {
        uint8_t *row = dst + (size_t)y * s->width * 4;
        if (s->y_mode == BS_SMK_Y_INTERLACED && (y & 1)) {
            for (uint32_t x = 0; x < s->width; x++) {
                row[x * 4] = row[x * 4 + 1] = row[x * 4 + 2] = 0;
                row[x * 4 + 3] = 255;
            }
            continue;
        }
        const uint8_t *src = s->pixels + (size_t)(s->y_mode == BS_SMK_Y_NORMAL ? y : y / 2) * s->width;
        for (uint32_t x = 0; x < s->width; x++) {
            memcpy(row + x * 4, s->palette + src[x] * 3, 3);
            row[x * 4 + 3] = 255;
        }
    }
}

void bs_video_pause(bs_video *v, bool paused)
{
    if (paused == v->paused)
        return;
    v->paused = paused;
    if (paused)
        v->pause_start = SDL_GetTicksNS();
    else
        v->paused_ns += SDL_GetTicksNS() - v->pause_start;
    if (v->audio)
        bs_audio_pause(v->audio, paused);
}

const uint8_t *bs_video_update(bs_video *v, bool *done)
{
    uint32_t frames = v->smk.frames;
    *done = false;
    /* Decode ahead, pushing each frame's audio as it is decoded. */
    while (v->decoded < frames && v->decoded < v->shown + LOOKAHEAD) {
        bs_result r = bs_smk_next_frame(&v->smk);
        if (r != BS_OK) {
            v->error = r;
            *done = true;
            return NULL;
        }
        convert_frame(&v->smk, v->slots[v->decoded % LOOKAHEAD], v->height);
        if (v->voice && v->smk.audio_bytes[0]) {
            bs_audio_push(v->audio, v->voice, v->smk.audio[0], v->smk.audio_bytes[0]);
            v->pushed += v->smk.audio_bytes[0];
        }
        v->decoded++;
        if (v->voice && v->decoded == frames)
            bs_audio_end_stream(v->audio, v->voice);
    }
    /* Clock: audio played so far, or wall time for silent videos. */
    uint64_t now = v->paused ? v->pause_start : SDL_GetTicksNS();
    double ms = v->voice ? (double)(v->pushed - bs_audio_queued(v->audio, v->voice)) / v->bytes_per_sec * 1000.0
                         : (double)(now - v->start - v->paused_ns) / 1e6;
    uint32_t due = (uint32_t)(ms / v->smk.frame_ms);
    bool finished = v->voice ? v->decoded == frames && bs_audio_queued(v->audio, v->voice) == 0 : due >= frames;
    if (finished) {
        *done = true;
        return NULL;
    }
    if (due >= v->decoded)
        due = v->decoded - 1; /* decoding fell behind: show the newest */
    if (v->any_shown && due <= v->shown)
        return NULL;
    v->any_shown = true;
    v->shown = due;
    return v->slots[due % LOOKAHEAD];
}
