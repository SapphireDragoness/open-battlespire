#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "commands.h"

static const char *g_data_dir;

void bstool_set_data_dir(const char *dir)
{
    g_data_dir = dir;
}

const char *bstool_data_dir(void)
{
    if (g_data_dir)
        return g_data_dir;
    const char *env = getenv("BS_DATA_DIR");
    return env && *env ? env : NULL;
}

bool bstool_open(bstool_ctx *ctx)
{
    memset(ctx, 0, sizeof *ctx);
    bs_arena_init(&ctx->arena, 0);
    const char *dir = bstool_data_dir();
    if (!dir) {
        fprintf(stderr, "bstool: no install directory; use -d <dir> or set BS_DATA_DIR\n");
        return false;
    }
    bs_result r = bs_install_detect(dir, &ctx->inst);
    if (r != BS_OK) {
        fprintf(stderr, "bstool: '%s' is not a Battlespire installation (%s)\n", dir, bs_result_str(r));
        return false;
    }
    bs_vfs_init(&ctx->vfs);
    r = bs_install_mount(&ctx->inst, &ctx->vfs, &ctx->cd);
    if (r != BS_OK) {
        fprintf(stderr, "bstool: cannot mount '%s' (%s)\n", dir, bs_result_str(r));
        bstool_close(ctx);
        return false;
    }
    return true;
}

void bstool_close(bstool_ctx *ctx)
{
    bs_cdimage_close(ctx->cd);
    ctx->cd = NULL;
    bs_arena_free(&ctx->arena);
}

bs_bsa *bstool_open_archive(bstool_ctx *ctx, bool *ctx_opened, const char *name)
{
    bs_stream *s = NULL;
    bs_result r;
    if (bs_fs_is_file(name)) {
        r = bs_stream_open_file(name, &s);
    } else {
        if (!*ctx_opened) {
            if (!bstool_open(ctx))
                return NULL;
            *ctx_opened = true;
        }
        r = bs_vfs_open(&ctx->vfs, name, &s);
    }
    if (r != BS_OK) {
        fprintf(stderr, "bstool: cannot open '%s' (%s)\n", name, bs_result_str(r));
        return NULL;
    }
    bs_bsa *bsa;
    r = bs_bsa_open(s, &bsa);
    if (r != BS_OK) {
        fprintf(stderr, "bstool: '%s' is not a valid BSA archive (%s)\n", name, bs_result_str(r));
        return NULL;
    }
    return bsa;
}

void bstool_record_label(const bs_bsa_record *rec, char *out, size_t cap)
{
    if (rec->name[0])
        snprintf(out, cap, "%s", rec->name);
    else
        snprintf(out, cap, "%u", rec->id);
}
