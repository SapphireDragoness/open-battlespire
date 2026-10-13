#ifndef BS_CORE_CDIMAGE_H
#define BS_CORE_CDIMAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "bs/core/fs.h"
#include "bs/core/result.h"
#include "bs/core/stream.h"

/*
 * Read-only access to the ISO 9660 filesystem of a CD image.
 *
 * Accepts a cue sheet (GOG's game.ins is one) or a data file directly. The
 * sector format is detected from the data: raw 2352-byte sectors (Mode 1 or
 * Mode 2 Form 1) or plain 2048-byte ISO sectors. Only the first data track is
 * used; audio tracks are ignored.
 *
 * Paths inside the image use '/' or '\\' and are matched case-insensitively;
 * ISO version suffixes (";1") are hidden.
 */
typedef struct bs_cdimage bs_cdimage;

bs_result bs_cdimage_open(const char *path, bs_cdimage **out);

/*
 * Opens an image from a stream holding the data track (raw 2352-byte or
 * 2048-byte sectors; no cue sheet). Takes ownership of the stream, even on
 * failure.
 */
bs_result bs_cdimage_open_stream(bs_stream *data, bs_cdimage **out);

/*
 * Finds the file of the first data (MODE*) track in a NUL-terminated cue
 * sheet and copies its name to out. Returns false if there is none.
 */
bool bs_cue_data_file(const char *cue_text, char *out, size_t cap);

/* Safe to call with NULL. Streams opened from the image must be closed first. */
void bs_cdimage_close(bs_cdimage *cd);

/* Volume identifier from the primary volume descriptor, e.g. "BSPIRE". */
const char *bs_cdimage_volume_id(const bs_cdimage *cd);

/* Looks up a path. "" or "/" is the root directory. */
bs_result bs_cdimage_stat(bs_cdimage *cd, const char *path, bool *out_is_dir, uint64_t *out_size);

/* Opens a file as a stream. The stream borrows the image; close it first. */
bs_result bs_cdimage_open_file(bs_cdimage *cd, const char *path, bs_stream **out);

/* Lists a directory. Names are passed without version suffixes. */
bs_result bs_cdimage_list_dir(bs_cdimage *cd, const char *path, bs_fs_dir_fn fn, void *user);

#endif
