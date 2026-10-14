#ifndef BS_FORMATS_SMACKER_H
#define BS_FORMATS_SMACKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"

/*
 * Smacker video (the .SMK files in the CD's VIDEOS folder): the intro story
 * and a few cutscenes. All shipped files are "SMK2", 640 wide and 8-bit
 * palettised; four are stored at 240 lines (doubled or interlaced to 480).
 * Five have one Huffman/DPCM-compressed audio track. See docs/formats/smacker.md.
 *
 * Usage: bs_smk_open() parses the header and Huffman trees, then
 * bs_smk_next_frame() decodes frames in order into the decoder's buffers.
 */

#define BS_SMK_TRACKS 7

enum { BS_SMK_Y_NORMAL, BS_SMK_Y_DOUBLED, BS_SMK_Y_INTERLACED };

typedef struct bs_smk_track {
    bool present;
    bool compressed;    /* Huffman/DPCM */
    bool bits16;
    bool stereo;
    uint32_t rate;      /* samples per second */
} bs_smk_track;

typedef struct bs_smk_tree bs_smk_tree;  /* internal */

typedef struct bs_smacker {
    uint32_t width, height; /* as stored; see y_mode */
    uint32_t frames;        /* playable frames (not counting a ring frame) */
    uint32_t flags;         /* bit 0: ring frame follows; bit 1: y-interlaced; bit 2: y-doubled */
    int y_mode;             /* display: BS_SMK_Y_NORMAL, _DOUBLED (each line twice), _INTERLACED
                               (each line followed by a black one); height is the stored height */
    double frame_ms;        /* duration of one frame */
    bs_smk_track tracks[BS_SMK_TRACKS];

    /* Decoder state, valid after each bs_smk_next_frame(). */
    uint32_t next;                 /* index of the next frame to decode */
    uint8_t *pixels;               /* width * height palette indices */
    uint8_t palette[768];          /* 8-bit RGB */
    uint8_t *audio[BS_SMK_TRACKS]; /* this frame's samples per track (unsigned 8-bit or s16le) */
    uint32_t audio_bytes[BS_SMK_TRACKS];

    /* internal */
    const uint8_t *data;
    size_t size;
    uint32_t total_frames;  /* including the ring frame */
    uint32_t *frame_offset, *frame_size;
    uint8_t *frame_type;
    bs_smk_tree *mmap, *mclr, *full, *type;
    uint32_t audio_cap[BS_SMK_TRACKS];
} bs_smacker;

/* Parses the header and trees. data must outlive the decoder. Buffers come
 * from arena, except the audio buffers, which bs_smk_close() frees. */
bs_result bs_smk_open(const uint8_t *data, size_t size, bs_arena *arena, bs_smacker *out);
void bs_smk_close(bs_smacker *s);

/* Decodes the next frame (s->next) into s->pixels, s->palette and s->audio.
 * Returns BS_ERR_NOT_FOUND after the last playable frame. */
bs_result bs_smk_next_frame(bs_smacker *s);

/* Restarts at frame 0. */
void bs_smk_rewind(bs_smacker *s);

#endif
