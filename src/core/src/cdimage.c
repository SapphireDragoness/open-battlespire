#include "bs/core/cdimage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/core/reader.h"

#define ISO_SECTOR 2048u
#define PVD_SECTOR 16u

struct bs_cdimage {
    bs_stream *data;        /* host file holding the data track */
    uint32_t sector_size;   /* 2048 or 2352 */
    uint32_t data_offset;   /* user data offset within a sector: 0, 16 or 24 */
    uint64_t sector_count;
    uint32_t root_lba;
    uint32_t root_size;
    char volume_id[33];
};

/* An ISO 9660 directory entry, as found by lookup. */
typedef struct iso_entry {
    uint32_t lba;
    uint32_t size;
    bool is_dir;
} iso_entry;

/* Reads n bytes starting `offset` bytes into the extent beginning at `lba`. */
static bs_result cd_read(bs_cdimage *cd, uint32_t lba, uint64_t offset, void *dst, size_t n)
{
    uint8_t *out = dst;
    while (n > 0) {
        uint64_t sector = lba + offset / ISO_SECTOR;
        uint32_t within = (uint32_t)(offset % ISO_SECTOR);
        size_t chunk = ISO_SECTOR - within;
        if (chunk > n)
            chunk = n;
        if (sector >= cd->sector_count)
            return BS_ERR_FORMAT;
        uint64_t phys = sector * cd->sector_size + cd->data_offset + within;
        bs_result r = bs_stream_read_at(cd->data, phys, out, chunk);
        if (r != BS_OK)
            return r;
        out += chunk;
        offset += chunk;
        n -= chunk;
    }
    return BS_OK;
}

/* ---- cue sheet ---- */

/* Copies the next whitespace-separated or "quoted" token from *p into tok. */
static bool next_token(const char **p, char *tok, size_t cap)
{
    const char *s = *p;
    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '\0' || *s == '\r' || *s == '\n')
        return false;
    size_t n = 0;
    if (*s == '"') {
        s++;
        while (*s && *s != '"' && *s != '\r' && *s != '\n') {
            if (n + 1 < cap)
                tok[n++] = *s;
            s++;
        }
        if (*s == '"')
            s++;
    } else {
        while (*s && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n') {
            if (n + 1 < cap)
                tok[n++] = *s;
            s++;
        }
    }
    tok[n] = '\0';
    *p = s;
    return true;
}

bool bs_cue_data_file(const char *text, char *out, size_t cap)
{
    char current_file[BS_PATH_MAX] = "";
    bool saw_file = false;
    const char *line = text;
    while (*line) {
        const char *p = line;
        char cmd[16], arg1[BS_PATH_MAX], arg2[32];
        if (next_token(&p, cmd, sizeof cmd)) {
            if (bs_strcasecmp(cmd, "FILE") == 0 && next_token(&p, arg1, sizeof arg1)) {
                snprintf(current_file, sizeof current_file, "%s", arg1);
                saw_file = true;
            } else if (bs_strcasecmp(cmd, "TRACK") == 0 && saw_file &&
                       next_token(&p, arg1, sizeof arg1) && next_token(&p, arg2, sizeof arg2) &&
                       strncmp(arg2, "MODE", 4) == 0) {
                snprintf(out, cap, "%s", current_file);
                return true;
            }
        }
        const char *nl = strchr(line, '\n');
        if (!nl)
            break;
        line = nl + 1;
    }
    return false;
}

/* Heuristic: a cue sheet is small text whose first token is FILE, REM, CATALOG, ... */
static bool looks_like_cue(const uint8_t *head, size_t n)
{
    static const char *const keywords[] = { "FILE", "REM", "CATALOG", "CDTEXTFILE", "TITLE", "PERFORMER" };
    const char *p = (const char *)head;
    char tok[16];
    for (size_t i = 0; i < n; i++) {
        if (head[i] == 0)
            return false;
    }
    if (!next_token(&p, tok, sizeof tok))
        return false;
    for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; i++) {
        if (bs_strcasecmp(tok, keywords[i]) == 0)
            return true;
    }
    return false;
}

/* ---- sector format and volume descriptor ---- */

static const uint8_t cd_sync[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };

static bs_result detect_format(bs_cdimage *cd)
{
    uint8_t head[16];
    if (cd->data->size < 16 || bs_stream_read_at(cd->data, 0, head, sizeof head) != BS_OK)
        return BS_ERR_FORMAT;

    if (memcmp(head, cd_sync, sizeof cd_sync) == 0) {
        cd->sector_size = 2352;
        if (head[15] == 1)
            cd->data_offset = 16;
        else if (head[15] == 2)
            cd->data_offset = 24; /* Mode 2 Form 1: 8-byte subheader */
        else
            return BS_ERR_UNSUPPORTED;
    } else {
        cd->sector_size = ISO_SECTOR;
        cd->data_offset = 0;
    }
    cd->sector_count = cd->data->size / cd->sector_size;
    return BS_OK;
}

static bs_result read_pvd(bs_cdimage *cd)
{
    uint8_t pvd[ISO_SECTOR];
    bs_result r = cd_read(cd, PVD_SECTOR, 0, pvd, sizeof pvd);
    if (r != BS_OK)
        return r;
    if (pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0)
        return BS_ERR_FORMAT;

    bs_reader rd = bs_reader_make(pvd, sizeof pvd);
    bs_reader_seek(&rd, 40);
    bs_read_fixed_str(&rd, 32, cd->volume_id, sizeof cd->volume_id);
    for (size_t n = strlen(cd->volume_id); n > 0 && cd->volume_id[n - 1] == ' '; n--)
        cd->volume_id[n - 1] = '\0';

    /* Root directory record at offset 156. */
    bs_reader_seek(&rd, 156 + 2);
    cd->root_lba = bs_read_u32le(&rd);
    bs_reader_skip(&rd, 4);
    cd->root_size = bs_read_u32le(&rd);
    if (!bs_reader_ok(&rd) || cd->root_size == 0)
        return BS_ERR_FORMAT;
    return BS_OK;
}

static void dir_name(const char *path, char *out, size_t cap)
{
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash))
        slash = bslash;
    if (!slash) {
        snprintf(out, cap, ".");
        return;
    }
    size_t len = (size_t)(slash - path);
    if (len >= cap)
        len = cap - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

bs_result bs_cdimage_open(const char *path, bs_cdimage **out)
{
    *out = NULL;
    bs_stream *s;
    bs_result r = bs_stream_open_file(path, &s);
    if (r != BS_OK)
        return r;

    /* A cue sheet points at the actual data file. */
    uint8_t head[4096];
    size_t head_len = s->size < sizeof head - 1 ? (size_t)s->size : sizeof head - 1;
    if (bs_stream_read_at(s, 0, head, head_len) == BS_OK && looks_like_cue(head, head_len)) {
        head[head_len] = 0;
        char data_name[BS_PATH_MAX], dir[BS_PATH_MAX], data_path[BS_PATH_MAX];
        bs_stream_close(s);
        if (!bs_cue_data_file((const char *)head, data_name, sizeof data_name))
            return BS_ERR_FORMAT;
        dir_name(path, dir, sizeof dir);
        r = bs_fs_resolve(dir, data_name, data_path, sizeof data_path);
        if (r != BS_OK)
            return r;
        r = bs_stream_open_file(data_path, &s);
        if (r != BS_OK)
            return r;
    }

    return bs_cdimage_open_stream(s, out);
}

bs_result bs_cdimage_open_stream(bs_stream *data, bs_cdimage **out)
{
    *out = NULL;
    bs_cdimage *cd = calloc(1, sizeof *cd);
    if (!cd) {
        bs_stream_close(data);
        return BS_ERR_NOMEM;
    }
    cd->data = data;
    bs_result r = detect_format(cd);
    if (r == BS_OK)
        r = read_pvd(cd);
    if (r != BS_OK) {
        bs_cdimage_close(cd);
        return r;
    }
    *out = cd;
    return BS_OK;
}

void bs_cdimage_close(bs_cdimage *cd)
{
    if (!cd)
        return;
    bs_stream_close(cd->data);
    free(cd);
}

const char *bs_cdimage_volume_id(const bs_cdimage *cd)
{
    return cd->volume_id;
}

/* ---- directory traversal ---- */

/* Callback per directory record; return false to stop. Name is NUL-terminated, version stripped. */
typedef bool (*record_fn)(void *user, const char *name, const iso_entry *e);

static bs_result walk_dir(bs_cdimage *cd, const iso_entry *dir, record_fn fn, void *user)
{
    /* A directory can't be larger than the image holding it. */
    if ((uint64_t)dir->size > cd->sector_count * ISO_SECTOR)
        return BS_ERR_FORMAT;
    uint8_t *buf = malloc(dir->size ? dir->size : 1);
    if (!buf)
        return BS_ERR_NOMEM;
    bs_result r = cd_read(cd, dir->lba, 0, buf, dir->size);
    if (r != BS_OK) {
        free(buf);
        return r;
    }

    size_t pos = 0;
    while (pos < dir->size) {
        uint8_t len = buf[pos];
        if (len == 0) {
            /* Records never span sectors; padding fills the rest. */
            pos = (pos / ISO_SECTOR + 1) * ISO_SECTOR;
            continue;
        }
        if (len < 34 || pos + len > dir->size) {
            r = BS_ERR_FORMAT;
            break;
        }
        bs_reader rd = bs_reader_make(buf + pos, len);
        bs_reader_skip(&rd, 2);
        iso_entry e;
        e.lba = bs_read_u32le(&rd);
        bs_reader_skip(&rd, 4);
        e.size = bs_read_u32le(&rd);
        bs_reader_seek(&rd, 25);
        e.is_dir = (bs_read_u8(&rd) & 0x02) != 0;
        bs_reader_seek(&rd, 32);
        uint8_t name_len = bs_read_u8(&rd);
        const uint8_t *raw = bs_read_span(&rd, name_len);
        if (!raw) {
            r = BS_ERR_FORMAT;
            break;
        }
        pos += len;

        /* Identifiers 0x00 and 0x01 are "." and "..". */
        if (name_len == 1 && (raw[0] == 0 || raw[0] == 1))
            continue;

        char name[256];
        size_t n = 0;
        while (n < name_len && raw[n] != ';')
            n++;
        memcpy(name, raw, n);
        name[n] = '\0';
        if (n > 0 && name[n - 1] == '.')
            name[n - 1] = '\0';

        if (!fn(user, name, &e))
            break;
    }
    free(buf);
    return r;
}

typedef struct find_ctx {
    const char *want;
    iso_entry found;
    bool ok;
} find_ctx;

static bool find_record(void *user, const char *name, const iso_entry *e)
{
    find_ctx *f = user;
    if (bs_strcasecmp(name, f->want) != 0)
        return true;
    f->found = *e;
    f->ok = true;
    return false;
}

static bs_result lookup(bs_cdimage *cd, const char *path, iso_entry *out)
{
    iso_entry cur = { cd->root_lba, cd->root_size, true };
    const char *p = path;
    while (*p) {
        while (*p == '/' || *p == '\\')
            p++;
        if (!*p)
            break;
        size_t len = strcspn(p, "/\\");
        char comp[256];
        if (len >= sizeof comp)
            return BS_ERR_NOT_FOUND;
        memcpy(comp, p, len);
        comp[len] = '\0';
        p += len;

        if (!cur.is_dir)
            return BS_ERR_NOT_FOUND;
        find_ctx f = { comp, { 0, 0, false }, false };
        bs_result r = walk_dir(cd, &cur, find_record, &f);
        if (r != BS_OK)
            return r;
        if (!f.ok)
            return BS_ERR_NOT_FOUND;
        cur = f.found;
    }
    *out = cur;
    return BS_OK;
}

bs_result bs_cdimage_stat(bs_cdimage *cd, const char *path, bool *out_is_dir, uint64_t *out_size)
{
    iso_entry e;
    bs_result r = lookup(cd, path, &e);
    if (r != BS_OK)
        return r;
    if (out_is_dir)
        *out_is_dir = e.is_dir;
    if (out_size)
        *out_size = e.size;
    return BS_OK;
}

typedef struct list_ctx {
    bs_fs_dir_fn fn;
    void *user;
} list_ctx;

static bool list_record(void *user, const char *name, const iso_entry *e)
{
    list_ctx *l = user;
    return l->fn(l->user, name, e->is_dir);
}

bs_result bs_cdimage_list_dir(bs_cdimage *cd, const char *path, bs_fs_dir_fn fn, void *user)
{
    iso_entry e;
    bs_result r = lookup(cd, path, &e);
    if (r != BS_OK)
        return r;
    if (!e.is_dir)
        return BS_ERR_ARGS;
    list_ctx l = { fn, user };
    return walk_dir(cd, &e, list_record, &l);
}

/* ---- file streams ---- */

typedef struct cd_stream {
    bs_stream base;
    bs_cdimage *cd;
    uint32_t lba;
} cd_stream;

static bs_result cd_stream_read_at(bs_stream *s, uint64_t offset, void *dst, size_t n)
{
    cd_stream *cs = (cd_stream *)s;
    return cd_read(cs->cd, cs->lba, offset, dst, n);
}

static void cd_stream_close(bs_stream *s)
{
    free(s);
}

bs_result bs_cdimage_open_file(bs_cdimage *cd, const char *path, bs_stream **out)
{
    *out = NULL;
    iso_entry e;
    bs_result r = lookup(cd, path, &e);
    if (r != BS_OK)
        return r;
    if (e.is_dir)
        return BS_ERR_NOT_FOUND;
    cd_stream *cs = calloc(1, sizeof *cs);
    if (!cs)
        return BS_ERR_NOMEM;
    cs->base.size = e.size;
    cs->base.read_at = cd_stream_read_at;
    cs->base.close = cd_stream_close;
    cs->cd = cd;
    cs->lba = e.lba;
    *out = &cs->base;
    return BS_OK;
}
