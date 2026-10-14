#include "bs/audio/level_audio.h"

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "bs/audio/place.h"
#include "bs/core/log.h"

/*
 * The original rolls for an ambient sound once per frame, so how often one
 * plays depends on the frame rate. Its tick rate is not known yet; 20 frames
 * per second is assumed (PLAN.md TODO).
 */
#define FRAME_RATE 20.0

/* GAME.EXE's ambient sound ids (table at 0xED334). Id 1 is not in SPIRE.SND. */
static const uint16_t ambient_ids[BS_LEVEL_AUDIO_AMBIENT_SOUNDS] = {
    1,   2,   3,   4,   5,   28,  29,  30,  31,  48,  50,  100, 217, 329, 330, 331,
    332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343, 344, 346, 347, 348,
};

#define AMBIENT_CHANCE 200     /* rand() <= 200 of 0..32767 */
#define AMBIENT_MIN_DISTANCE 64
#define AMBIENT_MAX_DISTANCE 600

/* The C library generator the game uses (Watcom rand()). */
static int32_t game_rand(bs_level_audio *la)
{
    la->rand_state = la->rand_state * 1103515245u + 12345u;
    return (int32_t)((la->rand_state >> 16) & 0x7FFF);
}

void bs_level_audio_open(bs_level_audio *la, bs_audio *audio, bs_vfs *vfs)
{
    memset(la, 0, sizeof *la);
    la->audio = audio;
    la->vfs = vfs;
    la->music = true;
    la->music_gain = 0.8f;
    la->effects_gain = 1.0f;
    la->rand_state = (uint32_t)SDL_GetTicksNS();
    bs_arena_init(&la->arena, 0);
    if (audio && bs_bsa_open_vfs(vfs, "GAMEDATA/SPIRE.SND", &la->effects) != BS_OK) {
        BS_WARN("cannot open SPIRE.SND: no sound effects");
        la->effects = NULL;
    }
}

void bs_level_audio_close(bs_level_audio *la)
{
    bs_level_audio_stop(la);
    bs_bsa_close(la->effects);
    bs_arena_free(&la->arena);
    memset(la, 0, sizeof *la);
}

void bs_level_audio_stop(bs_level_audio *la)
{
    if (la->audio)
        bs_audio_stop_music(la->audio); /* before the music data is freed */
    bs_arena_free(&la->arena);
    bs_arena_init(&la->arena, 0);
    memset(la->sound_state, 0, sizeof la->sound_state);
    la->effects_cached = 0;
    la->have_nodes = false;
    la->tick_time = 0;
}

void bs_level_audio_start(bs_level_audio *la, const bs_level_info *info)
{
    if (!la->audio)
        return;
    bs_level_audio_stop(la);
    if (!info)
        return;

    int track = bs_level_music_track(info);
    if (la->music && track) {
        char path[32];
        snprintf(path, sizeof path, "MUSIC/Track%02d.ogg", track);
        uint8_t *data;
        size_t size;
        bs_result r = bs_vfs_read(la->vfs, path, &la->arena, &data, &size);
        if (r == BS_OK)
            r = bs_audio_play_music(la->audio, data, size, true, la->music_gain);
        if (r == BS_OK)
            printf("music: %s\n", path);
        else if (r == BS_ERR_NOT_FOUND)
            printf("music: none (the level asks for CD track %d, which the disc does not have)\n", track);
        else
            BS_WARN("%s: %s", path, bs_result_str(r));
    }

    if (info->nodes[0]) {
        char path[64];
        snprintf(path, sizeof path, "GAMEDATA/%s", info->nodes);
        uint8_t *data;
        size_t size;
        bs_result r = bs_vfs_read(la->vfs, path, &la->arena, &data, &size);
        if (r == BS_OK)
            r = bs_nodes_parse(data, size, &la->arena, &la->nodes);
        if (r == BS_OK)
            la->have_nodes = true;
        else
            BS_WARN("%s: %s: no ambient sounds", path, bs_result_str(r));
    }
}

static const bs_wav *ambient_sound(bs_level_audio *la, int k)
{
    if (la->sound_state[k] == 0) {
        la->sound_state[k] = 2;
        const bs_bsa_record *rec = la->effects ? bs_bsa_find_id(la->effects, ambient_ids[k]) : NULL;
        uint8_t *data;
        size_t size;
        if (rec && bs_bsa_read(la->effects, rec, &la->arena, &data, &size) == BS_OK &&
            bs_wav_parse(data, size, &la->sounds[k]) == BS_OK)
            la->sound_state[k] = 1;
    }
    return la->sound_state[k] == 1 ? &la->sounds[k] : NULL;
}

/* One frame of the original's ambient sound routine (0x1CDA5). */
static void ambient_tick(bs_level_audio *la, const float pos[3], int32_t yaw)
{
    if (game_rand(la) > AMBIENT_CHANCE)
        return;
    /* The navigation nodes 64..600 units away. */
    uint32_t in_range = 0, pick = 0;
    for (uint32_t i = 0; i < la->nodes.count; i++) {
        int32_t d = bs_sound_distance(pos, la->nodes.nodes[i].pos);
        in_range += d >= AMBIENT_MIN_DISTANCE && d <= AMBIENT_MAX_DISTANCE;
    }
    if (in_range == 0)
        return;
    if (la->fixed_ambience) {
        /* Presumably the intent: one of the nodes in range. */
        uint32_t k = (uint32_t)game_rand(la) % in_range;
        for (uint32_t i = 0; i < la->nodes.count; i++) {
            int32_t d = bs_sound_distance(pos, la->nodes.nodes[i].pos);
            if (d >= AMBIENT_MIN_DISTANCE && d <= AMBIENT_MAX_DISTANCE && k-- == 0) {
                pick = i;
                break;
            }
        }
    } else {
        /* What the game does: it uses the number of nodes in range as an index
         * into the whole list, so the sound comes from a fixed low-numbered
         * node, which is often too far away to be heard. */
        pick = in_range;
        if (pick >= la->nodes.count)
            return;
    }
    int k = game_rand(la) % BS_LEVEL_AUDIO_AMBIENT_SOUNDS;
    const bs_wav *w = ambient_sound(la, k);
    bs_sound_place p;
    if (!w || !bs_sound_place_at(pos, yaw, la->nodes.nodes[pick].pos, BS_SOUND_DISTANCE_NEAR, false, &p))
        return;
    bs_pcm_format f = {w->bits, w->channels, w->rate};
    if (bs_audio_play(la->audio, w->samples, w->sample_bytes, f, p.gain * la->effects_gain, p.pan, false))
        printf("ambient: sound %u at node %u, volume %.2f, pan %+.2f\n", ambient_ids[k], pick, (double)p.gain,
               (double)p.pan);
}

static int32_t game_yaw(float yaw)
{
    /* bsview's and the game's camera yaw: 0 faces +z, increasing towards +x. */
    return (int32_t)SDL_lroundf(yaw * 2048.0f / (2.0f * SDL_PI_F)) & 0x7FF;
}

static const bs_wav *effect(bs_level_audio *la, int32_t id)
{
    for (uint32_t i = 0; i < la->effects_cached; i++)
        if (la->effects_cache[i].id == id)
            return la->effects_cache[i].ok ? &la->effects_cache[i].wav : NULL;
    if (la->effects_cached == BS_LEVEL_AUDIO_EFFECT_CACHE)
        la->effects_cached = 0; /* full: start over (the data stays in the level arena) */
    uint32_t i = la->effects_cached++;
    la->effects_cache[i].id = id;
    la->effects_cache[i].ok = false;
    const bs_bsa_record *rec = la->effects && id >= 0 && id <= 0xFFFF ? bs_bsa_find_id(la->effects, (uint16_t)id) : NULL;
    uint8_t *data;
    size_t size;
    if (rec && bs_bsa_read(la->effects, rec, &la->arena, &data, &size) == BS_OK &&
        bs_wav_parse(data, size, &la->effects_cache[i].wav) == BS_OK)
        la->effects_cache[i].ok = true;
    return la->effects_cache[i].ok ? &la->effects_cache[i].wav : NULL;
}

void bs_level_audio_play_at(bs_level_audio *la, int32_t id, const float pos[3], const float listener[3], float yaw)
{
    if (!la->audio)
        return;
    const bs_wav *w = effect(la, id);
    bs_sound_place p;
    if (!w || !bs_sound_place_at(listener, game_yaw(yaw), pos, BS_SOUND_DISTANCE_NEAR, false, &p))
        return;
    bs_pcm_format f = {w->bits, w->channels, w->rate};
    bs_audio_play(la->audio, w->samples, w->sample_bytes, f, p.gain * la->effects_gain, p.pan, false);
}

void bs_level_audio_update(bs_level_audio *la, const float pos[3], float yaw, double dt)
{
    if (!la->audio || !la->have_nodes)
        return;
    int32_t gyaw = game_yaw(yaw);
    la->tick_time += dt;
    for (int n = 0; la->tick_time >= 1.0 / FRAME_RATE && n < 4; n++) {
        la->tick_time -= 1.0 / FRAME_RATE;
        ambient_tick(la, pos, gyaw);
    }
    if (la->tick_time > 1.0 / FRAME_RATE)
        la->tick_time = 0; /* after a stall, don't catch up */
}
