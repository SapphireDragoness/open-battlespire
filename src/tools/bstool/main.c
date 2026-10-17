/* bstool: command-line utilities for inspecting Battlespire data files. */

#include <stdio.h>
#include <string.h>

#include "commands.h"

static const bstool_command commands[] = {
    { "info",    "[install-dir]",              "detect an installation and check its data files", cmd_info },
    { "ls",      "<archive>",                  "list the records in a BSA archive", cmd_ls },
    { "extract", "<archive> [-o dir] [--raw] [record...]", "extract records (default: all)", cmd_extract },
    { "cat",     "<archive> <record> [--text]", "write one record to stdout", cmd_cat },
    { "verify",  "",                           "read and decompress every record of every archive", cmd_verify },
    { "levels",  "",                           "print the level table (LEVELS.TXT)", cmd_levels },
    { "rsc",     "[id...]",                    "print TEXT.RSC strings as UTF-8", cmd_rsc },
    { "cdls",    "[path]",                     "list a directory of the CD image", cmd_cdls },
    { "get",     "<path> [-o file]",           "copy a file out of the install or CD image", cmd_get },
    { "bsi",     "[-o dir] [record...]",       "print BSI image headers, or export them as PNG", cmd_bsi },
    { "flc",     "[-o dir] [record...]",       "decode and check FLIC animations, export frames as PNG", cmd_flc },
    { "smk",     "[-o dir] [--cut N] [video...]", "decode and check Smacker videos, export frames and audio", cmd_smk },
    { "fnt",     "[-o dir]",                   "print game fonts, export glyph sheets as PNG", cmd_fnt },
    { "model",   "[-o dir] [name...]",         "print 3D model summaries, or export models as glTF", cmd_model },
    { "level",   "[-o dir] [--tree] <name...>", "print BS6 level summaries, or export levels as glTF", cmd_level },
    { "save",    "<SAVEn|dir> [--vars] [--hex id] [-o png] [--diff dir]", "print an original saved game's records and variables", cmd_save },
    { "le2elf",  "[exe] -o <out.elf>",         "convert GAME.EXE to ELF for Ghidra/objdump", cmd_le2elf },
};

#define NUM_COMMANDS (sizeof commands / sizeof commands[0])

static void usage(FILE *out)
{
    fprintf(out, "usage: bstool [-d install-dir] <command> [args]\n\ncommands:\n");
    for (size_t i = 0; i < NUM_COMMANDS; i++)
        fprintf(out, "  %-8s %-40s %s\n", commands[i].name, commands[i].usage, commands[i].summary);
    fprintf(out,
            "\nThe install directory defaults to the BS_DATA_DIR environment variable.\n"
            "<archive> is a host file path or a name inside the install, e.g.\n"
            "GAMEDATA/3D.BSA or WAVES.BSA (read from the CD image if not installed).\n");
}

int main(int argc, char **argv)
{
    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        if ((strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--data") == 0) && i + 1 < argc) {
            bstool_set_data_dir(argv[i + 1]);
            i += 2;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        } else {
            fprintf(stderr, "bstool: unknown option '%s'\n\n", argv[i]);
            usage(stderr);
            return 2;
        }
    }
    if (i >= argc) {
        usage(stderr);
        return 2;
    }
    if (strcmp(argv[i], "help") == 0) {
        usage(stdout);
        return 0;
    }
    for (size_t c = 0; c < NUM_COMMANDS; c++) {
        if (strcmp(argv[i], commands[c].name) == 0)
            return commands[c].run(argc - i, argv + i);
    }
    fprintf(stderr, "bstool: unknown command '%s'\n\n", argv[i]);
    usage(stderr);
    return 2;
}
