/* bstool: command-line utilities for inspecting Battlespire data files. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "commands.h"

static const bstool_command commands[] = {
    { "info", "[install-dir]", "detect an installation and check its data files", cmd_info },
};

#define NUM_COMMANDS (sizeof commands / sizeof commands[0])

static void usage(FILE *out)
{
    fprintf(out, "usage: bstool <command> [args]\n\ncommands:\n");
    for (size_t i = 0; i < NUM_COMMANDS; i++)
        fprintf(out, "  %-6s %-16s %s\n", commands[i].name, commands[i].usage, commands[i].summary);
    fprintf(out, "\nIf install-dir is omitted, the BS_DATA_DIR environment variable is used.\n");
}

const char *bstool_install_arg(int argc, char **argv)
{
    if (argc >= 2)
        return argv[1];
    const char *env = getenv("BS_DATA_DIR");
    if (env && *env)
        return env;
    fprintf(stderr, "%s: no install directory given and BS_DATA_DIR is not set\n", argv[0]);
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(stderr);
        return 2;
    }
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "help") == 0) {
        usage(stdout);
        return 0;
    }
    for (size_t i = 0; i < NUM_COMMANDS; i++) {
        if (strcmp(argv[1], commands[i].name) == 0)
            return commands[i].run(argc - 1, argv + 1);
    }
    fprintf(stderr, "bstool: unknown command '%s'\n\n", argv[1]);
    usage(stderr);
    return 2;
}
