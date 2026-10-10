#ifndef BS_CORE_INSTALL_H
#define BS_CORE_INSTALL_H

#include <stdbool.h>
#include <stdint.h>

#include "bs/core/fs.h"
#include "bs/core/result.h"

/* Detection of an original Battlespire installation. */

typedef enum bs_game_version {
    BS_VERSION_UNKNOWN,
    BS_VERSION_CD,      /* GAME.EXE as shipped on the retail CD */
    BS_VERSION_PATCHED  /* GAME.EXE from the v1.5 patch (as distributed by GOG) */
} bs_game_version;

typedef struct bs_install {
    char root[BS_PATH_MAX];      /* directory containing GAME.EXE */
    char gamedata[BS_PATH_MAX];  /* real path of GAMEDATA/ */
    bs_game_version version;
    uint64_t exe_size;

    /* Raw CD image (GOG ships it as game.gog + game.ins cue sheet). */
    bool has_cd_image;
    char cd_cue[BS_PATH_MAX];
    char cd_image[BS_PATH_MAX];

    /* WAVES.BSA is only on the CD; some installs copy it next to GAME.EXE. */
    bool has_loose_waves;
} bs_install;

/* Inspects `root`. Returns BS_ERR_NOT_FOUND if it is not a Battlespire install. */
bs_result bs_install_detect(const char *root, bs_install *out);

const char *bs_game_version_str(bs_game_version v);

/* Data files every install must have; NULL-terminated. Paths relative to the install root. */
extern const char *const bs_install_required_files[];

#endif
