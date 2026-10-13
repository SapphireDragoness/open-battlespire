#include "bs/assets/scene.h"

#include <float.h>
#include <stdio.h>
#include <string.h>

#include "bs/core/log.h"
#include "bs/core/strmap.h"
#include "bs/formats/palette.h"

/* Editor icons (spawn points, item spots, triggers) have BHDR field 8 = 0;
 * images drawn in game have a non-zero value there (inferred). */
#define BHDR_GAME_IMAGE 8

/*
 * Sprite size and anchor (inferred from where flats sit in the levels):
 * - BHDR field 11 is a scale in 1/256: 256 for flames, 87..102 for NPC
 *   figures, 56..184 across monsters. At that scale (1 level unit per pixel
 *   for 256), flames centred on their position rest exactly on their torch
 *   pillars.
 * - Scaled figures (field 11 < 256) stand on their position: NPC flats sit
 *   1..3 units above the floor.
 * Editor markers are drawn at 1 unit per pixel, centred.
 */
#define BHDR_SCALE 11
#define SCALE_ONE 256

void bs_scene_transform(const float rot[9], const int32_t pos[3], const bs_model_point *p, float out[3])
{
    float x = (float)p->x / BS_MODEL_UNITS_PER_WORLD;
    float y = (float)p->y / BS_MODEL_UNITS_PER_WORLD;
    float z = (float)p->z / BS_MODEL_UNITS_PER_WORLD;
    for (int i = 0; i < 3; i++)
        out[i] = rot[i * 3] * x + rot[i * 3 + 1] * y + rot[i * 3 + 2] * z + (float)pos[i];
}

typedef struct builder {
    bs_assets *assets;
    bs_arena *arena;
    bs_strmap keys;       /* material key -> index + 1 */
    bs_scene_material *materials;
    uint32_t material_count, material_cap;
    uint32_t *tri_counts; /* per material */
} builder;

static bs_result material_for(builder *b, uint32_t texture, uint32_t *out)
{
    char key[16];
    snprintf(key, sizeof key, "%08x", texture);
    void *v;
    if (bs_strmap_get(&b->keys, key, &v)) {
        *out = (uint32_t)(uintptr_t)v - 1;
        return BS_OK;
    }
    if (b->material_count == b->material_cap) {
        uint32_t cap = b->material_cap ? b->material_cap * 2 : 64;
        bs_scene_material *m = BS_ARENA_ARRAY(b->arena, bs_scene_material, cap);
        uint32_t *c = BS_ARENA_ARRAY(b->arena, uint32_t, cap);
        if (!m || !c)
            return BS_ERR_NOMEM;
        if (b->material_count) {
            memcpy(m, b->materials, b->material_count * sizeof *m);
            memcpy(c, b->tri_counts, b->material_count * sizeof *c);
        }
        b->materials = m;
        b->tri_counts = c;
        b->material_cap = cap;
    }
    bs_scene_material *m = &b->materials[b->material_count];
    memset(m, 0, sizeof *m);
    if (bs_model_texture_kind(texture) == BS_MODEL_TEX_COLOR) {
        m->kind = BS_SCENE_MAT_COLOR;
        bs_hicl_to_rgb((uint16_t)texture, m->rgb);
    } else {
        bs_model_texture_name(texture, m->name);
        m->image = bs_assets_texture(b->assets, m->name);
        m->kind = m->image ? BS_SCENE_MAT_TEXTURE : BS_SCENE_MAT_MISSING;
    }
    *out = b->material_count++;
    return bs_strmap_set(&b->keys, key, (void *)(uintptr_t)(*out + 1));
}

static bool face_drawn(const bs_model_face *f)
{
    return f->vertex_count >= 3 && bs_model_texture_kind(f->texture) != BS_MODEL_TEX_NONE;
}

/* Pass 1: create materials and count triangles per material. */
static bs_result count_pass(builder *b, const bs_bs6_level *level, const bs_model **models)
{
    for (uint32_t i = 0; i < level->object_count; i++) {
        const bs_model *m = models[i];
        for (uint32_t k = 0; m && k < m->face_count; k++) {
            const bs_model_face *f = &m->faces[k];
            if (!face_drawn(f))
                continue;
            uint32_t mat;
            bs_result r = material_for(b, f->texture, &mat);
            if (r != BS_OK)
                return r;
            b->tri_counts[mat] += f->vertex_count - 2u;
        }
    }
    return BS_OK;
}

static void emit_face(const bs_model *m, const bs_model_face *f, const float rot[9], const int32_t pos[3],
                      const bs_scene_material *mat, bs_scene_vertex *out)
{
    float uv[256][2];
    bs_model_face_uvs(m, f, uv);
    float su = 1.0f, sv = 1.0f;
    if (mat->kind == BS_SCENE_MAT_TEXTURE && mat->image->width && mat->image->height) {
        su = 1.0f / mat->image->width;
        sv = 1.0f / mat->image->height;
    }
    float n[3];
    for (int i = 0; i < 3; i++)
        n[i] = (rot[i * 3] * (float)f->normal[0] + rot[i * 3 + 1] * (float)f->normal[1] +
                rot[i * 3 + 2] * (float)f->normal[2]) / BS_MODEL_NORMAL_ONE;

    const bs_model_vertex *vs = m->vertices + f->first_vertex;
    /* Fan triangulation; the faces are convex. */
    for (uint32_t t = 0; t + 2 < f->vertex_count; t++) {
        uint32_t idx[3] = {0, t + 1, t + 2};
        for (int c = 0; c < 3; c++) {
            bs_scene_vertex *v = out++;
            bs_scene_transform(rot, pos, &m->points[vs[idx[c]].point], v->pos);
            memcpy(v->normal, n, sizeof n);
            v->face_byte = (float)f->light;
            v->uv[0] = uv[idx[c]][0] * su;
            v->uv[1] = uv[idx[c]][1] * sv;
        }
    }
}

bs_result bs_scene_build(bs_assets *assets, const bs_bs6_level *level, bs_arena *arena, bs_scene *out)
{
    memset(out, 0, sizeof *out);
    builder b = {.assets = assets, .arena = arena};
    bs_strmap_init(&b.keys);

    bs_result r = BS_ERR_NOMEM;
    const bs_model **models = BS_ARENA_ARRAY(arena, const bs_model *, level->object_count + 1);
    if (!models)
        goto done;
    for (uint32_t i = 0; i < level->object_count; i++) {
        models[i] = bs_assets_model(assets, level->model_names[level->objects[i].model]);
        if (!models[i])
            out->missing_models++;
    }

    r = count_pass(&b, level, models);
    if (r != BS_OK)
        goto done;

    /* Lay out batches contiguously in material order. */
    uint64_t total = 0;
    uint32_t *cursor = BS_ARENA_ARRAY(arena, uint32_t, b.material_count + 1);
    out->batches = BS_ARENA_ARRAY(arena, bs_scene_batch, b.material_count + 1);
    if (!cursor || !out->batches) {
        r = BS_ERR_NOMEM;
        goto done;
    }
    for (uint32_t i = 0; i < b.material_count; i++) {
        out->batches[i] = (bs_scene_batch){i, (uint32_t)total, b.tri_counts[i] * 3};
        cursor[i] = (uint32_t)total;
        total += (uint64_t)b.tri_counts[i] * 3;
    }
    if (total > UINT32_MAX / 2) {
        r = BS_ERR_UNSUPPORTED;
        goto done;
    }
    out->vertices = BS_ARENA_ARRAY(arena, bs_scene_vertex, total + 1);
    if (!out->vertices) {
        r = BS_ERR_NOMEM;
        goto done;
    }

    for (int i = 0; i < 3; i++) {
        out->min[i] = FLT_MAX;
        out->max[i] = -FLT_MAX;
    }
    for (uint32_t i = 0; i < level->object_count; i++) {
        const bs_model *m = models[i];
        if (!m)
            continue;
        const bs_bs6_object *o = &level->objects[i];
        float rot[9];
        bs_bs6_rotation(o->angles, rot);
        for (uint32_t k = 0; k < m->face_count; k++) {
            const bs_model_face *f = &m->faces[k];
            if (!face_drawn(f))
                continue;
            uint32_t mat;
            material_for(&b, f->texture, &mat); /* already present */
            bs_scene_vertex *dst = out->vertices + cursor[mat];
            emit_face(m, f, rot, o->pos, &b.materials[mat], dst);
            uint32_t n = (f->vertex_count - 2u) * 3;
            for (uint32_t v = 0; v < n; v++)
                for (int c = 0; c < 3; c++) {
                    if (dst[v].pos[c] < out->min[c])
                        out->min[c] = dst[v].pos[c];
                    if (dst[v].pos[c] > out->max[c])
                        out->max[c] = dst[v].pos[c];
                }
            cursor[mat] += n;
        }
    }
    out->vertex_count = (uint32_t)total;
    out->materials = b.materials;
    out->material_count = b.material_count;
    out->batch_count = b.material_count;

    out->sprites = BS_ARENA_ARRAY(arena, bs_scene_sprite, level->flat_count + 1);
    if (!out->sprites) {
        r = BS_ERR_NOMEM;
        goto done;
    }
    for (uint32_t i = 0; i < level->flat_count; i++) {
        const bs_bs6_flat *fl = &level->flats[i];
        bs_scene_sprite *s = &out->sprites[i];
        s->name = fl->name;
        s->image = fl->name[0] ? bs_assets_image(assets, fl->name) : NULL;
        for (int c = 0; c < 3; c++)
            s->pos[c] = (float)fl->pos[c];
        s->marker = !s->image || s->image->bhdr[BHDR_GAME_IMAGE] == 0;
        memcpy(s->center, s->pos, sizeof s->center);
        if (s->image) {
            int scale = s->image->bhdr[BHDR_SCALE];
            float k = s->marker || scale <= 0 ? 1.0f : (float)scale / SCALE_ONE;
            s->size[0] = s->image->width * k;
            s->size[1] = s->image->height * k;
            if (!s->marker && scale > 0 && scale < SCALE_ONE)
                s->center[1] -= s->size[1] * 0.5f; /* stands on its position; up is -y */
        }
    }
    out->sprite_count = level->flat_count;
    r = BS_OK;

done:
    bs_strmap_free(&b.keys);
    return r;
}
