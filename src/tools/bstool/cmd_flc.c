/* bstool flc: decode FLIC animations, check them, and export frames as PNG. */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/core/png.h"
#include "bs/formats/flic.h"
#include "commands.h"

typedef struct flc_result {
    uint32_t decoded;   /* frame chunks decoded */
    int ring;           /* 1 ring frame matches frame 1, 0 mismatch, -1 no ring frame */
} flc_result;

/*
 * Decodes every frame. Exports frame k (0-based) when out_dir is set and
 * k % every == 0. Returns false on decode or write errors.
 */
static bool process(const char *record, const uint8_t *data, size_t size, const char *out_dir, uint32_t every,
                    bs_arena *arena, flc_result *res)
{
    bs_flic flic;
    bs_flic_player p;
    bs_result r = bs_flic_open(data, size, &flic);
    if (r == BS_OK)
        r = bs_flic_player_init(&p, &flic, arena);
    if (r != BS_OK) {
        fprintf(stderr, "flc: %s: %s\n", record, bs_result_str(r));
        return false;
    }
    size_t npix = (size_t)flic.width * flic.height;
    uint8_t *first = bs_arena_alloc(arena, npix, 16);
    if (!first)
        return false;

    char stem[32];
    snprintf(stem, sizeof stem, "%s", record);
    char *dot = strrchr(stem, '.');
    if (dot)
        *dot = '\0';

    res->ring = -1;
    for (;;) {
        bool ended;
        r = bs_flic_next_frame(&p, &ended);
        if (r != BS_OK) {
            fprintf(stderr, "flc: %s frame %" PRIu32 ": %s\n", record, p.frame + 1, bs_result_str(r));
            return false;
        }
        if (ended)
            break;
        uint32_t k = p.frame - 1;
        if (p.frame == 1)
            memcpy(first, p.pixels, npix);
        if (p.frame == (uint32_t)flic.frames + 1u)
            res->ring = memcmp(first, p.pixels, npix) == 0;
        else if (out_dir && k % every == 0) {
            char file[64], path[BS_PATH_MAX];
            snprintf(file, sizeof file, "%s_f%03" PRIu32 ".png", stem, k);
            if (!bs_path_join(path, sizeof path, out_dir, file) ||
                bs_png_write_indexed(path, flic.width, flic.height, p.pixels, p.palette, -1) != BS_OK) {
                fprintf(stderr, "flc: cannot write %s\n", file);
                return false;
            }
        }
    }
    res->decoded = p.frame;
    printf("%-12s %ux%u  %3u frames  %4" PRIu32 " ms/frame  ring %s\n", record, flic.width, flic.height,
           flic.frames, flic.speed, res->ring < 0 ? "none" : res->ring ? "ok" : "MISMATCH");
    return res->ring != 0 && res->decoded >= flic.frames;
}

int cmd_flc(int argc, char **argv)
{
    const char *out_dir = NULL, *archive = "GAMEDATA/FLC.BSA";
    uint32_t every = 0;
    char **records = calloc((size_t)argc, sizeof *records);
    int num_records = 0;
    if (!records)
        return 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            out_dir = argv[++i];
        else if (strcmp(argv[i], "-a") == 0 && i + 1 < argc)
            archive = argv[++i];
        else if (strcmp(argv[i], "--every") == 0 && i + 1 < argc)
            every = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (argv[i][0] == '-') {
            fprintf(stderr, "usage: bstool flc [-a archive] [-o dir [--every N]] [record...]\n"
                            "  decodes every frame and checks the ring frame; with -o, exports\n"
                            "  frame 0 (or every Nth frame) as PNG\n");
            free(records);
            return 2;
        } else
            records[num_records++] = argv[i];
    }
    if (every == 0)
        every = UINT32_MAX; /* first frame only */

    bstool_ctx ctx = { 0 };
    bool opened = false;
    int status = 0;
    bs_bsa *bsa = bstool_open_archive(&ctx, &opened, archive);
    if (!bsa) {
        status = 1;
        goto done;
    }
    if (out_dir && bs_fs_mkdirs(out_dir) != BS_OK) {
        fprintf(stderr, "flc: cannot create '%s'\n", out_dir);
        status = 1;
        goto done;
    }

    bs_arena tmp;
    bs_arena_init(&tmp, 0);
    uint32_t files = 0, failed = 0, frames = 0;
    for (uint32_t i = 0; i < bsa->count; i++) {
        const bs_bsa_record *rec = &bsa->records[i];
        size_t len = strlen(rec->name);
        if (num_records) {
            bool match = false;
            for (int k = 0; k < num_records && !match; k++)
                match = bs_strcasecmp(records[k], rec->name) == 0;
            if (!match)
                continue;
        } else if (len < 4 || bs_strcasecmp(rec->name + len - 4, ".FLC") != 0) {
            continue;
        }
        uint8_t *data;
        size_t size;
        flc_result res = { 0, -1 };
        if (bs_bsa_read(bsa, rec, &tmp, &data, &size) != BS_OK ||
            !process(rec->name, data, size, out_dir, every, &tmp, &res))
            failed++;
        files++;
        frames += res.decoded;
        bs_arena_reset(&tmp);
    }
    bs_arena_free(&tmp);
    printf("%" PRIu32 " file(s), %" PRIu32 " frames decoded, %" PRIu32 " failed\n", files, frames, failed);
    status = failed ? 1 : 0;
done:
    bs_bsa_close(bsa);
    if (opened)
        bstool_close(&ctx);
    free(records);
    return status;
}
