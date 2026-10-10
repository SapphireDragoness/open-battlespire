#include "bs/core/vfs.h"

#include <stdio.h>
#include <string.h>

void bs_vfs_init(bs_vfs *vfs)
{
    memset(vfs, 0, sizeof *vfs);
}

bs_result bs_vfs_mount_dir(bs_vfs *vfs, const char *path)
{
    if (vfs->count >= BS_VFS_MAX_MOUNTS)
        return BS_ERR_ARGS;
    if (!bs_fs_is_dir(path))
        return BS_ERR_NOT_FOUND;

    bs_mount *m = &vfs->mounts[vfs->count];
    if (snprintf(m->path, sizeof m->path, "%s", path) >= (int)sizeof m->path)
        return BS_ERR_ARGS;
    m->kind = BS_MOUNT_DIR;
    vfs->count++;
    return BS_OK;
}

/* Finds `name` in the highest-priority mount; writes the host path to out. */
static bs_result find(const bs_vfs *vfs, const char *name, char *out, size_t cap)
{
    for (int i = vfs->count - 1; i >= 0; i--) {
        const bs_mount *m = &vfs->mounts[i];
        switch (m->kind) {
        case BS_MOUNT_DIR:
            if (bs_fs_resolve(m->path, name, out, cap) == BS_OK && bs_fs_is_file(out))
                return BS_OK;
            break;
        }
    }
    return BS_ERR_NOT_FOUND;
}

bs_result bs_vfs_exists(const bs_vfs *vfs, const char *name)
{
    char path[BS_PATH_MAX];
    return find(vfs, name, path, sizeof path);
}

bs_result bs_vfs_read(const bs_vfs *vfs, const char *name, bs_arena *arena,
                      uint8_t **out_data, size_t *out_size)
{
    char path[BS_PATH_MAX];
    bs_result r = find(vfs, name, path, sizeof path);
    if (r != BS_OK)
        return r;
    return bs_fs_read_file(path, arena, out_data, out_size);
}
