#include "bs/core/install.h"

#include <stdio.h>
#include <string.h>

const char *const bs_install_required_files[] = {
    "GAME.EXE",
    "GAMEDATA/LEVELS.TXT",
    "GAMEDATA/3D.BSA",
    "GAMEDATA/3D.BS6",
    "GAMEDATA/BS6.BSA",
    "GAMEDATA/BSI.BSA",
    "GAMEDATA/TXT.BSA",
    "GAMEDATA/FLC.BSA",
    "GAMEDATA/SPIRE.SND",
    "GAMEDATA/TEXT.RSC",
    "GAMEDATA/PAL.RAW",
    NULL
};

/* Known GAME.EXE sizes. Add entries as other releases are identified. */
static const struct {
    uint64_t size;
    bs_game_version version;
} known_exes[] = {
    { 1214687, BS_VERSION_CD },      /* retail CD, dated 1997-11-17 */
    { 1228254, BS_VERSION_PATCHED }, /* v1.5 patch (GOG release) */
};

const char *bs_game_version_str(bs_game_version v)
{
    switch (v) {
    case BS_VERSION_CD:      return "retail CD";
    case BS_VERSION_PATCHED: return "v1.5 patch";
    case BS_VERSION_UNKNOWN: break;
    }
    return "unknown";
}

bs_result bs_install_detect(const char *root, bs_install *out)
{
    memset(out, 0, sizeof *out);
    if (!bs_fs_is_dir(root))
        return BS_ERR_NOT_FOUND;
    if (snprintf(out->root, sizeof out->root, "%s", root) >= (int)sizeof out->root)
        return BS_ERR_ARGS;

    char exe[BS_PATH_MAX];
    if (bs_fs_resolve(root, "GAME.EXE", exe, sizeof exe) != BS_OK)
        return BS_ERR_NOT_FOUND;
    if (bs_fs_resolve(root, "GAMEDATA", out->gamedata, sizeof out->gamedata) != BS_OK ||
        !bs_fs_is_dir(out->gamedata))
        return BS_ERR_NOT_FOUND;

    char levels[BS_PATH_MAX];
    if (bs_fs_resolve(out->gamedata, "LEVELS.TXT", levels, sizeof levels) != BS_OK)
        return BS_ERR_NOT_FOUND;

    if (bs_fs_file_size(exe, &out->exe_size) == BS_OK) {
        for (size_t i = 0; i < sizeof known_exes / sizeof known_exes[0]; i++) {
            if (known_exes[i].size == out->exe_size)
                out->version = known_exes[i].version;
        }
    }

    if (bs_fs_resolve(root, "game.ins", out->cd_cue, sizeof out->cd_cue) == BS_OK &&
        bs_fs_resolve(root, "game.gog", out->cd_image, sizeof out->cd_image) == BS_OK)
        out->has_cd_image = true;

    char waves[BS_PATH_MAX];
    out->has_loose_waves = bs_fs_resolve(root, "WAVES.BSA", waves, sizeof waves) == BS_OK;

    return BS_OK;
}
