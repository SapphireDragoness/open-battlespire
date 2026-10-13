/* bstool bsi: inspect BSI images and export them as PNG. */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/core/png.h"
#include "bs/formats/bsi.h"
#include "bs/formats/palette.h"
#include "commands.h"

typedef enum pal_mode { PAL_HICL, PAL_CMAP, PAL_GLOBAL } pal_mode;

typedef struct bsi_opts {
    const char *out_dir;  /* NULL: info only */
    pal_mode palette;
    bs_palette global;    /* PAL.RAW, for PAL_GLOBAL */
} bsi_opts;

static bool has_ext(const char *name, const char *ext)
{
    size_t n = strlen(name), e = strlen(ext);
    return n >= e && bs_strcasecmp(name + n - e, ext) == 0;
}

static void image_palette(const bsi_opts *o, const bs_bsi_image *img, uint8_t pal[768])
{
    for (int i = 0; i < 256; i++) {
        switch (o->palette) {
        case PAL_HICL:
            bs_bsi_pixel_rgb(img, (uint8_t)i, pal + i * 3);
            break;
        case PAL_CMAP:
            for (int k = 0; k < 3; k++)
                pal[i * 3 + k] = bs_vga6_to_8(img->cmap[i * 3 + k]);
            break;
        case PAL_GLOBAL:
            memcpy(pal + i * 3, o->global.rgb[i], 3);
            break;
        }
    }
}

/* Prints and/or exports one BSI record. Returns false on error. */
static bool process(const bsi_opts *o, const char *record, const uint8_t *data, size_t size, bs_arena *arena)
{
    bs_bsi bsi;
    bs_result r = bs_bsi_parse(data, size, arena, &bsi);
    if (r != BS_OK) {
        fprintf(stderr, "bsi: %s: %s\n", record, bs_result_str(r));
        return false;
    }
    char stem[32];
    snprintf(stem, sizeof stem, "%s", record);
    char *dot = strrchr(stem, '.');
    if (dot)
        *dot = '\0';

    for (size_t i = 0; i < bsi.count; i++) {
        const bs_bsi_image *img = &bsi.images[i];
        if (!o->out_dir) {
            printf("%-12s", i == 0 ? record : "");
            if (bsi.count > 1)
                printf(" [%zu]", i);
            printf(" %4ux%-4u", img->width, img->height);
            if (img->frames != 1)
                printf(" x%-2u", img->frames);
            printf(" at %d,%d enc=%u%s", img->x, img->y, img->encoding, img->transparent ? " transparent" : "");
            printf(" bhdr[4,8,9,11]=%d,%d,%d,%d", img->bhdr[4], img->bhdr[8], img->bhdr[9], img->bhdr[11]);
            if (img->name[0])
                printf(" name=\"%s\"", img->name);
            if (!img->has_hicl)
                printf(" (no HICL)");
            putchar('\n');
            continue;
        }

        uint8_t pal[768];
        image_palette(o, img, pal);
        size_t frame_size = (size_t)img->width * img->height;
        for (uint32_t f = 0; f < img->frames && frame_size; f++) {
            char file[64], path[BS_PATH_MAX];
            int n = snprintf(file, sizeof file, "%s", stem);
            if (bsi.count > 1)
                n += snprintf(file + n, sizeof file - (size_t)n, "_%zu", i);
            if (img->frames > 1)
                n += snprintf(file + n, sizeof file - (size_t)n, "_f%02" PRIu32, f);
            snprintf(file + n, sizeof file - (size_t)n, ".png");
            if (!bs_path_join(path, sizeof path, o->out_dir, file))
                return false;
            r = bs_png_write_indexed(path, img->width, img->height, img->pixels + f * frame_size, pal,
                                  img->transparent ? 0 : -1);
            if (r != BS_OK) {
                fprintf(stderr, "bsi: %s: %s\n", path, bs_result_str(r));
                return false;
            }
        }
    }
    return true;
}

int cmd_bsi(int argc, char **argv)
{
    bsi_opts o = { NULL, PAL_HICL, { { { 0 } } } };
    const char *archive = "GAMEDATA/BSI.BSA";
    char **records = calloc((size_t)argc, sizeof *records);
    int num_records = 0;
    if (!records)
        return 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            o.out_dir = argv[++i];
        else if (strcmp(argv[i], "-a") == 0 && i + 1 < argc)
            archive = argv[++i];
        else if (strcmp(argv[i], "--palette") == 0 && i + 1 < argc) {
            const char *p = argv[++i];
            if (strcmp(p, "hicl") == 0)
                o.palette = PAL_HICL;
            else if (strcmp(p, "cmap") == 0)
                o.palette = PAL_CMAP;
            else if (strcmp(p, "global") == 0)
                o.palette = PAL_GLOBAL;
            else {
                fprintf(stderr, "bsi: unknown palette '%s'\n", p);
                free(records);
                return 2;
            }
        } else if (argv[i][0] == '-') {
            fprintf(stderr,
                    "usage: bstool bsi [-a archive] [-o dir] [--palette hicl|cmap|global] [record...]\n"
                    "  without -o: print image headers; with -o: export PNGs (one per frame)\n"
                    "  records default to every .BSI in the archive (default GAMEDATA/BSI.BSA)\n"
                    "  palettes: hicl = image colour table (correct), cmap = image VGA palette,\n"
                    "            global = PAL.RAW\n");
            free(records);
            return 2;
        } else
            records[num_records++] = argv[i];
    }

    bstool_ctx ctx = { 0 };
    bool opened = false;
    int status = 0;
    bs_bsa *bsa = bstool_open_archive(&ctx, &opened, archive);
    if (!bsa) {
        status = 1;
        goto done;
    }
    if (o.palette == PAL_GLOBAL) {
        uint8_t *pal;
        size_t n;
        if (!opened && !bstool_open(&ctx)) {
            status = 1;
            goto done;
        }
        opened = true;
        if (bs_vfs_read(&ctx.vfs, "GAMEDATA/PAL.RAW", &ctx.arena, &pal, &n) != BS_OK ||
            bs_palette_from_vga6(pal, n, &o.global) != BS_OK) {
            fprintf(stderr, "bsi: cannot read PAL.RAW\n");
            status = 1;
            goto done;
        }
    }
    if (o.out_dir && bs_fs_mkdirs(o.out_dir) != BS_OK) {
        fprintf(stderr, "bsi: cannot create '%s'\n", o.out_dir);
        status = 1;
        goto done;
    }

    bs_arena tmp;
    bs_arena_init(&tmp, 0);
    uint32_t done_count = 0, failed = 0;
    for (uint32_t i = 0; i < bsa->count; i++) {
        const bs_bsa_record *rec = &bsa->records[i];
        if (num_records) {
            bool match = false;
            for (int k = 0; k < num_records && !match; k++)
                match = bs_strcasecmp(records[k], rec->name) == 0;
            if (!match)
                continue;
        } else if (!has_ext(rec->name, ".BSI")) {
            continue;
        }
        uint8_t *data;
        size_t size;
        if (bs_bsa_read(bsa, rec, &tmp, &data, &size) != BS_OK || !process(&o, rec->name, data, size, &tmp))
            failed++;
        else
            done_count++;
        bs_arena_reset(&tmp);
    }
    bs_arena_free(&tmp);
    if (o.out_dir)
        printf("Exported %" PRIu32 " record(s) to %s\n", done_count, o.out_dir);
    if (failed) {
        fprintf(stderr, "bsi: %" PRIu32 " record(s) failed\n", failed);
        status = 1;
    }
    if (num_records && done_count + failed < (uint32_t)num_records) {
        fprintf(stderr, "bsi: some requested records were not found\n");
        status = 1;
    }
done:
    bs_bsa_close(bsa);
    if (opened)
        bstool_close(&ctx);
    free(records);
    return status;
}
