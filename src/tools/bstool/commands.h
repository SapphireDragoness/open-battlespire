#ifndef BSTOOL_COMMANDS_H
#define BSTOOL_COMMANDS_H

/*
 * Each subcommand receives argv with argv[0] set to the subcommand name.
 * Returns a process exit code.
 */
typedef int (*bstool_cmd_fn)(int argc, char **argv);

typedef struct bstool_command {
    const char *name;
    const char *usage;    /* arguments, e.g. "[install-dir]" */
    const char *summary;
    bstool_cmd_fn run;
} bstool_command;

int cmd_info(int argc, char **argv);

/*
 * Returns the install directory from argv[1] if present, otherwise from the
 * BS_DATA_DIR environment variable, otherwise NULL (and prints an error).
 */
const char *bstool_install_arg(int argc, char **argv);

#endif
