#include "gltf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/core/fs.h"
#include "bs/core/png.h"
#include "bs/formats/bsi.h"

#define GL_FLOAT 5126
#define GL_ARRAY_BUFFER 34962
#define GL_NEAREST 9728
#define GL_REPEAT 10497
#define MODE_TRIANGLES 4

static void to_gltf(const float in[3], float scale, float out[3])
{
    out[0] = in[0] * scale;
    out[1] = -in[1] * scale;
    out[2] = -in[2] * scale;
}

static float srgb_to_linear(uint8_t c)
{
    float v = (float)c / 255.0f;
    return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
}

/* Writes the vertex arrays (positions, normals, uvs) as one binary buffer. */
static bs_result write_bin(const char *path, const bs_scene *s, float scale)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return BS_ERR_IO;
    bool ok = true;
    for (uint32_t i = 0; i < s->vertex_count && ok; i++) {
        float p[3];
        to_gltf(s->vertices[i].pos, scale, p);
        ok = fwrite(p, sizeof p, 1, f) == 1;
    }
    for (uint32_t i = 0; i < s->vertex_count && ok; i++) {
        float n[3];
        to_gltf(s->vertices[i].normal, 1.0f, n);
        float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (int c = 0; c < 3; c++)
            n[c] = len > 0 ? n[c] / len : (c == 1 ? 1.0f : 0.0f);
        ok = fwrite(n, sizeof n, 1, f) == 1;
    }
    for (uint32_t i = 0; i < s->vertex_count && ok; i++)
        ok = fwrite(s->vertices[i].uv, sizeof s->vertices[i].uv, 1, f) == 1;
    if (fclose(f) != 0)
        ok = false;
    return ok ? BS_OK : BS_ERR_IO;
}

static bs_result write_texture(const char *dir, const bs_scene_material *m)
{
    const bs_bsi_image *img = m->image;
    char file[64], path[BS_PATH_MAX];
    snprintf(file, sizeof file, "textures/%s.png", m->name);
    if (!bs_path_join(path, sizeof path, dir, file))
        return BS_ERR_ARGS;
    uint8_t pal[768];
    for (int i = 0; i < 256; i++)
        bs_bsi_pixel_rgb(img, (uint8_t)i, pal + i * 3);
    return bs_png_write_indexed(path, img->width, img->height, img->pixels, pal, img->transparent ? 0 : -1);
}

/* JSON string body for game names (plain ASCII, but escape to be safe). */
static void json_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\')
            fprintf(f, "\\%c", c);
        else if (c < 0x20 || c >= 0x7F)
            fprintf(f, "\\u%04x", c);
        else
            fputc(c, f);
    }
    fputc('"', f);
}

bs_result gltf_write_scene(const char *dir, const char *stem, const bs_scene *s, float scale, bool markers)
{
    char path[BS_PATH_MAX], tex_dir[BS_PATH_MAX], bin_name[256], gltf_name[256];
    snprintf(bin_name, sizeof bin_name, "%s.bin", stem);
    snprintf(gltf_name, sizeof gltf_name, "%s.gltf", stem);
    if (!bs_path_join(tex_dir, sizeof tex_dir, dir, "textures") || bs_fs_mkdirs(tex_dir) != BS_OK)
        return BS_ERR_IO;

    if (!bs_path_join(path, sizeof path, dir, bin_name))
        return BS_ERR_ARGS;
    bs_result r = write_bin(path, s, scale);
    if (r != BS_OK)
        return r;

    /* Texture index per material (-1 if none). */
    int *tex_of = calloc(s->material_count + 1, sizeof *tex_of);
    if (!tex_of)
        return BS_ERR_NOMEM;
    int textures = 0;
    for (uint32_t i = 0; i < s->material_count; i++) {
        tex_of[i] = -1;
        if (s->materials[i].kind == BS_SCENE_MAT_TEXTURE) {
            r = write_texture(dir, &s->materials[i]);
            if (r != BS_OK) {
                free(tex_of);
                return r;
            }
            tex_of[i] = textures++;
        }
    }

    if (!bs_path_join(path, sizeof path, dir, gltf_name)) {
        free(tex_of);
        return BS_ERR_ARGS;
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        free(tex_of);
        return BS_ERR_IO;
    }
    uint64_t n = s->vertex_count;
    fprintf(f, "{\n\"asset\":{\"version\":\"2.0\",\"generator\":\"OpenBattlespire bstool\"},\n");
    fprintf(f, "\"buffers\":[{\"uri\":");
    json_str(f, bin_name);
    fprintf(f, ",\"byteLength\":%llu}],\n", (unsigned long long)(n * 32));
    fprintf(f, "\"bufferViews\":[\n"
               "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":%llu,\"target\":%d},\n"
               "{\"buffer\":0,\"byteOffset\":%llu,\"byteLength\":%llu,\"target\":%d},\n"
               "{\"buffer\":0,\"byteOffset\":%llu,\"byteLength\":%llu,\"target\":%d}],\n",
            (unsigned long long)(n * 12), GL_ARRAY_BUFFER, (unsigned long long)(n * 12),
            (unsigned long long)(n * 12), GL_ARRAY_BUFFER, (unsigned long long)(n * 24),
            (unsigned long long)(n * 8), GL_ARRAY_BUFFER);

    /* Three accessors per batch: position, normal, uv. */
    fprintf(f, "\"accessors\":[");
    for (uint32_t b = 0; b < s->batch_count; b++) {
        const bs_scene_batch *batch = &s->batches[b];
        float mn[3] = {INFINITY, INFINITY, INFINITY}, mx[3] = {-INFINITY, -INFINITY, -INFINITY};
        for (uint32_t v = batch->first; v < batch->first + batch->count; v++) {
            float p[3];
            to_gltf(s->vertices[v].pos, scale, p);
            for (int c = 0; c < 3; c++) {
                mn[c] = fminf(mn[c], p[c]);
                mx[c] = fmaxf(mx[c], p[c]);
            }
        }
        if (!batch->count)
            mn[0] = mn[1] = mn[2] = mx[0] = mx[1] = mx[2] = 0;
        fprintf(f, "%s\n{\"bufferView\":0,\"byteOffset\":%llu,\"componentType\":%d,\"count\":%u,"
                   "\"type\":\"VEC3\",\"min\":[%g,%g,%g],\"max\":[%g,%g,%g]},",
                b ? "," : "", (unsigned long long)batch->first * 12, GL_FLOAT, batch->count,
                (double)mn[0], (double)mn[1], (double)mn[2], (double)mx[0], (double)mx[1], (double)mx[2]);
        fprintf(f, "\n{\"bufferView\":1,\"byteOffset\":%llu,\"componentType\":%d,\"count\":%u,\"type\":\"VEC3\"},",
                (unsigned long long)batch->first * 12, GL_FLOAT, batch->count);
        fprintf(f, "\n{\"bufferView\":2,\"byteOffset\":%llu,\"componentType\":%d,\"count\":%u,\"type\":\"VEC2\"}",
                (unsigned long long)batch->first * 8, GL_FLOAT, batch->count);
    }
    fprintf(f, "],\n");

    fprintf(f, "\"materials\":[");
    for (uint32_t i = 0; i < s->material_count; i++) {
        const bs_scene_material *m = &s->materials[i];
        fprintf(f, "%s\n{\"name\":", i ? "," : "");
        if (m->kind == BS_SCENE_MAT_COLOR) {
            char name[32];
            snprintf(name, sizeof name, "color_%02x%02x%02x", m->rgb[0], m->rgb[1], m->rgb[2]);
            json_str(f, name);
            fprintf(f, ",\"pbrMetallicRoughness\":{\"baseColorFactor\":[%g,%g,%g,1],"
                       "\"metallicFactor\":0,\"roughnessFactor\":1}}",
                    (double)srgb_to_linear(m->rgb[0]), (double)srgb_to_linear(m->rgb[1]),
                    (double)srgb_to_linear(m->rgb[2]));
        } else if (m->kind == BS_SCENE_MAT_TEXTURE) {
            json_str(f, m->name);
            fprintf(f, ",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":%d},"
                       "\"metallicFactor\":0,\"roughnessFactor\":1}%s}",
                    tex_of[i], m->image->transparent ? ",\"alphaMode\":\"MASK\"" : "");
        } else {
            char name[32];
            snprintf(name, sizeof name, "missing_%s", m->name);
            json_str(f, name);
            fprintf(f, ",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0,1,1],"
                       "\"metallicFactor\":0,\"roughnessFactor\":1}}");
        }
    }
    fprintf(f, "],\n");

    if (textures) {
        fprintf(f, "\"samplers\":[{\"magFilter\":%d,\"minFilter\":%d,\"wrapS\":%d,\"wrapT\":%d}],\n",
                GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT);
        fprintf(f, "\"images\":[");
        int k = 0;
        for (uint32_t i = 0; i < s->material_count; i++) {
            if (tex_of[i] < 0)
                continue;
            char uri[64];
            snprintf(uri, sizeof uri, "textures/%s.png", s->materials[i].name);
            fprintf(f, "%s\n{\"uri\":", k++ ? "," : "");
            json_str(f, uri);
            fputc('}', f);
        }
        fprintf(f, "],\n\"textures\":[");
        for (int i = 0; i < textures; i++)
            fprintf(f, "%s{\"sampler\":0,\"source\":%d}", i ? "," : "", i);
        fprintf(f, "],\n");
    }

    fprintf(f, "\"meshes\":[{\"name\":");
    json_str(f, stem);
    fprintf(f, ",\"primitives\":[");
    for (uint32_t b = 0; b < s->batch_count; b++)
        fprintf(f, "%s\n{\"attributes\":{\"POSITION\":%u,\"NORMAL\":%u,\"TEXCOORD_0\":%u},\"material\":%u,\"mode\":%d}",
                b ? "," : "", b * 3, b * 3 + 1, b * 3 + 2, s->batches[b].material, MODE_TRIANGLES);
    fprintf(f, "]}],\n");

    fprintf(f, "\"nodes\":[{\"name\":");
    json_str(f, stem);
    fprintf(f, ",\"mesh\":0}");
    uint32_t nodes = 1;
    for (uint32_t i = 0; i < s->sprite_count; i++) {
        const bs_scene_sprite *sp = &s->sprites[i];
        if (sp->marker && !markers)
            continue;
        char name[96];
        snprintf(name, sizeof name, "flat:%s", sp->name);
        float p[3];
        to_gltf(sp->pos, scale, p);
        fprintf(f, ",\n{\"name\":");
        json_str(f, name);
        fprintf(f, ",\"translation\":[%g,%g,%g]}", (double)p[0], (double)p[1], (double)p[2]);
        nodes++;
    }
    fprintf(f, "],\n\"scenes\":[{\"nodes\":[");
    for (uint32_t i = 0; i < nodes; i++)
        fprintf(f, "%s%u", i ? "," : "", i);
    fprintf(f, "]}],\n\"scene\":0\n}\n");

    free(tex_of);
    return fclose(f) == 0 ? BS_OK : BS_ERR_IO;
}
