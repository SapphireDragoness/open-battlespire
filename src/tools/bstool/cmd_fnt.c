/* bstool fnt: inspect game fonts and export glyph sheets as PNG. */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "bs/core/png.h"
#include "bs/formats/font.h"
#include "bs/formats/palette.h"
#include "commands.h"

#define SHEET_COLUMNS 16

/*
 * Draws all glyphs on a grid (16 per row, in character order starting at 33).
 * Colours are collected into an indexed palette; index 0 is the transparent
 * background.
 */
static bool export_sheet(const bs_font *font, const char *path)
{
    uint32_t cell_w = font->max_width + 2u, cell_h = font->max_height + 2u;
    uint32_t rows = (font->count + SHEET_COLUMNS - 1) / SHEET_COLUMNS;
    uint32_t w = cell_w * SHEET_COLUMNS, h = cell_h * rows;
    static uint8_t pixels[1 << 22];
    if ((size_t)w * h > sizeof pixels)
        return false;
    memset(pixels, 0, (size_t)w * h);

    uint16_t colors[256];
    uint8_t palette[768] = { 0 };
    int ncolors = 1; /* 0 = background */
    for (uint32_t i = 0; i < font->count; i++) {
        const bs_font_glyph *g = &font->glyphs[i];
        if (!g->pixels)
            continue;
        uint32_t ox = (i % SHEET_COLUMNS) * cell_w + 1, oy = (i / SHEET_COLUMNS) * cell_h + 1;
        for (uint32_t y = 0; y < g->height; y++) {
            for (uint32_t x = 0; x < g->width; x++) {
                uint16_t c = bs_font_pixel(g, x, y);
                if (c == 0)
                    continue;
                int k = 1;
                while (k < ncolors && colors[k] != c)
                    k++;
                if (k == ncolors) {
                    if (ncolors == 256)
                        return false;
                    colors[ncolors] = c;
                    bs_rgb555_to_rgb(c, palette + ncolors * 3);
                    ncolors++;
                }
                pixels[(size_t)(oy + y) * w + ox + x] = (uint8_t)k;
            }
        }
    }
    return bs_png_write_indexed(path, w, h, pixels, palette, 0) == BS_OK;
}

int cmd_fnt(int argc, char **argv)
{
    const char *out_dir = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            out_dir = argv[++i];
        else {
            fprintf(stderr, "usage: bstool fnt [-o dir]\n"
                            "  prints GAMEDATA/FONT0001-0010.FNT; with -o, exports glyph sheets\n");
            return 2;
        }
    }
    bstool_ctx ctx;
    if (!bstool_open(&ctx))
        return 1;
    int status = 0;
    if (out_dir && bs_fs_mkdirs(out_dir) != BS_OK) {
        fprintf(stderr, "fnt: cannot create '%s'\n", out_dir);
        status = 1;
        goto done;
    }
    for (int n = 1; n <= 10; n++) {
        char name[32];
        snprintf(name, sizeof name, "GAMEDATA/FONT%04d.FNT", n);
        uint8_t *data;
        size_t size;
        bs_font font;
        bs_arena_mark m = bs_arena_get_mark(&ctx.arena);
        bs_result r = bs_vfs_read(&ctx.vfs, name, &ctx.arena, &data, &size);
        if (r == BS_OK)
            r = bs_font_parse(data, size, &ctx.arena, &font);
        if (r != BS_OK) {
            fprintf(stderr, "fnt: %s: %s\n", name, bs_result_str(r));
            status = 1;
            continue;
        }
        uint32_t present = 0;
        for (uint32_t i = 0; i < font.count; i++)
            present += font.glyphs[i].pixels != NULL;
        printf("%s  %" PRIu32 " glyphs (%" PRIu32 " present), max %ux%u\n", name + 9, font.count, present,
               font.max_width, font.max_height);
        if (out_dir) {
            char file[32], path[BS_PATH_MAX];
            snprintf(file, sizeof file, "FONT%04d.png", n);
            if (!bs_path_join(path, sizeof path, out_dir, file) || !export_sheet(&font, path)) {
                fprintf(stderr, "fnt: cannot export %s\n", file);
                status = 1;
            }
        }
        bs_arena_rewind(&ctx.arena, m);
    }
done:
    bstool_close(&ctx);
    return status;
}
