/* bsplay: plays the game's videos, sounds and music. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "bs/audio/audio.h"
#include "bs/audio/video.h"
#include "bs/core/fs.h"
#include "bs/core/install.h"
#include "bs/formats/bsa.h"
#include "bs/formats/wav.h"

typedef struct options {
    const char *data_dir;
    const char *mode;   /* video, sound, music */
    const char *name;
    const char *audio_out;
    bool headless;
    float gain, pan, seconds;
} options;

static void usage(FILE *out)
{
    fprintf(out,
            "usage: bsplay [-d install-dir] [options] <mode> <name>\n"
            "  video NAME     a video in the CD's VIDEOS folder (STORY, MALE, FEMALE, ANCHORS,\n"
            "                 CHOP2, JUMP) or a host .SMK path; Esc skips, Space pauses\n"
            "  sound NAME     a sound: a number from SPIRE.SND (e.g. 102) or a WAVES.BSA name\n"
            "                 (e.g. SM1A01)\n"
            "  music N        MUSIC/TrackNN.ogg\n"
            "options:\n"
            "  --gain G --pan P   sound volume (default 1) and stereo position (-1..1)\n"
            "  --seconds S        stop after S seconds\n"
            "  --headless         no window (videos still play their sound)\n"
            "  --audio-out FILE   write the mixed output to FILE (raw, SDL's disk driver) instead of\n"
            "                     the sound card, at 10x speed, for testing\n");
}

static bool parse_args(int argc, char **argv, options *o)
{
    memset(o, 0, sizeof *o);
    o->gain = 1.0f;
    o->data_dir = getenv("BS_DATA_DIR");
    int pos = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        bool has = i + 1 < argc;
        if ((strcmp(a, "-d") == 0 || strcmp(a, "--data") == 0) && has)
            o->data_dir = argv[++i];
        else if (strcmp(a, "--gain") == 0 && has)
            o->gain = strtof(argv[++i], NULL);
        else if (strcmp(a, "--pan") == 0 && has)
            o->pan = strtof(argv[++i], NULL);
        else if (strcmp(a, "--seconds") == 0 && has)
            o->seconds = strtof(argv[++i], NULL);
        else if (strcmp(a, "--headless") == 0)
            o->headless = true;
        else if (strcmp(a, "--audio-out") == 0 && has)
            o->audio_out = argv[++i];
        else if (a[0] == '-')
            return false;
        else if (pos == 0)
            o->mode = a, pos++;
        else if (pos == 1)
            o->name = a, pos++;
        else
            return false;
    }
    return o->mode && o->name;
}

typedef struct app {
    bs_install inst;
    bs_vfs vfs;
    bs_cdimage *cd;
    bs_arena arena;
    bs_audio *audio;
    SDL_Window *window;
    SDL_Renderer *renderer;
    bool quit;
} app;

static bool open_install(app *a, const char *dir)
{
    if (!dir) {
        fprintf(stderr, "bsplay: no install directory; use -d <dir> or set BS_DATA_DIR\n");
        return false;
    }
    if (bs_install_detect(dir, &a->inst) != BS_OK) {
        fprintf(stderr, "bsplay: '%s' is not a Battlespire installation\n", dir);
        return false;
    }
    bs_vfs_init(&a->vfs);
    return bs_install_mount(&a->inst, &a->vfs, &a->cd) == BS_OK;
}

/* Handles window events; returns false to stop. */
static bool pump(app *a, bool *paused)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT)
            return false;
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
            if (e.key.key == SDLK_ESCAPE)
                return false;
            if (e.key.key == SDLK_SPACE && paused) {
                *paused = !*paused;
                bs_audio_pause(a->audio, *paused);
            }
        }
    }
    return true;
}

/* ---- video ---- */

static int play_video(app *a, const options *o)
{
    char path[BS_PATH_MAX];
    uint8_t *data;
    size_t size;
    bs_result r;
    if (bs_fs_is_file(o->name)) {
        r = bs_fs_read_file(o->name, &a->arena, &data, &size);
    } else {
        snprintf(path, sizeof path, "VIDEOS/%s%s", o->name, strchr(o->name, '.') ? "" : ".SMK");
        r = bs_vfs_read(&a->vfs, path, &a->arena, &data, &size);
    }
    bs_video *v = NULL;
    if (r == BS_OK)
        r = bs_video_open(a->audio, data, size, &v);
    if (r != BS_OK) {
        fprintf(stderr, "bsplay: %s: %s\n", o->name, bs_result_str(r));
        return 1;
    }
    uint32_t w = bs_video_width(v), h = bs_video_height(v), frames = bs_video_frame_count(v);
    printf("%s: %ux%u, %u frames\n", o->name, w, h, frames);

    SDL_Texture *tex = NULL;
    if (!o->headless) {
        a->window = SDL_CreateWindow("bsplay", (int)w, (int)h, SDL_WINDOW_RESIZABLE);
        a->renderer = a->window ? SDL_CreateRenderer(a->window, NULL) : NULL;
        tex = a->renderer ? SDL_CreateTexture(a->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                              (int)w, (int)h)
                          : NULL;
        if (!tex) {
            fprintf(stderr, "bsplay: cannot open a window: %s\n", SDL_GetError());
            bs_video_close(v);
            return 1;
        }
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
        SDL_SetRenderLogicalPresentation(a->renderer, (int)w, (int)h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    }

    uint64_t start = SDL_GetTicksNS();
    bool paused = false, done = false;
    while (!done) {
        bool was_paused = paused;
        if (!pump(a, &paused)) {
            printf("skipped at frame %u\n", bs_video_position(v));
            break;
        }
        if (paused != was_paused)
            bs_video_pause(v, paused);
        const uint8_t *frame = bs_video_update(v, &done);
        if (frame && tex) {
            SDL_UpdateTexture(tex, NULL, frame, (int)w * 4);
            SDL_RenderClear(a->renderer);
            SDL_RenderTexture(a->renderer, tex, NULL, NULL);
            SDL_RenderPresent(a->renderer);
        }
        if (o->seconds > 0 && (double)(SDL_GetTicksNS() - start) / 1e9 > o->seconds)
            break;
        SDL_Delay(2);
    }
    r = bs_video_error(v);
    if (r != BS_OK)
        fprintf(stderr, "bsplay: frame %u: %s\n", bs_video_position(v), bs_result_str(r));
    else if (done)
        printf("played %u frames\n", frames);
    if (tex)
        SDL_DestroyTexture(tex);
    bs_video_close(v);
    return r == BS_OK ? 0 : 1;
}

/* ---- sounds and music ---- */

static bool wait_while(app *a, bool (*playing)(app *, bs_voice), bs_voice v, float seconds)
{
    uint64_t start = SDL_GetTicksNS();
    while (playing(a, v)) {
        if (a->window && !pump(a, NULL))
            return false;
        if (seconds > 0 && (double)(SDL_GetTicksNS() - start) / 1e9 > seconds)
            return true;
        SDL_Delay(10);
    }
    return true;
}

static bool sound_playing(app *a, bs_voice v)
{
    return bs_audio_playing(a->audio, v);
}

static bool music_playing(app *a, bs_voice v)
{
    (void)v;
    return bs_audio_music_playing(a->audio);
}

static int play_sound(app *a, const options *o)
{
    char *end;
    unsigned long id = strtoul(o->name, &end, 10);
    bool numbered = *end == '\0';
    const char *archive = numbered ? "GAMEDATA/SPIRE.SND" : "WAVES.BSA";
    bs_bsa *bsa;
    if (bs_bsa_open_vfs(&a->vfs, archive, &bsa) != BS_OK) {
        fprintf(stderr, "bsplay: cannot open %s\n", archive);
        return 1;
    }
    char name[32];
    snprintf(name, sizeof name, "%s%s", o->name, numbered || strchr(o->name, '.') ? "" : ".WAV");
    const bs_bsa_record *rec = numbered ? bs_bsa_find_id(bsa, (uint16_t)id) : bs_bsa_find(bsa, name);
    uint8_t *data;
    size_t size;
    bs_wav wav;
    bs_result r = rec ? bs_bsa_read(bsa, rec, &a->arena, &data, &size) : BS_ERR_NOT_FOUND;
    if (r == BS_OK)
        r = bs_wav_parse(data, size, &wav);
    bs_bsa_close(bsa);
    if (r != BS_OK) {
        fprintf(stderr, "bsplay: sound %s: %s\n", o->name, bs_result_str(r));
        return 1;
    }
    printf("%s %s: %u Hz, %d-bit, %d channel(s), %.2f s\n", archive, o->name, wav.rate, wav.bits, wav.channels,
           (double)wav.frame_count / wav.rate);
    bs_pcm_format f = {wav.bits, wav.channels, wav.rate};
    bs_voice v = bs_audio_play(a->audio, wav.samples, wav.sample_bytes, f, o->gain, o->pan, o->seconds > 0);
    if (!v)
        return 1;
    wait_while(a, sound_playing, v, o->seconds);
    return 0;
}

static int play_music(app *a, const options *o)
{
    char path[BS_PATH_MAX];
    snprintf(path, sizeof path, "MUSIC/Track%02d.ogg", atoi(o->name));
    uint8_t *data;
    size_t size;
    bs_result r = bs_vfs_read(&a->vfs, path, &a->arena, &data, &size);
    if (r == BS_OK)
        r = bs_audio_play_music(a->audio, data, size, false, o->gain);
    if (r != BS_OK) {
        fprintf(stderr, "bsplay: %s: %s\n", path, bs_result_str(r));
        return 1;
    }
    printf("playing %s\n", path);
    wait_while(a, music_playing, 0, o->seconds);
    return 0;
}

int main(int argc, char **argv)
{
    options o;
    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        usage(stdout);
        return 0;
    }
    if (!parse_args(argc, argv, &o)) {
        usage(stderr);
        return 2;
    }
    if (o.audio_out) {
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "disk");
        SDL_SetHint("SDL_AUDIO_DISK_OUTPUT_FILE", o.audio_out);
        SDL_SetHint("SDL_AUDIO_DISK_TIMESCALE", "0.1");
    }
    static app a;
    bs_arena_init(&a.arena, 0);
    if (!open_install(&a, o.data_dir))
        return 1;
    SDL_InitFlags flags = SDL_INIT_AUDIO | (strcmp(o.mode, "video") == 0 && !o.headless ? SDL_INIT_VIDEO : 0);
    if (!SDL_Init(flags) || bs_audio_open(&a.audio) != BS_OK) {
        fprintf(stderr, "bsplay: cannot initialise SDL audio: %s\n", SDL_GetError());
        return 1;
    }
    int status;
    if (strcmp(o.mode, "video") == 0)
        status = play_video(&a, &o);
    else if (strcmp(o.mode, "sound") == 0)
        status = play_sound(&a, &o);
    else if (strcmp(o.mode, "music") == 0)
        status = play_music(&a, &o);
    else {
        usage(stderr);
        status = 2;
    }
    bs_audio_close(a.audio);
    if (a.renderer)
        SDL_DestroyRenderer(a.renderer);
    if (a.window)
        SDL_DestroyWindow(a.window);
    SDL_Quit();
    bs_cdimage_close(a.cd);
    bs_arena_free(&a.arena);
    return status;
}
