#ifndef BSTOOL_COMMANDS_H
#define BSTOOL_COMMANDS_H

#include <stdbool.h>

#include "bs/core/arena.h"
#include "bs/core/cdimage.h"
#include "bs/core/install.h"
#include "bs/core/vfs.h"
#include "bs/formats/bsa.h"

/*
 * Each subcommand receives argv with argv[0] set to the subcommand name.
 * Returns a process exit code: 0 success, 1 failure, 2 usage error.
 */
typedef int (*bstool_cmd_fn)(int argc, char **argv);

typedef struct bstool_command {
    const char *name;
    const char *usage;    /* arguments, e.g. "<archive> [record...]" */
    const char *summary;
    bstool_cmd_fn run;
} bstool_command;

int cmd_info(int argc, char **argv);
int cmd_ls(int argc, char **argv);
int cmd_extract(int argc, char **argv);
int cmd_cat(int argc, char **argv);
int cmd_verify(int argc, char **argv);
int cmd_levels(int argc, char **argv);
int cmd_rsc(int argc, char **argv);
int cmd_cdls(int argc, char **argv);
int cmd_get(int argc, char **argv);
int cmd_smk(int argc, char **argv);
int cmd_le2elf(int argc, char **argv);
int cmd_bsi(int argc, char **argv);
int cmd_flc(int argc, char **argv);
int cmd_fnt(int argc, char **argv);
int cmd_model(int argc, char **argv);
int cmd_level(int argc, char **argv);
int cmd_save(int argc, char **argv);

/* Install directory from -d, else BS_DATA_DIR; NULL if neither is set. */
const char *bstool_data_dir(void);
void bstool_set_data_dir(const char *dir);

/* An opened installation with the standard VFS layout. */
typedef struct bstool_ctx {
    bs_install inst;
    bs_vfs vfs;
    bs_cdimage *cd;  /* NULL if the install has no CD image */
    bs_arena arena;
} bstool_ctx;

/* Detects and mounts the install; prints an error and returns false on failure. */
bool bstool_open(bstool_ctx *ctx);
void bstool_close(bstool_ctx *ctx);

/*
 * Opens an archive given as a host file path or, failing that, a VFS name
 * (e.g. "GAMEDATA/3D.BSA", "WAVES.BSA"). ctx is opened on demand and must be
 * zero-initialized by the caller. Prints an error on failure.
 */
bs_bsa *bstool_open_archive(bstool_ctx *ctx, bool *ctx_opened, const char *name);

/* Display name for a record: its name, or its id for numbered archives. */
void bstool_record_label(const bs_bsa_record *rec, char *out, size_t cap);

#endif
