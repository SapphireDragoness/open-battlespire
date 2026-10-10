#include "bs/core/fs.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

int bs_strcasecmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb || ca == 0)
            return ca - cb;
    }
}

bool bs_path_join(char *out, size_t cap, const char *a, const char *b)
{
    size_t la = strlen(a);
    bool need_sep = la > 0 && a[la - 1] != '/' && a[la - 1] != '\\';
    int n = snprintf(out, cap, "%s%s%s", a, need_sep ? "/" : "", b);
    return n >= 0 && (size_t)n < cap;
}

#ifdef _WIN32

bool bs_fs_is_dir(const char *path)
{
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool bs_fs_is_file(const char *path)
{
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bs_result bs_fs_file_size(const char *path, uint64_t *out_size)
{
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &d))
        return BS_ERR_NOT_FOUND;
    *out_size = ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
    return BS_OK;
}

bs_result bs_fs_list_dir(const char *path, bs_fs_dir_fn fn, void *user)
{
    char pattern[BS_PATH_MAX];
    if (!bs_path_join(pattern, sizeof pattern, path, "*"))
        return BS_ERR_ARGS;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return BS_ERR_NOT_FOUND;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        if (!fn(user, fd.cFileName, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0))
            break;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return BS_OK;
}

#else

bool bs_fs_is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool bs_fs_is_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bs_result bs_fs_file_size(const char *path, uint64_t *out_size)
{
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
        return BS_ERR_NOT_FOUND;
    *out_size = (uint64_t)st.st_size;
    return BS_OK;
}

bs_result bs_fs_list_dir(const char *path, bs_fs_dir_fn fn, void *user)
{
    DIR *d = opendir(path);
    if (!d)
        return BS_ERR_NOT_FOUND;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        char full[BS_PATH_MAX];
        bool is_dir = bs_path_join(full, sizeof full, path, e->d_name) && bs_fs_is_dir(full);
        if (!fn(user, e->d_name, is_dir))
            break;
    }
    closedir(d);
    return BS_OK;
}

#endif

/* State for finding one path component case-insensitively. */
typedef struct match_ctx {
    const char *want;
    char found[256];
    bool ok;
} match_ctx;

static bool match_entry(void *user, const char *name, bool is_dir)
{
    (void)is_dir;
    match_ctx *m = user;
    if (bs_strcasecmp(name, m->want) != 0)
        return true;
    snprintf(m->found, sizeof m->found, "%s", name);
    m->ok = true;
    return strcmp(name, m->want) != 0; /* exact match wins; keep looking otherwise */
}

bs_result bs_fs_resolve(const char *root, const char *rel, char *out, size_t cap)
{
    char cur[BS_PATH_MAX];
    if (snprintf(cur, sizeof cur, "%s", root) >= (int)sizeof cur)
        return BS_ERR_ARGS;

    const char *p = rel;
    while (*p) {
        while (*p == '/' || *p == '\\')
            p++;
        if (!*p)
            break;
        size_t len = strcspn(p, "/\\");
        match_ctx m = { 0 };
        char comp[256];
        if (len >= sizeof comp)
            return BS_ERR_ARGS;
        memcpy(comp, p, len);
        comp[len] = '\0';
        p += len;

        m.want = comp;
        bs_result r = bs_fs_list_dir(cur, match_entry, &m);
        if (r != BS_OK)
            return r;
        if (!m.ok)
            return BS_ERR_NOT_FOUND;

        char next[BS_PATH_MAX];
        if (!bs_path_join(next, sizeof next, cur, m.found))
            return BS_ERR_ARGS;
        memcpy(cur, next, sizeof cur);
    }

    if (snprintf(out, cap, "%s", cur) >= (int)cap)
        return BS_ERR_ARGS;
    return BS_OK;
}

bs_result bs_fs_read_file(const char *path, bs_arena *arena, uint8_t **out_data, size_t *out_size)
{
    *out_data = NULL;
    *out_size = 0;

    FILE *f = fopen(path, "rb");
    if (!f)
        return BS_ERR_NOT_FOUND;

    bs_result res = BS_ERR_IO;
    uint64_t size64;
    if (bs_fs_file_size(path, &size64) != BS_OK || size64 >= SIZE_MAX)
        goto done;

    size_t size = (size_t)size64;
    uint8_t *buf = bs_arena_alloc(arena, size + 1, 16);
    if (!buf) {
        res = BS_ERR_NOMEM;
        goto done;
    }
    if (size && fread(buf, 1, size, f) != size)
        goto done;
    buf[size] = 0;

    *out_data = buf;
    *out_size = size;
    res = BS_OK;
done:
    fclose(f);
    return res;
}
