/* Fuzzes 3D model parsing, texture name decoding and UV derivation. */
#include "bs/formats/model.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_model m;
    if (bs_model_parse(data, size, &a, &m) == BS_OK) {
        volatile float sink = 0;
        float uv[256][2];
        for (uint32_t i = 0; i < m.face_count; i++) {
            const bs_model_face *f = &m.faces[i];
            char name[BS_MODEL_TEX_NAME_MAX];
            bs_model_texture_name(f->texture, name);
            bs_model_face_uvs(&m, f, uv);
            for (uint32_t k = 0; k < f->vertex_count; k++)
                sink += uv[k][0] + (float)m.points[m.vertices[f->first_vertex + k].point].x;
        }
        (void)sink;
    }
    bs_arena_free(&a);
    return 0;
}
