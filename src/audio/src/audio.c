#include "bs/audio/audio.h"

#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#include "bs/core/log.h"

#define MAX_VOICES 64
#define CHUNK_FRAMES 1024

typedef enum voice_kind { VOICE_FREE, VOICE_SOUND, VOICE_STREAM } voice_kind;

typedef struct voice {
    voice_kind kind;
    SDL_AudioStream *stream;
    /* sounds: signed 16-bit samples, 1 or 2 channels */
    int16_t *pcm;
    size_t frames, pos;
    int channels;
    bool loop;
    float gain, pan;  /* read by the audio thread under the stream lock */
} voice;

struct bs_audio {
    SDL_AudioDeviceID dev;
    SDL_AudioSpec dev_spec;
    voice voices[MAX_VOICES];

    SDL_AudioStream *music;
    stb_vorbis *vorbis;
    int music_channels;
    bool music_loop, music_done;
};

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* ---- device ---- */

bs_result bs_audio_open(bs_audio **out)
{
    *out = NULL;
    bs_audio *a = calloc(1, sizeof *a);
    if (!a)
        return BS_ERR_NOMEM;
    a->dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (!a->dev) {
        BS_ERROR("cannot open audio device: %s", SDL_GetError());
        free(a);
        return BS_ERR_IO;
    }
    if (!SDL_GetAudioDeviceFormat(a->dev, &a->dev_spec, NULL)) {
        SDL_CloseAudioDevice(a->dev);
        free(a);
        return BS_ERR_IO;
    }
    *out = a;
    return BS_OK;
}

static void free_voice(voice *v)
{
    if (v->stream)
        SDL_DestroyAudioStream(v->stream); /* unbinds; the callback no longer runs */
    free(v->pcm);
    memset(v, 0, sizeof *v);
}

void bs_audio_close(bs_audio *a)
{
    if (!a)
        return;
    bs_audio_stop_music(a);
    for (int i = 0; i < MAX_VOICES; i++)
        free_voice(&a->voices[i]);
    SDL_CloseAudioDevice(a->dev);
    free(a);
}

void bs_audio_pause(bs_audio *a, bool paused)
{
    if (paused)
        SDL_PauseAudioDevice(a->dev);
    else
        SDL_ResumeAudioDevice(a->dev);
}

static bool sound_finished(voice *v)
{
    return v->kind == VOICE_SOUND && !v->loop && v->pos >= v->frames && SDL_GetAudioStreamQueued(v->stream) <= 0;
}

/* Finds a free voice, reclaiming finished sounds. Returns its index or -1. */
static int alloc_voice(bs_audio *a)
{
    for (int i = 0; i < MAX_VOICES; i++) {
        voice *v = &a->voices[i];
        if (v->kind == VOICE_SOUND) {
            SDL_LockAudioStream(v->stream);
            bool done = sound_finished(v);
            SDL_UnlockAudioStream(v->stream);
            if (done)
                free_voice(v);
        }
        if (v->kind == VOICE_FREE)
            return i;
    }
    return -1;
}

static voice *get_voice(bs_audio *a, bs_voice h, voice_kind kind)
{
    if (h < 1 || h > MAX_VOICES)
        return NULL;
    voice *v = &a->voices[h - 1];
    return v->kind == kind ? v : NULL;
}

static bool valid_format(bs_pcm_format f)
{
    return (f.bits == 8 || f.bits == 16) && (f.channels == 1 || f.channels == 2) && f.rate > 0 && f.rate <= 192000;
}

/* ---- sounds ---- */

/* Audio-thread callback: feeds the next chunk with the current gain and pan, as stereo S16. */
static void SDLCALL feed_sound(void *userdata, SDL_AudioStream *stream, int additional, int total)
{
    (void)total;
    voice *v = userdata;
    int16_t buf[CHUNK_FRAMES * 2];
    float gl = clampf(1.0f - v->pan, 0, 1) * v->gain, gr = clampf(1.0f + v->pan, 0, 1) * v->gain;
    while (additional > 0) {
        if (v->pos >= v->frames) {
            if (!v->loop || v->frames == 0) {
                SDL_FlushAudioStream(stream); /* let the resampler release its last samples */
                return;
            }
            v->pos = 0;
        }
        size_t n = v->frames - v->pos;
        if (n > CHUNK_FRAMES)
            n = CHUNK_FRAMES;
        for (size_t i = 0; i < n; i++) {
            const int16_t *s = v->pcm + (v->pos + i) * (size_t)v->channels;
            float l = s[0], r = v->channels == 2 ? s[1] : s[0];
            buf[i * 2] = (int16_t)clampf(l * gl, -32768, 32767);
            buf[i * 2 + 1] = (int16_t)clampf(r * gr, -32768, 32767);
        }
        v->pos += n;
        SDL_PutAudioStreamData(stream, buf, (int)(n * 4));
        additional -= (int)(n * 4);
    }
}

bs_voice bs_audio_play(bs_audio *a, const void *pcm, size_t bytes, bs_pcm_format fmt, float gain, float pan,
                       bool loop)
{
    if (!valid_format(fmt))
        return 0;
    int i = alloc_voice(a);
    if (i < 0)
        return 0;
    voice *v = &a->voices[i];
    size_t frame_bytes = (size_t)fmt.channels * (size_t)(fmt.bits / 8);
    v->frames = bytes / frame_bytes;
    v->channels = fmt.channels;
    v->pcm = malloc(v->frames * (size_t)fmt.channels * sizeof *v->pcm + 1);
    if (!v->pcm)
        return 0;
    const uint8_t *src = pcm;
    for (size_t k = 0; k < v->frames * (size_t)fmt.channels; k++) {
        if (fmt.bits == 8) {
            int sample = ((int)src[k] - 128) * 256;
            v->pcm[k] = (int16_t)sample;
        } else {
            uint16_t sample = (uint16_t)(src[k * 2] | src[k * 2 + 1] << 8);
            v->pcm[k] = (int16_t)sample;
        }
    }
    v->gain = gain;
    v->pan = clampf(pan, -1, 1);
    v->loop = loop;
    SDL_AudioSpec spec = {SDL_AUDIO_S16LE, 2, (int)fmt.rate};
    v->stream = SDL_CreateAudioStream(&spec, &a->dev_spec);
    if (!v->stream) {
        free_voice(v);
        return 0;
    }
    v->kind = VOICE_SOUND;
    SDL_SetAudioStreamGetCallback(v->stream, feed_sound, v);
    if (!SDL_BindAudioStream(a->dev, v->stream)) {
        free_voice(v);
        return 0;
    }
    return i + 1;
}

void bs_audio_set(bs_audio *a, bs_voice h, float gain, float pan)
{
    voice *v = get_voice(a, h, VOICE_SOUND);
    if (!v)
        return;
    SDL_LockAudioStream(v->stream);
    v->gain = gain;
    v->pan = clampf(pan, -1, 1);
    SDL_UnlockAudioStream(v->stream);
}

void bs_audio_stop(bs_audio *a, bs_voice h)
{
    voice *v = get_voice(a, h, VOICE_SOUND);
    if (!v)
        v = get_voice(a, h, VOICE_STREAM);
    if (v)
        free_voice(v);
}

bool bs_audio_playing(bs_audio *a, bs_voice h)
{
    voice *v = get_voice(a, h, VOICE_SOUND);
    if (!v)
        return get_voice(a, h, VOICE_STREAM) != NULL;
    SDL_LockAudioStream(v->stream);
    bool done = sound_finished(v);
    SDL_UnlockAudioStream(v->stream);
    return !done;
}

/* ---- music ---- */

static void SDLCALL feed_music(void *userdata, SDL_AudioStream *stream, int additional, int total)
{
    (void)total;
    bs_audio *a = userdata;
    short buf[CHUNK_FRAMES * 2];
    int ch = a->music_channels;
    while (additional > 0 && !a->music_done) {
        int n = stb_vorbis_get_samples_short_interleaved(a->vorbis, ch, buf, CHUNK_FRAMES * ch);
        if (n <= 0) {
            if (a->music_loop && stb_vorbis_seek_start(a->vorbis))
                continue;
            a->music_done = true;
            SDL_FlushAudioStream(stream);
            return;
        }
        SDL_PutAudioStreamData(stream, buf, n * ch * 2);
        additional -= n * ch * 2;
    }
}

bs_result bs_audio_play_music(bs_audio *a, const uint8_t *ogg, size_t size, bool loop, float gain)
{
    bs_audio_stop_music(a);
    if (size > INT32_MAX)
        return BS_ERR_UNSUPPORTED;
    int err = 0;
    a->vorbis = stb_vorbis_open_memory(ogg, (int)size, &err, NULL);
    if (!a->vorbis)
        return BS_ERR_FORMAT;
    stb_vorbis_info info = stb_vorbis_get_info(a->vorbis);
    a->music_channels = info.channels > 2 ? 2 : info.channels;
    a->music_loop = loop;
    a->music_done = false;
    SDL_AudioSpec spec = {SDL_AUDIO_S16, a->music_channels, (int)info.sample_rate};
    a->music = SDL_CreateAudioStream(&spec, &a->dev_spec);
    if (!a->music) {
        bs_audio_stop_music(a);
        return BS_ERR_IO;
    }
    SDL_SetAudioStreamGain(a->music, gain);
    SDL_SetAudioStreamGetCallback(a->music, feed_music, a);
    if (!SDL_BindAudioStream(a->dev, a->music)) {
        bs_audio_stop_music(a);
        return BS_ERR_IO;
    }
    return BS_OK;
}

void bs_audio_set_music_gain(bs_audio *a, float gain)
{
    if (a && a->music)
        SDL_SetAudioStreamGain(a->music, gain);
}

void bs_audio_stop_music(bs_audio *a)
{
    if (a->music)
        SDL_DestroyAudioStream(a->music);
    a->music = NULL;
    if (a->vorbis)
        stb_vorbis_close(a->vorbis);
    a->vorbis = NULL;
}

bool bs_audio_music_playing(bs_audio *a)
{
    if (!a->music)
        return false;
    SDL_LockAudioStream(a->music);
    bool done = a->music_done && SDL_GetAudioStreamQueued(a->music) <= 0;
    SDL_UnlockAudioStream(a->music);
    return !done;
}

/* ---- push streams ---- */

bs_voice bs_audio_open_stream(bs_audio *a, bs_pcm_format fmt)
{
    if (!valid_format(fmt))
        return 0;
    int i = alloc_voice(a);
    if (i < 0)
        return 0;
    voice *v = &a->voices[i];
    SDL_AudioSpec spec = {fmt.bits == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE, fmt.channels, (int)fmt.rate};
    v->stream = SDL_CreateAudioStream(&spec, &a->dev_spec);
    if (!v->stream || !SDL_BindAudioStream(a->dev, v->stream)) {
        free_voice(v);
        return 0;
    }
    v->kind = VOICE_STREAM;
    return i + 1;
}

bs_result bs_audio_push(bs_audio *a, bs_voice h, const void *pcm, size_t bytes)
{
    voice *v = get_voice(a, h, VOICE_STREAM);
    if (!v || bytes > INT32_MAX)
        return BS_ERR_ARGS;
    return SDL_PutAudioStreamData(v->stream, pcm, (int)bytes) ? BS_OK : BS_ERR_IO;
}

void bs_audio_end_stream(bs_audio *a, bs_voice h)
{
    voice *v = get_voice(a, h, VOICE_STREAM);
    if (v)
        SDL_FlushAudioStream(v->stream);
}

size_t bs_audio_queued(bs_audio *a, bs_voice h)
{
    voice *v = get_voice(a, h, VOICE_STREAM);
    if (!v)
        return 0;
    int q = SDL_GetAudioStreamQueued(v->stream);
    return q > 0 ? (size_t)q : 0;
}
