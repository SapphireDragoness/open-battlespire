/* bstool smk: decode Smacker videos, check them, export frames and audio. */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/core/fs.h"
#include "bs/core/png.h"
#include "bs/formats/smacker.h"
#include "commands.h"

static void put_le(uint8_t *p, uint32_t v, int n)
{
    for (int i = 0; i < n; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

/* Writes a PCM WAV file. */
static bs_result write_wav(const char *path, const uint8_t *pcm, size_t bytes, uint32_t rate, int channels, int bits)
{
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    put_le(h + 4, (uint32_t)(36 + bytes), 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    put_le(h + 16, 16, 4);
    put_le(h + 20, 1, 2);
    put_le(h + 22, (uint32_t)channels, 2);
    put_le(h + 24, rate, 4);
    put_le(h + 28, rate * (uint32_t)channels * (uint32_t)bits / 8, 4);
    put_le(h + 32, (uint32_t)(channels * bits / 8), 2);
    put_le(h + 34, (uint32_t)bits, 2);
    memcpy(h + 36, "data", 4);
    put_le(h + 40, (uint32_t)bytes, 4);
    FILE *f = fopen(path, "wb");
    if (!f)
        return BS_ERR_IO;
    bool ok = fwrite(h, 1, sizeof h, f) == sizeof h && (bytes == 0 || fwrite(pcm, 1, bytes, f) == bytes);
    if (fclose(f) != 0)
        ok = false;
    return ok ? BS_OK : BS_ERR_IO;
}

static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/*
 * Writes the first n frames as a standalone video: the 104-byte header with the
 * frame count patched and the ring frame dropped, the first n frame sizes and
 * types, the trees, then the frames. Used for small test and fuzz inputs.
 */
static bs_result write_cut(const char *path, const uint8_t *data, size_t size, uint32_t n)
{
    if (size < 104)
        return BS_ERR_FORMAT;
    uint32_t frames = get_le32(data + 12), flags = get_le32(data + 20), trees = get_le32(data + 52);
    uint32_t stored = frames + (flags & 1);
    if (n > frames)
        n = frames;
    size_t tables = 104 + (size_t)stored * 5;
    if (tables > size || trees > size - tables)
        return BS_ERR_FORMAT;
    size_t body = 0;
    for (uint32_t i = 0; i < n; i++)
        body += get_le32(data + 104 + (size_t)i * 4) & ~3u;
    size_t data_start = tables + trees;
    if (body > size - data_start)
        return BS_ERR_FORMAT;
    size_t out_size = 104 + (size_t)n * 5 + trees + body;
    uint8_t *out = malloc(out_size);
    if (!out)
        return BS_ERR_NOMEM;
    uint8_t *p = out;
    memcpy(p, data, 104);
    put_le(p + 12, n, 4);
    put_le(p + 20, flags & ~1u, 4);
    p += 104;
    memcpy(p, data + 104, (size_t)n * 4);
    p += (size_t)n * 4;
    memcpy(p, data + 104 + (size_t)stored * 4, n);
    p += n;
    memcpy(p, data + tables, trees + body);
    bs_result r = bs_fs_write_file(path, out, out_size);
    free(out);
    return r;
}

static bool process(const char *name, const uint8_t *data, size_t size, const char *out_dir, uint32_t every,
                    uint32_t cut)
{
    bs_arena arena;
    bs_arena_init(&arena, 0);
    bs_smacker s;
    bs_result r = bs_smk_open(data, size, &arena, &s);
    if (r != BS_OK) {
        fprintf(stderr, "smk: %s: %s\n", name, bs_result_str(r));
        bs_arena_free(&arena);
        return false;
    }
    char stem[64];
    snprintf(stem, sizeof stem, "%s", name);
    char *dot = strrchr(stem, '.');
    if (dot)
        *dot = '\0';
    if (out_dir && cut) {
        char file[96], path[BS_PATH_MAX];
        snprintf(file, sizeof file, "%s_cut%" PRIu32 ".smk", stem, cut);
        if (!bs_path_join(path, sizeof path, out_dir, file) || (r = write_cut(path, data, size, cut)) != BS_OK) {
            fprintf(stderr, "smk: %s: cannot write %s: %s\n", name, file, bs_result_str(r));
            bs_smk_close(&s);
            bs_arena_free(&arena);
            return false;
        }
        printf("wrote %s\n", path);
    }
    printf("%-12s %ux%u%s  %u frames at %.2f ms (%.2f fps)%s", name, s.width, s.height,
           s.y_mode == BS_SMK_Y_DOUBLED ? " (y-doubled)" : s.y_mode == BS_SMK_Y_INTERLACED ? " (y-interlaced)" : "",
           s.frames, s.frame_ms, 1000.0 / s.frame_ms, (s.flags & 1) ? " +ring" : "");
    for (int t = 0; t < BS_SMK_TRACKS; t++)
        if (s.tracks[t].present)
            printf("  track %d: %u Hz %s %s%s", t, s.tracks[t].rate, s.tracks[t].bits16 ? "16-bit" : "8-bit",
                   s.tracks[t].stereo ? "stereo" : "mono", s.tracks[t].compressed ? " compressed" : "");
    putchar('\n');

    /* Audio of every track is collected for export and for the totals. */
    uint8_t *audio[BS_SMK_TRACKS] = {0};
    size_t audio_len[BS_SMK_TRACKS] = {0}, audio_cap[BS_SMK_TRACKS] = {0};
    bool ok = true;
    uint32_t decoded = 0;
    while ((r = bs_smk_next_frame(&s)) == BS_OK) {
        uint32_t f = s.next - 1;
        for (int t = 0; t < BS_SMK_TRACKS; t++) {
            if (!s.audio_bytes[t])
                continue;
            if (audio_len[t] + s.audio_bytes[t] > audio_cap[t]) {
                size_t cap = (audio_len[t] + s.audio_bytes[t]) * 2;
                uint8_t *p = realloc(audio[t], cap);
                if (!p) {
                    ok = false;
                    break;
                }
                audio[t] = p;
                audio_cap[t] = cap;
            }
            memcpy(audio[t] + audio_len[t], s.audio[t], s.audio_bytes[t]);
            audio_len[t] += s.audio_bytes[t];
        }
        if (out_dir && every && f % every == 0) {
            char file[96], path[BS_PATH_MAX];
            snprintf(file, sizeof file, "%s_%04" PRIu32 ".png", stem, f);
            /* Exported at display height: doubled or interlaced lines expanded. */
            uint32_t dh = s.y_mode == BS_SMK_Y_NORMAL ? s.height : s.height * 2;
            uint8_t *img = malloc((size_t)s.width * dh);
            if (!img) {
                ok = false;
            } else {
                for (uint32_t y = 0; y < dh; y++) {
                    uint8_t *row = img + (size_t)y * s.width;
                    if (s.y_mode == BS_SMK_Y_NORMAL)
                        memcpy(row, s.pixels + (size_t)y * s.width, s.width);
                    else if (s.y_mode == BS_SMK_Y_DOUBLED || y % 2 == 0)
                        memcpy(row, s.pixels + (size_t)(y / 2) * s.width, s.width);
                    else
                        memset(row, 0, s.width);
                }
                if (!bs_path_join(path, sizeof path, out_dir, file) ||
                    bs_png_write_indexed(path, s.width, dh, img, s.palette, -1) != BS_OK)
                    ok = false;
                free(img);
            }
        }
        decoded++;
    }
    if (r != BS_ERR_NOT_FOUND) {
        fprintf(stderr, "smk: %s: frame %u: %s\n", name, s.next, bs_result_str(r));
        ok = false;
    }
    for (int t = 0; t < BS_SMK_TRACKS; t++) {
        if (!audio_len[t])
            continue;
        const bs_smk_track *tr = &s.tracks[t];
        int ch = tr->stereo ? 2 : 1, bits = tr->bits16 ? 16 : 8;
        double secs = (double)audio_len[t] / (tr->rate * (double)ch * bits / 8);
        printf("    track %d: %zu bytes = %.2f s of audio (video %.2f s)\n", t, audio_len[t], secs,
               s.frames * s.frame_ms / 1000.0);
        if (out_dir) {
            char file[96], path[BS_PATH_MAX];
            snprintf(file, sizeof file, "%s_track%d.wav", stem, t);
            if (!bs_path_join(path, sizeof path, out_dir, file) ||
                write_wav(path, audio[t], audio_len[t], tr->rate, ch, bits) != BS_OK)
                ok = false;
        }
        free(audio[t]);
    }
    printf("    decoded %u/%u frames%s\n", decoded, s.frames, ok ? "" : "  FAILED");
    bs_smk_close(&s);
    bs_arena_free(&arena);
    return ok;
}

int cmd_smk(int argc, char **argv)
{
    const char *out_dir = NULL;
    uint32_t every = 0, cut = 0;
    static const char *const all[] = {"STORY.SMK", "MALE.SMK", "FEMALE.SMK", "ANCHORS.SMK", "CHOP2.SMK", "JUMP.SMK"};
    size_t n_all = sizeof all / sizeof all[0];
    char **names = calloc((size_t)argc > n_all ? (size_t)argc : n_all, sizeof *names);
    int count = 0;
    if (!names)
        return 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            out_dir = argv[++i];
        else if (strcmp(argv[i], "--every") == 0 && i + 1 < argc)
            every = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--cut") == 0 && i + 1 < argc)
            cut = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (argv[i][0] == '-') {
            fprintf(stderr, "usage: bstool smk [-o dir [--every N] [--cut N]] [video...]\n"
                            "  videos: names in the CD's VIDEOS folder or host paths (default: all)\n"
                            "  -o writes each track's audio as WAV and, with --every N, every Nth frame as PNG\n"
                            "  --cut N also writes the first N frames as NAME_cutN.smk\n");
            free(names);
            return 2;
        } else
            names[count++] = argv[i];
    }
    if (!count) {
        for (size_t i = 0; i < n_all; i++)
            names[count++] = (char *)all[i];
    }
    if (out_dir && bs_fs_mkdirs(out_dir) != BS_OK) {
        fprintf(stderr, "smk: cannot create %s\n", out_dir);
        free(names);
        return 1;
    }
    bstool_ctx ctx;
    bool opened = false;
    int status = 0;
    for (int i = 0; i < count; i++) {
        bs_arena tmp;
        bs_arena_init(&tmp, 0);
        uint8_t *data = NULL;
        size_t size = 0;
        bs_result r;
        const char *base = strrchr(names[i], '/');
        base = base ? base + 1 : names[i];
        if (bs_fs_is_file(names[i])) {
            r = bs_fs_read_file(names[i], &tmp, &data, &size);
        } else {
            if (!opened && !(opened = bstool_open(&ctx))) {
                bs_arena_free(&tmp);
                status = 1;
                break;
            }
            char path[BS_PATH_MAX];
            snprintf(path, sizeof path, "VIDEOS/%s", names[i]);
            r = bs_vfs_read(&ctx.vfs, path, &tmp, &data, &size);
        }
        if (r != BS_OK) {
            fprintf(stderr, "smk: %s: %s\n", names[i], bs_result_str(r));
            status = 1;
        } else if (!process(base, data, size, out_dir, every, cut)) {
            status = 1;
        }
        bs_arena_free(&tmp);
    }
    if (opened)
        bstool_close(&ctx);
    free(names);
    return status;
}
