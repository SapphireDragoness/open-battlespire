/* bstool levels / rsc / cdls: text tables and CD image listing. */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/formats/levels.h"
#include "bs/formats/rsc.h"
#include "commands.h"

int cmd_levels(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    bstool_ctx ctx;
    if (!bstool_open(&ctx))
        return 1;

    int status = 1;
    uint8_t *text;
    size_t size;
    bs_level_table table;
    bs_result r = bs_vfs_read(&ctx.vfs, "GAMEDATA/LEVELS.TXT", &ctx.arena, &text, &size);
    if (r == BS_OK)
        r = bs_levels_parse(text, size, &ctx.arena, &table);
    if (r != BS_OK) {
        fprintf(stderr, "levels: LEVELS.TXT: %s\n", bs_result_str(r));
        goto done;
    }

    printf("%-4s %-10s %-4s %-10s %-4s %-9s %5s %6s %5s %5s %4s  %s\n",
           "id", "file", "mode", "sky", "haze", "nodes", "view", "ambient", "active", "grid", "monsters%", "name");
    for (size_t i = 0; i < table.count; i++) {
        const bs_level_info *l = &table.levels[i];
        printf("%-4s %-10s %-4s %-10s %-4s %-9s %5" PRId32 " %6" PRId32 " %5" PRId32 " %5" PRId32 " %4" PRId32 "  %s\n",
               l->id, l->file, l->mode, l->sky[0] ? l->sky : "-", l->haze,
               l->nodes[0] ? l->nodes : "-", l->view_distance, l->ambient, l->active_distance, l->grid_cell, l->monster_percent, l->name);
    }
    status = 0;
done:
    bstool_close(&ctx);
    return status;
}

int cmd_rsc(int argc, char **argv)
{
    bstool_ctx ctx;
    if (!bstool_open(&ctx))
        return 1;

    int status = 1;
    uint8_t *data;
    size_t size;
    bs_rsc rsc;
    bs_result r = bs_vfs_read(&ctx.vfs, "GAMEDATA/TEXT.RSC", &ctx.arena, &data, &size);
    if (r == BS_OK)
        r = bs_rsc_parse(data, size, &ctx.arena, &rsc);
    if (r != BS_OK) {
        fprintf(stderr, "rsc: TEXT.RSC: %s\n", bs_result_str(r));
        goto done;
    }

    status = 0;
    if (argc <= 1) {
        for (size_t i = 0; i < rsc.count; i++)
            printf("[%u]\n%s\n\n", rsc.records[i].id, bs_rsc_to_utf8(&ctx.arena, &rsc.records[i]));
    } else {
        for (int i = 1; i < argc; i++) {
            char *end;
            unsigned long id = strtoul(argv[i], &end, 10);
            const bs_rsc_record *rec = (*end == '\0' && id <= 0xFFFF) ? bs_rsc_find(&rsc, (uint16_t)id) : NULL;
            if (!rec) {
                fprintf(stderr, "rsc: no record '%s'\n", argv[i]);
                status = 1;
                continue;
            }
            printf("[%u]\n%s\n\n", rec->id, bs_rsc_to_utf8(&ctx.arena, rec));
        }
    }
done:
    bstool_close(&ctx);
    return status;
}

typedef struct cdls_ctx {
    bs_cdimage *cd;
    const char *dir;
} cdls_ctx;

static bool print_entry(void *user, const char *name, bool is_dir)
{
    cdls_ctx *c = user;
    if (is_dir) {
        printf("  %12s  %s/\n", "<dir>", name);
        return true;
    }
    char path[BS_PATH_MAX];
    uint64_t size = 0;
    if (bs_path_join(path, sizeof path, c->dir, name))
        bs_cdimage_stat(c->cd, path, NULL, &size);
    printf("  %12" PRIu64 "  %s\n", size, name);
    return true;
}

int cmd_cdls(int argc, char **argv)
{
    bstool_ctx ctx;
    if (!bstool_open(&ctx))
        return 1;
    int status = 1;
    if (!ctx.cd) {
        fprintf(stderr, "cdls: this install has no CD image\n");
        goto done;
    }
    const char *dir = argc >= 2 ? argv[1] : "";
    printf("%s:/%s\n", bs_cdimage_volume_id(ctx.cd), dir);
    cdls_ctx c = { ctx.cd, dir };
    bs_result r = bs_cdimage_list_dir(ctx.cd, dir, print_entry, &c);
    if (r != BS_OK) {
        fprintf(stderr, "cdls: %s: %s\n", dir, bs_result_str(r));
        goto done;
    }
    status = 0;
done:
    bstool_close(&ctx);
    return status;
}

/* bstool get: copy a file out of the install's VFS (installed files, then the CD image). */
int cmd_get(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: bstool get <path> [-o file]\n"
                        "  path inside the install or CD image, e.g. VIDEOS/STORY.SMK or GAMEDATA/PAL.RAW\n"
                        "  default output: the file's name in the current directory\n");
        return 2;
    }
    const char *src = argv[1];
    const char *out = NULL;
    for (int i = 2; i < argc; i++)
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            out = argv[++i];
    if (!out) {
        const char *slash = strrchr(src, '/');
        const char *bslash = strrchr(src, '\\');
        if (bslash && (!slash || bslash > slash))
            slash = bslash;
        out = slash ? slash + 1 : src;
    }
    bstool_ctx ctx;
    if (!bstool_open(&ctx))
        return 1;
    uint8_t *data;
    size_t size;
    int status = 1;
    bs_result r = bs_vfs_read(&ctx.vfs, src, &ctx.arena, &data, &size);
    if (r != BS_OK)
        fprintf(stderr, "get: %s: %s\n", src, bs_result_str(r));
    else if ((r = bs_fs_write_file(out, data, size)) != BS_OK)
        fprintf(stderr, "get: cannot write %s: %s\n", out, bs_result_str(r));
    else {
        printf("wrote %s (%zu bytes)\n", out, size);
        status = 0;
    }
    bstool_close(&ctx);
    return status;
}
