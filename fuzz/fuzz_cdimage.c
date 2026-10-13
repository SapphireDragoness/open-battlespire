/*
 * Fuzzes the CD image reader: sector format detection, the volume descriptor,
 * directory walking, path lookup and file reads; plus cue sheet parsing.
 */
#include <stdlib.h>
#include <string.h>

#include "bs/core/cdimage.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

typedef struct visit {
    bs_cdimage *cd;
    const char *dir;
    int depth;
    int budget;
} visit;

static bool on_entry(void *user, const char *name, bool is_dir)
{
    visit *v = user;
    if (--v->budget <= 0)
        return false;
    char path[BS_PATH_MAX];
    if (!bs_path_join(path, sizeof path, v->dir, name))
        return true;
    if (is_dir) {
        if (v->depth < 3) {
            visit child = { v->cd, path, v->depth + 1, v->budget };
            bs_cdimage_list_dir(v->cd, path, on_entry, &child);
            v->budget = child.budget;
        }
        return true;
    }
    bs_stream *s;
    if (bs_cdimage_open_file(v->cd, path, &s) == BS_OK) {
        uint8_t buf[4096];
        size_t n = s->size < sizeof buf ? (size_t)s->size : sizeof buf;
        bs_stream_read_at(s, 0, buf, n);
        if (s->size > sizeof buf)
            bs_stream_read_at(s, s->size - 16, buf, 16);
        bs_stream_close(s);
    }
    return true;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* Cue parsing on the input as text. */
    char *text = malloc(size + 1);
    if (!text)
        return 0;
    memcpy(text, data, size);
    text[size] = '\0';
    char file[64];
    bs_cue_data_file(text, file, sizeof file);
    free(text);

    bs_stream *s;
    if (bs_stream_open_memory(data, size, &s) != BS_OK)
        return 0;
    bs_cdimage *cd;
    if (bs_cdimage_open_stream(s, &cd) != BS_OK)
        return 0;
    visit v = { cd, "", 0, 64 };
    bs_cdimage_list_dir(cd, "", on_entry, &v);
    bs_cdimage_stat(cd, "SUBDIR/BIG.BIN", NULL, NULL);
    bs_cdimage_close(cd);
    return 0;
}
