#ifndef BS_CORE_VFS_H
#define BS_CORE_VFS_H

#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/cdimage.h"
#include "bs/core/fs.h"
#include "bs/core/result.h"
#include "bs/core/stream.h"

/*
 * Virtual file system: a stack of mounts searched from the most recently
 * mounted (highest priority) to the first. Lookups are case-insensitive and
 * accept '/' or '\\' separators.
 *
 * Mount kinds: host directories, and directories inside a CD image.
 */

#define BS_VFS_MAX_MOUNTS 16

typedef enum bs_mount_kind {
    BS_MOUNT_DIR,
    BS_MOUNT_CD
} bs_mount_kind;

typedef struct bs_mount {
    bs_mount_kind kind;
    char path[BS_PATH_MAX];  /* host directory, or directory inside the CD image */
    bs_cdimage *cd;          /* BS_MOUNT_CD only; borrowed */
} bs_mount;

typedef struct bs_vfs {
    bs_mount mounts[BS_VFS_MAX_MOUNTS];
    int count;
} bs_vfs;

void bs_vfs_init(bs_vfs *vfs);

bs_result bs_vfs_mount_dir(bs_vfs *vfs, const char *path);

/* Mounts directory `subdir` ("" for the root) of a CD image. The image must outlive the VFS. */
bs_result bs_vfs_mount_cd(bs_vfs *vfs, bs_cdimage *cd, const char *subdir);

/* Returns BS_OK if `name` exists as a file in any mount. */
bs_result bs_vfs_exists(const bs_vfs *vfs, const char *name);

/* Opens `name` from the highest-priority mount containing it. */
bs_result bs_vfs_open(const bs_vfs *vfs, const char *name, bs_stream **out);

/* Reads `name` whole (NUL-terminated, see bs_stream_read_all). */
bs_result bs_vfs_read(const bs_vfs *vfs, const char *name, bs_arena *arena,
                      uint8_t **out_data, size_t *out_size);

#endif
