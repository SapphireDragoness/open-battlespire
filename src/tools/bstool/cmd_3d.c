/* bstool model / level: inspect 3D models and BS6 levels, export glTF. */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/assets/assets.h"
#include "bs/assets/scene.h"
#include "bs/formats/bs6.h"
#include "bs/formats/model.h"
#include "commands.h"
#include "gltf.h"

typedef struct export_opts {
    const char *out_dir;  /* NULL: info only */
    float scale;
    bool markers;
    bool tree;
} export_opts;

/* Parses the common options; positional arguments are collected in names. */
static int parse_opts(int argc, char **argv, export_opts *o, char **names, int *count, const char *usage)
{
    *o = (export_opts){NULL, GLTF_DEFAULT_SCALE, false, false};
    *count = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            o->out_dir = argv[++i];
        else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc)
            o->scale = strtof(argv[++i], NULL);
        else if (strcmp(argv[i], "--markers") == 0)
            o->markers = true;
        else if (strcmp(argv[i], "--tree") == 0)
            o->tree = true;
        else if (argv[i][0] == '-') {
            fprintf(stderr, "%s", usage);
            return 2;
        } else
            names[(*count)++] = argv[i];
    }
    if (!(o->scale > 0)) {
        fprintf(stderr, "bstool: --scale must be positive\n");
        return 2;
    }
    return 0;
}

static void stem_of(const char *name, char *out, size_t cap)
{
    snprintf(out, cap, "%s", name);
    char *dot = strrchr(out, '.');
    if (dot)
        *dot = '\0';
    for (char *p = out; *p; p++)
        if (*p >= 'A' && *p <= 'Z')
            *p = (char)(*p + 32);
}

static bool export_scene(const export_opts *o, const char *name, const bs_scene *scene)
{
    char stem[64];
    stem_of(name, stem, sizeof stem);
    bs_result r = gltf_write_scene(o->out_dir, stem, scene, o->scale, o->markers);
    if (r != BS_OK) {
        fprintf(stderr, "bstool: cannot write %s.gltf: %s\n", stem, bs_result_str(r));
        return false;
    }
    printf("wrote %s/%s.gltf (%" PRIu32 " triangles, %" PRIu32 " materials)\n", o->out_dir, stem,
           scene->vertex_count / 3, scene->material_count);
    return true;
}

/* ---- model ---- */

typedef struct model_stats {
    uint32_t models, failed, faces, textured, colored, invisible, unknown_tex, missing_tex;
} model_stats;

static void model_info(bs_assets *assets, const char *name, const bs_model *m, bool verbose, model_stats *st)
{
    uint32_t tex = 0, col = 0, inv = 0, unk = 0, missing = 0;
    for (uint32_t i = 0; i < m->face_count; i++) {
        uint32_t t = m->faces[i].texture;
        switch (bs_model_texture_kind(t)) {
        case BS_MODEL_TEX_IMAGE: {
            char tn[BS_MODEL_TEX_NAME_MAX];
            if (!bs_model_texture_name(t, tn) || !bs_assets_texture(assets, tn))
                missing++;
            tex++;
            break;
        }
        case BS_MODEL_TEX_COLOR:
            col++;
            break;
        case BS_MODEL_TEX_NONE:
            if (t == BS_MODEL_TEX_INVISIBLE)
                inv++;
            else
                unk++;
            break;
        }
    }
    st->models++;
    st->faces += m->face_count;
    st->textured += tex;
    st->colored += col;
    st->invisible += inv;
    st->unknown_tex += unk;
    st->missing_tex += missing;
    if (!verbose)
        return;
    printf("%-12s %s points=%-4" PRIu32 " faces=%-4" PRIu32 " radius=%-7" PRIu32, name, m->version,
           m->point_count, m->face_count, m->radius);
    printf(" textured=%" PRIu32 " colour=%" PRIu32 " invisible=%" PRIu32, tex, col, inv);
    if (missing)
        printf(" MISSING-TEXTURES=%" PRIu32, missing);
    if (unk)
        printf(" unknown-tex=%" PRIu32, unk);
    putchar('\n');
}

/* Builds a one-object scene so models export through the same path as levels. */
static bs_result model_scene(bs_assets *assets, const char *name, bs_arena *arena, bs_scene *scene)
{
    const char *names[1] = {name};
    bs_bs6_object obj;
    memset(&obj, 0, sizeof obj);
    bs_bs6_level level;
    memset(&level, 0, sizeof level);
    level.model_names = names;
    level.model_count = 1;
    level.objects = &obj;
    level.object_count = 1;
    return bs_scene_build(assets, &level, arena, scene);
}

int cmd_model(int argc, char **argv)
{
    static const char usage[] =
        "usage: bstool model [-o dir] [--scale s] [name...]\n"
        "  without -o: print model summaries (default: every model in 3D.BSA)\n"
        "  with -o: export each named model as glTF\n";
    char **names = calloc((size_t)argc, sizeof *names);
    if (!names)
        return 1;
    export_opts o;
    int count;
    int status = parse_opts(argc, argv, &o, names, &count, usage);
    if (status) {
        free(names);
        return status;
    }
    if (o.out_dir && !count) {
        fprintf(stderr, "model: name the models to export\n");
        free(names);
        return 2;
    }

    bstool_ctx ctx;
    if (!bstool_open(&ctx)) {
        free(names);
        return 1;
    }
    bs_assets *assets;
    bs_assets_open(&ctx.vfs, &assets);
    bs_bsa *bsa = NULL;
    if (!count) {
        if (bs_bsa_open_vfs(&ctx.vfs, "GAMEDATA/3D.BSA", &bsa) != BS_OK) {
            fprintf(stderr, "model: cannot open 3D.BSA\n");
            status = 1;
            goto done;
        }
    }
    if (o.out_dir && bs_fs_mkdirs(o.out_dir) != BS_OK) {
        fprintf(stderr, "model: cannot create '%s'\n", o.out_dir);
        status = 1;
        goto done;
    }

    model_stats st = {0};
    uint32_t total = bsa ? bsa->count : (uint32_t)count;
    for (uint32_t i = 0; i < total; i++) {
        const char *name = bsa ? bsa->records[i].name : names[i];
        const char *dot = strrchr(name, '.');
        if (bsa && (!dot || bs_strcasecmp(dot, ".3d") != 0))
            continue;
        const bs_model *m = bs_assets_model(assets, name);
        if (!m) {
            st.failed++;
            continue;
        }
        if (o.out_dir) {
            bs_arena arena;
            bs_arena_init(&arena, 0);
            bs_scene scene;
            bs_result r = model_scene(assets, name, &arena, &scene);
            if (r != BS_OK || !export_scene(&o, name, &scene))
                st.failed++;
            bs_arena_free(&arena);
        } else {
            model_info(assets, name, m, true, &st);
        }
    }
    if (!o.out_dir)
        printf("\n%" PRIu32 " models, %" PRIu32 " failed; %" PRIu32 " faces: %" PRIu32 " textured (%" PRIu32
               " missing texture), %" PRIu32 " colour, %" PRIu32 " invisible, %" PRIu32 " unknown\n",
               st.models, st.failed, st.faces, st.textured, st.missing_tex, st.colored, st.invisible,
               st.unknown_tex);
    if (st.failed)
        status = 1;
done:
    bs_bsa_close(bsa);
    bs_assets_close(assets);
    bstool_close(&ctx);
    free(names);
    return status;
}

/* ---- level ---- */

static int field_len(const uint8_t *p, size_t cap)
{
    const uint8_t *nul = memchr(p, 0, cap);
    return (int)(nul ? (size_t)(nul - p) : cap);
}

static void print_tree(const bs_bs6_chunk *parent, int depth)
{
    /* Chunks that contain other chunks. */
    static const char *const containers[] = {"GNRL", "TEXI", "SNAP", "VIEW", "STRU", "OBJS", "OBJD",
                                             "CTRL", "LINK", "LITS", "LITD", "FLAS", "FLAD"};
    bs_bs6_iter it = bs_bs6_iter_make(parent->data, parent->size);
    bs_bs6_chunk c;
    while (bs_bs6_next(&it, &c)) {
        bool container = false;
        for (size_t i = 0; i < sizeof containers / sizeof containers[0]; i++)
            container |= memcmp(c.tag, containers[i], 4) == 0;
        printf("%*s%s %" PRIu32, depth * 2, "", c.tag, c.size);
        if (container) {
            putchar('\n');
            print_tree(&c, depth + 1);
            continue;
        }
        bool text = c.size >= BS_BS6_NAME_FIELD && c.size % BS_BS6_NAME_FIELD == 0;
        if (text) {
            printf("  ");
            for (uint32_t off = 0; off < c.size && off < 4 * BS_BS6_NAME_FIELD; off += BS_BS6_NAME_FIELD)
                printf("\"%.*s\" ", field_len(c.data + off, BS_BS6_NAME_FIELD), (const char *)c.data + off);
            if (c.size > 4 * BS_BS6_NAME_FIELD)
                printf("... (%" PRIu32 " names)", c.size / BS_BS6_NAME_FIELD);
        } else {
            printf("  ");
            for (uint32_t k = 0; k < c.size && k < 32; k += 4) {
                if (c.size - k >= 4)
                    printf("%" PRId32 " ", (int32_t)((uint32_t)c.data[k] | (uint32_t)c.data[k + 1] << 8 |
                                                     (uint32_t)c.data[k + 2] << 16 | (uint32_t)c.data[k + 3] << 24));
                else
                    printf("%02x ", c.data[k]);
            }
            if (c.size > 32)
                printf("...");
        }
        putchar('\n');
    }
    if (it.err)
        printf("%*s(truncated chunk)\n", depth * 2, "");
}

static void level_info(const bs_bs6_level *lvl, const bs_scene *scene)
{
    printf("  models:  %" PRIu32 " files, %" PRIu32 " placed (%" PRIu32 " missing)\n", lvl->model_count,
           lvl->object_count, scene->missing_models);
    uint32_t markers = 0, unresolved = 0;
    for (uint32_t i = 0; i < scene->sprite_count; i++) {
        markers += scene->sprites[i].marker;
        unresolved += !scene->sprites[i].image;
    }
    printf("  flats:   %" PRIu32 " (%" PRIu32 " markers, %" PRIu32 " unresolved)\n", lvl->flat_count, markers,
           unresolved);
    printf("  lights:  %" PRIu32 "\n  views:   %" PRIu32 "\n", lvl->light_count, lvl->view_count);
    uint32_t missing = 0, colours = 0;
    for (uint32_t i = 0; i < scene->material_count; i++) {
        missing += scene->materials[i].kind == BS_SCENE_MAT_MISSING;
        colours += scene->materials[i].kind == BS_SCENE_MAT_COLOR;
    }
    printf("  geometry: %" PRIu32 " triangles, %" PRIu32 " materials (%" PRIu32 " colours, %" PRIu32
           " missing textures)\n",
           scene->vertex_count / 3, scene->material_count, colours, missing);
    printf("  bounds:  x %.0f..%.0f  y %.0f..%.0f  z %.0f..%.0f\n", (double)scene->min[0], (double)scene->max[0],
           (double)scene->min[1], (double)scene->max[1], (double)scene->min[2], (double)scene->max[2]);
    if (lvl->water)
        printf("  water:   %" PRId32 "\n", lvl->water);
}

int cmd_level(int argc, char **argv)
{
    static const char usage[] =
        "usage: bstool level [-o dir] [--scale s] [--markers] [--tree] <name...>\n"
        "  name: a level file such as l1.bs6 (BS6.BSA or loose in GAMEDATA)\n"
        "  without -o: print a summary; --tree dumps the chunk tree\n"
        "  with -o: export the level as glTF (--markers adds editor markers as nodes)\n";
    char **names = calloc((size_t)argc, sizeof *names);
    if (!names)
        return 1;
    export_opts o;
    int count;
    int status = parse_opts(argc, argv, &o, names, &count, usage);
    if (status || !count) {
        if (!status)
            fprintf(stderr, "%s", usage);
        free(names);
        return status ? status : 2;
    }

    bstool_ctx ctx;
    if (!bstool_open(&ctx)) {
        free(names);
        return 1;
    }
    bs_assets *assets;
    bs_assets_open(&ctx.vfs, &assets);
    if (o.out_dir && bs_fs_mkdirs(o.out_dir) != BS_OK) {
        fprintf(stderr, "level: cannot create '%s'\n", o.out_dir);
        status = 1;
        goto done;
    }
    for (int i = 0; i < count; i++) {
        char file[64];
        snprintf(file, sizeof file, "%s%s", names[i], strchr(names[i], '.') ? "" : ".bs6");
        bs_arena arena;
        bs_arena_init(&arena, 0);
        uint8_t *data;
        size_t size;
        bs_bs6_level lvl;
        bs_scene scene;
        bs_result r = bs_assets_read(assets, file, &arena, &data, &size);
        if (r == BS_OK && o.tree) {
            bs_bs6_chunk root = {"", data, (uint32_t)size};
            print_tree(&root, 0);
        }
        if (r == BS_OK)
            r = bs_bs6_parse(data, size, &arena, &lvl);
        if (r == BS_OK)
            r = bs_scene_build(assets, &lvl, &arena, &scene);
        if (r != BS_OK) {
            fprintf(stderr, "level: %s: %s\n", file, bs_result_str(r));
            status = 1;
        } else if (o.out_dir) {
            if (!export_scene(&o, file, &scene))
                status = 1;
        } else {
            printf("%s\n", file);
            level_info(&lvl, &scene);
        }
        bs_arena_free(&arena);
    }
done:
    bs_assets_close(assets);
    bstool_close(&ctx);
    free(names);
    return status;
}
