/*
 * Data tests: need a real installation in BS_DATA_DIR; skipped otherwise.
 * These are the regression gate for format parsers against the shipped data.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/assets/assets.h"
#include "bs/assets/scene.h"
#include "bs/core/install.h"
#include "bs/formats/automap.h"
#include "bs/formats/save.h"
#include "bs/formats/scl.h"
#include "bs/formats/bs6.h"
#include "bs/formats/items.h"
#include "bs/game/creation.h"
#include "bs/game/dialogue.h"
#include "bs/formats/talk.h"
#include "bs/game/items.h"
#include "bs/game/level_logic.h"
#include "bs/game/player.h"
#include "bs/game/game_save.h"
#include "bs/game/save_edit.h"
#include "bs/game/save_import.h"
#include "bs/formats/bsa.h"
#include "bs/formats/creatures.h"
#include "bs/game/monsters.h"
#include "bs/formats/spells.h"
#include "bs/formats/bsi.h"
#include "bs/formats/exe_text.h"
#include "bs/formats/flic.h"
#include "bs/formats/font.h"
#include "bs/formats/le.h"
#include "bs/formats/levels.h"
#include "bs/formats/model.h"
#include "bs/formats/nodes.h"
#include "bs/formats/rsc.h"
#include "bs/formats/smacker.h"
#include "bs/formats/wav.h"
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
    /* Campaign Lv1..Lv7 play tracks 2..7 and 9; every multiplayer level plays 5. */
    static const int tracks[17] = {2, 3, 4, 5, 6, 7, 9, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5};
    for (size_t i = 0; i < levels.count && i < 17; i++)
        BS_CHECK_EQ_INT(bs_level_music_track(&levels.levels[i]), tracks[i]);

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
    /* The built-in messages. */
    bs_exe_text text;
    BS_CHECK_EQ_INT(bs_exe_text_read(&img, &a, &text), BS_OK);
    BS_CHECK_EQ_INT(text.count, 543);
    BS_CHECK(strcmp(bs_exe_message(&text, BS_MSG_CANNOT_CARRY), "You cannot carry any more stuff.") == 0);
    BS_CHECK(strcmp(bs_exe_message(&text, BS_MSG_ATTRIBUTES), "strength") == 0);
    BS_CHECK(strcmp(bs_exe_message(&text, BS_MSG_ATTRIBUTES + 7), "luck") == 0);
    BS_CHECK(strcmp(bs_exe_message(&text, 543), "") == 0);
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
    uint32_t levels = 0, props = 0, ctrls = 0, links = 0;
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
        for (uint32_t k = 0; r == BS_OK && k < lvl.object_count + lvl.flat_count + lvl.light_count; k++) {
            const bs_bs6_chunk *ent = k < lvl.object_count ? &lvl.objects[k].chunk
                                      : k < lvl.object_count + lvl.flat_count
                                          ? &lvl.flats[k - lvl.object_count].chunk
                                          : &lvl.lights[k - lvl.object_count - lvl.flat_count].chunk;
            bs_bs6_logic g;
            r = bs_bs6_entity_logic(ent, &a, &g);
            props += g.prop_count;
            ctrls += g.ctrl_count;
            links += g.link_count;
        }
        if (r == BS_OK) {
            /* The logic builds, and every level's triggers can be fired and run for a minute. */
            bs_level_logic lg;
            r = bs_level_logic_build(&lvl, &a, &lg);
            for (uint32_t k = 0; r == BS_OK && k < lg.object_count; k++) {
                bs_logic_output out;
                bs_level_logic_activate(&lg, k, BS_LOGIC_EVENT_USE, 0, &out);
                bs_level_logic_activate(&lg, k, BS_LOGIC_EVENT_STEP, 0, &out);
            }
            for (uint32_t t = 0; r == BS_OK && t <= 60000; t += 50) {
                bs_logic_output out;
                bs_level_logic_update(&lg, t, &out);
            }
        }
        if (r != BS_OK)
            fprintf(stderr, "  %s: %s\n", bsa->records[i].name, bs_result_str(r));
        BS_CHECK_EQ_INT(r, BS_OK);
        levels++;
        bs_arena_free(&a);
    }
    BS_CHECK_EQ_INT(levels, 45);
    BS_CHECK_EQ_INT(props, 4194); /* as in docs/formats/bs6.md's survey */
    BS_CHECK_EQ_INT(ctrls, 1590);
    BS_CHECK_EQ_INT(links, 486);

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

/*
 * Level 1 built with its interactive objects as separate, movable parts
 * matches the all-static build: same triangles, and rays hit at the same
 * distances.
 */
static void test_separate_objects(void)
{
    bs_assets *assets;
    BS_CHECK_EQ_INT(bs_assets_open(&vfs, &assets), BS_OK);
    bs_arena a;
    bs_arena_init(&a, 0);
    uint8_t *data;
    size_t n;
    bs_bs6_level lvl;
    BS_CHECK_EQ_INT(bs_assets_read(assets, "l1.bs6", &a, &data, &n), BS_OK);
    BS_CHECK_EQ_INT(bs_bs6_parse(data, n, &a, &lvl), BS_OK);
    bool *sep = BS_ARENA_ARRAY(&a, bool, lvl.object_count);
    uint32_t nsep = 0;
    for (uint32_t i = 0; i < lvl.object_count; i++) {
        bs_bs6_logic g;
        BS_CHECK_EQ_INT(bs_bs6_entity_logic(&lvl.objects[i].chunk, &a, &g), BS_OK);
        sep[i] = g.ctrl_count || g.link_count;
        nsep += sep[i];
    }
    bs_scene all, part;
    BS_CHECK_EQ_INT(bs_scene_build(assets, &lvl, &a, &all), BS_OK);
    BS_CHECK_EQ_INT(bs_scene_build_ex(assets, &lvl, sep, &a, &part), BS_OK);
    BS_CHECK(nsep > 50);
    BS_CHECK_EQ_INT(all.vertex_count, part.vertex_count);
    BS_CHECK(part.batch_count > part.static_batch_count);
    for (int c = 0; c < 3; c++)
        BS_CHECK(fabsf(all.min[c] - part.min[c]) < 0.01f && fabsf(all.max[c] - part.max[c]) < 0.01f);
    /* Rays from the start marker in 64 directions. */
    float o[3] = {0, 0, 0};
    for (uint32_t i = 0; i < lvl.flat_count; i++)
        if (bs_strcasecmp(lvl.flats[i].name, "start") == 0)
            for (int c = 0; c < 3; c++)
                o[c] = (float)lvl.flats[i].pos[c];
    uint32_t hits = 0, object_hits = 0;
    for (int k = 0; k < 64; k++) {
        float yaw = (float)k * 0.7f, pitch = (float)(k % 7 - 3) * 0.4f;
        float d[3] = {sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch)};
        int32_t ob1, ob2;
        float t1 = bs_scene_raycast(&all, &lvl, NULL, o, d, 20000, &ob1);
        float t2 = bs_scene_raycast(&part, &lvl, NULL, o, d, 20000, &ob2);
        BS_CHECK(ob1 == -1);
        BS_CHECK((t1 < 0) == (t2 < 0) && fabsf(t1 - t2) < 0.05f);
        hits += t1 >= 0;
        object_hits += ob2 >= 0;
    }
    BS_CHECK(hits > 32);
    printf("  l1: %u separate objects, %u of 64 rays hit, %u on them\n", nsep, hits, object_hits);
    bs_arena_free(&a);
    bs_assets_close(assets);
}

/*
 * A player dropped at level 1's start lands on the floor at height 0 (the
 * memory dumps show the player standing at y = 0 there) and can walk about.
 */
static void test_player_level1(void)
{
    bs_assets *assets;
    BS_CHECK_EQ_INT(bs_assets_open(&vfs, &assets), BS_OK);
    bs_arena a;
    bs_arena_init(&a, 0);
    uint8_t *data;
    size_t n;
    bs_bs6_level lvl;
    BS_CHECK_EQ_INT(bs_assets_read(assets, "l1.bs6", &a, &data, &n), BS_OK);
    BS_CHECK_EQ_INT(bs_bs6_parse(data, n, &a, &lvl), BS_OK);
    bool *sep = BS_ARENA_ARRAY(&a, bool, lvl.object_count);
    for (uint32_t i = 0; i < lvl.object_count; i++) {
        bs_bs6_logic g;
        BS_CHECK_EQ_INT(bs_bs6_entity_logic(&lvl.objects[i].chunk, &a, &g), BS_OK);
        sep[i] = g.ctrl_count || g.link_count;
    }
    bs_scene scene;
    bs_collision_world w;
    BS_CHECK_EQ_INT(bs_scene_build_ex(assets, &lvl, sep, &a, &scene), BS_OK);
    BS_CHECK_EQ_INT(bs_collision_build(&scene, &lvl, &a, &w), BS_OK);
    float start[3] = {0, 0, 0};
    for (uint32_t i = 0; i < lvl.flat_count; i++)
        if (bs_strcasecmp(lvl.flats[i].name, "start") == 0)
            for (int c = 0; c < 3; c++)
                start[c] = (float)lvl.flats[i].pos[c];
    bs_player p;
    bs_player_init(&p, start, 0);
    static const bs_player_stats stats = {.speed = 50, .strength = 50};
    bs_player_input in = {0};
    bs_player_events ev;
    for (int i = 0; i < 100; i++)
        bs_player_move(&p, &stats, &w, &in, 20, &ev);
    printf("  l1 start flat at %.0f %.0f %.0f; player stands at %.1f\n", (double)start[0], (double)start[1],
           (double)start[2], (double)p.pos[1]);
    BS_CHECK(p.on_ground && fabsf(p.pos[1]) < 0.5f);
    /* Walk from the start in eight directions for 3 s, then wait 4 s: every
     * walk ends standing on something (two of them drop into the pit beside
     * the start ramp, whose floor is at 1026), never falling out of the level. */
    for (int d = 0; d < 8; d++) {
        bs_player_init(&p, start, (float)d * 0.785398f);
        in.forward = 1;
        for (int i = 0; i < 150; i++)
            bs_player_move(&p, &stats, &w, &in, 20, &ev);
        in.forward = 0;
        for (int i = 0; i < 200; i++)
            bs_player_move(&p, &stats, &w, &in, 20, &ev);
        BS_CHECK(p.on_ground && p.pos[1] < 1100);
    }
    bs_arena_free(&a);
    bs_assets_close(assets);
}

/*
 * Every NODES file named in LEVELS.TXT parses, links included. Three
 * deathmatch levels have none; nodes.m1 serves both versions of Cf1.
 */
static void test_all_nodes(void)
{
    bs_assets *assets;
    BS_CHECK_EQ_INT(bs_assets_open(&vfs, &assets), BS_OK);
    bs_arena a;
    bs_arena_init(&a, 0);
    uint8_t *data;
    size_t n;
    bs_level_table table;
    BS_CHECK_EQ_INT(bs_vfs_read(&vfs, "GAMEDATA/LEVELS.TXT", &a, &data, &n), BS_OK);
    BS_CHECK_EQ_INT(bs_levels_parse(data, n, &a, &table), BS_OK);
    uint32_t files = 0, nodes = 0, links = 0;
    for (size_t i = 0; i < table.count; i++) {
        const char *name = table.levels[i].nodes;
        bool seen = !name[0];
        for (size_t k = 0; k < i && !seen; k++)
            seen = strcmp(table.levels[k].nodes, name) == 0;
        if (seen)
            continue;
        bs_nav_graph g;
        BS_CHECK_EQ_INT(bs_assets_read(assets, name, &a, &data, &n), BS_OK);
        bs_result r = bs_nodes_parse(data, n, &a, &g);
        if (r != BS_OK)
            fprintf(stderr, "  %s: %s\n", name, bs_result_str(r));
        BS_CHECK_EQ_INT(r, BS_OK);
        files++;
        nodes += g.count;
        for (uint32_t k = 0; k < g.count; k++)
            links += g.nodes[k].link_count;
    }
    BS_CHECK_EQ_INT(files, 13);
    BS_CHECK_EQ_INT(nodes, 16558);
    printf("  %u nodes, %u links\n", nodes, links);
    bs_arena_free(&a);
    bs_assets_close(assets);
}

/*
 * The creature tables in GAME.EXE (docs/formats/creatures.md): every creature's
 * images are there, and every campaign level's spawn points make valid monsters.
 */
static void test_creatures(void)
{
    bs_assets *assets;
    BS_CHECK_EQ_INT(bs_assets_open(&vfs, &assets), BS_OK);
    bs_arena a;
    bs_arena_init(&a, 0);
    uint8_t *data;
    size_t n;
    static bs_creature_tables t;
    BS_CHECK_EQ_INT(bs_vfs_read(&vfs, "GAME.EXE", &a, &data, &n), BS_OK);
    BS_CHECK_EQ_INT(bs_creature_tables_read(data, n, &t), BS_OK);
    BS_CHECK(strcmp(t.creatures[0].name, "Scamp") == 0);
    BS_CHECK(strcmp(t.creatures[15].name, "Daedra Lord") == 0);
    BS_CHECK_EQ_INT(t.creatures[0].health, 30);
    BS_CHECK_EQ_INT(t.creatures[0].attributes[6], 65);
    BS_CHECK_EQ_INT(t.creatures[4].weapon.damage_min, 12); /* Skeleton 12-18 */
    BS_CHECK_EQ_INT(t.creatures[4].weapon.damage_max, 18);
    BS_CHECK_EQ_INT(t.creatures[4].armour, 8);
    BS_CHECK_EQ_INT(t.creatures[0].unk_7c, 20);
    BS_CHECK_EQ_INT(t.creatures[5].unk_7c, 0); /* Ghosts aren't pushed */
    BS_CHECK_EQ_INT(t.level_creatures[1][1], 1);
    BS_CHECK_EQ_INT(t.level_creatures[8][3], 15);
    BS_CHECK_EQ_INT(t.sounds[0][0], 445);
    BS_CHECK_EQ_INT(t.sounds[15][3], 504);
    /* The casters' spells: Dremora Cause Damage, Spider Daedra Poison, Daedra Lord Continuous Damage and
     * Vampiric Drain or Cure Health and the three pools; the elements' tolerances. */
    BS_CHECK(t.creatures[2].spells[0][0] == 18 && t.creatures[2].spells[0][1] == 0 && !t.creatures[2].spells[1][0]);
    BS_CHECK(t.creatures[3].spells[0][0] == 10 && !t.creatures[0].spell_caster && t.creatures[13].spells[1][0] == 1);
    BS_CHECK(t.creatures[15].spells[0][0] == 9 && t.creatures[15].spells[0][1] == 12);
    BS_CHECK(t.creatures[15].spells[1][0] == 21 && t.creatures[15].spells[1][3] == 17 && !t.creatures[15].spells[1][4]);
    BS_CHECK(t.creatures[8].immune == bs_element_bit(BS_ELEMENT_FIRE));
    BS_CHECK(t.creatures[8].critical_weakness == bs_element_bit(BS_ELEMENT_COLD));
    BS_CHECK(t.creatures[3].immune == bs_element_bit(BS_ELEMENT_POISON) && t.creatures[15].resists == 0x1F);
    uint32_t images = 0;
    for (int c = 0; c < BS_CREATURE_COUNT; c++)
        for (int s = 0; s < BS_CREATURE_IMAGES; s++) {
            if (!t.creatures[c].images[s][0])
                continue;
            const bs_bsi_image *img = bs_assets_image(assets, t.creatures[c].images[s]);
            if (!img)
                fprintf(stderr, "  no %s\n", t.creatures[c].images[s]);
            BS_CHECK(img && img->frames >= 1 && img->bhdr[8] > 0);
            images++;
        }
    BS_CHECK_EQ_INT(images, 16 * 16);
    /* Every campaign level spawns. */
    BS_CHECK_EQ_INT(bs_vfs_read(&vfs, "GAMEDATA/LEVELS.TXT", &a, &data, &n), BS_OK);
    bs_level_table table;
    BS_CHECK_EQ_INT(bs_levels_parse(data, n, &a, &table), BS_OK);
    uint32_t total = 0;
    for (size_t i = 0; i < table.count; i++) {
        const bs_level_info *info = &table.levels[i];
        if (bs_level_game_type(info) != BS_GAME_CAMPAIGN)
            continue;
        bs_bs6_level lv;
        BS_CHECK_EQ_INT(bs_assets_read(assets, info->file, &a, &data, &n), BS_OK);
        BS_CHECK_EQ_INT(bs_bs6_parse(data, n, &a, &lv), BS_OK);
        bs_monsters ms;
        memset(&ms, 0, sizeof ms);
        BS_CHECK_EQ_INT(bs_monsters_find_points(&ms, &lv), BS_OK);
        bs_rng r = {(uint32_t)i + 1};
        BS_CHECK_EQ_INT(bs_monsters_spawn(&ms, &t, NULL, bs_level_number(info), 100, &r), BS_OK);
        BS_CHECK(ms.point_count > 40 && ms.count > 40 && ms.count <= ms.point_count);
        for (uint32_t k = 0; k < ms.count; k++)
            BS_CHECK(ms.list[k].type < BS_CREATURE_COUNT && ms.list[k].health > 0);
        printf("  %s: %u spawn points, %u monsters\n", info->file, ms.point_count, ms.count);
        total += ms.count;
        bs_monsters_free(&ms);
    }
    BS_CHECK(total > 400);
    bs_arena_free(&a);
    bs_assets_close(assets);
}

/*
 * The item tables in GAME.EXE, both enchantment lists, and every level's
 * items: all of them can be made (the game stops on one it can't).
 */
static void test_items(void)
{
    bs_assets *assets;
    BS_CHECK_EQ_INT(bs_assets_open(&vfs, &assets), BS_OK);
    bs_arena a;
    bs_arena_init(&a, 0);
    uint8_t *data;
    size_t n;
    static bs_item_tables tables;
    BS_CHECK_EQ_INT(bs_vfs_read(&vfs, "GAME.EXE", &a, &data, &n), BS_OK);
    BS_CHECK_EQ_INT(bs_item_tables_read(data, n, &tables), BS_OK);
    BS_CHECK(strcmp(tables.bases[0].name, "Dagger") == 0);
    BS_CHECK(strcmp(tables.bases[42].name, "Coffer of Restoration") == 0);
    BS_CHECK(tables.bases[16].container && tables.bases[17].container && tables.bases[18].container &&
             tables.bases[42].container && !tables.bases[15].container);
    BS_CHECK_EQ_INT(tables.weapons[3].damage_min, 4); /* Broad Sword 4-12, Long Blade */
    BS_CHECK_EQ_INT(tables.weapons[3].damage_max, 12);
    BS_CHECK_EQ_INT(tables.weapons[3].skill, 15);
    BS_CHECK(strcmp(tables.materials[9], "Daedric") == 0);
    static bs_spell spells[BS_SPELL_COUNT];
    BS_CHECK_EQ_INT(bs_spells_read(data, n, spells), BS_OK);
    BS_CHECK(strcmp(spells[0].name, "Monster Summoning") == 0 && strcmp(spells[20].name, "Cure Health") == 0);
    BS_CHECK(strcmp(spells[30].name, "Dispel Sigil") == 0);
    BS_CHECK(spells[20].magnitude_min == 2 && spells[20].magnitude_max == 20 && spells[7].duration_base == 5);
    /* Schools' skills: Cause Damage Destruction (8), Cure Health Restoration (9), Summoning Thaumaturgy (12);
     * hostile spells are touch by default, Teleport on oneself only. */
    BS_CHECK(spells[17].skill == 8 && spells[20].skill == 9 && spells[0].skill == 12);
    BS_CHECK(spells[17].delivery == BS_DELIVERY_TOUCH && spells[17].target == BS_TARGET_OTHER);
    BS_CHECK(spells[24].target == BS_TARGET_SELF && spells[20].delivery == BS_DELIVERY_CASTER);
    BS_CHECK(spells[8].icon == 8 && spells[17].cost == 60 && spells[0].cost == 50);
    for (int i = 0; i < BS_SPELL_COUNT; i++)
        BS_CHECK(spells[i].element < BS_ELEMENT_COUNT && spells[i].delivery < BS_DELIVERY_COUNT &&
                 spells[i].detonation >= 0 && spells[i].detonation < BS_DETONATION_COUNT && spells[i].skill >= 8 &&
                 spells[i].skill <= 13 && (spells[i].icon < 27 || spells[i].icon >= 200)); /* 200+: unused spells */
    /* The model names, through the loaded image (pointers). */
    bs_le_image img;
    BS_CHECK_EQ_INT(bs_le_load(data, n, &a, &img), BS_OK);
    BS_CHECK_EQ_INT(bs_item_models_read(&img, &tables), BS_OK);
    BS_CHECK(strcmp(tables.models[0], "bataxe.3d") == 0 && strcmp(tables.models[39], "arrow.3d") == 0);
    BS_CHECK(strcmp(tables.models[11], "bow.3d") == 0 && strcmp(tables.models[19], "bow.3d") == 0);
    char mbuf[16];
    BS_CHECK(strcmp(bs_item_model_name(&tables, 0, 0, mbuf), "dagger.3d") == 0);
    BS_CHECK(strcmp(bs_item_model_name(&tables, 3, 0, mbuf), "brdsword.3d") == 0);
    /* Every item's model exists. */
    for (uint32_t b = 0; b < BS_ITEM_BASE_COUNT; b++) {
        bs_item_model_name(&tables, b, 0, mbuf);
        if (!bs_assets_model(assets, mbuf))
            fprintf(stderr, "  base %u: no model '%s'\n", b, mbuf);
        BS_CHECK(mbuf[0] && bs_assets_model(assets, mbuf) != NULL);
    }
    /* The weapons in hand (0xE34AD): every one's model exists. */
    BS_CHECK_EQ_INT(bs_item_hand_models_read(&img, &tables), BS_OK);
    BS_CHECK(strcmp(tables.hand_models[0], "edagger.3d") == 0 && strcmp(tables.hand_models[14], "espear.3d") == 0);
    BS_CHECK(strcmp(tables.hand_models[1], "eloswrd.3d") == 0 && strcmp(tables.hand_models[9], "ebow.3d") == 0);
    for (uint32_t b = 0; b <= BS_ITEM_LAST_WEAPON; b++)
        BS_CHECK(bs_assets_model(assets, tables.hand_models[b]) != NULL);
    BS_CHECK(bs_assets_model(assets, "earrow.3d") != NULL); /* an arrow in flight (0x48A26) */
    /* The automap's table (0xF1083): every map picture exists; level 3 has two. */
    static bs_automap_table maps;
    BS_CHECK_EQ_INT(bs_automap_table_read(data, n, &maps), BS_OK);
    BS_CHECK_EQ_INT(maps.count, BS_AUTOMAP_MAPS);
    BS_CHECK(strcmp(maps.maps[0].image, "map000.bsi") == 0 && strcmp(maps.maps[28].level, "yaga.bs6") == 0);
    BS_CHECK(maps.maps[0].world[0] == -5921 && maps.maps[0].screen[3] == 468 && maps.maps[0].shift[1] == -1);
    BS_CHECK_EQ_INT(bs_automap_find(&maps, "L1.BS6", 0), 0);
    BS_CHECK_EQ_INT(bs_automap_find(&maps, "l3.bs6", 0), 2);
    BS_CHECK_EQ_INT(bs_automap_find(&maps, "l3.bs6", 800), 3);
    BS_CHECK_EQ_INT(bs_automap_find(&maps, "l7.bs6", 0), -1);
    for (uint32_t i = 0; i < maps.count; i++)
        BS_CHECK(bs_assets_image(assets, maps.maps[i].image) != NULL);
    /* The character sheet's skill names (0xE34E9) and attributes' short names (0xE8B15). */
    bs_exe_text names;
    BS_CHECK_EQ_INT(bs_exe_string_table(&img, "Medical", 21, &a, &names), BS_OK);
    BS_CHECK(names.count >= 21 && strcmp(names.messages[20], "Critical Strike") == 0);
    BS_CHECK_EQ_INT(bs_exe_string_table(&img, "STR", 8, &a, &names), BS_OK);
    BS_CHECK(names.count >= 8 && strcmp(names.messages[7], "LCK") == 0);
    /* The screens' pictures. */
    static const char *const screens[] = {"cscr00.bsi", "map030.bsi", "options.bsi", "optnon00.bsi", "optn07.bsi",
                                          "optn08.bsi", "sure00.bsi", "hyes00.bsi", "hno000.bsi"};
    for (size_t i = 0; i < sizeof screens / sizeof *screens; i++)
        BS_CHECK(bs_assets_image(assets, screens[i]) != NULL);
    for (int i = 0; i <= 22; i++) {
        char hn[16];
        snprintf(hn, sizeof hn, "help%02d.bsi", i);
        BS_CHECK(bs_assets_image(assets, hn) != NULL);
    }
    for (int32_t sg = 9001; sg <= 9052; sg++)
        BS_CHECK(bs_assets_model(assets, bs_item_model_name(&tables, BS_ITEM_SIGIL_AMULET, sg, mbuf)) != NULL);

    bs_magic_table mg[2];
    static const char *const files[2] = {"MG0_GEN.TXT", "MG2_SPC.TXT"};
    for (int i = 0; i < 2; i++) {
        BS_CHECK_EQ_INT(bs_assets_read(assets, files[i], &a, &data, &n), BS_OK);
        BS_CHECK_EQ_INT(bs_magic_parse(data, n, &a, &mg[i]), BS_OK);
    }
    BS_CHECK_EQ_INT(mg[0].count, 212);
    BS_CHECK_EQ_INT(mg[1].count, 232);
    bs_item_db db = {.tables = &tables, .magic = {&mg[0], &mg[1]}};
    const bs_magic *reiver = bs_item_db_magic(&db, 8011);
    BS_CHECK(reiver && strcmp(reiver->name, "of the Moon Reiver") == 0);
    bs_item sword = {.base = 3, .material = 0, .magic = 8011, .quantity = 1};
    char name[96];
    bs_item_name(&db, &sword, name, sizeof name);
    BS_CHECK(strcmp(name, "Iron Broad Sword of the Moon Reiver") == 0);

    /* Worn items' pictures: every BSI name. */
    bs_bsa *bsi;
    BS_CHECK_EQ_INT(bs_bsa_open_vfs(&vfs, "GAMEDATA/BSI.BSA", &bsi), BS_OK);
    const char **pics = BS_ARENA_ARRAY(&a, const char *, bsi->count);
    for (uint32_t i = 0; i < bsi->count; i++) {
        char *pic = bs_arena_strdup(&a, bsi->records[i].name);
        char *dot = strrchr(pic, '.');
        if (dot)
            *dot = '\0';
        pics[i] = pic;
    }
    db.pictures = pics;
    db.picture_count = bsi->count;

    bs_level_table table;
    BS_CHECK_EQ_INT(bs_vfs_read(&vfs, "GAMEDATA/LEVELS.TXT", &a, &data, &n), BS_OK);
    BS_CHECK_EQ_INT(bs_levels_parse(data, n, &a, &table), BS_OK);
    uint32_t containers = 0, items = 0, specs = 0, placed = 0, pictured = 0, unpictured = 0, books = 0;
    bs_bsa *txt;
    BS_CHECK_EQ_INT(bs_bsa_open_vfs(&vfs, "GAMEDATA/TXT.BSA", &txt), BS_OK);
    for (size_t i = 0; i < table.count; i++) {
        bs_bs6_level lvl;
        BS_CHECK_EQ_INT(bs_assets_read(assets, table.levels[i].file, &a, &data, &n), BS_OK);
        BS_CHECK_EQ_INT(bs_bs6_parse(data, n, &a, &lvl), BS_OK);
        bs_items li = {0};
        bs_rng r = {(uint32_t)i + 1};
        bool mp = bs_level_game_type(&table.levels[i]) != BS_GAME_CAMPAIGN;
        BS_CHECK_EQ_INT(bs_items_add_level(&li, &lvl, &db, bs_level_number(&table.levels[i]), mp,
                                           table.levels[i].grid_cell, &r),
                        BS_OK);
        items += li.count;
        containers += li.container_count;
        for (uint32_t k = 0; k < li.count; k++) {
            placed += li.items[k].place.where == BS_ITEM_FLOOR;
            const bs_item *x = &li.items[k].item;
            /* Potions cast spells 1-31, or are Restoration (40) or Water Breathing (39). */
            if (x->base == BS_ITEM_POTION)
                BS_CHECK((x->number >= 1 && x->number <= BS_SPELL_COUNT) || x->number == 39 || x->number == 40);
            /* Books and scrolls have their text: bk?_NNN.txt. */
            if (x->base == BS_ITEM_BOOK || x->base == BS_ITEM_SCROLL) {
                bool found = false;
                char suffix[16];
                snprintf(suffix, sizeof suffix, "%03d.TXT", x->number);
                for (uint32_t t = 0; t < txt->count && !found; t++)
                    found = strlen(txt->records[t].name) == 11 && bs_strcasecmp(txt->records[t].name + 4, suffix) == 0 &&
                            (txt->records[t].name[0] | 0x20) == 'b' && (txt->records[t].name[1] | 0x20) == 'k';
                if (!found)
                    fprintf(stderr, "  %s: no text for book %d\n", table.levels[i].file, x->number);
                books += found;
            }
            /* Items with an icon show a picture that exists (slings have none). */
            if (!tables.bases[x->base].icon[0])
                continue;
            char pic[16];
            snprintf(pic, sizeof pic, "%s.BSI", x->picture);
            if (x->picture[0] && bs_bsa_find(bsi, pic))
                pictured++;
            else if (x->base != 12) {
                unpictured++;
                fprintf(stderr, "  %s: base %u has no picture '%s'\n", table.levels[i].file, x->base, x->picture);
            }
        }
        /* Every placement made an item. */
        for (uint32_t k = 0; k < lvl.object_count; k++) {
            bs_bs6_logic lg;
            BS_CHECK_EQ_INT(bs_bs6_entity_logic(&lvl.objects[k].chunk, &a, &lg), BS_OK);
            for (uint32_t p = 0; p < lg.prop_count; p++)
                specs += lg.props[p].id == 105 || lg.props[p].id == 107 || lg.props[p].id == 115 ||
                         lg.props[p].id == 116;
        }
        if (strcmp(table.levels[i].file, "l1.bs6") == 0) {
            /* A level 1 chest (#330): magic gauntlets, a helmet and a magic spear. */
            uint32_t chest = UINT32_MAX, list[8];
            for (uint32_t k = 0; k < lvl.object_count; k++)
                if (lvl.objects[k].id == 330)
                    chest = k;
            BS_CHECK(chest != UINT32_MAX && bs_items_is_container_object(&li, chest));
            BS_CHECK_EQ_INT(bs_items_list(&li, (bs_item_place){BS_ITEM_IN_OBJECT, chest}, list, 8), 3);
            BS_CHECK(li.items[list[0]].item.base == 29 && li.items[list[0]].item.magic == 9265);
            BS_CHECK(li.items[list[1]].item.base == 24 && li.items[list[1]].item.magic == 0);
            BS_CHECK(li.items[list[2]].item.base == 14 && li.items[list[2]].item.magic == 9242);
        }
        bs_items_free(&li);
    }
    BS_CHECK_EQ_INT(items, specs);
    BS_CHECK_EQ_INT(containers, 115);
    printf("  %u items: %u on the floor, the rest in %u containers; %u with pictures\n", items, placed, containers,
           pictured);
    BS_CHECK_EQ_INT(unpictured, 0);
    printf("  %u books and scrolls with texts\n", books);
    bs_bsa_close(txt);
    BS_CHECK(pictured > 100);
    bs_bsa_close(bsi);
    bs_arena_free(&a);
    bs_assets_close(assets);
}

/*
 * Every sound effect and voice clip is an 8-bit mono PCM WAV. Voices are all
 * 11025 Hz; effects use 11025 Hz (280), 32000 Hz (89) or 8114 Hz (1).
 */
/* The 18 classes (GAMEDATA/CLASSnnn.SCL): each reads, with twelve class skills and its spells and items. */
/* What a class cost, made again from a new one with bs_creation. */
static int32_t class_cost(const bs_class *k)
{
    static bs_creation c;
    bs_creation_new(&c);
    c.points += 1000000;
    int32_t start = c.points + 300;
    for (int a = 0; a < 8; a++)
        while (c.cls.attributes[a] != k->attributes[a])
            if (bs_creation_attribute(&c, a, c.cls.attributes[a] < k->attributes[a]) != BS_CREATE_OK)
                return -1;
    while (c.cls.wounds < k->wounds)
        bs_creation_wounds(&c, true);
    for (int s = 0; s < BS_SCL_CLASS_SKILLS; s++) {
        if (!bs_creation_set_skill(&c, s, k->class_skills[s]))
            return -1;
        int32_t *v = &c.cls.skills[k->class_skills[s]];
        while (*v != k->skills[k->class_skills[s]])
            if (bs_creation_skill(&c, s, *v < k->skills[k->class_skills[s]]) != BS_CREATE_OK)
                return -1;
    }
    while (c.cls.magery < k->magery)
        bs_creation_magery(&c, true);
    for (int e = 0; e < 5; e++) {
        int bit = 1 << e;
        if (k->resists & bit)
            bs_creation_tolerance(&c, BS_TOL_RESISTANCE, bit);
        if (k->immune & bit)
            bs_creation_tolerance(&c, BS_TOL_IMMUNITY, bit);
        if (k->low_tolerance & bit)
            bs_creation_tolerance(&c, BS_TOL_LOW, bit);
        if (k->critical_weakness & bit)
            bs_creation_tolerance(&c, BS_TOL_CRITICAL, bit);
        if (k->forbidden_weapons & bit)
            bs_creation_forbid_weapon(&c, (uint8_t)bit);
    }
    const bool flags[BS_CREATION_FLAGS] = {k->acute_hearing, k->spell_absorption, k->rapid_healing,
                                           k->regenerate_health, k->regenerate_sp, k->athleticism,
                                           k->adrenaline_rush, k->no_sp_regen, k->forbidden_shield};
    for (int f = 0; f < BS_CREATION_FLAGS; f++)
        if (flags[f])
            bs_creation_toggle(&c, (bs_creation_flag)f);
    while (c.cls.max_armour > k->max_armour)
        bs_creation_armour(&c, false);
    while (c.cls.max_material > k->max_material)
        bs_creation_material(&c, false);
    for (uint32_t i = 0; i < k->spell_count; i++)
        bs_creation_add_spell(&c, &k->spells[i]);
    static bs_creation_shop equipment, magic;
    memset(&equipment, 0, sizeof equipment);
    memset(&magic, 0, sizeof magic);
    magic.magic = true;
    bs_item others[BS_SCL_MAX_ITEMS];
    bs_creation_class_items(k, false, &equipment, &magic, others, BS_SCL_MAX_ITEMS);
    int32_t items = 0;
    for (uint32_t i = 0; i < equipment.count; i++)
        items += bs_creation_shop_cost(&equipment, i);
    for (uint32_t i = 0; i < magic.count; i++)
        items += bs_creation_shop_cost(&magic, i);
    return start - c.points + items;
}

static void test_classes(void)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    static bs_class c;
    int consistent = 0;
    for (int i = 0; i < 18; i++) {
        char path[48];
        snprintf(path, sizeof path, "GAMEDATA/CLASS%03d.SCL", i);
        uint8_t *data;
        size_t n;
        BS_CHECK_EQ_INT(bs_vfs_read(&vfs, path, &a, &data, &n), BS_OK);
        BS_CHECK_EQ_INT(bs_class_parse(data, n, &c), BS_OK);
        BS_CHECK(c.class_name[0] != '\0' && c.level == 1);
        for (int k = 0; k < BS_SCL_CLASS_SKILLS; k++)
            BS_CHECK(c.class_skills[k] < BS_SCL_SKILLS && c.class_skills[k] != 0 && c.class_skills[k] != 2 &&
                     c.class_skills[k] != 7);
        BS_CHECK(c.spell_count >= 1 && c.item_count >= 1);
        /* Seven come to 4980 with what they have left (docs/formats/scl.md). */
        int32_t cost = class_cost(&c);
        BS_CHECK(cost > 3000);
        consistent += cost + c.points_left == 4980;
        BS_CHECK(c.magery == 0x100 || c.magery == 0x180 || c.magery == 0x200 || c.magery == 0x300);
        for (uint32_t k = 0; k < c.spell_count; k++)
            BS_CHECK(c.spells[k].effect >= 1 && c.spells[k].effect <= BS_SPELL_COUNT);
        for (uint32_t k = 0; k < c.item_count; k++)
            BS_CHECK(c.items[k].base < 43 && (c.items[k].flags & 0x10));
        if (i == 0) {
            /* The Acrobat: Strength and Speed 60, wounds 75, Blunt Weapon 60, Athleticism, no axes, light armour. */
            BS_CHECK(strcmp(c.class_name, "Acrobat") == 0);
            BS_CHECK(c.attributes[0] == 60 && c.attributes[6] == 60 && c.wounds == 75 && c.skills[18] == 60);
            BS_CHECK(c.class_skills[0] == 1 && c.class_skills[1] == 6 && c.class_skills[2] == 18);
            BS_CHECK(c.athleticism && c.adrenaline_rush && c.forbidden_weapons == 8 && c.max_armour == 1);
            BS_CHECK(c.spell_count == 2 && c.spells[0].effect == 21 && c.spells[1].effect == 22);
            BS_CHECK(c.item_count == 8 && c.items[3].base == 22 && c.items[3].quantity == 25);
        }
    }
    BS_CHECK_EQ_INT(consistent, 7);
    bs_arena_free(&a);
}

/* Saved games in the install's SAVE0-9, if any (the user's own): every file reads; the player is there;
 * parents resolve; the level's controllers take up the rest of the tree exactly, in our chain order. */
static void test_saves(void)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    uint8_t *data;
    size_t n;
    bs_level_table table;
    bs_assets *assets;
    BS_CHECK_EQ_INT(bs_assets_open(&vfs, &assets), BS_OK);
    BS_CHECK_EQ_INT(bs_vfs_read(&vfs, "GAMEDATA/LEVELS.TXT", &a, &data, &n), BS_OK);
    BS_CHECK_EQ_INT(bs_levels_parse(data, n, &a, &table), BS_OK);
    uint8_t *exe;
    size_t exe_size;
    static bs_automap_table maps;
    static bs_spell spells[BS_SPELL_COUNT];
    BS_CHECK_EQ_INT(bs_vfs_read(&vfs, "GAME.EXE", &a, &exe, &exe_size), BS_OK);
    BS_CHECK_EQ_INT(bs_automap_table_read(exe, exe_size, &maps), BS_OK);
    BS_CHECK_EQ_INT(bs_spells_read(exe, exe_size, spells), BS_OK);
    int found = 0;
    for (int slot = 0; slot < 10; slot++) {
        char path[1024];
        uint8_t *tree_data, *vars_data;
        size_t tree_size, vars_size;
        snprintf(path, sizeof path, "%s/SAVE%d/SAVETREE.DAT", data_dir, slot);
        if (bs_fs_read_file(path, &a, &tree_data, &tree_size) != BS_OK)
            continue;
        snprintf(path, sizeof path, "%s/SAVE%d/SAVEVARS.DAT", data_dir, slot);
        BS_CHECK_EQ_INT(bs_fs_read_file(path, &a, &vars_data, &vars_size), BS_OK);
        found++;
        bs_save_tree t;
        static bs_save_vars v;
        BS_CHECK_EQ_INT(bs_save_tree_parse(tree_data, tree_size, &a, &t), BS_OK);
        BS_CHECK_EQ_INT(bs_save_vars_parse(vars_data, vars_size, &v), BS_OK);
        BS_CHECK_EQ_INT(t.version, BS_SAVE_VERSION);
        int32_t player = bs_save_find(&t, BS_SAVE_PLAYER_ID);
        BS_CHECK(player >= 0 && t.records[player].type == BS_SAVE_PLAYER &&
                 t.records[player].parent == BS_SAVE_PARENT_ROOT);
        /* The campaign level's controller chains, in the logic's order. */
        const bs_level_info *info = NULL;
        for (size_t i = 0; i < table.count && !info; i++)
            if (bs_level_game_type(&table.levels[i]) == BS_GAME_CAMPAIGN &&
                bs_level_number(&table.levels[i]) == v.level_number)
                info = &table.levels[i];
        BS_CHECK(info != NULL);
        if (!info)
            continue;
        bs_bs6_level lvl;
        bs_level_logic lg;
        BS_CHECK_EQ_INT(bs_assets_read(assets, info->file, &a, &data, &n), BS_OK);
        BS_CHECK_EQ_INT(bs_bs6_parse(data, n, &a, &lvl), BS_OK);
        BS_CHECK_EQ_INT(bs_level_logic_build(&lvl, &a, &lg), BS_OK);
        uint32_t *chains = BS_ARENA_ARRAY(&a, uint32_t, lg.object_count + 1), *ids =
            BS_ARENA_ARRAY(&a, uint32_t, lg.object_count + 1), nch = 0;
        for (uint32_t i = 0; chains && ids && i < lg.object_count; i++)
            if (lg.objects[i].ctrl_count) {
                ids[nch] = lg.objects[i].entity_id;
                chains[nch++] = lg.objects[i].ctrl_count;
            }
        /* Parents not saved are the level's own objects (chests), by their BS6 ids. */
        for (uint32_t i = 0; i < t.count; i++) {
            const bs_save_record *r = &t.records[i];
            if (r->parent <= BS_SAVE_PARENT_LEVEL || r->parent_index >= 0 || r->reserve)
                continue;
            bool level_object = false;
            for (uint32_t k = 0; k < lg.object_count && !level_object; k++)
                level_object = lg.objects[k].entity_id == r->parent;
            BS_CHECK(level_object);
        }
        /* Opened for editing and written untouched, both files come back as they were. */
        static bs_save_edit ed;
        BS_CHECK_EQ_INT(bs_save_edit_open(&ed, tree_data, tree_size, vars_data, vars_size), BS_OK);
        uint8_t *again;
        size_t again_size;
        BS_CHECK_EQ_INT(bs_save_edit_write(&ed, &again, &again_size), BS_OK);
        BS_CHECK(again_size == tree_size && memcmp(again, tree_data, tree_size) == 0);
        BS_CHECK(memcmp(ed.vars, vars_data, BS_SAVE_VARS_SIZE) == 0);
        free(again);
        bs_save_edit_free(&ed);
        /* Imported: every item somewhere the level has, every saved model one of the level's, the player on
         * a cell of the automap that was seen (the grid row by row, as the engine keeps it). */
        static bs_saved_game sg;
        BS_CHECK_EQ_INT(bs_save_import(&t, &v, spells, &a, &sg), BS_OK);
        BS_CHECK(sg.name[0] != '\0' && sg.character.health_max > 0 && sg.character.spells_known != 0);
        uint32_t carried = 0;
        for (uint32_t i = 0; i < sg.item_count; i++) {
            const bs_saved_item *it = &sg.items[i];
            carried += it->place == BS_SAVED_CARRIED;
            bool known = it->place != BS_SAVED_IN_OBJECT;
            for (uint32_t k = 0; k < lvl.object_count && !known; k++)
                known = lvl.objects[k].id == it->ref;
            BS_CHECK(known);
            BS_CHECK(it->place != BS_SAVED_IN_ITEM || it->ref < sg.item_count);
            BS_CHECK(it->place != BS_SAVED_MONSTER || it->ref < sg.monster_count);
        }
        BS_CHECK(carried > 0);
        for (uint32_t i = 0; i < sg.object_count; i++) {
            bool known = false;
            for (uint32_t k = 0; k < lvl.object_count && !known; k++)
                known = lvl.objects[k].id == sg.objects[i].id;
            BS_CHECK(known);
        }
        for (uint32_t i = 0; i < sg.monster_count; i++)
            BS_CHECK(sg.monsters[i].type < BS_CREATURE_COUNT && sg.monsters[i].health > 0);
        /* As our text and back: the same game. */
        {
            char *text;
            size_t text_size;
            static bs_saved_game back;
            char err[BS_GAME_SAVE_ERROR];
            BS_CHECK_EQ_INT(bs_game_save_write(&sg, NULL, &text, &text_size), BS_OK);
            BS_CHECK_EQ_INT(bs_game_save_read(text, text_size, spells, NULL, &a, &back, err), BS_OK);
            BS_CHECK(back.item_count == sg.item_count && back.monster_count == sg.monster_count &&
                     back.object_count == sg.object_count);
            for (uint32_t i = 0; i < sg.item_count && i < back.item_count; i++)
                BS_CHECK(memcmp(&back.items[i].item, &sg.items[i].item, sizeof(bs_item)) == 0 &&
                         back.items[i].place == sg.items[i].place && back.items[i].ref == sg.items[i].ref);
            for (uint32_t i = 0; i < sg.monster_count && i < back.monster_count; i++) {
                /* Positions are kept to a thousandth. */
                bs_saved_monster m = back.monsters[i];
                for (int c = 0; c < 3; c++) {
                    BS_CHECK(fabsf(m.pos[c] - sg.monsters[i].pos[c]) <= 0.0005f);
                    m.pos[c] = sg.monsters[i].pos[c];
                }
                BS_CHECK(memcmp(&m, &sg.monsters[i], sizeof m) == 0);
            }
            BS_CHECK(memcmp(back.seen, sg.seen, sizeof back.seen) == 0);
            BS_CHECK(memcmp(back.character.attributes, sg.character.attributes, sizeof back.character.attributes) == 0);
            BS_CHECK(memcmp(back.character.skills, sg.character.skills, sizeof back.character.skills) == 0);
            BS_CHECK_EQ_INT(back.character.spells_known, sg.character.spells_known);
            BS_CHECK(memcmp(back.character.book, sg.character.book, sizeof back.character.book) == 0);
            BS_CHECK_EQ_INT(back.dialogue.global_count, sg.dialogue.global_count);
            /* Speakers' variables, by id (the game fills its table from the end; slots don't matter). */
            for (int l = 0; l < BS_DIALOGUE_LOCALS; l++) {
                if (!sg.dialogue.locals[l].id)
                    continue;
                bool same = false;
                for (int m = 0; m < BS_DIALOGUE_LOCALS && !same; m++)
                    same = back.dialogue.locals[m].id == sg.dialogue.locals[l].id;
                BS_CHECK(same);
            }
            free(text);
        }
        int map = bs_automap_find(&maps, info->file, sg.pos[1]);
        uint32_t seen = 0;
        for (uint32_t k = 0; k < BS_SAVE_AUTOMAP_CELLS; k++)
            seen += sg.seen[k];
        if (map >= 0 && seen) {
            int sx, sy;
            bs_automap_point(&maps.maps[map], sg.pos[0], sg.pos[2], &sx, &sy);
            BS_CHECK(sg.seen[(sy / 16) * 40 + sx / 16]);
        }
        bool ours = true; /* saved in this level, not one of its interiors */
        for (uint32_t i = 0; i < t.count; i++)
            if (t.records[i].type == BS_SAVE_OBJECT && t.records[i].link2 >= 0)
                ours = ours && (uint32_t)t.records[i].link2 < nch && ids[t.records[i].link2] == t.records[i].id;
        if (!ours) {
            fprintf(stderr, "  SAVE%d: not in %s itself; controllers not checked\n", slot, info->file);
            continue;
        }
        bs_save_controller *ctl;
        uint32_t nc, na;
        uint8_t *active;
        BS_CHECK_EQ_INT(bs_save_controllers_read(t.controls, t.controls_size, chains, nch, &a, &ctl, &nc, &active, &na),
                        BS_OK);
        size_t used = 4 + na;
        for (uint32_t i = 0; i < nc; i++)
            used += (ctl[i].flags & 1) ? 45 : 1;
        BS_CHECK_EQ_INT(used, t.controls_size);
        for (uint32_t i = 0; i < na; i++)
            BS_CHECK(active[i] < nch);
    }
    if (found)
        fprintf(stderr, "  %d saved games checked\n", found);
    bs_assets_close(assets);
    bs_arena_free(&a);
}

/* Every conversation in TXT.BSA: it opens for both sexes on every visit, and every line it can reach
 * through replies is in its script. */
static void test_dialogue_tables(void)
{
    bs_bsa *bsa;
    BS_CHECK_EQ_INT(bs_bsa_open_vfs(&vfs, "GAMEDATA/TXT.BSA", &bsa), BS_OK);
    if (!bsa)
        return;
    bs_arena a;
    bs_arena_init(&a, 0);
    const char **names = BS_ARENA_ARRAY(&a, const char *, bsa->count);
    for (uint32_t i = 0; i < bsa->count; i++)
        names[i] = bsa->records[i].name;
    static bs_dialogue_state s;
    static bs_dialogue d;
    int stems = 0, opened = 0, messages = 0, nothing = 0, lines = 0, missing_goto = 0;
    for (uint32_t i = 0; i < bsa->count; i++) {
        const char *nm = names[i];
        if (strlen(nm) != 8 || (nm[3] != 'T' && nm[3] != 't') || strcmp(nm + 4, ".TXT") != 0)
            continue;
        char stem[4] = {nm[0], nm[1], nm[2], 0};
        for (int female = 0; female < 2; female++) {
            int32_t gi, ti;
            bs_dialogue_files(names, bsa->count, stem, female, &gi, &ti);
            if (gi < 0 || ti < 0)
                continue;
            stems += !female;
            uint8_t *data;
            size_t n;
            bs_talk_file group, talk;
            BS_CHECK_EQ_INT(bs_bsa_read(bsa, &bsa->records[gi], &a, &data, &n), BS_OK);
            BS_CHECK_EQ_INT(bs_talk_parse(data, n, &a, &group), BS_OK);
            BS_CHECK_EQ_INT(bs_bsa_read(bsa, &bsa->records[ti], &a, &data, &n), BS_OK);
            BS_CHECK_EQ_INT(bs_talk_parse(data, n, &a, &talk), BS_OK);
            bs_dialogue_state_init(&s);
            bs_dialogue_update_vars(&s, female, NULL, NULL);
            bs_rng r = {(uint32_t)(i * 7 + (uint32_t)female + 1)};
            for (int visit = 0; visit < 6; visit++) {
                char msg[512];
                bs_dialogue_opening o = bs_dialogue_start(&d, &s, &group, &talk, 1000 + i, 0, &r, msg, sizeof msg);
                opened += o == BS_DIALOGUE_TALK;
                messages += o == BS_DIALOGUE_MESSAGE;
                nothing += o == BS_DIALOGUE_NOTHING;
            }
            /* Every line's replies lead somewhere. */
            for (uint32_t row = 2; row < talk.count; row++) {
                const char *code = bs_talk_cell(&talk, row, 1);
                if (strlen(code) < 5)
                    continue;
                lines++;
                d.say_row = (int32_t)row;
                bs_dialogue_update_replies(&d);
                for (uint32_t k = 0; k < d.reply_count; k++) {
                    const char *to = bs_talk_cell(&talk, d.replies[k], 8);
                    if (!to[0] || strchr(to, ':') || bs_talk_prefix(to, "end", 3))
                        continue;
                    if (bs_talk_find(&talk, 0, 1, to) < 0) {
                        missing_goto++;
                        fprintf(stderr, "  %s: %s goes to %s, which isn't there\n", nm, code, to);
                    }
                }
            }
        }
    }
    fprintf(stderr, "  %d stems, %d lines; %d openings, %d messages, %d with nothing; %d gotos missing\n", stems,
            lines, opened, messages, nothing, missing_goto);
    /* Some tables leave later visits blank or write "Random" for "GotoRandom", and two are tests (TA1, TF1):
     * those open nothing. */
    BS_CHECK(stems > 70);
    BS_CHECK(opened > 550);
    BS_CHECK(nothing < 60);
    BS_CHECK_EQ_INT(missing_goto, 0);
    bs_arena_free(&a);
    bs_bsa_close(bsa);
}

static void test_all_sounds(void)
{
    static const struct {
        const char *archive;
        uint32_t count, at_11025, at_32000, at_8114;
    } archives[] = {{"GAMEDATA/SPIRE.SND", 370, 280, 89, 1}, {"WAVES.BSA", 775, 775, 0, 0}};
    for (size_t k = 0; k < sizeof archives / sizeof archives[0]; k++) {
        bs_bsa *bsa;
        BS_CHECK_EQ_INT(bs_bsa_open_vfs(&vfs, archives[k].archive, &bsa), BS_OK);
        if (!bsa)
            continue;
        uint32_t ok = 0, wavs = 0, r11 = 0, r32 = 0, r8 = 0;
        bs_arena a;
        bs_arena_init(&a, 0);
        for (uint32_t i = 0; i < bsa->count; i++) {
            const bs_bsa_record *rec = &bsa->records[i];
            if (rec->name[0] && !ends_with(rec->name, ".WAV"))
                continue;
            wavs++;
            uint8_t *data;
            size_t n;
            bs_wav w;
            if (bs_bsa_read(bsa, rec, &a, &data, &n) == BS_OK && bs_wav_parse(data, n, &w) == BS_OK &&
                w.bits == 8 && w.channels == 1 && w.frame_count > 0) {
                ok++;
                r11 += w.rate == 11025;
                r32 += w.rate == 32000;
                r8 += w.rate == 8114;
            }
            bs_arena_reset(&a);
        }
        BS_CHECK_EQ_INT(wavs, archives[k].count);
        BS_CHECK_EQ_INT(ok, archives[k].count);
        BS_CHECK_EQ_INT(r11, archives[k].at_11025);
        BS_CHECK_EQ_INT(r32, archives[k].at_32000);
        BS_CHECK_EQ_INT(r8, archives[k].at_8114);
        bs_arena_free(&a);
        bs_bsa_close(bsa);
    }
}

/* Every video decodes completely, with a soundtrack as long as the video. */
static void test_all_videos(void)
{
    static const struct {
        const char *name;
        uint32_t frames;
    } videos[] = {{"STORY.SMK", 2371}, {"MALE.SMK", 1791}, {"FEMALE.SMK", 1791},
                  {"ANCHORS.SMK", 101}, {"CHOP2.SMK", 80},   {"JUMP.SMK", 46}};
    for (size_t k = 0; k < sizeof videos / sizeof videos[0]; k++) {
        char path[64];
        snprintf(path, sizeof path, "VIDEOS/%s", videos[k].name);
        bs_arena a;
        bs_arena_init(&a, 0);
        uint8_t *data;
        size_t n;
        bs_smacker s;
        BS_CHECK_EQ_INT(bs_vfs_read(&vfs, path, &a, &data, &n), BS_OK);
        BS_CHECK_EQ_INT(bs_smk_open(data, n, &a, &s), BS_OK);
        BS_CHECK_EQ_INT(s.frames, videos[k].frames);
        uint64_t audio = 0;
        bs_result r;
        while ((r = bs_smk_next_frame(&s)) == BS_OK)
            audio += s.audio_bytes[0];
        BS_CHECK_EQ_INT(r, BS_ERR_NOT_FOUND);
        BS_CHECK_EQ_INT(s.next, videos[k].frames);
        if (s.tracks[0].present) {
            double audio_ms = (double)audio / s.tracks[0].rate * 1000.0; /* all tracks are 8-bit mono */
            double video_ms = s.frames * s.frame_ms;
            BS_CHECK(audio_ms > video_ms - s.frame_ms && audio_ms < video_ms + s.frame_ms);
        }
        bs_smk_close(&s);
        bs_arena_free(&a);
    }
    BS_CHECK_EQ_INT(bs_vfs_exists(&vfs, "MUSIC/Track02.ogg"), BS_OK);
    BS_CHECK_EQ_INT(bs_vfs_exists(&vfs, "MUSIC/Track03.ogg"), BS_OK);
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
    BS_RUN(test_separate_objects);
    BS_RUN(test_player_level1);
    BS_RUN(test_all_nodes);
    BS_RUN(test_items);
    BS_RUN(test_creatures);
    BS_RUN(test_classes);
    BS_RUN(test_saves);
    BS_RUN(test_dialogue_tables);
    BS_RUN(test_all_sounds);
    BS_RUN(test_all_videos);
    bs_cdimage_close(cd);
    return BS_TEST_RESULT();
}
