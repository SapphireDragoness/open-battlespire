#include "bs/render/overlay.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "bs/core/log.h"
#include "bs/render/gl.h"

struct bs_overlay {
    uint8_t *pixels;        /* RGBA, top row first */
    bool dirty;
    int clip[4];            /* x0, y0, x1, y1 (exclusive) */
    GLuint texture, program, vao;
    GLint u_tex;
};

static const char *const vs_src =
    "#version 330 core\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
    "    v_uv = vec2(p.x, 1.0 - p.y);\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char *const fs_src =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "out vec4 o_color;\n"
    "uniform sampler2D u_tex;\n"
    "void main() { o_color = texture(u_tex, v_uv); }\n";

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        BS_ERROR("overlay shader: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bs_result bs_overlay_create(bs_overlay **out)
{
    *out = NULL;
    bs_overlay *o = calloc(1, sizeof *o);
    if (!o)
        return BS_ERR_NOMEM;
    o->pixels = calloc((size_t)BS_OVERLAY_WIDTH * BS_OVERLAY_HEIGHT, 4);
    bs_overlay_clip(o, 0, 0, 0, 0);
    GLuint vs = compile(GL_VERTEX_SHADER, vs_src), fs = compile(GL_FRAGMENT_SHADER, fs_src);
    if (!o->pixels || !vs || !fs) {
        bs_result r = o->pixels ? BS_ERR_UNSUPPORTED : BS_ERR_NOMEM;
        glDeleteShader(vs);
        glDeleteShader(fs);
        free(o->pixels);
        free(o);
        return r;
    }
    o->program = glCreateProgram();
    glAttachShader(o->program, vs);
    glAttachShader(o->program, fs);
    glLinkProgram(o->program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(o->program, GL_LINK_STATUS, &ok);
    if (!ok) {
        BS_ERROR("overlay shader: link failed");
        bs_overlay_destroy(o);
        return BS_ERR_UNSUPPORTED;
    }
    o->u_tex = glGetUniformLocation(o->program, "u_tex");
    glGenVertexArrays(1, &o->vao);
    glGenTextures(1, &o->texture);
    glBindTexture(GL_TEXTURE_2D, o->texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, BS_OVERLAY_WIDTH, BS_OVERLAY_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 o->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    *out = o;
    return BS_OK;
}

void bs_overlay_destroy(bs_overlay *o)
{
    if (!o)
        return;
    glDeleteTextures(1, &o->texture);
    glDeleteVertexArrays(1, &o->vao);
    glDeleteProgram(o->program);
    free(o->pixels);
    free(o);
}

void bs_overlay_clear(bs_overlay *o)
{
    memset(o->pixels, 0, (size_t)BS_OVERLAY_WIDTH * BS_OVERLAY_HEIGHT * 4);
    o->dirty = true;
}

void bs_overlay_clip(bs_overlay *o, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) {
        x = y = 0;
        w = BS_OVERLAY_WIDTH;
        h = BS_OVERLAY_HEIGHT;
    }
    o->clip[0] = x < 0 ? 0 : x;
    o->clip[1] = y < 0 ? 0 : y;
    o->clip[2] = x + w > BS_OVERLAY_WIDTH ? BS_OVERLAY_WIDTH : x + w;
    o->clip[3] = y + h > BS_OVERLAY_HEIGHT ? BS_OVERLAY_HEIGHT : y + h;
}

static void put(bs_overlay *o, int x, int y, uint32_t rgba)
{
    if (x < o->clip[0] || y < o->clip[1] || x >= o->clip[2] || y >= o->clip[3])
        return;
    uint8_t *p = o->pixels + ((size_t)y * BS_OVERLAY_WIDTH + (size_t)x) * 4;
    p[0] = (uint8_t)(rgba >> 24);
    p[1] = (uint8_t)(rgba >> 16);
    p[2] = (uint8_t)(rgba >> 8);
    p[3] = (uint8_t)rgba;
}

void bs_overlay_fill(bs_overlay *o, int x, int y, int w, int h, uint32_t rgba)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            put(o, i, j, rgba);
    o->dirty = true;
}

void bs_overlay_image(bs_overlay *o, const bs_bsi_image *img, int dx, int dy)
{
    bs_overlay_image_frame(o, img, 0, dx, dy);
}

void bs_overlay_image_frame(bs_overlay *o, const bs_bsi_image *img, uint32_t frame, int dx, int dy)
{
    if (!img || !img->pixels)
        return;
    uint32_t frames = img->frames ? img->frames : 1;
    const uint8_t *px = img->pixels + (size_t)(frame % frames) * img->width * img->height;
    uint8_t lut[256][4];
    for (int i = 0; i < 256; i++)
        bs_bsi_pixel_rgb(img, (uint8_t)i, lut[i]);
    /* Only the part inside the clip. */
    int x0 = img->x + dx, y0 = img->y + dy;
    uint32_t i0 = o->clip[0] > x0 ? (uint32_t)(o->clip[0] - x0) : 0, j0 = o->clip[1] > y0 ? (uint32_t)(o->clip[1] - y0) : 0;
    uint32_t i1 = img->width, j1 = img->height;
    if (o->clip[2] - x0 < (int)i1)
        i1 = o->clip[2] > x0 ? (uint32_t)(o->clip[2] - x0) : 0;
    if (o->clip[3] - y0 < (int)j1)
        j1 = o->clip[3] > y0 ? (uint32_t)(o->clip[3] - y0) : 0;
    for (uint32_t j = j0; j < j1; j++)
        for (uint32_t i = i0; i < i1; i++) {
            uint8_t p = px[(size_t)j * img->width + i];
            if (img->transparent && p == 0)
                continue;
            put(o, img->x + dx + (int)i, img->y + dy + (int)j,
                (uint32_t)lut[p][0] << 24 | (uint32_t)lut[p][1] << 16 | (uint32_t)lut[p][2] << 8 | 0xFF);
        }
    o->dirty = true;
}

void bs_overlay_rgb(bs_overlay *o, const uint8_t *rgb, int w, int h, int x, int y)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            const uint8_t *p = rgb + ((size_t)j * (size_t)w + (size_t)i) * 3;
            put(o, x + i, y + j, (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | 0xFFu);
        }
    o->dirty = true;
}

void bs_overlay_frame(bs_overlay *o, int x, int y, int w, int h, uint32_t rgba)
{
    bs_overlay_fill(o, x, y, w, 1, rgba);
    bs_overlay_fill(o, x, y + h - 1, w, 1, rgba);
    bs_overlay_fill(o, x, y, 1, h, rgba);
    bs_overlay_fill(o, x + w - 1, y, 1, h, rgba);
}

/* The game's text measure (0x13A81): a glyph advances its width + 1; a space,
 * or a byte past the font's range, advances 5. */
#define SPACE_ADVANCE 5

static const bs_font_glyph *glyph(const bs_font *font, uint8_t c)
{
    if (c < BS_FONT_FIRST_CHAR || (int8_t)(c - BS_FONT_FIRST_CHAR) < 0)
        return NULL;
    const bs_font_glyph *g = bs_font_glyph_for(font, c);
    return g && g->pixels ? g : NULL;
}

int bs_text_width(const bs_font *font, const char *text)
{
    int w = 0;
    for (const uint8_t *p = (const uint8_t *)text; *p; p++) {
        const bs_font_glyph *g = glyph(font, *p);
        w += g ? g->width + 1 : SPACE_ADVANCE;
    }
    return w;
}

int bs_text_advance(const bs_font *font, uint8_t c)
{
    const bs_font_glyph *g = glyph(font, c);
    return g ? g->width + 1 : SPACE_ADVANCE;
}

int bs_text_height(const bs_font *font)
{
    return font->max_height;
}

int bs_overlay_text(bs_overlay *o, const bs_font *font, int x, int y, const char *text)
{
    int x0 = x;
    for (const uint8_t *p = (const uint8_t *)text; *p; p++) {
        const bs_font_glyph *g = glyph(font, *p);
        if (!g) {
            x += SPACE_ADVANCE;
            continue;
        }
        for (uint32_t j = 0; j < g->height; j++)
            for (uint32_t i = 0; i < g->width; i++) {
                uint16_t c = bs_font_pixel(g, i, j);
                if (!c)
                    continue;
                /* RGB555 */
                uint32_t r = (c >> 10) & 31, gr = (c >> 5) & 31, b = c & 31;
                put(o, x + (int)i, y + (int)j,
                    (r * 255 / 31) << 24 | (gr * 255 / 31) << 16 | (b * 255 / 31) << 8 | 0xFF);
            }
        x += g->width + 1;
    }
    o->dirty = true;
    return x - x0;
}

void bs_overlay_draw(bs_overlay *o, int width, int height)
{
    if (o->dirty) {
        glBindTexture(GL_TEXTURE_2D, o->texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, BS_OVERLAY_WIDTH, BS_OVERLAY_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE,
                        o->pixels);
        o->dirty = false;
    }
    /* Letterboxed like bs_screen. */
    int w = width, h = (int)((int64_t)width * BS_OVERLAY_HEIGHT / BS_OVERLAY_WIDTH);
    if (h > height) {
        h = height;
        w = (int)((int64_t)height * BS_OVERLAY_WIDTH / BS_OVERLAY_HEIGHT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport((width - w) / 2, (height - h) / 2, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(o->program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, o->texture);
    glUniform1i(o->u_tex, 0);
    glBindVertexArray(o->vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glViewport(0, 0, width, height);
}
