#include "bs/render/level_renderer.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "bs/core/fs.h"
#include "bs/core/log.h"
#include "bs/formats/palette.h"
#include "bs/render/gl.h"

#define MAX_LIGHTS 256
#define CLASSIC_W 320  /* the game renders its 3D view at 320x240 and doubles each pixel (measured) */
#define CLASSIC_H 240
#define PLAYER_LIGHT_LEVELS 40.0f    /* measured in two memory dumps */
#define PLAYER_LIGHT_RADIUS 384.0f
#define BHDR_FRAME_MS 8              /* BSI header field 8: frame time in ms (inferred; 71 like FLIC) */
#define BHDR_SCALE 11                /* field 11: the scale in 1/256 (see assets/scene.c) */
#define SKY_REPEATS 4.0f             /* sky texture repeats around the horizon (approximation) */

/*
 * Light as GAME.EXE computes it, measured against the original with memory
 * dumps (docs/re/README.md): per pixel, each light adds BRIT shade levels (of
 * 31) scaled by 1 - d^2/R^2, but only to faces whose front side faces it (no
 * cosine falloff). The player carries a light of 40 levels and radius 384, 12
 * units above the eye. The face header byte adds half a level per unit, and
 * the level's ambient (LEVELS.TXT field 7 / 512 levels) comes in u_ambient.
 */
#define LIGHT_GLSL \
    "layout(std140) uniform Lights {\n" \
    "    vec4 light_pos_radius[256];\n" \
    "    vec4 light_brightness[256];\n" \
    "};\n" \
    "uniform int u_light_count;\n" \
    "uniform vec4 u_player_light;  /* xyz position, w radius; w = 0: none */\n" \
    "uniform float u_player_brightness;\n" \
    "/* n = 0 (sprites): no front-side test. */\n" \
    "float light_at(vec3 p, vec3 n) {\n" \
    "    float l = 0.0;\n" \
    "    bool sided = dot(n, n) > 0.0;\n" \
    "    for (int i = 0; i < u_light_count; i++) {\n" \
    "        vec4 lr = light_pos_radius[i];\n" \
    "        vec3 v = lr.xyz - p;\n" \
    "        if (sided && dot(n, v) <= 0.0) continue;\n" \
    "        l += light_brightness[i].x * max(0.0, 1.0 - dot(v, v) / (lr.w * lr.w));\n" \
    "    }\n" \
    "    if (u_player_light.w > 0.0) {\n" \
    "        vec3 v = u_player_light.xyz - p;\n" \
    "        if (!sided || dot(n, v) > 0.0)\n" \
    "            l += u_player_brightness * max(0.0, 1.0 - dot(v, v) / (u_player_light.w * u_player_light.w));\n" \
    "    }\n" \
    "    return l;\n" \
    "}\n"

/* ---- shaders ---- */

static const char *const geometry_vs =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_pos;\n"
    "layout(location = 1) in vec3 a_normal;\n"
    "layout(location = 2) in vec2 a_uv;\n"
    "layout(location = 3) in float a_face_byte;\n"
    "uniform mat4 u_mvp;\n"
    "uniform mat4 u_model;\n"   /* separate objects: model space to level; otherwise identity */
    "uniform int u_vertex_light;\n"
    LIGHT_GLSL
    "out vec3 v_pos;\n"
    "out vec3 v_normal;\n"
    "out vec2 v_uv;\n"
    "out float v_light;\n"
    "flat out float v_face_byte;\n"
    "void main() {\n"
    "    v_pos = (u_model * vec4(a_pos, 1.0)).xyz;\n"
    "    v_normal = mat3(u_model) * a_normal;\n"
    "    v_face_byte = a_face_byte;\n"
    "    v_light = u_vertex_light != 0 ? light_at(v_pos, v_normal) : -1.0;\n"
    "    v_uv = a_uv;\n"
    "    gl_Position = u_mvp * vec4(v_pos, 1.0);\n"
    "}\n";

/* Billboards: a_pos.xy is the corner in [-0.5, 0.5]. */
static const char *const sprite_vs =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_pos;\n"
    "uniform mat4 u_mvp;\n"
    "uniform vec3 u_center;\n"
    "uniform vec2 u_size;\n"
    "uniform vec3 u_right;\n"
    "uniform vec3 u_up;\n"
    "uniform vec2 u_frame;  /* v offset and scale of the current frame */\n"
    "out vec3 v_pos;\n"
    "out vec3 v_normal;\n"
    "out vec2 v_uv;\n"
    "out float v_light;\n"
    "flat out float v_face_byte;\n"
    "void main() {\n"
    "    v_normal = vec3(0.0);\n"
    "    v_face_byte = 0.0;\n"
    "    v_light = -1.0;\n"
    "    v_pos = u_center + u_right * a_pos.x * u_size.x + u_up * a_pos.y * u_size.y;\n"
    "    v_uv = vec2(a_pos.x + 0.5, u_frame.x + (0.5 - a_pos.y) * u_frame.y);\n"
    "    gl_Position = u_mvp * vec4(v_pos, 1.0);\n"
    "}\n";

static const char *const shade_fs =
    "#version 330 core\n"
    "in vec3 v_pos;\n"
    "in vec3 v_normal;\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "uniform int u_textured;\n"
    "uniform int u_debug;\n"
    "uniform vec4 u_color;\n"
    "uniform vec3 u_eye;\n"
    "uniform int u_lighting;\n"
    "uniform int u_classic;\n"
    "uniform float u_ambient;\n"
    "uniform int u_fog;\n"
    "uniform vec2 u_fog_range;\n"
    "uniform vec3 u_fog_color;\n"
    "in float v_light;\n"
    "flat in float v_face_byte;\n"
    LIGHT_GLSL
    "out vec4 o_color;\n"
    "void main() {\n"
    "    vec4 c = u_textured != 0 ? texture(u_tex, v_uv) : u_color;\n"
    "    if (c.a < 0.5) discard;\n"
    "    if (u_debug == 1) { o_color = vec4(normalize(v_normal) * 0.5 + 0.5, 1.0); return; }\n"
    "    if (u_debug == 2) { o_color = vec4(vec3(v_face_byte / 255.0), 1.0); return; }\n"
    "    /* Nothing is drawn past the view distance, where fog is complete (inferred). */\n"
    "    if (u_fog != 0 && distance(v_pos, u_eye) > u_fog_range.y) discard;\n"
    "    float l = 1.0;\n"
    "    if (u_lighting != 0) {\n"
    "        /* shade level in units of the full 31 levels */\n"
    "        float lit = v_light >= 0.0 ? v_light : light_at(v_pos, v_normal);\n"
    "        /* the face byte counts in half levels (GAME.EXE: byte << 8, a level is 512) */\n"
    "        l = min(u_ambient + v_face_byte / 62.0 + lit, 1.0);\n"
    "        if (u_classic != 0) l = floor(l * 31.0) / 31.0;  /* HTBL has 32 levels */\n"
    "    }\n"
    "    vec3 rgb = c.rgb * l;\n"
    "    if (u_fog != 0) {\n"
    "        float f = clamp((distance(v_pos, u_eye) - u_fog_range.x) / max(u_fog_range.y - u_fog_range.x, 1.0), 0.0, 1.0);\n"
    "        if (u_classic != 0) f = floor(f * 16.0 + 0.5) / 16.0;\n"
    "        rgb = mix(rgb, u_fog_color, f);\n"
    "    }\n"
    "    o_color = vec4(rgb, 1.0);\n"
    "}\n";

/* Full-screen triangle; the view direction is rebuilt from the camera basis. */
static const char *const sky_vs =
    "#version 330 core\n"
    "out vec2 v_ndc;\n"
    "void main() {\n"
    "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;\n"
    "    v_ndc = p;\n"
    "    gl_Position = vec4(p, 1.0, 1.0);\n"
    "}\n";

static const char *const sky_fs =
    "#version 330 core\n"
    "in vec2 v_ndc;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec3 u_fwd;\n"
    "uniform vec3 u_right;\n"
    "uniform vec3 u_up;\n"
    "uniform vec2 u_tan;\n"
    "uniform float u_repeats;\n"
    "uniform vec3 u_fog_color;\n"
    "uniform int u_fog_on;\n"
    "out vec4 o_color;\n"
    "void main() {\n"
    "    vec3 d = normalize(u_fwd + v_ndc.x * u_tan.x * u_right + v_ndc.y * u_tan.y * u_up);\n"
    "    float elev = asin(clamp(-d.y, -1.0, 1.0));  /* game axes: up is -y */\n"
    "    float u = atan(d.x, d.z) / 6.28318530718 * u_repeats;\n"
    "    float v = 1.0 - clamp(elev / 1.57079632679, 0.0, 1.0);\n"
    "    vec3 c = texture(u_tex, vec2(u, v)).rgb;\n"
    "    /* With fog, a band of fog colour rises above the horizon (as in the game's\n"
    "       outdoor levels); without, only fade below it instead of smearing the bottom row. */\n"
    "    float f = u_fog_on != 0 ? clamp(1.0 - elev / 0.15, 0.0, 1.0) : clamp(-elev * 6.0, 0.0, 1.0);\n"
    "    o_color = vec4(mix(c, u_fog_color, f), 1.0);\n"
    "}\n";

/* ---- state ---- */

typedef struct gpu_material {
    GLuint texture;  /* 0 for colour materials */
    float color[4];
} gpu_material;

typedef struct gpu_sprite {
    GLuint texture;
    float pos[3];
    float size[2];
    uint32_t frames;
    uint32_t frame_ms;
    bool marker;
} gpu_sprite;

typedef struct program {
    GLuint id;
    GLint mvp, model, tex, textured, color, eye, lighting, classic, ambient, fog, fog_range, fog_color, light_count, debug;
    GLint vertex_light, player_light, player_brightness;
    GLint center, size, right, up, frame;
} program;

struct bs_level_renderer {
    const bs_scene *scene;
    bs_scene_xform *xforms;   /* per level object: placement of separate objects */
    bool *hidden;             /* per level object */
    uint32_t object_count;
    uint32_t *object_first, *object_batches;  /* per level object: its batches (they are grouped) */
    bs_level_instance *instances;
    uint32_t instance_count, instance_cap;
    bs_level_sprite *dyn;            /* sprites set for the frame */
    uint32_t dyn_count, dyn_cap;
    const bs_bsi_image **dyn_images; /* their images' textures, kept */
    GLuint *dyn_textures;
    uint32_t dyn_image_count, dyn_image_cap;
    program geometry, sprite;
    GLuint sky_program;
    GLint sky_tex, sky_fwd, sky_right, sky_up, sky_tan, sky_repeats, sky_fog, sky_fog_on;
    GLuint vao, vbo, quad_vao, quad_vbo, empty_vao, lights_ubo;
    uint32_t light_count;
    gpu_material *materials;
    gpu_sprite *sprites;
    uint32_t sprite_count;
    GLuint sky_texture;
    bool classic_filters;  /* current texture filter state */
    GLuint fbo, fbo_color, fbo_depth;
    GLuint water_vao, water_vbo, water_texture;  /* water plane; 0 if none */
};

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        BS_ERROR("shader compile failed: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link_program(const char *vs_src, const char *fs_src)
{
    GLuint vs = compile(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        BS_ERROR("shader link failed: %s", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

static bool make_program(program *p, const char *vs)
{
    p->id = link_program(vs, shade_fs);
    if (!p->id)
        return false;
    p->mvp = glGetUniformLocation(p->id, "u_mvp");
    p->model = glGetUniformLocation(p->id, "u_model");
    p->tex = glGetUniformLocation(p->id, "u_tex");
    p->textured = glGetUniformLocation(p->id, "u_textured");
    p->color = glGetUniformLocation(p->id, "u_color");
    p->eye = glGetUniformLocation(p->id, "u_eye");
    p->lighting = glGetUniformLocation(p->id, "u_lighting");
    p->classic = glGetUniformLocation(p->id, "u_classic");
    p->ambient = glGetUniformLocation(p->id, "u_ambient");
    p->fog = glGetUniformLocation(p->id, "u_fog");
    p->fog_range = glGetUniformLocation(p->id, "u_fog_range");
    p->fog_color = glGetUniformLocation(p->id, "u_fog_color");
    p->light_count = glGetUniformLocation(p->id, "u_light_count");
    p->debug = glGetUniformLocation(p->id, "u_debug");
    p->vertex_light = glGetUniformLocation(p->id, "u_vertex_light");
    p->player_light = glGetUniformLocation(p->id, "u_player_light");
    p->player_brightness = glGetUniformLocation(p->id, "u_player_brightness");
    p->center = glGetUniformLocation(p->id, "u_center");
    p->size = glGetUniformLocation(p->id, "u_size");
    p->right = glGetUniformLocation(p->id, "u_right");
    p->up = glGetUniformLocation(p->id, "u_up");
    p->frame = glGetUniformLocation(p->id, "u_frame");
    GLuint block = glGetUniformBlockIndex(p->id, "Lights");
    if (block != GL_INVALID_INDEX)
        glUniformBlockBinding(p->id, block, 0);
    return true;
}

/* RGBA8 pixels for `frames` frames of an image (HICL colours; pixel 0 clear if transparent). */
static uint8_t *image_rgba(const bs_bsi_image *img, uint32_t frames)
{
    size_t n = (size_t)img->width * img->height * frames;
    uint8_t *rgba = malloc(n * 4 + 4);
    if (!rgba)
        return NULL;
    uint8_t lut[256][4];
    for (int i = 0; i < 256; i++) {
        bs_bsi_pixel_rgb(img, (uint8_t)i, lut[i]);
        lut[i][3] = (img->transparent && i == 0) ? 0 : 255;
    }
    for (size_t i = 0; i < n; i++)
        memcpy(rgba + i * 4, lut[img->pixels[i]], 4);
    return rgba;
}

static GLuint upload_image(const bs_bsi_image *img, uint32_t frames, bool repeat)
{
    if (!img->width || !img->height || !img->pixels)
        return 0;
    uint8_t *rgba = image_rgba(img, frames);
    if (!rgba)
        return 0;
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img->width, (GLsizei)(img->height * frames), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba);
    glGenerateMipmap(GL_TEXTURE_2D);
    GLint wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    free(rgba);
    return t;
}

static void set_filter(GLuint t, bool classic)
{
    if (!t)
        return;
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, classic ? GL_NEAREST : GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, classic ? GL_NEAREST : GL_LINEAR);
}

static void apply_filters(bs_level_renderer *r, bool classic)
{
    for (uint32_t i = 0; i < r->scene->material_count; i++)
        set_filter(r->materials[i].texture, classic);
    for (uint32_t i = 0; i < r->sprite_count; i++)
        set_filter(r->sprites[i].texture, classic);
    for (uint32_t i = 0; i < r->dyn_image_count; i++)
        set_filter(r->dyn_textures[i], classic);
    set_filter(r->sky_texture, classic);
    r->classic_filters = classic;
}

static void upload_lights(bs_level_renderer *r, const bs_bs6_level *level)
{
    static float data[MAX_LIGHTS * 8];
    memset(data, 0, sizeof data);
    uint32_t n = level->light_count < MAX_LIGHTS ? level->light_count : MAX_LIGHTS;
    if (level->light_count > MAX_LIGHTS)
        BS_WARN("level has %u lights; using the first %d", level->light_count, MAX_LIGHTS);
    for (uint32_t i = 0; i < n; i++) {
        const bs_bs6_light *l = &level->lights[i];
        for (int c = 0; c < 3; c++)
            data[i * 4 + c] = (float)l->pos[c];
        data[i * 4 + 3] = l->radius > 0 ? (float)l->radius : 1.0f;
        /* BRIT is a shade level (31 = full); values above 31 saturate. */
        data[MAX_LIGHTS * 4 + i * 4] = (float)l->brightness / 31.0f;
    }
    glGenBuffers(1, &r->lights_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, r->lights_ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof data, data, GL_STATIC_DRAW);
    r->light_count = n;
}

static bool make_classic_target(bs_level_renderer *r)
{
    glGenFramebuffers(1, &r->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r->fbo);
    glGenTextures(1, &r->fbo_color);
    glBindTexture(GL_TEXTURE_2D, r->fbo_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, CLASSIC_W, CLASSIC_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->fbo_color, 0);
    glGenRenderbuffers(1, &r->fbo_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, r->fbo_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, CLASSIC_W, CLASSIC_H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, r->fbo_depth);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok;
}

bs_render_settings bs_render_settings_default(void)
{
    bs_render_settings s = {
        .classic = false,
        .lighting = true,
        .fog = true,
        .sky = true,
        .sprites = true,
        .markers = false,
        .cull = true,
        .ambient = 0.0f,
        .player_light = true,
        .vertex_lighting = false,
        .fog_start = 0.0f,
        .fog_end = 4000.0f,
        .fog_color = {0, 0, 0},
    };
    return s;
}

bs_result bs_level_renderer_create(const bs_scene *scene, const bs_bs6_level *level, const bs_bsi_image *sky,
                                   bs_level_renderer **out)
{
    *out = NULL;
    bs_level_renderer *r = calloc(1, sizeof *r);
    if (!r)
        return BS_ERR_NOMEM;
    r->scene = scene;
    r->object_count = level->object_count;
    r->xforms = calloc(level->object_count + 1, sizeof *r->xforms);
    r->hidden = calloc(level->object_count + 1, sizeof *r->hidden);
    for (uint32_t i = 0; r->xforms && i < level->object_count; i++) {
        const bs_bs6_object *o = &level->objects[i];
        float pos[3] = {(float)o->pos[0], (float)o->pos[1], (float)o->pos[2]};
        r->xforms[i] = bs_scene_object_xform(o->angles, pos);
    }
    r->object_first = calloc(level->object_count + 1, sizeof *r->object_first);
    r->object_batches = calloc(level->object_count + 1, sizeof *r->object_batches);
    for (uint32_t i = 0; r->object_first && r->object_batches && i < scene->batch_count; i++) {
        int32_t ob = scene->batches[i].object;
        if (ob < 0 || (uint32_t)ob >= level->object_count)
            continue;
        if (!r->object_batches[ob]++)
            r->object_first[ob] = i;
    }
    r->materials = calloc(scene->material_count + 1, sizeof *r->materials);
    r->sprites = calloc(scene->sprite_count + 1, sizeof *r->sprites);
    if (!r->materials || !r->sprites || !r->xforms || !r->hidden || !r->object_first || !r->object_batches) {
        bs_level_renderer_destroy(r);
        return BS_ERR_NOMEM;
    }
    if (!make_program(&r->geometry, geometry_vs) || !make_program(&r->sprite, sprite_vs) ||
        !(r->sky_program = link_program(sky_vs, sky_fs)) || !make_classic_target(r)) {
        bs_level_renderer_destroy(r);
        return BS_ERR_UNSUPPORTED;
    }
    r->sky_tex = glGetUniformLocation(r->sky_program, "u_tex");
    r->sky_fwd = glGetUniformLocation(r->sky_program, "u_fwd");
    r->sky_right = glGetUniformLocation(r->sky_program, "u_right");
    r->sky_up = glGetUniformLocation(r->sky_program, "u_up");
    r->sky_tan = glGetUniformLocation(r->sky_program, "u_tan");
    r->sky_repeats = glGetUniformLocation(r->sky_program, "u_repeats");
    r->sky_fog = glGetUniformLocation(r->sky_program, "u_fog_color");
    r->sky_fog_on = glGetUniformLocation(r->sky_program, "u_fog_on");

    /* Geometry */
    glGenVertexArrays(1, &r->vao);
    glBindVertexArray(r->vao);
    glGenBuffers(1, &r->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(scene->vertex_count * sizeof(bs_scene_vertex)), scene->vertices,
                 GL_STATIC_DRAW);
    GLsizei stride = (GLsizei)sizeof(bs_scene_vertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void *)offsetof(bs_scene_vertex, pos));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (const void *)offsetof(bs_scene_vertex, normal));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (const void *)offsetof(bs_scene_vertex, uv));
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (const void *)offsetof(bs_scene_vertex, face_byte));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);

    /* Billboard quad (two triangles) */
    static const float quad[] = {-0.5f, -0.5f, 0, 0.5f, -0.5f, 0, 0.5f, 0.5f, 0,
                                 -0.5f, -0.5f, 0, 0.5f, 0.5f,  0, -0.5f, 0.5f, 0};
    glGenVertexArrays(1, &r->quad_vao);
    glBindVertexArray(r->quad_vao);
    glGenBuffers(1, &r->quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, NULL);
    glEnableVertexAttribArray(0);
    glGenVertexArrays(1, &r->empty_vao);
    glBindVertexArray(0);

    for (uint32_t i = 0; i < scene->material_count; i++) {
        const bs_scene_material *m = &scene->materials[i];
        gpu_material *g = &r->materials[i];
        g->color[3] = 1.0f;
        if (m->kind == BS_SCENE_MAT_TEXTURE) {
            g->texture = upload_image(m->image, 1, true); /* first frame only for now */
        } else if (m->kind == BS_SCENE_MAT_COLOR) {
            for (int c = 0; c < 3; c++)
                g->color[c] = m->rgb[c] / 255.0f;
        } else {
            g->color[0] = g->color[2] = 1.0f; /* missing texture: magenta */
        }
    }

    for (uint32_t i = 0; i < scene->sprite_count; i++) {
        const bs_scene_sprite *s = &scene->sprites[i];
        gpu_sprite *g = &r->sprites[r->sprite_count];
        if (!s->image || !s->image->width || !s->image->height)
            continue;
        uint32_t frames = s->image->frames ? s->image->frames : 1;
        g->texture = upload_image(s->image, frames, false);
        if (!g->texture)
            continue;
        memcpy(g->pos, s->center, sizeof g->pos);
        memcpy(g->size, s->size, sizeof g->size);
        g->frames = frames;
        int16_t ms = s->image->bhdr[BHDR_FRAME_MS];
        g->frame_ms = ms > 0 ? (uint32_t)ms : 100;
        g->marker = s->marker;
        r->sprite_count++;
    }

    if (sky)
        r->sky_texture = upload_image(sky, 1, true);
    upload_lights(r, level);
    apply_filters(r, true);
    *out = r;
    return BS_OK;
}

void bs_level_renderer_destroy(bs_level_renderer *r)
{
    if (!r)
        return;
    if (r->materials)
        for (uint32_t i = 0; r->scene && i < r->scene->material_count; i++)
            if (r->materials[i].texture)
                glDeleteTextures(1, &r->materials[i].texture);
    for (uint32_t i = 0; i < r->sprite_count; i++)
        glDeleteTextures(1, &r->sprites[i].texture);
    for (uint32_t i = 0; i < r->dyn_image_count; i++)
        glDeleteTextures(1, &r->dyn_textures[i]);
    free(r->dyn);
    free(r->dyn_images);
    free(r->dyn_textures);
    if (r->sky_texture)
        glDeleteTextures(1, &r->sky_texture);
    if (r->water_texture)
        glDeleteTextures(1, &r->water_texture);
    glDeleteBuffers(1, &r->water_vbo);
    glDeleteVertexArrays(1, &r->water_vao);
    glDeleteBuffers(1, &r->vbo);
    glDeleteBuffers(1, &r->quad_vbo);
    glDeleteBuffers(1, &r->lights_ubo);
    glDeleteVertexArrays(1, &r->vao);
    glDeleteVertexArrays(1, &r->quad_vao);
    glDeleteVertexArrays(1, &r->empty_vao);
    free(r->xforms);
    free(r->hidden);
    free(r->object_first);
    free(r->object_batches);
    free(r->instances);
    glDeleteProgram(r->geometry.id);
    glDeleteProgram(r->sprite.id);
    glDeleteProgram(r->sky_program);
    glDeleteFramebuffers(1, &r->fbo);
    glDeleteTextures(1, &r->fbo_color);
    glDeleteRenderbuffers(1, &r->fbo_depth);
    free(r->materials);
    free(r->sprites);
    free(r);
}

/* ---- matrices (column-major) ---- */

static void mat4_mul(const float a[16], const float b[16], float out[16])
{
    float t[16];
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++)
                s += a[k * 4 + rr] * b[c * 4 + k];
            t[c * 4 + rr] = s;
        }
    memcpy(out, t, sizeof t);
}

/* Camera basis in game axes (+y down). */
static void camera_basis(const bs_camera *cam, float fwd[3], float right[3], float up[3])
{
    float cy = cosf(cam->yaw), sy = sinf(cam->yaw), cp = cosf(cam->pitch), sp = sinf(cam->pitch);
    fwd[0] = sy * cp;
    fwd[1] = -sp;
    fwd[2] = cy * cp;
    /* right = fwd x world-up, where world up is -y */
    right[0] = cy;
    right[1] = 0;
    right[2] = -sy;
    /* up = right x fwd */
    up[0] = right[1] * fwd[2] - right[2] * fwd[1];
    up[1] = right[2] * fwd[0] - right[0] * fwd[2];
    up[2] = right[0] * fwd[1] - right[1] * fwd[0];
}

static void view_projection(const bs_camera *cam, float aspect, float out[16])
{
    float f[3], rt[3], up[3];
    camera_basis(cam, f, rt, up);
    /* View: rows are right, up, -forward, applied to (p - eye). */
    float view[16] = {
        rt[0], up[0], -f[0], 0,
        rt[1], up[1], -f[1], 0,
        rt[2], up[2], -f[2], 0,
        0, 0, 0, 1,
    };
    for (int rr = 0; rr < 3; rr++)
        view[12 + rr] = -(view[rr] * cam->pos[0] + view[4 + rr] * cam->pos[1] + view[8 + rr] * cam->pos[2]);
    float n = 4.0f, fa = 60000.0f;
    float t = 1.0f / tanf(cam->fov_y * 0.5f);
    float proj[16] = {
        t / aspect, 0, 0, 0,
        0, t, 0, 0,
        0, 0, (fa + n) / (n - fa), -1,
        0, 0, 2 * fa * n / (n - fa), 0,
    };
    mat4_mul(proj, view, out);
}

static const float identity4[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

/* Column-major model matrix of an object placement. */
static void xform_matrix(const bs_scene_xform *x, float m[16])
{
    for (int c = 0; c < 3; c++) {
        for (int rr = 0; rr < 3; rr++)
            m[c * 4 + rr] = x->rot[rr * 3 + c];
        m[c * 4 + 3] = 0;
    }
    m[12] = x->pos[0];
    m[13] = x->pos[1];
    m[14] = x->pos[2];
    m[15] = 1;
}

static void set_shading(const program *p, const bs_level_renderer *r, const bs_camera *cam,
                        const bs_render_settings *s, const float mvp[16])
{
    glUseProgram(p->id);
    glUniformMatrix4fv(p->mvp, 1, GL_FALSE, mvp);
    if (p->model >= 0)
        glUniformMatrix4fv(p->model, 1, GL_FALSE, identity4);
    glUniform1i(p->tex, 0);
    glUniform3f(p->eye, cam->pos[0], cam->pos[1], cam->pos[2]);
    glUniform1i(p->lighting, s->lighting);
    glUniform1i(p->classic, s->classic);
    glUniform1f(p->ambient, s->ambient);
    glUniform1i(p->fog, s->fog);
    glUniform2f(p->fog_range, s->fog_start, s->fog_end);
    glUniform3f(p->fog_color, s->fog_color[0], s->fog_color[1], s->fog_color[2]);
    glUniform1i(p->light_count, (GLint)r->light_count);
    glUniform1i(p->debug, s->debug_view);
    glUniform1i(p->vertex_light, s->vertex_lighting);
    /* The player's light sits 12 units above the eye (up is -y). */
    glUniform4f(p->player_light, cam->pos[0], cam->pos[1] - 12.0f, cam->pos[2],
                s->player_light ? PLAYER_LIGHT_RADIUS : 0.0f);
    glUniform1f(p->player_brightness, PLAYER_LIGHT_LEVELS / 31.0f);
}

/* An object's batches with a placement (the geometry program in use, its VAO bound). */
static void draw_object_batches(const bs_level_renderer *r, uint32_t object, const bs_scene_xform *x)
{
    if (object >= r->object_count || !r->object_batches[object])
        return;
    float model[16];
    xform_matrix(x, model);
    glUniformMatrix4fv(r->geometry.model, 1, GL_FALSE, model);
    for (uint32_t i = r->object_first[object]; i < r->object_first[object] + r->object_batches[object]; i++) {
        const bs_scene_batch *b = &r->scene->batches[i];
        const gpu_material *m = &r->materials[b->material];
        glUniform1i(r->geometry.textured, m->texture != 0);
        glUniform4f(r->geometry.color, m->color[0], m->color[1], m->color[2], m->color[3]);
        glBindTexture(GL_TEXTURE_2D, m->texture);
        glDrawArrays(GL_TRIANGLES, (GLint)b->first, (GLsizei)b->count);
    }
}

/* The texture of a sprite image, uploaded the first time (all frames, stacked). */
static GLuint dynamic_texture(bs_level_renderer *r, const bs_bsi_image *img)
{
    for (uint32_t i = 0; i < r->dyn_image_count; i++)
        if (r->dyn_images[i] == img)
            return r->dyn_textures[i];
    if (r->dyn_image_count == r->dyn_image_cap) {
        uint32_t cap = r->dyn_image_cap ? r->dyn_image_cap * 2 : 32;
        const bs_bsi_image **im = realloc(r->dyn_images, sizeof *im * cap);
        if (!im)
            return 0;
        r->dyn_images = im;
        GLuint *t = realloc(r->dyn_textures, sizeof *t * cap);
        if (!t)
            return 0;
        r->dyn_textures = t;
        r->dyn_image_cap = cap;
    }
    GLuint t = upload_image(img, img->frames ? img->frames : 1, false);
    set_filter(t, r->classic_filters);
    r->dyn_images[r->dyn_image_count] = img;
    r->dyn_textures[r->dyn_image_count++] = t;
    return t;
}

/* A sprite standing on its position, sized by its BHDR scale as the level's figures (assets/scene.c). */
static void draw_dynamic_sprite(bs_level_renderer *r, const bs_level_sprite *sp)
{
    const bs_bsi_image *img = sp->image;
    if (!img || !img->width || !img->height)
        return;
    GLuint t = dynamic_texture(r, img);
    if (!t)
        return;
    uint32_t frames = img->frames ? img->frames : 1;
    int scale = img->bhdr[BHDR_SCALE];
    float k = scale > 0 ? (float)scale / 256.0f : 1.0f;
    float w = (float)img->width * k, h = (float)img->height * k;
    glUniform3f(r->sprite.center, sp->pos[0], sp->pos[1] - h * 0.5f, sp->pos[2]);
    glUniform2f(r->sprite.size, sp->mirror ? -w : w, h);
    glUniform2f(r->sprite.frame, (float)(sp->frame % frames) / (float)frames, 1.0f / (float)frames);
    glBindTexture(GL_TEXTURE_2D, t);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void draw_scene(bs_level_renderer *r, const bs_camera *cam, const bs_render_settings *s, int w, int h,
                       double time)
{
    float aspect = (float)w / (float)(h > 0 ? h : 1);
    float mvp[16], fwd[3], right[3], up[3];
    view_projection(cam, aspect, mvp);
    camera_basis(cam, fwd, right, up);

    glViewport(0, 0, w, h);
    glClearColor(s->fog_color[0], s->fog_color[1], s->fog_color[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glActiveTexture(GL_TEXTURE0);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, r->lights_ubo);

    if (s->sky && r->sky_texture) {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glUseProgram(r->sky_program);
        glUniform1i(r->sky_tex, 0);
        glUniform3f(r->sky_fwd, fwd[0], fwd[1], fwd[2]);
        glUniform3f(r->sky_right, right[0], right[1], right[2]);
        glUniform3f(r->sky_up, up[0], up[1], up[2]);
        float ty = tanf(cam->fov_y * 0.5f);
        glUniform2f(r->sky_tan, ty * aspect, ty);
        glUniform1f(r->sky_repeats, SKY_REPEATS);
        glUniform3f(r->sky_fog, s->fog_color[0], s->fog_color[1], s->fog_color[2]);
        glUniform1i(r->sky_fog_on, s->fog);
        glBindTexture(GL_TEXTURE_2D, r->sky_texture);
        glBindVertexArray(r->empty_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    if (s->cull) {
        glEnable(GL_CULL_FACE);
        glFrontFace(GL_CCW);
        glCullFace(GL_BACK);
    } else {
        glDisable(GL_CULL_FACE);
    }

    /* The view matrix maps game axes straight to eye space, and flipping y and z
     * together keeps handedness, so model winding stays counter-clockwise. */
    set_shading(&r->geometry, r, cam, s, mvp);
    glBindVertexArray(r->vao);
    int32_t placed = -1;
    for (uint32_t i = 0; i < r->scene->batch_count; i++) {
        const bs_scene_batch *b = &r->scene->batches[i];
        const gpu_material *m = &r->materials[b->material];
        if (b->object >= 0 && (uint32_t)b->object < r->object_count && r->hidden[b->object])
            continue;
        if (b->object != placed) {
            float model[16];
            memcpy(model, identity4, sizeof model);
            if (b->object >= 0 && (uint32_t)b->object < r->object_count)
                xform_matrix(&r->xforms[b->object], model);
            glUniformMatrix4fv(r->geometry.model, 1, GL_FALSE, b->object >= 0 ? model : identity4);
            placed = b->object;
        }
        glUniform1i(r->geometry.textured, m->texture != 0);
        glUniform4f(r->geometry.color, m->color[0], m->color[1], m->color[2], m->color[3]);
        glBindTexture(GL_TEXTURE_2D, m->texture);
        glDrawArrays(GL_TRIANGLES, (GLint)b->first, (GLsizei)b->count);
    }

    for (uint32_t k = 0; k < r->instance_count; k++)
        draw_object_batches(r, r->instances[k].object, &r->instances[k].xform);
    if (placed != -1 || r->instance_count)
        glUniformMatrix4fv(r->geometry.model, 1, GL_FALSE, identity4);
    if (r->water_vao) {
        glDisable(GL_CULL_FACE); /* visible from below too */
        glUniform1i(r->geometry.textured, 1);
        glBindTexture(GL_TEXTURE_2D, r->water_texture);
        glBindVertexArray(r->water_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    if (s->sprites || s->markers) {
        glDisable(GL_CULL_FACE);
        set_shading(&r->sprite, r, cam, s, mvp);
        glUniform1i(r->sprite.textured, 1);
        /* Billboards turn about the vertical axis only. */
        glUniform3f(r->sprite.right, right[0], right[1], right[2]);
        glUniform3f(r->sprite.up, 0.0f, -1.0f, 0.0f);
        glBindVertexArray(r->quad_vao);
        for (uint32_t i = 0; i < r->sprite_count; i++) {
            const gpu_sprite *g = &r->sprites[i];
            if (g->marker ? !s->markers : !s->sprites)
                continue;
            uint32_t frame = (uint32_t)(time * 1000.0 / g->frame_ms) % g->frames;
            glUniform3f(r->sprite.center, g->pos[0], g->pos[1], g->pos[2]);
            glUniform2f(r->sprite.size, g->size[0], g->size[1]);
            glUniform2f(r->sprite.frame, (float)frame / (float)g->frames, 1.0f / (float)g->frames);
            glBindTexture(GL_TEXTURE_2D, g->texture);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        for (uint32_t i = 0; s->sprites && i < r->dyn_count; i++)
            draw_dynamic_sprite(r, &r->dyn[i]);
    }
    glBindVertexArray(0);
}

void bs_level_renderer_draw(bs_level_renderer *r, const bs_camera *cam, const bs_render_settings *s, int width,
                            int height, double time)
{
    if (r->classic_filters != s->classic)
        apply_filters(r, s->classic);
    if (!s->classic) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        draw_scene(r, cam, s, width, height, time);
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, r->fbo);
    draw_scene(r, cam, s, CLASSIC_W, CLASSIC_H, time);

    /* Scale up to the window, keeping 4:3. */
    int w = width, h = width * CLASSIC_H / CLASSIC_W;
    if (h > height) {
        h = height;
        w = height * CLASSIC_W / CLASSIC_H;
    }
    int x = (width - w) / 2, y = (height - h) / 2;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, r->fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, CLASSIC_W, CLASSIC_H, x, y, x + w, y + h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bs_result bs_level_renderer_set_water(bs_level_renderer *r, const bs_bsi_image *image, float height)
{
    if (r->water_texture)
        glDeleteTextures(1, &r->water_texture);
    glDeleteBuffers(1, &r->water_vbo);
    glDeleteVertexArrays(1, &r->water_vao);
    r->water_texture = r->water_vbo = r->water_vao = 0;
    if (!image || !image->width || !image->height)
        return BS_OK;

    r->water_texture = upload_image(image, 1, true); /* first frame; animation not yet */
    if (!r->water_texture)
        return BS_ERR_NOMEM;
    set_filter(r->water_texture, r->classic_filters);

    /* A quad well beyond the level's bounds; one texel per level unit (inferred). */
    const float margin = 20000.0f;
    float x0 = r->scene->min[0] - margin, x1 = r->scene->max[0] + margin;
    float z0 = r->scene->min[2] - margin, z1 = r->scene->max[2] + margin;
    float corners[4][2] = {{x0, z0}, {x1, z0}, {x1, z1}, {x0, z1}};
    static const int order[6] = {0, 1, 2, 0, 2, 3};
    bs_scene_vertex v[6];
    for (int i = 0; i < 6; i++) {
        const float *c = corners[order[i]];
        v[i] = (bs_scene_vertex){{c[0], height, c[1]}, {0, -1, 0},
                                 {c[0] / (float)image->width, c[1] / (float)image->height}, 0.0f};
    }
    glGenVertexArrays(1, &r->water_vao);
    glBindVertexArray(r->water_vao);
    glGenBuffers(1, &r->water_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->water_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW);
    GLsizei stride = (GLsizei)sizeof(bs_scene_vertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void *)offsetof(bs_scene_vertex, pos));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (const void *)offsetof(bs_scene_vertex, normal));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (const void *)offsetof(bs_scene_vertex, uv));
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (const void *)offsetof(bs_scene_vertex, face_byte));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glBindVertexArray(0);
    return BS_OK;
}

void bs_level_renderer_place(bs_level_renderer *r, uint32_t object, const float pos[3], const int32_t angles[3])
{
    if (object < r->object_count)
        r->xforms[object] = bs_scene_object_xform(angles, pos);
}

const bs_scene_xform *bs_level_renderer_xforms(const bs_level_renderer *r)
{
    return r->xforms;
}

void bs_level_renderer_show(bs_level_renderer *r, uint32_t object, bool visible)
{
    if (object < r->object_count)
        r->hidden[object] = !visible;
}

bs_result bs_level_renderer_set_instances(bs_level_renderer *r, const bs_level_instance *inst, uint32_t count)
{
    if (count > r->instance_cap) {
        bs_level_instance *n = realloc(r->instances, sizeof *n * count);
        if (!n)
            return BS_ERR_NOMEM;
        r->instances = n;
        r->instance_cap = count;
    }
    if (count)
        memcpy(r->instances, inst, sizeof *inst * count);
    r->instance_count = count;
    return BS_OK;
}

void bs_level_renderer_draw_object(bs_level_renderer *r, uint32_t object, const int32_t angles[3],
                                   const float offset[3], float sx, float sy, int x, int y, int w, int h,
                                   int window_h)
{
    if (object >= r->object_count || w <= 0 || h <= 0)
        return;
    /* Camera space is game space here: +x right, +y down, +z ahead. Clip: x_c = sx x, y_c = -sy y (up),
     * w_c = z; depth over 4 .. 60000 as the level's. */
    float n = 4.0f, fa = 60000.0f;
    float proj[16] = {
        sx, 0, 0, 0,
        0, -sy, 0, 0,
        0, 0, (fa + n) / (fa - n), 1,
        0, 0, -2 * fa * n / (fa - n), 0,
    };
    bs_scene_xform xf = bs_scene_object_xform(angles, offset);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(x, window_h - y - h, w, h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, window_h - y - h, w, h);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    bs_render_settings s = bs_render_settings_default();
    s.lighting = false;
    s.fog = false;
    bs_camera cam = {{0, 0, 0}, 0, 0, 1};
    set_shading(&r->geometry, r, &cam, &s, proj);
    glUniform1i(r->geometry.vertex_light, 0);
    glBindVertexArray(r->vao);
    draw_object_batches(r, object, &xf);
    glBindVertexArray(0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
}

uint32_t bs_level_renderer_set_texture(bs_level_renderer *r, const char *name, const bs_bsi_image *img)
{
    if (!img || !img->width || !img->height)
        return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < r->scene->material_count; i++) {
        const bs_scene_material *m = &r->scene->materials[i];
        if (m->kind != BS_SCENE_MAT_TEXTURE || bs_strcasecmp(m->name, name) != 0)
            continue;
        GLuint t = upload_image(img, 1, true);
        if (!t)
            continue;
        set_filter(t, r->classic_filters);
        if (r->materials[i].texture)
            glDeleteTextures(1, &r->materials[i].texture);
        r->materials[i].texture = t;
        n++;
    }
    return n;
}

bs_result bs_level_renderer_set_sprites(bs_level_renderer *r, const bs_level_sprite *sp, uint32_t count)
{
    if (count > r->dyn_cap) {
        bs_level_sprite *n = realloc(r->dyn, sizeof *n * count);
        if (!n)
            return BS_ERR_NOMEM;
        r->dyn = n;
        r->dyn_cap = count;
    }
    if (count)
        memcpy(r->dyn, sp, sizeof *sp * count);
    r->dyn_count = count;
    return BS_OK;
}
