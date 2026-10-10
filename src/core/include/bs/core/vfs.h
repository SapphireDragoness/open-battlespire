#ifndef BS_CORE_VFS_H
#define BS_CORE_VFS_H

#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/fs.h"
#include "bs/core/result.h"

/*
 * Virtual file system: a stack of mounts searched from the most recently
 * mounted (highest priority) to the first. Lookups are case-insensitive.
 *
 * Currently only host directories can be mounted. BSA archives and the CD
 * image will be added as further mount kinds (see docs/PLAN.md §4.1).
 */

#define BS_VFS_MAX_MOUNTS 16

typedef enum bs_mount_kind {
    BS_MOUNT_DIR
} bs_mount_kind;

typedef struct bs_mount {
    bs_mount_kind kind;
    char path[BS_PATH_MAX];
} bs_mount;

typedef struct bs_vfs {
    bs_mount mounts[BS_VFS_MAX_MOUNTS];
    int count;
} bs_vfs;

void bs_vfs_init(bs_vfs *vfs);

bs_result bs_vfs_mount_dir(bs_vfs *vfs, const char *path);

/* Returns BS_OK if `name` exists in any mount. */
bs_result bs_vfs_exists(const bs_vfs *vfs, const char *name);

/* Reads `name` from the highest-priority mount containing it (NUL-terminated, see bs_fs_read_file). */
bs_result bs_vfs_read(const bs_vfs *vfs, const char *name, bs_arena *arena,
                      uint8_t **out_data, size_t *out_size);

#endif
