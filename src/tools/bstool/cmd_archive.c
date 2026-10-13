/* bstool ls / extract / cat / verify: BSA archive commands. */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/core/text.h"
#include "bs/formats/bsa.h"
#include "commands.h"

/* Archives checked by `verify`, as VFS names. */
static const char *const known_archives[] = {
    "GAMEDATA/3D.BSA",
    "GAMEDATA/3D.BS6",
    "GAMEDATA/BSI.BSA",
    "GAMEDATA/BS6.BSA",
    "GAMEDATA/TXT.BSA",
    "GAMEDATA/FLC.BSA",
    "GAMEDATA/SPIRE.SND",
    "WAVES.BSA",
};

/* Finds a record by name, or by numeric id in numbered archives. */
static const bs_bsa_record *find_record(const bs_bsa *bsa, const char *key)
{
    if (bsa->type == BS_BSA_TYPE_NUMBERED) {
        char *end;
        unsigned long id = strtoul(key, &end, 10);
        return (*end == '\0' && id <= 0xFFFF) ? bs_bsa_find_id(bsa, (uint16_t)id) : NULL;
    }
    return bs_bsa_find(bsa, key);
}

/* Host file name for a record: unsafe characters replaced, extension added for numbered records. */
static void output_name(const bs_bsa_record *rec, const uint8_t *data, size_t size, char *out, size_t cap)
{
    char label[32];
    bstool_record_label(rec, label, sizeof label);
    for (char *p = label; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ':' || (unsigned char)*p < 0x20)
            *p = '_';
    }
    if (label[0] == '.')
        label[0] = '_';
    const char *ext = "";
    if (!rec->name[0])
        ext = (size >= 4 && memcmp(data, "RIFF", 4) == 0) ? ".wav" : ".bin";
    snprintf(out, cap, "%s%s", label, ext);
}

static const char *base_name(const char *path)
{
    const char *b = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\')
            b = p + 1;
    }
    return b;
}

int cmd_ls(int argc, char **argv)
{
    bool unpacked = false;
    const char *archive = NULL;
    int positional = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-u") == 0)
            unpacked = true;
        else if (positional++ == 0)
            archive = argv[i];
    }
    if (!archive || positional != 1) {
        fprintf(stderr, "usage: bstool ls [-u] <archive>\n  -u  also show decompressed sizes\n");
        return 2;
    }

    bstool_ctx ctx = { 0 };
    bool opened = false;
    bs_bsa *bsa = bstool_open_archive(&ctx, &opened, archive);
    if (!bsa) {
        if (opened)
            bstool_close(&ctx);
        return 1;
    }

    printf("%s: %" PRIu32 " records, %s\n", archive, bsa->count,
           bsa->type == BS_BSA_TYPE_NAMED ? "named" : "numbered");
    bs_arena tmp;
    bs_arena_init(&tmp, 0);
    int status = 0;
    for (uint32_t i = 0; i < bsa->count; i++) {
        const bs_bsa_record *rec = &bsa->records[i];
        char label[32];
        bstool_record_label(rec, label, sizeof label);
        printf("%5" PRIu32 "  %-12s  %-4s  %10" PRIu32, i, label,
               bs_bsa_record_compressed(rec) ? "lzss" : "", rec->size);
        if (unpacked) {
            uint8_t *data;
            size_t size;
            bs_result r = bs_bsa_read(bsa, rec, &tmp, &data, &size);
            if (r == BS_OK) {
                printf("  %10zu", size);
            } else {
                printf("  ERROR: %s", bs_result_str(r));
                status = 1;
            }
            bs_arena_reset(&tmp);
        }
        if (rec->flags & ~BS_BSA_FLAG_COMPRESSED)
            printf("  flags=0x%04x", rec->flags);
        putchar('\n');
    }
    bs_arena_free(&tmp);
    bs_bsa_close(bsa);
    if (opened)
        bstool_close(&ctx);
    return status;
}

int cmd_extract(int argc, char **argv)
{
    const char *archive = NULL, *out_dir = NULL;
    bool raw = false;
    char **wanted = calloc((size_t)argc, sizeof *wanted);
    int num_wanted = 0;
    if (!wanted)
        return 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            out_dir = argv[++i];
        else if (strcmp(argv[i], "--raw") == 0)
            raw = true;
        else if (!archive)
            archive = argv[i];
        else
            wanted[num_wanted++] = argv[i];
    }
    if (!archive) {
        fprintf(stderr, "usage: bstool extract <archive> [-o dir] [--raw] [record...]\n"
                        "  -o dir  output directory (default: extracted/<archive name>)\n"
                        "  --raw   write compressed records as stored\n");
        free(wanted);
        return 2;
    }

    char default_dir[BS_PATH_MAX];
    if (!out_dir) {
        snprintf(default_dir, sizeof default_dir, "extracted/%s", base_name(archive));
        out_dir = default_dir;
    }

    bstool_ctx ctx = { 0 };
    bool opened = false;
    bs_bsa *bsa = bstool_open_archive(&ctx, &opened, archive);
    int status = 0;
    if (!bsa) {
        status = 1;
        goto done;
    }
    if (bs_fs_mkdirs(out_dir) != BS_OK) {
        fprintf(stderr, "extract: cannot create '%s'\n", out_dir);
        status = 1;
        goto done;
    }

    /* Check requested names up front so typos fail before writing anything. */
    for (int w = 0; w < num_wanted; w++) {
        if (!find_record(bsa, wanted[w])) {
            fprintf(stderr, "extract: no record '%s' in %s\n", wanted[w], archive);
            status = 1;
            goto done;
        }
    }

    bs_arena tmp;
    bs_arena_init(&tmp, 0);
    uint32_t written = 0;
    for (uint32_t i = 0; i < bsa->count; i++) {
        const bs_bsa_record *rec = &bsa->records[i];
        if (num_wanted) {
            bool match = false;
            for (int w = 0; w < num_wanted && !match; w++)
                match = find_record(bsa, wanted[w]) == rec;
            if (!match)
                continue;
        }
        uint8_t *data;
        size_t size;
        bs_result r = raw ? bs_bsa_read_raw(bsa, rec, &tmp, &data, &size)
                          : bs_bsa_read(bsa, rec, &tmp, &data, &size);
        char name[64], path[BS_PATH_MAX];
        output_name(rec, data, r == BS_OK ? size : 0, name, sizeof name);
        if (r == BS_OK && !bs_path_join(path, sizeof path, out_dir, name))
            r = BS_ERR_ARGS;
        if (r == BS_OK)
            r = bs_fs_write_file(path, data, size);
        if (r != BS_OK) {
            fprintf(stderr, "extract: %s: %s\n", name, bs_result_str(r));
            status = 1;
        } else {
            written++;
        }
        bs_arena_reset(&tmp);
    }
    bs_arena_free(&tmp);
    printf("Extracted %" PRIu32 " record(s) to %s\n", written, out_dir);

done:
    bs_bsa_close(bsa);
    if (opened)
        bstool_close(&ctx);
    free(wanted);
    return status;
}

int cmd_cat(int argc, char **argv)
{
    const char *archive = NULL, *record = NULL;
    bool text = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--text") == 0)
            text = true;
        else if (!archive)
            archive = argv[i];
        else if (!record)
            record = argv[i];
    }
    if (!archive || !record) {
        fprintf(stderr, "usage: bstool cat <archive> <record> [--text]\n"
                        "  --text  convert from Windows-1252 to UTF-8\n");
        return 2;
    }

    bstool_ctx ctx = { 0 };
    bool opened = false;
    bs_bsa *bsa = bstool_open_archive(&ctx, &opened, archive);
    int status = 1;
    if (!bsa)
        goto done;

    const bs_bsa_record *rec = find_record(bsa, record);
    if (!rec) {
        fprintf(stderr, "cat: no record '%s' in %s\n", record, archive);
        goto done;
    }
    bs_arena tmp;
    bs_arena_init(&tmp, 0);
    uint8_t *data;
    size_t size;
    bs_result r = bs_bsa_read(bsa, rec, &tmp, &data, &size);
    if (r == BS_OK) {
        if (text) {
            char *utf8 = bs_cp1252_to_utf8(&tmp, data, size);
            if (utf8)
                fputs(utf8, stdout);
        } else {
            fwrite(data, 1, size, stdout);
        }
        status = 0;
    } else {
        fprintf(stderr, "cat: %s: %s\n", record, bs_result_str(r));
    }
    bs_arena_free(&tmp);

done:
    bs_bsa_close(bsa);
    if (opened)
        bstool_close(&ctx);
    return status;
}

/*
 * Compares records against loose copies in GAMEDATA/ with the same name.
 * The original game shipped ~200 .3d files both loose and archived; identical
 * bytes confirm the decompressor end to end.
 */
static void compare_loose(bstool_ctx *ctx, const char *archive, const bs_bsa_record *rec,
                          const uint8_t *data, size_t size, uint32_t *same, uint32_t *differ)
{
    if (!rec->name[0] || bs_strcasecmp(rec->name, base_name(archive)) == 0)
        return;
    char rel[64];
    snprintf(rel, sizeof rel, "GAMEDATA/%s", rec->name);
    bs_arena_mark m = bs_arena_get_mark(&ctx->arena);
    uint8_t *loose;
    size_t loose_size;
    if (bs_vfs_read(&ctx->vfs, rel, &ctx->arena, &loose, &loose_size) == BS_OK) {
        if (loose_size == size && memcmp(loose, data, size) == 0)
            (*same)++;
        else
            (*differ)++;
    }
    bs_arena_rewind(&ctx->arena, m);
}

int cmd_verify(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    bstool_ctx ctx;
    if (!bstool_open(&ctx))
        return 1;

    int status = 0;
    bs_arena tmp;
    bs_arena_init(&tmp, 0);
    printf("%-20s %8s %8s %12s %12s  %s\n", "archive", "records", "lzss", "stored", "unpacked", "loose twins");
    for (size_t a = 0; a < sizeof known_archives / sizeof known_archives[0]; a++) {
        const char *name = known_archives[a];
        bs_bsa *bsa;
        bs_result r = bs_bsa_open_vfs(&ctx.vfs, name, &bsa);
        if (r != BS_OK) {
            printf("%-20s FAILED to open: %s\n", name, bs_result_str(r));
            status = 1;
            continue;
        }
        uint32_t compressed = 0, errors = 0, same = 0, differ = 0;
        uint64_t stored = 0, unpacked = 0;
        for (uint32_t i = 0; i < bsa->count; i++) {
            const bs_bsa_record *rec = &bsa->records[i];
            uint8_t *data;
            size_t size;
            r = bs_bsa_read(bsa, rec, &tmp, &data, &size);
            if (r != BS_OK || (bs_bsa_record_compressed(rec) && size == 0 && rec->size > 0)) {
                char label[32];
                bstool_record_label(rec, label, sizeof label);
                fprintf(stderr, "verify: %s/%s: %s\n", name, label, bs_result_str(r));
                errors++;
            } else {
                compressed += bs_bsa_record_compressed(rec);
                stored += rec->size;
                unpacked += size;
                compare_loose(&ctx, name, rec, data, size, &same, &differ);
            }
            bs_arena_reset(&tmp);
        }
        printf("%-20s %8" PRIu32 " %8" PRIu32 " %12" PRIu64 " %12" PRIu64 "  ",
               name, bsa->count, compressed, stored, unpacked);
        if (same || differ)
            printf("%" PRIu32 " identical, %" PRIu32 " differ", same, differ);
        else
            printf("-");
        if (errors) {
            printf("  %" PRIu32 " ERRORS", errors);
            status = 1;
        }
        putchar('\n');
        bs_bsa_close(bsa);
    }
    bs_arena_free(&tmp);
    bstool_close(&ctx);
    printf(status ? "\nVerification FAILED.\n" : "\nAll records read successfully.\n");
    return status;
}
