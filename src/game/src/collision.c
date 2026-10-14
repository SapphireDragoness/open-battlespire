#include "bs/game/collision.h"

#include <float.h>
#include <math.h>
#include <string.h>

#define CELL 256.0f
#define MAX_CELLS_PER_SIDE 2048u

static void sub(const float a[3], const float b[3], float o[3])
{
    o[0] = a[0] - b[0];
    o[1] = a[1] - b[1];
    o[2] = a[2] - b[2];
}

static float dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void cross(const float a[3], const float b[3], float o[3])
{
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

static bool make_tri(const bs_scene_vertex *v, bs_collision_tri *t)
{
    for (int k = 0; k < 3; k++)
        memcpy(t->v[k], v[k].pos, sizeof t->v[k]);
    float e1[3], e2[3];
    sub(t->v[1], t->v[0], e1);
    sub(t->v[2], t->v[0], e2);
    cross(e1, e2, t->n);
    float len = sqrtf(dot(t->n, t->n));
    if (!(len > 1e-6f))
        return false; /* degenerate */
    for (int c = 0; c < 3; c++)
        t->n[c] /= len;
    return true;
}

/* Grid cell range of an x/z box, clamped. */
static void cell_range(const bs_collision_world *w, float x0, float z0, float x1, float z1, uint32_t r[4])
{
    float lo[2] = {(x0 - w->origin[0]) / w->cell, (z0 - w->origin[1]) / w->cell};
    float hi[2] = {(x1 - w->origin[0]) / w->cell, (z1 - w->origin[1]) / w->cell};
    uint32_t lim[2] = {w->cols - 1, w->rows - 1};
    for (int a = 0; a < 2; a++) {
        r[a] = lo[a] <= 0 ? 0 : lo[a] >= (float)lim[a] ? lim[a] : (uint32_t)lo[a];
        r[2 + a] = hi[a] <= 0 ? 0 : hi[a] >= (float)lim[a] ? lim[a] : (uint32_t)hi[a];
    }
}

static void tri_cells(const bs_collision_world *w, const bs_collision_tri *t, uint32_t r[4])
{
    float x0 = FLT_MAX, z0 = FLT_MAX, x1 = -FLT_MAX, z1 = -FLT_MAX;
    for (int k = 0; k < 3; k++) {
        x0 = fminf(x0, t->v[k][0]);
        x1 = fmaxf(x1, t->v[k][0]);
        z0 = fminf(z0, t->v[k][2]);
        z1 = fmaxf(z1, t->v[k][2]);
    }
    cell_range(w, x0, z0, x1, z1, r);
}

bs_result bs_collision_build(const bs_scene *scene, const bs_bs6_level *level, bs_arena *arena,
                             bs_collision_world *out)
{
    memset(out, 0, sizeof *out);
    uint32_t n = scene->vertex_count / 3;
    out->tris = BS_ARENA_ARRAY(arena, bs_collision_tri, n + 1);
    out->object_count = level->object_count;
    out->objects = BS_ARENA_ARRAY(arena, bs_collision_object, level->object_count + 1);
    if (!out->tris || !out->objects)
        return BS_ERR_NOMEM;
    memset(out->objects, 0, sizeof *out->objects * (level->object_count + 1));

    /* Static triangles first, then each object's (the scene groups them by object). */
    float x0 = FLT_MAX, z0 = FLT_MAX, x1 = -FLT_MAX, z1 = -FLT_MAX;
    for (uint32_t b = 0; b < scene->batch_count; b++) {
        const bs_scene_batch *bt = &scene->batches[b];
        if (bt->object >= 0)
            continue;
        for (uint32_t v = bt->first; v + 3 <= bt->first + bt->count; v += 3) {
            bs_collision_tri *t = &out->tris[out->tri_count];
            if (!make_tri(&scene->vertices[v], t))
                continue;
            for (int k = 0; k < 3; k++) {
                x0 = fminf(x0, t->v[k][0]);
                x1 = fmaxf(x1, t->v[k][0]);
                z0 = fminf(z0, t->v[k][2]);
                z1 = fmaxf(z1, t->v[k][2]);
            }
            out->tri_count++;
        }
    }
    out->static_count = out->tri_count;
    for (uint32_t b = 0; b < scene->batch_count; b++) {
        const bs_scene_batch *bt = &scene->batches[b];
        if (bt->object < 0 || (uint32_t)bt->object >= level->object_count)
            continue;
        bs_collision_object *o = &out->objects[bt->object];
        if (!o->present) {
            o->present = true;
            o->first = out->tri_count;
            const bs_bs6_object *lo = &level->objects[bt->object];
            float pos[3] = {(float)lo->pos[0], (float)lo->pos[1], (float)lo->pos[2]};
            o->xform = bs_scene_object_xform(lo->angles, pos);
        }
        for (uint32_t v = bt->first; v + 3 <= bt->first + bt->count; v += 3) {
            bs_collision_tri *t = &out->tris[out->tri_count];
            if (!make_tri(&scene->vertices[v], t))
                continue;
            for (int k = 0; k < 3; k++)
                o->radius = fmaxf(o->radius, sqrtf(dot(t->v[k], t->v[k])));
            out->tri_count++;
            o->count++;
        }
    }

    /* Grid over the static triangles. */
    if (out->static_count == 0) {
        x0 = z0 = 0;
        x1 = z1 = 1;
    }
    /* Cells of 256 units, larger only if a level is too big for the grid. */
    out->cell = fmaxf(CELL, fmaxf(x1 - x0, z1 - z0) / (float)(MAX_CELLS_PER_SIDE - 1));
    out->origin[0] = x0;
    out->origin[1] = z0;
    out->cols = (uint32_t)floorf((x1 - x0) / out->cell) + 1;
    out->rows = (uint32_t)floorf((z1 - z0) / out->cell) + 1;
    uint32_t ncell = out->cols * out->rows;
    out->cell_first = BS_ARENA_ARRAY(arena, uint32_t, ncell + 1);
    if (!out->cell_first)
        return BS_ERR_NOMEM;
    memset(out->cell_first, 0, sizeof *out->cell_first * (ncell + 1));
    uint64_t total = 0;
    for (uint32_t i = 0; i < out->static_count; i++) {
        uint32_t r[4];
        tri_cells(out, &out->tris[i], r);
        for (uint32_t z = r[1]; z <= r[3]; z++)
            for (uint32_t x = r[0]; x <= r[2]; x++)
                out->cell_first[z * out->cols + x + 1]++;
        total += (uint64_t)(r[2] - r[0] + 1) * (r[3] - r[1] + 1);
    }
    if (total > UINT32_MAX / 2)
        return BS_ERR_UNSUPPORTED;
    for (uint32_t c = 0; c < ncell; c++)
        out->cell_first[c + 1] += out->cell_first[c];
    out->cell_tris = BS_ARENA_ARRAY(arena, uint32_t, total + 1);
    uint32_t *fill = BS_ARENA_ARRAY(arena, uint32_t, ncell + 1);
    if (!out->cell_tris || !fill)
        return BS_ERR_NOMEM;
    memcpy(fill, out->cell_first, sizeof *fill * ncell);
    for (uint32_t i = 0; i < out->static_count; i++) {
        uint32_t r[4];
        tri_cells(out, &out->tris[i], r);
        for (uint32_t z = r[1]; z <= r[3]; z++)
            for (uint32_t x = r[0]; x <= r[2]; x++)
                out->cell_tris[fill[z * out->cols + x]++] = i;
    }
    return BS_OK;
}

void bs_collision_place(bs_collision_world *w, uint32_t object, const float pos[3], const int32_t angles[3])
{
    if (object < w->object_count && w->objects[object].present)
        w->objects[object].xform = bs_scene_object_xform(angles, pos);
}

/* ---- queries ---- */

/* Closest point on a triangle to p (Ericson, Real-Time Collision Detection 5.1.5). */
static void closest_on_tri(const float p[3], const float a[3], const float b[3], const float c[3], float out[3])
{
    float ab[3], ac[3], ap[3];
    sub(b, a, ab);
    sub(c, a, ac);
    sub(p, a, ap);
    float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) {
        memcpy(out, a, 12);
        return;
    }
    float bp[3];
    sub(p, b, bp);
    float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) {
        memcpy(out, b, 12);
        return;
    }
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        float v = d1 / (d1 - d3);
        for (int i = 0; i < 3; i++)
            out[i] = a[i] + v * ab[i];
        return;
    }
    float cp[3];
    sub(p, c, cp);
    float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) {
        memcpy(out, c, 12);
        return;
    }
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        float w = d2 / (d2 - d6);
        for (int i = 0; i < 3; i++)
            out[i] = a[i] + w * ac[i];
        return;
    }
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        for (int i = 0; i < 3; i++)
            out[i] = b[i] + w * (c[i] - b[i]);
        return;
    }
    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom, w = vc * denom;
    for (int i = 0; i < 3; i++)
        out[i] = a[i] + ab[i] * v + ac[i] * w;
}

static bool sphere_tri(const bs_collision_tri *t, const float c[3], float r, float normal[3], float *depth)
{
    float q[3], d[3];
    closest_on_tri(c, t->v[0], t->v[1], t->v[2], q);
    sub(c, q, d);
    float dd = dot(d, d);
    if (dd >= r * r)
        return false;
    float len = sqrtf(dd);
    if (len > 1e-5f) {
        for (int i = 0; i < 3; i++)
            normal[i] = d[i] / len;
    } else {
        /* Centre on the triangle: push out on the side it came from is unknown; use the face normal. */
        memcpy(normal, t->n, 12);
    }
    *depth = r - len;
    return true;
}

/* Rotation transpose times a vector (world to model). */
static void to_local(const bs_scene_xform *x, const float p[3], float out[3])
{
    float d[3];
    sub(p, x->pos, d);
    for (int i = 0; i < 3; i++)
        out[i] = x->rot[i] * d[0] + x->rot[3 + i] * d[1] + x->rot[6 + i] * d[2];
}

static void rotate(const bs_scene_xform *x, const float v[3], float out[3])
{
    for (int i = 0; i < 3; i++)
        out[i] = x->rot[i * 3] * v[0] + x->rot[i * 3 + 1] * v[1] + x->rot[i * 3 + 2] * v[2];
}

/* Whether this cell is the first (lowest) one a triangle and a query box share,
 * so each triangle is tested once per query. */
static bool owner_cell(const uint32_t tr[4], const uint32_t qr[4], uint32_t x, uint32_t z)
{
    uint32_t ox = tr[0] > qr[0] ? tr[0] : qr[0], oz = tr[1] > qr[1] ? tr[1] : qr[1];
    return x == ox && z == oz;
}

static void add_hit(bs_collision_hit *hits, uint32_t max, uint32_t *n, const float normal[3], float depth,
                    int32_t object)
{
    if (*n < max) {
        memcpy(hits[*n].normal, normal, 12);
        hits[*n].depth = depth;
        hits[*n].object = object;
    }
    (*n)++;
}

uint32_t bs_collision_sphere(const bs_collision_world *w, const float c[3], float r, bs_collision_hit *hits,
                             uint32_t max)
{
    uint32_t n = 0;
    uint32_t qr[4];
    cell_range(w, c[0] - r, c[2] - r, c[0] + r, c[2] + r, qr);
    for (uint32_t z = qr[1]; z <= qr[3]; z++)
        for (uint32_t x = qr[0]; x <= qr[2]; x++) {
            uint32_t cell = z * w->cols + x;
            for (uint32_t k = w->cell_first[cell]; k < w->cell_first[cell + 1]; k++) {
                const bs_collision_tri *t = &w->tris[w->cell_tris[k]];
                uint32_t tr[4];
                tri_cells(w, t, tr);
                if (!owner_cell(tr, qr, x, z))
                    continue;
                float nrm[3], depth;
                if (sphere_tri(t, c, r, nrm, &depth))
                    add_hit(hits, max, &n, nrm, depth, -1);
            }
        }
    for (uint32_t o = 0; o < w->object_count; o++) {
        const bs_collision_object *ob = &w->objects[o];
        if (!ob->present)
            continue;
        float lc[3];
        to_local(&ob->xform, c, lc);
        if (sqrtf(dot(lc, lc)) > ob->radius + r)
            continue;
        for (uint32_t k = ob->first; k < ob->first + ob->count; k++) {
            float nl[3], depth;
            if (sphere_tri(&w->tris[k], lc, r, nl, &depth)) {
                float nw[3];
                rotate(&ob->xform, nl, nw);
                add_hit(hits, max, &n, nw, depth, (int32_t)o);
            }
        }
    }
    return n;
}

/* Moller-Trumbore on a segment: t in [0, 1]. */
static bool seg_tri(const bs_collision_tri *tr, const float o[3], const float d[3], float *t)
{
    float e1[3], e2[3], p[3], q[3], s[3];
    sub(tr->v[1], tr->v[0], e1);
    sub(tr->v[2], tr->v[0], e2);
    cross(d, e2, p);
    float det = dot(e1, p);
    if (fabsf(det) < 1e-9f)
        return false;
    float inv = 1.0f / det;
    sub(o, tr->v[0], s);
    float u = dot(s, p) * inv;
    if (u < 0 || u > 1)
        return false;
    cross(s, e1, q);
    float v = dot(d, q) * inv;
    if (v < 0 || u + v > 1)
        return false;
    float tt = dot(e2, q) * inv;
    if (tt < 0 || tt > 1)
        return false;
    *t = tt;
    return true;
}

bool bs_collision_segment(const bs_collision_world *w, const float from[3], const float to[3], float *t,
                          int32_t *object)
{
    float d[3];
    sub(to, from, d);
    float best = 2;
    int32_t best_obj = -1;
    uint32_t qr[4];
    cell_range(w, fminf(from[0], to[0]), fminf(from[2], to[2]), fmaxf(from[0], to[0]), fmaxf(from[2], to[2]), qr);
    for (uint32_t z = qr[1]; z <= qr[3]; z++)
        for (uint32_t x = qr[0]; x <= qr[2]; x++) {
            uint32_t cell = z * w->cols + x;
            for (uint32_t k = w->cell_first[cell]; k < w->cell_first[cell + 1]; k++) {
                const bs_collision_tri *tr = &w->tris[w->cell_tris[k]];
                uint32_t trc[4];
                tri_cells(w, tr, trc);
                if (!owner_cell(trc, qr, x, z))
                    continue;
                float tt;
                if (seg_tri(tr, from, d, &tt) && tt < best) {
                    best = tt;
                    best_obj = -1;
                }
            }
        }
    float len = sqrtf(dot(d, d));
    for (uint32_t o = 0; o < w->object_count; o++) {
        const bs_collision_object *ob = &w->objects[o];
        if (!ob->present)
            continue;
        float lf[3], lt[3], ld[3];
        to_local(&ob->xform, from, lf);
        to_local(&ob->xform, to, lt);
        /* Skip objects whose bounding sphere the segment can't reach. */
        float mid[3] = {(lf[0] + lt[0]) * 0.5f, (lf[1] + lt[1]) * 0.5f, (lf[2] + lt[2]) * 0.5f};
        if (sqrtf(dot(mid, mid)) > ob->radius + len * 0.5f)
            continue;
        sub(lt, lf, ld);
        for (uint32_t k = ob->first; k < ob->first + ob->count; k++) {
            float tt;
            if (seg_tri(&w->tris[k], lf, ld, &tt) && tt < best) {
                best = tt;
                best_obj = (int32_t)o;
            }
        }
    }
    if (best > 1)
        return false;
    *t = best;
    *object = best_obj;
    return true;
}
