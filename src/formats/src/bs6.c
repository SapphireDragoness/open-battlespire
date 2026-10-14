#include "bs/formats/bs6.h"

#include <math.h>
#include <string.h>

#include "bs/core/reader.h"

#define MAX_ENTITIES 65536u

bs_bs6_iter bs_bs6_iter_make(const uint8_t *data, size_t size)
{
    bs_bs6_iter it = {data, data ? size : 0, 0, false};
    return it;
}

bool bs_bs6_next(bs_bs6_iter *it, bs_bs6_chunk *out)
{
    if (it->err || it->pos >= it->size)
        return false;
    bs_reader r = bs_reader_make(it->data + it->pos, it->size - it->pos);
    const uint8_t *tag = bs_read_span(&r, 4);
    uint32_t len = bs_read_u32le(&r);
    const uint8_t *body = bs_read_span(&r, len);
    if (!tag || !body) {
        it->err = true;
        return false;
    }
    memcpy(out->tag, tag, 4);
    out->tag[4] = '\0';
    out->data = body;
    out->size = len;
    it->pos += 8 + (size_t)len;
    return true;
}

bool bs_bs6_find(const bs_bs6_chunk *parent, const char *tag, bs_bs6_chunk *out)
{
    bs_bs6_iter it = bs_bs6_iter_make(parent->data, parent->size);
    while (bs_bs6_next(&it, out))
        if (memcmp(out->tag, tag, 4) == 0)
            return true;
    return false;
}

static bool is(const bs_bs6_chunk *c, const char *tag)
{
    return memcmp(c->tag, tag, 4) == 0;
}

/* Fixed-size integer chunks. A wrong size marks the level malformed. */
static bool get_i32(const bs_bs6_chunk *c, int32_t *out, int n)
{
    if (c->size != (uint32_t)n * 4)
        return false;
    bs_reader r = bs_reader_make(c->data, c->size);
    for (int i = 0; i < n; i++)
        out[i] = bs_read_i32le(&r);
    return true;
}

static bool get_u32(const bs_bs6_chunk *c, uint32_t *out)
{
    int32_t v;
    if (!get_i32(c, &v, 1))
        return false;
    *out = (uint32_t)v;
    return true;
}

/* NUL-padded name field. */
static const char *get_name(const bs_bs6_chunk *c, bs_arena *arena)
{
    const uint8_t *nul = memchr(c->data, 0, c->size);
    size_t len = nul ? (size_t)(nul - c->data) : c->size;
    char *s = bs_arena_alloc(arena, len + 1, 1);
    if (s)
        memcpy(s, c->data, len);
    return s;
}

static uint32_t count_children(const bs_bs6_chunk *parent, const char *tag, bool *err)
{
    uint32_t n = 0;
    bs_bs6_chunk c;
    bs_bs6_iter it = bs_bs6_iter_make(parent->data, parent->size);
    while (bs_bs6_next(&it, &c))
        if (is(&c, tag))
            n++;
    if (it.err || n > MAX_ENTITIES)
        *err = true;
    return n;
}

static bs_result parse_object(const bs_bs6_chunk *c, bs_bs6_object *o)
{
    memset(o, 0, sizeof *o);
    o->chunk = *c;
    bool ok = true, have_file = false;
    bs_bs6_chunk f;
    bs_bs6_iter it = bs_bs6_iter_make(c->data, c->size);
    while (bs_bs6_next(&it, &f)) {
        if (is(&f, "IDNB"))
            ok &= get_u32(&f, &o->id);
        else if (is(&f, "IDFI"))
            ok &= have_file = get_u32(&f, &o->model);
        else if (is(&f, "POSI"))
            ok &= get_i32(&f, o->pos, 3);
        else if (is(&f, "ANGS"))
            ok &= get_i32(&f, o->angles, 3);
        else if (is(&f, "SELE"))
            ok &= get_u32(&f, &o->selected);
    }
    return ok && have_file && !it.err ? BS_OK : BS_ERR_FORMAT;
}

static bs_result parse_flat(const bs_bs6_chunk *c, bs_arena *arena, bs_bs6_flat *fl)
{
    memset(fl, 0, sizeof *fl);
    fl->chunk = *c;
    fl->name = "";
    bool ok = true;
    bs_bs6_chunk f;
    bs_bs6_iter it = bs_bs6_iter_make(c->data, c->size);
    while (bs_bs6_next(&it, &f)) {
        if (is(&f, "IDNB"))
            ok &= get_u32(&f, &fl->id);
        else if (is(&f, "FILN")) {
            fl->name = get_name(&f, arena);
            if (!fl->name)
                return BS_ERR_NOMEM;
        } else if (is(&f, "POSI"))
            ok &= get_i32(&f, fl->pos, 3);
        else if (is(&f, "SCAL"))
            ok &= get_i32(&f, &fl->scale, 1);
        else if (is(&f, "AMBI"))
            ok &= get_i32(&f, &fl->ambient, 1);
        else if (is(&f, "SELE"))
            ok &= get_u32(&f, &fl->selected);
    }
    return ok && !it.err ? BS_OK : BS_ERR_FORMAT;
}

static bs_result parse_light(const bs_bs6_chunk *c, bs_bs6_light *l)
{
    memset(l, 0, sizeof *l);
    l->chunk = *c;
    bool ok = true;
    bs_bs6_chunk f;
    bs_bs6_iter it = bs_bs6_iter_make(c->data, c->size);
    while (bs_bs6_next(&it, &f)) {
        if (is(&f, "IDNB"))
            ok &= get_u32(&f, &l->id);
        else if (is(&f, "IDTY"))
            ok &= get_u32(&f, &l->type);
        else if (is(&f, "POSI"))
            ok &= get_i32(&f, l->pos, 3);
        else if (is(&f, "RADI"))
            ok &= get_i32(&f, &l->radius, 1);
        else if (is(&f, "BRIT"))
            ok &= get_i32(&f, &l->brightness, 1);
    }
    return ok && !it.err ? BS_OK : BS_ERR_FORMAT;
}

static bs_result parse_view(const bs_bs6_chunk *c, bs_bs6_view *v)
{
    memset(v, 0, sizeof *v);
    bool ok = true;
    bs_bs6_chunk f;
    bs_bs6_iter it = bs_bs6_iter_make(c->data, c->size);
    while (bs_bs6_next(&it, &f)) {
        if (is(&f, "IDNB"))
            ok &= get_u32(&f, &v->id);
        else if (is(&f, "IDTY"))
            ok &= get_u32(&f, &v->type);
        else if (is(&f, "POSI"))
            ok &= get_i32(&f, v->pos, 3);
        else if (is(&f, "ANGS"))
            ok &= get_i32(&f, v->angles, 3);
        else if (is(&f, "AMBI"))
            ok &= get_i32(&f, &v->ambient, 1);
    }
    return ok && !it.err ? BS_OK : BS_ERR_FORMAT;
}

static bs_result parse_model_names(const bs_bs6_chunk *lfil, bs_arena *arena, bs_bs6_level *out)
{
    if (lfil->size % BS_BS6_NAME_FIELD != 0 || lfil->size / BS_BS6_NAME_FIELD > MAX_ENTITIES)
        return BS_ERR_FORMAT;
    uint32_t n = lfil->size / BS_BS6_NAME_FIELD;
    const char **names = BS_ARENA_ARRAY(arena, const char *, n ? n : 1);
    if (!names)
        return BS_ERR_NOMEM;
    for (uint32_t i = 0; i < n; i++) {
        bs_bs6_chunk entry = {"", lfil->data + (size_t)i * BS_BS6_NAME_FIELD, BS_BS6_NAME_FIELD};
        names[i] = get_name(&entry, arena);
        if (!names[i])
            return BS_ERR_NOMEM;
    }
    out->model_names = names;
    out->model_count = n;
    return BS_OK;
}

/* Allocates and fills one entity array from the children of `list` tagged `tag`. */
#define PARSE_LIST(list, tag, T, arr, count, ...)                         \
    do {                                                                  \
        bool err_ = false;                                                \
        uint32_t n_ = count_children(&(list), tag, &err_);                \
        if (err_)                                                         \
            return BS_ERR_FORMAT;                                         \
        T *a_ = BS_ARENA_ARRAY(arena, T, n_ ? n_ : 1);                    \
        if (!a_)                                                          \
            return BS_ERR_NOMEM;                                          \
        uint32_t i_ = 0;                                                  \
        bs_bs6_chunk c_;                                                  \
        bs_bs6_iter it_ = bs_bs6_iter_make((list).data, (list).size);     \
        while (bs_bs6_next(&it_, &c_)) {                                  \
            if (!is(&c_, tag))                                            \
                continue;                                                 \
            bs_result r_ = __VA_ARGS__;                                   \
            if (r_ != BS_OK)                                              \
                return r_;                                                \
            i_++;                                                         \
        }                                                                 \
        arr = a_;                                                         \
        count = n_;                                                       \
    } while (0)

bs_result bs_bs6_parse(const uint8_t *data, size_t size, bs_arena *arena, bs_bs6_level *out)
{
    memset(out, 0, sizeof *out);
    bs_bs6_iter top = bs_bs6_iter_make(data, size);
    bs_bs6_chunk gnrl;
    if (!bs_bs6_next(&top, &gnrl) || !is(&gnrl, "GNRL"))
        return BS_ERR_FORMAT;

    bs_bs6_chunk objs, c;
    if (!bs_bs6_find(&gnrl, "OBJS", &objs) || !bs_bs6_find(&objs, "LFIL", &c))
        return BS_ERR_FORMAT;
    bs_result r = parse_model_names(&c, arena, out);
    if (r != BS_OK)
        return r;
    PARSE_LIST(objs, "OBJD", bs_bs6_object, out->objects, out->object_count, parse_object(&c_, &a_[i_]));
    for (uint32_t i = 0; i < out->object_count; i++)
        if (out->objects[i].model >= out->model_count)
            return BS_ERR_FORMAT;

    bs_bs6_chunk list;
    if (bs_bs6_find(&gnrl, "FLAS", &list))
        PARSE_LIST(list, "FLAD", bs_bs6_flat, out->flats, out->flat_count, parse_flat(&c_, arena, &a_[i_]));
    if (bs_bs6_find(&gnrl, "LITS", &list))
        PARSE_LIST(list, "LITD", bs_bs6_light, out->lights, out->light_count, parse_light(&c_, &a_[i_]));
    PARSE_LIST(gnrl, "VIEW", bs_bs6_view, out->views, out->view_count, parse_view(&c_, &a_[i_]));

    if (bs_bs6_find(&gnrl, "BBOX", &c)) {
        if (!get_i32(&c, out->bbox, 6))
            return BS_ERR_FORMAT;
        out->has_bbox = true;
    }
    if (bs_bs6_find(&gnrl, "WATR", &c) && !get_i32(&c, &out->water, 1))
        return BS_ERR_FORMAT;
    return BS_OK;
}

void bs_bs6_rotation(const int32_t angles[3], float m[9])
{
    const double k = 2.0 * 3.14159265358979323846 / BS_BS6_ANGLE_TURN;
    /* Reduce to one turn first: keeps precision, and negating stays in double
     * (negating INT32_MIN as an int would overflow). */
    double ax = (double)(angles[0] % BS_BS6_ANGLE_TURN) * k;
    double ay = -(double)(angles[1] % BS_BS6_ANGLE_TURN) * k;
    double az = (double)(angles[2] % BS_BS6_ANGLE_TURN) * k;
    double cx = cos(ax), sx = sin(ax), cy = cos(ay), sy = sin(ay), cz = cos(az), sz = sin(az);
    /* Rz * Rx * Ry */
    double rx_ry[9] = {
        cy, 0, sy,
        sx * sy, cx, -sx * cy,
        -cx * sy, sx, cx * cy,
    };
    for (int col = 0; col < 3; col++) {
        double a = rx_ry[0 + col], b = rx_ry[3 + col], c = rx_ry[6 + col];
        m[0 + col] = (float)(cz * a - sz * b);
        m[3 + col] = (float)(sz * a + cz * b);
        m[6 + col] = (float)c;
    }
}

/* ---- entity logic ---- */

/* IDNB plus RAWD (STRU, CTRL) or IDTY plus IDNB (LINK). */
static bool parse_record(const bs_bs6_chunk *c, bs_bs6_record *out)
{
    bool have_id = false, have_raw = false;
    bs_bs6_chunk f;
    bs_bs6_iter it = bs_bs6_iter_make(c->data, c->size);
    while (bs_bs6_next(&it, &f)) {
        if (is(&f, "IDNB"))
            have_id = get_u32(&f, &out->id);
        else if (is(&f, "RAWD")) {
            out->data = f.data;
            out->size = f.size;
            have_raw = true;
        }
    }
    return !it.err && have_id && have_raw;
}

static bool parse_link(const bs_bs6_chunk *c, bs_bs6_link *out)
{
    bool have_type = false, have_target = false;
    bs_bs6_chunk f;
    bs_bs6_iter it = bs_bs6_iter_make(c->data, c->size);
    while (bs_bs6_next(&it, &f)) {
        if (is(&f, "IDTY"))
            have_type = get_u32(&f, &out->type);
        else if (is(&f, "IDNB"))
            have_target = get_u32(&f, &out->target);
    }
    return !it.err && have_type && have_target;
}

bs_result bs_bs6_entity_logic(const bs_bs6_chunk *entity, bs_arena *arena, bs_bs6_logic *out)
{
    memset(out, 0, sizeof *out);
    bool err = false;
    uint32_t np = count_children(entity, "STRU", &err), nc = count_children(entity, "CTRL", &err),
             nl = count_children(entity, "LINK", &err);
    if (err)
        return BS_ERR_FORMAT;
    out->props = BS_ARENA_ARRAY(arena, bs_bs6_record, np ? np : 1);
    out->ctrls = BS_ARENA_ARRAY(arena, bs_bs6_record, nc ? nc : 1);
    out->links = BS_ARENA_ARRAY(arena, bs_bs6_link, nl ? nl : 1);
    if (!out->props || !out->ctrls || !out->links)
        return BS_ERR_NOMEM;
    bs_bs6_chunk c;
    bs_bs6_iter it = bs_bs6_iter_make(entity->data, entity->size);
    while (bs_bs6_next(&it, &c)) {
        bool ok = true;
        if (is(&c, "STRU"))
            ok = parse_record(&c, &out->props[out->prop_count++]);
        else if (is(&c, "CTRL"))
            ok = parse_record(&c, &out->ctrls[out->ctrl_count++]);
        else if (is(&c, "LINK"))
            ok = parse_link(&c, &out->links[out->link_count++]);
        if (!ok)
            return BS_ERR_FORMAT;
    }
    return it.err ? BS_ERR_FORMAT : BS_OK;
}

int32_t bs_bs6_record_i32(const bs_bs6_record *r, uint32_t i)
{
    if (!r || (uint64_t)i * 4 + 4 > r->size)
        return 0;
    const uint8_t *p = r->data + (size_t)i * 4;
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

const bs_bs6_record *bs_bs6_logic_prop(const bs_bs6_logic *l, uint32_t id)
{
    for (uint32_t i = 0; i < l->prop_count; i++)
        if (l->props[i].id == id)
            return &l->props[i];
    return NULL;
}
