#include "bs/formats/model.h"

#include <string.h>

#include "bs/core/reader.h"

#define HEADER_SIZE 64
#define POINT_SIZE 12
#define NORMAL_SIZE 12
#define FACE_HEADER_SIZE 10
#define FACE_VERTEX_SIZE 8
#define MAX_COUNT 65536u

/* Header field offsets. */
#define OFF_POINT_COUNT 4
#define OFF_FACE_COUNT 8
#define OFF_RADIUS 12
#define OFF_POINT_LIST 48
#define OFF_NORMAL_LIST 52
#define OFF_FACE_LIST 60

#define BASE40_LIMIT 4096000000u  /* 40^6 */
#define BASE40_PAD 39

static uint32_t u32_at(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bs_result bs_model_parse(const uint8_t *data, size_t size, bs_arena *arena, bs_model *out)
{
    memset(out, 0, sizeof *out);
    if (!data || size < HEADER_SIZE)
        return BS_ERR_FORMAT;
    if (memcmp(data, "v2.7", 4) != 0 && memcmp(data, "v2.6", 4) != 0)
        return BS_ERR_UNSUPPORTED;

    uint32_t point_count = u32_at(data + OFF_POINT_COUNT);
    uint32_t face_count = u32_at(data + OFF_FACE_COUNT);
    if (point_count > MAX_COUNT || face_count > MAX_COUNT)
        return BS_ERR_FORMAT;

    /* Pass 1: walk the face list to size the vertex array and validate it. */
    bs_reader r = bs_reader_make(data, size);
    if (!bs_reader_seek(&r, u32_at(data + OFF_FACE_LIST)))
        return BS_ERR_FORMAT;
    size_t face_list = r.pos;
    uint64_t vertex_count = 0;
    for (uint32_t i = 0; i < face_count; i++) {
        uint8_t n = bs_read_u8(&r);
        bs_reader_skip(&r, FACE_HEADER_SIZE - 1 + (size_t)n * FACE_VERTEX_SIZE);
        vertex_count += n;
    }
    if (!bs_reader_ok(&r))
        return BS_ERR_FORMAT;

    bs_model_point *points = BS_ARENA_ARRAY(arena, bs_model_point, point_count ? point_count : 1);
    bs_model_face *faces = BS_ARENA_ARRAY(arena, bs_model_face, face_count ? face_count : 1);
    bs_model_vertex *vertices = BS_ARENA_ARRAY(arena, bs_model_vertex, vertex_count ? vertex_count : 1);
    if (!points || !faces || !vertices)
        return BS_ERR_NOMEM;

    bs_reader_seek(&r, u32_at(data + OFF_POINT_LIST));
    for (uint32_t i = 0; i < point_count; i++) {
        points[i].x = bs_read_i32le(&r);
        points[i].y = bs_read_i32le(&r);
        points[i].z = bs_read_i32le(&r);
    }
    bs_reader_seek(&r, u32_at(data + OFF_NORMAL_LIST));
    for (uint32_t i = 0; i < face_count; i++)
        for (int k = 0; k < 3; k++)
            faces[i].normal[k] = bs_read_i32le(&r);
    if (!bs_reader_ok(&r))
        return BS_ERR_FORMAT;

    bs_reader_seek(&r, face_list);
    uint32_t v = 0;
    for (uint32_t i = 0; i < face_count; i++) {
        bs_model_face *f = &faces[i];
        f->vertex_count = bs_read_u8(&r);
        f->light = bs_read_u8(&r);
        f->texture = bs_read_u32le(&r);
        bs_reader_skip(&r, 4); /* always zero */
        f->first_vertex = v;
        for (uint32_t k = 0; k < f->vertex_count; k++, v++) {
            uint32_t offset = bs_read_u32le(&r);
            if (offset % POINT_SIZE != 0 || offset / POINT_SIZE >= point_count)
                return BS_ERR_FORMAT;
            vertices[v].point = offset / POINT_SIZE;
            vertices[v].u = bs_read_i16le(&r);
            vertices[v].v = bs_read_i16le(&r);
        }
    }
    if (!bs_reader_ok(&r))
        return BS_ERR_FORMAT;

    memcpy(out->version, data, 4);
    out->version[4] = '\0';
    out->radius = u32_at(data + OFF_RADIUS);
    out->point_count = point_count;
    out->points = points;
    out->face_count = face_count;
    out->faces = faces;
    out->vertex_count = (uint32_t)vertex_count;
    out->vertices = vertices;
    return BS_OK;
}

bs_model_tex_kind bs_model_texture_kind(uint32_t texture)
{
    if (texture == BS_MODEL_TEX_INVISIBLE)
        return BS_MODEL_TEX_NONE;
    if ((texture >> 16) == 0xFFFFu)
        return BS_MODEL_TEX_COLOR;
    if (texture < BASE40_LIMIT)
        return BS_MODEL_TEX_IMAGE;
    return BS_MODEL_TEX_NONE;
}

bool bs_model_texture_name(uint32_t texture, char out[BS_MODEL_TEX_NAME_MAX])
{
    out[0] = '\0';
    if (texture >= BASE40_LIMIT)
        return false;
    uint32_t digits[6];
    for (int i = 5; i >= 0; i--) {
        digits[i] = texture % 40;
        texture /= 40;
    }
    int len = 6;
    while (len > 0 && digits[len - 1] == BASE40_PAD)
        len--;
    for (int i = 0; i < len; i++) {
        uint32_t d = digits[i];
        if (d < 10)
            out[i] = (char)('0' + d);
        else if (d < 36)
            out[i] = (char)('A' + d - 10);
        else if (d == 37)
            out[i] = '_';
        else {
            out[0] = '\0';
            return false; /* 36, 38 never seen; padding only at the end */
        }
    }
    out[len] = '\0';
    return len > 0;
}

void bs_model_face_uvs(const bs_model *m, const bs_model_face *f, float (*uv)[2])
{
    const bs_model_vertex *vs = m->vertices + f->first_vertex;
    uint32_t n = f->vertex_count;
    double su = 0, sv = 0;
    for (uint32_t k = 0; k < n && k < 3; k++) {
        su += vs[k].u;
        sv += vs[k].v;
        uv[k][0] = (float)(su / 16.0);
        uv[k][1] = (float)(sv / 16.0);
    }
    if (n <= 3)
        return;

    /* Express each further point as p0 + s*(p1-p0) + t*(p2-p0) (least squares)
     * and apply the same combination to the UVs. */
    const bs_model_point *p0 = &m->points[vs[0].point];
    const bs_model_point *p1 = &m->points[vs[1].point];
    const bs_model_point *p2 = &m->points[vs[2].point];
    double a[3] = {(double)p1->x - p0->x, (double)p1->y - p0->y, (double)p1->z - p0->z};
    double b[3] = {(double)p2->x - p0->x, (double)p2->y - p0->y, (double)p2->z - p0->z};
    double aa = a[0] * a[0] + a[1] * a[1] + a[2] * a[2];
    double ab = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    double bb = b[0] * b[0] + b[1] * b[1] + b[2] * b[2];
    double det = aa * bb - ab * ab;
    for (uint32_t k = 3; k < n; k++) {
        if (det <= 1e-9 * aa * bb || aa == 0 || bb == 0) {
            uv[k][0] = uv[2][0]; /* degenerate first triangle */
            uv[k][1] = uv[2][1];
            continue;
        }
        const bs_model_point *p = &m->points[vs[k].point];
        double d[3] = {(double)p->x - p0->x, (double)p->y - p0->y, (double)p->z - p0->z};
        double da = d[0] * a[0] + d[1] * a[1] + d[2] * a[2];
        double db = d[0] * b[0] + d[1] * b[1] + d[2] * b[2];
        double s = (da * bb - db * ab) / det;
        double t = (db * aa - da * ab) / det;
        uv[k][0] = (float)(uv[0][0] + s * (uv[1][0] - uv[0][0]) + t * (uv[2][0] - uv[0][0]));
        uv[k][1] = (float)(uv[0][1] + s * (uv[1][1] - uv[0][1]) + t * (uv[2][1] - uv[0][1]));
    }
}
