#include "bs/core/vfs.h"

#include <stdio.h>
#include <string.h>

void bs_vfs_init(bs_vfs *vfs)
{
    memset(vfs, 0, sizeof *vfs);
}

static bs_mount *add_mount(bs_vfs *vfs, bs_mount_kind kind, const char *path)
{
    if (vfs->count >= BS_VFS_MAX_MOUNTS)
        return NULL;
    bs_mount *m = &vfs->mounts[vfs->count];
    if (snprintf(m->path, sizeof m->path, "%s", path) >= (int)sizeof m->path)
        return NULL;
    m->kind = kind;
    m->cd = NULL;
    vfs->count++;
    return m;
}

bs_result bs_vfs_mount_dir(bs_vfs *vfs, const char *path)
{
    if (!bs_fs_is_dir(path))
        return BS_ERR_NOT_FOUND;
    return add_mount(vfs, BS_MOUNT_DIR, path) ? BS_OK : BS_ERR_ARGS;
}

bs_result bs_vfs_mount_cd(bs_vfs *vfs, bs_cdimage *cd, const char *subdir)
{
    bool is_dir = false;
    if (bs_cdimage_stat(cd, subdir, &is_dir, NULL) != BS_OK || !is_dir)
        return BS_ERR_NOT_FOUND;
    bs_mount *m = add_mount(vfs, BS_MOUNT_CD, subdir);
    if (!m)
        return BS_ERR_ARGS;
    m->cd = cd;
    return BS_OK;
}

/* Tries to open `name` from one mount. */
static bs_result open_in(const bs_mount *m, const char *name, bs_stream **out)
{
    char path[BS_PATH_MAX];
    switch (m->kind) {
    case BS_MOUNT_DIR:
        if (bs_fs_resolve(m->path, name, path, sizeof path) != BS_OK || !bs_fs_is_file(path))
            return BS_ERR_NOT_FOUND;
        return bs_stream_open_file(path, out);
    case BS_MOUNT_CD:
        if (!bs_path_join(path, sizeof path, m->path, name))
            return BS_ERR_ARGS;
        return bs_cdimage_open_file(m->cd, path, out);
    }
    return BS_ERR_NOT_FOUND;
}

bs_result bs_vfs_open(const bs_vfs *vfs, const char *name, bs_stream **out)
{
    *out = NULL;
    for (int i = vfs->count - 1; i >= 0; i--) {
        bs_result r = open_in(&vfs->mounts[i], name, out);
        if (r != BS_ERR_NOT_FOUND)
            return r;
    }
    return BS_ERR_NOT_FOUND;
}

bs_result bs_vfs_exists(const bs_vfs *vfs, const char *name)
{
    bs_stream *s;
    bs_result r = bs_vfs_open(vfs, name, &s);
    bs_stream_close(s);
    return r;
}

bs_result bs_vfs_read(const bs_vfs *vfs, const char *name, bs_arena *arena,
                      uint8_t **out_data, size_t *out_size)
{
    *out_data = NULL;
    *out_size = 0;
    bs_stream *s;
    bs_result r = bs_vfs_open(vfs, name, &s);
    if (r != BS_OK)
        return r;
    r = bs_stream_read_all(s, arena, out_data, out_size);
    bs_stream_close(s);
    return r;
}
