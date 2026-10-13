/*
 * Data tests: need a real installation in BS_DATA_DIR; skipped otherwise.
 * These are the regression gate for format parsers against the shipped data.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/assets/assets.h"
#include "bs/assets/scene.h"
#include "bs/core/install.h"
#include "bs/formats/bs6.h"
#include "bs/formats/bsa.h"
#include "bs/formats/bsi.h"
#include "bs/formats/flic.h"
#include "bs/formats/font.h"
#include "bs/formats/le.h"
#include "bs/formats/levels.h"
#include "bs/formats/model.h"
#include "bs/formats/rsc.h"
#include "bs_test.h"

static const char *data_dir;
static bs_install inst;
static bs_vfs vfs;
static bs_cdimage *cd;

static void test_detect(void)
{
    BS_CHECK(inst.version != BS_VERSION_UNKNOWN);
    for (const char *const *f = bs_install_required_files; *f; f++) {
        char path[BS_PATH_MAX];
        BS_CHECK_EQ_INT(bs_fs_resolve(inst.root, *f, path, sizeof path), BS_OK);
    }
}

static void test_rejects_non_install(void)
{
    bs_install other;
    BS_CHECK_EQ_INT(bs_install_detect(BS_SOURCE_DIR, &other), BS_ERR_NOT_FOUND);
}

/* Every record of every archive reads and decompresses. */
static void test_all_archives(void)
{
    static const struct {
        const char *name;
        uint32_t count;
    } archives[] = {
        { "GAMEDATA/3D.BSA", 2400 },  { "GAMEDATA/3D.BS6", 2115 },  { "GAMEDATA/BSI.BSA", 2599 },
        { "GAMEDATA/BS6.BSA", 47 },   { "GAMEDATA/TXT.BSA", 254 },  { "GAMEDATA/FLC.BSA", 164 },
        { "GAMEDATA/SPIRE.SND", 370 }, { "WAVES.BSA", 775 },
    };
    bs_arena a;
    bs_arena_init(&a, 0);
    for (size_t i = 0; i < sizeof archives / sizeof archives[0]; i++) {
        if (!cd && strcmp(archives[i].name, "WAVES.BSA") == 0 && !inst.has_loose_waves)
            continue;
        bs_bsa *bsa;
        BS_CHECK_EQ_INT(bs_bsa_open_vfs(&vfs, archives[i].name, &bsa), BS_OK);
        if (!bsa)
            continue;
        BS_CHECK_EQ_INT(bsa->count, archives[i].count);
        uint32_t failures = 0;
        for (uint32_t k = 0; k < bsa->count; k++) {
            uint8_t *data;
            size_t n;
            if (bs_bsa_read(bsa, &bsa->records[k], &a, &data, &n) != BS_OK)
                failures++;
            bs_arena_reset(&a);
        }
        BS_CHECK_EQ_INT(failures, 0);
        bs_bsa_close(bsa);
    }
    bs_arena_free(&a);
}

/* Loose .3d files are byte-identical to their compressed archive copies. */
static void test_decompression_matches_loose(void)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_bsa *bsa;
    BS_CHECK_EQ_INT(bs_bsa_open_vfs(&vfs, "GAMEDATA/3D.BSA", &bsa), BS_OK);
    const bs_bsa_record *rec = bsa ? bs_bsa_find(bsa, "POLY.3D") : NULL;
    BS_CHECK(rec && bs_bsa_record_compressed(rec));
    if (rec) {
        uint8_t *packed, *loose;
        size_t packed_n, loose_n;
        BS_CHECK_EQ_INT(bs_bsa_read(bsa, rec, &a, &packed, &packed_n), BS_OK);
        BS_CHECK_EQ_INT(bs_vfs_read(&vfs, "GAMEDATA/poly.3d", &a, &loose, &loose_n), BS_OK);
        BS_CHECK(packed_n == loose_n && memcmp(packed, loose, loose_n) == 0);
    }
    bs_bsa_close(bsa);
    bs_arena_free(&a);
}

static void test_levels_and_rsc(void)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    uint8_t *data;
    size_t n;

    bs_level_table levels;
    BS_CHECK_EQ_INT(bs_vfs_read(&vfs, "GAMEDATA/LEVELS.TXT", &a, &data, &n), BS_OK);
    BS_CHECK_EQ_INT(bs_levels_parse(data, n, &a, &levels), BS_OK);
    BS_CHECK_EQ_INT(levels.count, 17);
    if (levels.count == 17)
        BS_CHECK(strcmp(levels.levels[0].name, "The Weir Gate") == 0);

    bs_rsc rsc;
    BS_CHECK_EQ_INT(bs_vfs_read(&vfs, "GAMEDATA/TEXT.RSC", &a, &data, &n), BS_OK);
    BS_CHECK_EQ_INT(bs_rsc_parse(data, n, &a, &rsc), BS_OK);
    BS_CHECK_EQ_INT(rsc.count, 300);
    bs_arena_free(&a);
}

static void test_cd_contents(void)
{
    if (!cd)
        return;
    BS_CHECK(strcmp(bs_cdimage_volume_id(cd), "BSPIRE") == 0);
    BS_CHECK_EQ_INT(bs_vfs_exists(&vfs, "VIDEOS/STORY.SMK"), BS_OK);
}

/* GAME.EXE loads with every fixup applied (32001 records: 31999 offset32, 2 selector). */
static void test_game_exe(void)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    uint8_t *exe;
    size_t n;
    bs_le_image img;
    BS_CHECK_EQ_INT(bs_vfs_read(&vfs, "GAME.EXE", &a, &exe, &n), BS_OK);
    BS_CHECK_EQ_INT(bs_le_load(exe, n, &a, &img), BS_OK);
    BS_CHECK_EQ_INT(img.object_count, 3);
    BS_CHECK_EQ_INT(img.stats.unsupported, 0);
    BS_CHECK_EQ_INT(img.stats.offset32 + img.stats.selector, 32001);
    if (inst.version == BS_VERSION_PATCHED)
        BS_CHECK_EQ_INT(img.entry_va, 0x701b0);
    bs_arena_free(&a);
}

/* Every BSI image decodes, and every pixel value is even (colour = HICL[pixel / 2]). */
static void test_all_bsi(void)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_bsa *bsa;
    BS_CHECK_EQ_INT(bs_bsa_open_vfs(&vfs, "GAMEDATA/BSI.BSA", &bsa), BS_OK);
    uint32_t records = 0, images = 0, failures = 0, odd = 0;
    for (uint32_t i = 0; bsa && i < bsa->count; i++) {
        const char *name = bsa->records[i].name;
        size_t len = strlen(name);
        if (len < 4 || bs_strcasecmp(name + len - 4, ".BSI") != 0)
            continue;
        uint8_t *data;
        size_t n;
        bs_bsi bsi;
        records++;
        if (bs_bsa_read(bsa, &bsa->records[i], &a, &data, &n) != BS_OK || bs_bsi_parse(data, n, &a, &bsi) != BS_OK) {
            failures++;
        } else {
            images += (uint32_t)bsi.count;
            for (size_t k = 0; k < bsi.count; k++) {
                const bs_bsi_image *img = &bsi.images[k];
                size_t px = (size_t)img->width * img->height * img->frames;
                for (size_t p = 0; p < px; p++)
                    odd += img->pixels[p] & 1;
            }
        }
        bs_arena_reset(&a);
    }
    BS_CHECK_EQ_INT(records, 2592);
    BS_CHECK_EQ_INT(failures, 0);
    BS_CHECK_EQ_INT(images, 2621); /* 2582 standalone + 39 inside BSIF containers */
    BS_CHECK_EQ_INT(odd, 0);
    bs_bsa_close(bsa);
    bs_arena_free(&a);
}

/* Every FLIC decodes, and every ring frame reproduces frame 1 exactly. */
static void test_all_flic(void)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_bsa *bsa;
    BS_CHECK_EQ_INT(bs_bsa_open_vfs(&vfs, "GAMEDATA/FLC.BSA", &bsa), BS_OK);
    uint32_t files = 0, frames = 0, failures = 0, ring_ok = 0;
    for (uint32_t i = 0; bsa && i < bsa->count; i++) {
        const char *name = bsa->records[i].name;
        size_t len = strlen(name);
        if (len < 4 || bs_strcasecmp(name + len - 4, ".FLC") != 0)
            continue;
        files++;
        uint8_t *data, *first = NULL;
        size_t n;
        bs_flic f;
        bs_flic_player p;
        if (bs_bsa_read(bsa, &bsa->records[i], &a, &data, &n) != BS_OK || bs_flic_open(data, n, &f) != BS_OK ||
            bs_flic_player_init(&p, &f, &a) != BS_OK) {
            failures++;
            continue;
        }
        size_t npix = (size_t)f.width * f.height;
        for (;;) {
            bool ended;
            if (bs_flic_next_frame(&p, &ended) != BS_OK) {
                failures++;
                break;
            }
            if (ended)
                break;
            if (p.frame == 1)
                first = bs_arena_dup(&a, p.pixels, npix);
            if (p.frame == (uint32_t)f.frames + 1u && first && memcmp(first, p.pixels, npix) == 0)
                ring_ok++;
        }
        frames += p.frame;
        bs_arena_reset(&a);
    }
    BS_CHECK_EQ_INT(files, 163);
    BS_CHECK_EQ_INT(failures, 0);
    BS_CHECK_EQ_INT(frames, 7365);
    BS_CHECK_EQ_INT(ring_ok, 163);
    bs_bsa_close(bsa);
    bs_arena_free(&a);
}

static void test_fonts(void)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    for (int i = 1; i <= 10; i++) {
        char name[32];
        snprintf(name, sizeof name, "GAMEDATA/FONT%04d.FNT", i);
        uint8_t *data;
        size_t n;
        bs_font font;
        BS_CHECK_EQ_INT(bs_vfs_read(&vfs, name, &a, &data, &n), BS_OK);
        BS_CHECK_EQ_INT(bs_font_parse(data, n, &a, &font), BS_OK);
        uint32_t present = 0;
        for (uint32_t k = 0; k < font.count; k++)
            present += font.glyphs[k].pixels != NULL;
        BS_CHECK_EQ_INT(present, 134);
        BS_CHECK(bs_font_glyph_for(&font, 'A') != NULL);
        bs_arena_reset(&a);
    }
    bs_arena_free(&a);
}

static bool ends_with(const char *s, const char *suffix)
{
    size_t n = strlen(s), k = strlen(suffix);
    return n >= k && bs_strcasecmp(s + n - k, suffix) == 0;
}

/* Every model in 3D.BSA parses, and every face texture resolves to an image. */
static void test_all_models(void)
{
    bs_assets *assets;
    BS_CHECK_EQ_INT(bs_assets_open(&vfs, &assets), BS_OK);
    bs_bsa *bsa;
    BS_CHECK_EQ_INT(bs_bsa_open_vfs(&vfs, "GAMEDATA/3D.BSA", &bsa), BS_OK);
    uint32_t models = 0, v26 = 0, faces = 0, textured = 0, colour = 0, invisible = 0, unresolved = 0;
    for (uint32_t i = 0; i < bsa->count; i++) {
        if (!ends_with(bsa->records[i].name, ".3D"))
            continue;
        const bs_model *m = bs_assets_model(assets, bsa->records[i].name);
        BS_CHECK(m != NULL);
        if (!m)
            continue;
        models++;
        v26 += strcmp(m->version, "v2.6") == 0;
        for (uint32_t k = 0; k < m->face_count; k++) {
            uint32_t t = m->faces[k].texture;
            faces++;
            switch (bs_model_texture_kind(t)) {
            case BS_MODEL_TEX_IMAGE: {
                char name[BS_MODEL_TEX_NAME_MAX];
                textured++;
                if (!bs_model_texture_name(t, name) || !bs_assets_texture(assets, name))
                    unresolved++;
                break;
            }
            case BS_MODEL_TEX_COLOR:
                colour++;
                break;
            case BS_MODEL_TEX_NONE:
                BS_CHECK_EQ_INT(t, BS_MODEL_TEX_INVISIBLE);
                invisible++;
                break;
            }
        }
    }
    BS_CHECK_EQ_INT(models, 2395);
    BS_CHECK_EQ_INT(v26, 25);
    BS_CHECK_EQ_INT(faces, 134754);
    BS_CHECK_EQ_INT(textured, 133821);
    BS_CHECK_EQ_INT(colour, 67);
    BS_CHECK_EQ_INT(invisible, 866);
    BS_CHECK_EQ_INT(unresolved, 0);
    bs_bsa_close(bsa);
    bs_assets_close(assets);
}

/*
 * Every level file parses and builds a scene. Levels in LEVELS.TXT (the ones
 * the game loads) miss exactly one model: dmsc.bs6 places scgrate.3d, which
 * isn't in the data.
 */
static void test_all_levels(void)
{
    bs_assets *assets;
    BS_CHECK_EQ_INT(bs_assets_open(&vfs, &assets), BS_OK);
    bs_bsa *bsa;
    BS_CHECK_EQ_INT(bs_bsa_open_vfs(&vfs, "GAMEDATA/BS6.BSA", &bsa), BS_OK);
    uint32_t levels = 0;
    for (uint32_t i = 0; i < bsa->count; i++) {
        if (!ends_with(bsa->records[i].name, ".BS6"))
            continue;
        bs_arena a;
        bs_arena_init(&a, 0);
        uint8_t *data;
        size_t n;
        bs_bs6_level lvl;
        bs_scene scene;
        BS_CHECK_EQ_INT(bs_bsa_read(bsa, &bsa->records[i], &a, &data, &n), BS_OK);
        bs_result r = bs_bs6_parse(data, n, &a, &lvl);
        if (r == BS_OK)
            r = bs_scene_build(assets, &lvl, &a, &scene);
        if (r != BS_OK)
            fprintf(stderr, "  %s: %s\n", bsa->records[i].name, bs_result_str(r));
        BS_CHECK_EQ_INT(r, BS_OK);
        levels++;
        bs_arena_free(&a);
    }
    BS_CHECK_EQ_INT(levels, 45);

    bs_arena a;
    bs_arena_init(&a, 0);
    uint8_t *data;
    size_t n;
    bs_level_table table;
    BS_CHECK_EQ_INT(bs_vfs_read(&vfs, "GAMEDATA/LEVELS.TXT", &a, &data, &n), BS_OK);
    BS_CHECK_EQ_INT(bs_levels_parse(data, n, &a, &table), BS_OK);
    uint32_t missing_models = 0, missing_textures = 0, objects = 0;
    for (size_t i = 0; i < table.count; i++) {
        bs_bs6_level lvl;
        bs_scene scene;
        BS_CHECK_EQ_INT(bs_assets_read(assets, table.levels[i].file, &a, &data, &n), BS_OK);
        BS_CHECK_EQ_INT(bs_bs6_parse(data, n, &a, &lvl), BS_OK);
        BS_CHECK_EQ_INT(bs_scene_build(assets, &lvl, &a, &scene), BS_OK);
        missing_models += scene.missing_models;
        objects += lvl.object_count;
        for (uint32_t k = 0; k < scene.material_count; k++)
            missing_textures += scene.materials[k].kind == BS_SCENE_MAT_MISSING;
        for (uint32_t k = 0; k < scene.sprite_count; k++)
            BS_CHECK(scene.sprites[k].image != NULL);
        if (table.levels[i].sky[0])
            BS_CHECK(bs_assets_image(assets, table.levels[i].sky) != NULL);
    }
    BS_CHECK_EQ_INT(missing_models, 1);
    BS_CHECK_EQ_INT(missing_textures, 0);
    BS_CHECK_EQ_INT(objects, 4085); /* dmkk.bs6 is listed twice */
    bs_arena_free(&a);
    bs_bsa_close(bsa);
    bs_assets_close(assets);
}

int main(void)
{
    data_dir = getenv("BS_DATA_DIR");
    if (!data_dir || !*data_dir)
        return BS_TEST_SKIP;
    if (bs_install_detect(data_dir, &inst) != BS_OK)
        return 1;
    bs_vfs_init(&vfs);
    if (bs_install_mount(&inst, &vfs, &cd) != BS_OK)
        return 1;

    BS_RUN(test_detect);
    BS_RUN(test_rejects_non_install);
    BS_RUN(test_all_archives);
    BS_RUN(test_decompression_matches_loose);
    BS_RUN(test_levels_and_rsc);
    BS_RUN(test_cd_contents);
    BS_RUN(test_game_exe);
    BS_RUN(test_all_bsi);
    BS_RUN(test_all_flic);
    BS_RUN(test_fonts);
    BS_RUN(test_all_models);
    BS_RUN(test_all_levels);
    bs_cdimage_close(cd);
    return BS_TEST_RESULT();
}
