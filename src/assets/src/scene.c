#include "bs/assets/scene.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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

/* Pass 1: create materials and count static triangles per material and separate ones per object. */
static bs_result count_pass(builder *b, const bs_bs6_level *level, const bs_model **models, const bool *separate,
                            uint64_t *separate_tris)
{
    for (uint32_t i = 0; i < level->object_count; i++) {
        const bs_model *m = models[i];
        bool sep = separate && separate[i];
        for (uint32_t k = 0; m && k < m->face_count; k++) {
            const bs_model_face *f = &m->faces[k];
            if (!face_drawn(f))
                continue;
            uint32_t mat;
            bs_result r = material_for(b, f->texture, &mat);
            if (r != BS_OK)
                return r;
            if (sep)
                *separate_tris += f->vertex_count - 2u;
            else
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
    return bs_scene_build_ex(assets, level, NULL, arena, out);
}

typedef struct face_ref {
    uint32_t material, face;
} face_ref;

static int cmp_face_ref(const void *a, const void *b)
{
    const face_ref *x = a, *y = b;
    if (x->material != y->material)
        return x->material < y->material ? -1 : 1;
    return x->face < y->face ? -1 : x->face > y->face;
}

static void grow_bounds(bs_scene *out, const bs_scene_vertex *v, uint32_t n, const bs_scene_xform *xf)
{
    for (uint32_t i = 0; i < n; i++) {
        float p[3];
        for (int c = 0; c < 3; c++)
            p[c] = xf ? xf->rot[c * 3] * v[i].pos[0] + xf->rot[c * 3 + 1] * v[i].pos[1] +
                            xf->rot[c * 3 + 2] * v[i].pos[2] + xf->pos[c]
                      : v[i].pos[c];
        for (int c = 0; c < 3; c++) {
            if (p[c] < out->min[c])
                out->min[c] = p[c];
            if (p[c] > out->max[c])
                out->max[c] = p[c];
        }
    }
}

bs_result bs_scene_build_ex(bs_assets *assets, const bs_bs6_level *level, const bool *separate, bs_arena *arena,
                            bs_scene *out)
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

    uint64_t separate_tris = 0;
    r = count_pass(&b, level, models, separate, &separate_tris);
    if (r != BS_OK)
        goto done;
    uint32_t separate_batches = 0;
    for (uint32_t i = 0; separate && i < level->object_count; i++)
        if (separate[i] && models[i])
            separate_batches += b.material_count; /* an upper bound */

    /* Lay out batches contiguously in material order. */
    uint64_t total = 0;
    uint32_t *cursor = BS_ARENA_ARRAY(arena, uint32_t, b.material_count + 1);
    out->batches = BS_ARENA_ARRAY(arena, bs_scene_batch, (size_t)b.material_count + separate_batches + 1);
    if (!cursor || !out->batches) {
        r = BS_ERR_NOMEM;
        goto done;
    }
    for (uint32_t i = 0; i < b.material_count; i++) {
        out->batches[i] = (bs_scene_batch){i, (uint32_t)total, b.tri_counts[i] * 3, -1};
        cursor[i] = (uint32_t)total;
        total += (uint64_t)b.tri_counts[i] * 3;
    }
    uint64_t static_total = total;
    total += separate_tris * 3;
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
        if (!m || (separate && separate[i]))
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
            grow_bounds(out, dst, n, NULL);
            cursor[mat] += n;
        }
    }

    /* Separate objects: in model space, their faces grouped by material. */
    out->batch_count = b.material_count;
    uint32_t vcur = (uint32_t)static_total;
    static const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    static const int32_t origin[3] = {0, 0, 0};
    for (uint32_t i = 0; separate && i < level->object_count; i++) {
        const bs_model *m = models[i];
        if (!separate[i] || !m)
            continue;
        face_ref *refs = malloc(((size_t)m->face_count + 1) * sizeof *refs);
        if (!refs) {
            r = BS_ERR_NOMEM;
            goto done;
        }
        uint32_t nref = 0;
        for (uint32_t k = 0; k < m->face_count; k++) {
            if (!face_drawn(&m->faces[k]))
                continue;
            material_for(&b, m->faces[k].texture, &refs[nref].material);
            refs[nref++].face = k;
        }
        qsort(refs, nref, sizeof *refs, cmp_face_ref);
        const bs_bs6_object *o = &level->objects[i];
        float fpos[3] = {(float)o->pos[0], (float)o->pos[1], (float)o->pos[2]};
        bs_scene_xform xf = bs_scene_object_xform(o->angles, fpos);
        for (uint32_t k = 0; k < nref; k++) {
            if (k == 0 || refs[k].material != refs[k - 1].material)
                out->batches[out->batch_count++] = (bs_scene_batch){refs[k].material, vcur, 0, (int32_t)i};
            const bs_model_face *f = &m->faces[refs[k].face];
            emit_face(m, f, identity, origin, &b.materials[refs[k].material], out->vertices + vcur);
            uint32_t n = (f->vertex_count - 2u) * 3;
            grow_bounds(out, out->vertices + vcur, n, &xf);
            vcur += n;
            out->batches[out->batch_count - 1].count += n;
        }
        free(refs);
    }
    out->vertex_count = (uint32_t)total;
    out->materials = b.materials;
    out->material_count = b.material_count;
    out->static_batch_count = b.material_count;

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

bs_scene_xform bs_scene_object_xform(const int32_t angles[3], const float pos[3])
{
    bs_scene_xform x;
    bs_bs6_rotation(angles, x.rot);
    memcpy(x.pos, pos, sizeof x.pos);
    return x;
}

/* Moller-Trumbore; returns t or -1. */
static float ray_triangle(const float o[3], const float d[3], const float a[3], const float b[3], const float c[3])
{
    float e1[3], e2[3], p[3], q[3], t[3];
    for (int i = 0; i < 3; i++) {
        e1[i] = b[i] - a[i];
        e2[i] = c[i] - a[i];
        t[i] = o[i] - a[i];
    }
    p[0] = d[1] * e2[2] - d[2] * e2[1];
    p[1] = d[2] * e2[0] - d[0] * e2[2];
    p[2] = d[0] * e2[1] - d[1] * e2[0];
    float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (fabsf(det) < 1e-9f)
        return -1;
    float inv = 1.0f / det;
    float u = (t[0] * p[0] + t[1] * p[1] + t[2] * p[2]) * inv;
    if (u < 0 || u > 1)
        return -1;
    q[0] = t[1] * e1[2] - t[2] * e1[1];
    q[1] = t[2] * e1[0] - t[0] * e1[2];
    q[2] = t[0] * e1[1] - t[1] * e1[0];
    float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
    if (v < 0 || u + v > 1)
        return -1;
    float dist = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
    return dist >= 0 ? dist : -1;
}

float bs_scene_raycast(const bs_scene *s, const bs_bs6_level *level, const bs_scene_xform *xforms,
                       const float origin[3], const float dir[3], float max_dist, int32_t *object)
{
    float best = -1;
    *object = -1;
    for (uint32_t bi = 0; bi < s->batch_count; bi++) {
        const bs_scene_batch *b = &s->batches[bi];
        bs_scene_xform xf;
        const bs_scene_xform *x = NULL;
        if (b->object >= 0 && (uint32_t)b->object >= level->object_count)
            continue; /* extra objects (e.g. the jump pointer) aren't part of the level */
        if (b->object >= 0) {
            if (xforms) {
                x = &xforms[b->object];
            } else {
                const bs_bs6_object *o = &level->objects[b->object];
                float fpos[3] = {(float)o->pos[0], (float)o->pos[1], (float)o->pos[2]};
                xf = bs_scene_object_xform(o->angles, fpos);
                x = &xf;
            }
        }
        for (uint32_t v = b->first; v + 2 < b->first + b->count + 0u && v + 2 < s->vertex_count; v += 3) {
            float tri[3][3];
            for (int k = 0; k < 3; k++) {
                const float *p = s->vertices[v + (uint32_t)k].pos;
                for (int c = 0; c < 3; c++)
                    tri[k][c] = x ? x->rot[c * 3] * p[0] + x->rot[c * 3 + 1] * p[1] + x->rot[c * 3 + 2] * p[2] + x->pos[c]
                                  : p[c];
            }
            float t = ray_triangle(origin, dir, tri[0], tri[1], tri[2]);
            if (t >= 0 && t <= max_dist && (best < 0 || t < best)) {
                best = t;
                *object = b->object;
            }
        }
    }
    return best;
}
