/* bstool info: detect an installation and report what was found. */

#include <inttypes.h>
#include <stdio.h>

#include "bs/core/install.h"
#include "commands.h"

int cmd_info(int argc, char **argv)
{
    const char *root = argc >= 2 ? argv[1] : bstool_data_dir();
    if (!root) {
        fprintf(stderr, "info: no install directory; pass one or set BS_DATA_DIR\n");
        return 2;
    }

    bs_install inst;
    bs_result r = bs_install_detect(root, &inst);
    if (r != BS_OK) {
        fprintf(stderr, "info: '%s' is not a Battlespire installation (%s)\n", root, bs_result_str(r));
        fprintf(stderr, "      expected GAME.EXE and GAMEDATA/LEVELS.TXT\n");
        return 1;
    }

    printf("Install:   %s\n", inst.root);
    printf("GAMEDATA:  %s\n", inst.gamedata);
    printf("GAME.EXE:  %" PRIu64 " bytes (%s)\n", inst.exe_size, bs_game_version_str(inst.version));
    if (inst.has_cd_image)
        printf("CD image:  %s\n", inst.cd_image);
    else
        printf("CD image:  not found (cutscenes and WAVES.BSA unavailable unless copied)\n");
    printf("WAVES.BSA: %s\n", inst.has_loose_waves ? "loose copy present" :
                              inst.has_cd_image ? "inside CD image" : "missing");

    printf("\nRequired files:\n");
    int missing = 0;
    for (const char *const *f = bs_install_required_files; *f; f++) {
        char path[BS_PATH_MAX];
        uint64_t size = 0;
        if (bs_fs_resolve(inst.root, *f, path, sizeof path) == BS_OK &&
            bs_fs_file_size(path, &size) == BS_OK) {
            printf("  ok       %-22s %12" PRIu64 "\n", *f, size);
        } else {
            printf("  MISSING  %s\n", *f);
            missing++;
        }
    }

    if (missing) {
        printf("\n%d required file(s) missing.\n", missing);
        return 1;
    }
    printf("\nAll required files present.\n");
    return 0;
}
