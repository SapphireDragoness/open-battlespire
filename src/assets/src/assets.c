#include "bs/assets/assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/core/fs.h"
#include "bs/core/log.h"
#include "bs/core/strmap.h"
#include "bs/formats/bsa.h"

#define NAME_MAX_LEN 64

typedef enum archive_id {
    ARCHIVE_3D,
    ARCHIVE_BSI,
    ARCHIVE_BS6,
    ARCHIVE_COUNT
} archive_id;

static const struct {
    const char *ext;
    const char *path;
} archive_info[ARCHIVE_COUNT] = {
    {"3d", "GAMEDATA/3D.BSA"},
    {"bsi", "GAMEDATA/BSI.BSA"},
    {"bs6", "GAMEDATA/BS6.BSA"},
};

struct bs_assets {
    const bs_vfs *vfs;
    bs_bsa *archives[ARCHIVE_COUNT];
    bool archive_tried[ARCHIVE_COUNT];
    bs_strmap models;    /* "name.3d" -> bs_model* or NULL */
    bs_strmap bsis;      /* "name.bsi" -> bs_bsi* or NULL */
    bs_strmap textures;  /* face texture name -> bs_bsi_image* or NULL */
    bs_arena arena;      /* parsed assets and their source data */
};

bs_result bs_assets_open(const bs_vfs *vfs, bs_assets **out)
{
    *out = NULL;
    bs_assets *a = calloc(1, sizeof *a);
    if (!a)
        return BS_ERR_NOMEM;
    a->vfs = vfs;
    bs_strmap_init(&a->models);
    bs_strmap_init(&a->bsis);
    bs_strmap_init(&a->textures);
    bs_arena_init(&a->arena, 4u << 20);
    *out = a;
    return BS_OK;
}

void bs_assets_close(bs_assets *a)
{
    if (!a)
        return;
    for (int i = 0; i < ARCHIVE_COUNT; i++)
        bs_bsa_close(a->archives[i]);
    bs_strmap_free(&a->models);
    bs_strmap_free(&a->bsis);
    bs_strmap_free(&a->textures);
    bs_arena_free(&a->arena);
    free(a);
}

static const char *extension(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot ? dot + 1 : "";
}

static bs_bsa *archive_for(bs_assets *a, const char *name)
{
    const char *ext = extension(name);
    for (int i = 0; i < ARCHIVE_COUNT; i++) {
        if (bs_strcasecmp(ext, archive_info[i].ext) != 0)
            continue;
        if (!a->archive_tried[i]) {
            a->archive_tried[i] = true;
            bs_result r = bs_bsa_open_vfs(a->vfs, archive_info[i].path, &a->archives[i]);
            if (r != BS_OK)
                BS_WARN("cannot open %s: %s", archive_info[i].path, bs_result_str(r));
        }
        return a->archives[i];
    }
    return NULL;
}

bs_result bs_assets_read(bs_assets *a, const char *name, bs_arena *arena, uint8_t **out, size_t *out_size)
{
    char loose[BS_PATH_MAX];
    if (!bs_path_join(loose, sizeof loose, "GAMEDATA", name))
        return BS_ERR_ARGS;
    if (bs_vfs_exists(a->vfs, loose) == BS_OK)
        return bs_vfs_read(a->vfs, loose, arena, out, out_size);

    bs_bsa *bsa = archive_for(a, name);
    if (!bsa)
        return BS_ERR_NOT_FOUND;
    const bs_bsa_record *rec = bs_bsa_find(bsa, name);
    if (!rec)
        return BS_ERR_NOT_FOUND;
    return bs_bsa_read(bsa, rec, arena, out, out_size);
}

/* name + ext unless name already has an extension. */
static bool with_ext(const char *name, const char *ext, char out[NAME_MAX_LEN])
{
    int n = strchr(name, '.') ? snprintf(out, NAME_MAX_LEN, "%s", name)
                              : snprintf(out, NAME_MAX_LEN, "%s.%s", name, ext);
    return n > 0 && n < NAME_MAX_LEN;
}

const bs_model *bs_assets_model(bs_assets *a, const char *name)
{
    char file[NAME_MAX_LEN];
    if (!with_ext(name, "3d", file))
        return NULL;
    void *cached;
    if (bs_strmap_get(&a->models, file, &cached))
        return cached;

    bs_model *model = BS_ARENA_NEW(&a->arena, bs_model);
    uint8_t *data;
    size_t size;
    bs_result r = model ? bs_assets_read(a, file, &a->arena, &data, &size) : BS_ERR_NOMEM;
    if (r == BS_OK)
        r = bs_model_parse(data, size, &a->arena, model);
    if (r != BS_OK) {
        BS_WARN("model %s: %s", file, bs_result_str(r));
        model = NULL;
    }
    bs_strmap_set(&a->models, file, model);
    return model;
}

const bs_bsi *bs_assets_bsi(bs_assets *a, const char *name)
{
    char file[NAME_MAX_LEN];
    if (!with_ext(name, "bsi", file))
        return NULL;
    void *cached;
    if (bs_strmap_get(&a->bsis, file, &cached))
        return cached;

    bs_bsi *bsi = BS_ARENA_NEW(&a->arena, bs_bsi);
    uint8_t *data;
    size_t size;
    bs_result r = bsi ? bs_assets_read(a, file, &a->arena, &data, &size) : BS_ERR_NOMEM;
    if (r == BS_OK)
        r = bs_bsi_parse(data, size, &a->arena, bsi);
    if (r == BS_OK && bsi->count == 0)
        r = BS_ERR_NOT_FOUND;
    if (r != BS_OK) {
        BS_DEBUG("image %s: %s", file, bs_result_str(r));
        bsi = NULL;
    }
    bs_strmap_set(&a->bsis, file, bsi);
    return bsi;
}

const bs_bsi_image *bs_assets_image(bs_assets *a, const char *name)
{
    const bs_bsi *bsi = bs_assets_bsi(a, name);
    return bsi ? &bsi->images[0] : NULL;
}

static const bs_bsi_image *resolve_texture(bs_assets *a, const char *name)
{
    const bs_bsi_image *img = bs_assets_image(a, name);
    if (img)
        return img;

    /* 1. Truncated long name: first record starting with the name. */
    bs_bsa *bsa = archive_for(a, "x.bsi");
    size_t len = strlen(name);
    for (uint32_t i = 0; bsa && i < bsa->count; i++) {
        const char *rec = bsa->records[i].name;
        if (strlen(rec) > len + 4 && bs_strcasecmp(extension(rec), "bsi") == 0) {
            char prefix[NAME_MAX_LEN];
            snprintf(prefix, sizeof prefix, "%.*s", (int)len, rec);
            if (bs_strcasecmp(prefix, name) == 0)
                return bs_assets_image(a, rec);
        }
    }

    /* 2. Without trailing digits. */
    char base[NAME_MAX_LEN];
    snprintf(base, sizeof base, "%s", name);
    size_t n = strlen(base);
    while (n > 0 && base[n - 1] >= '0' && base[n - 1] <= '9')
        base[--n] = '\0';
    if (n > 0 && n < len)
        return bs_assets_image(a, base);
    return NULL;
}

const bs_bsi_image *bs_assets_texture(bs_assets *a, const char *name)
{
    void *cached;
    if (bs_strmap_get(&a->textures, name, &cached))
        return cached;
    const bs_bsi_image *img = resolve_texture(a, name);
    if (!img)
        BS_WARN("texture %s not found", name);
    bs_strmap_set(&a->textures, name, (void *)img);
    return img;
}
