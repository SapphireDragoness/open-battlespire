#ifndef BS_CORE_FS_H
#define BS_CORE_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"

/*
 * Thin host filesystem layer.
 *
 * Battlespire was made for DOS, so file names in its data are
 * case-insensitive. bs_fs_resolve() finds the real on-disk path for a
 * relative path regardless of case. Both '/' and '\\' are accepted as
 * separators in `rel`.
 */

#define BS_PATH_MAX 1024

bool bs_fs_is_dir(const char *path);
bool bs_fs_is_file(const char *path);

/* Returns BS_OK and fills *out_size with the file size. */
bs_result bs_fs_file_size(const char *path, uint64_t *out_size);

/*
 * Resolves `rel` under `root`, matching each path component
 * case-insensitively. On success writes the real path to out (capacity cap).
 */
bs_result bs_fs_resolve(const char *root, const char *rel, char *out, size_t cap);

/*
 * Reads a whole file into memory allocated from `arena`. A NUL byte is
 * appended (not counted in *out_size) so text files can be used as C strings.
 */
bs_result bs_fs_read_file(const char *path, bs_arena *arena, uint8_t **out_data, size_t *out_size);

/* Calls fn for each entry (excluding "." and ".."). Stop early by returning false. */
typedef bool (*bs_fs_dir_fn)(void *user, const char *name, bool is_dir);
bs_result bs_fs_list_dir(const char *path, bs_fs_dir_fn fn, void *user);

/* Joins a and b with a '/' separator. Returns false if it does not fit. */
bool bs_path_join(char *out, size_t cap, const char *a, const char *b);

/* ASCII case-insensitive string comparison (data file names are ASCII). */
int bs_strcasecmp(const char *a, const char *b);

#endif
