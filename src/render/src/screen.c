#include "bs/render/screen.h"

#include <stdlib.h>

#include "bs/render/gl.h"

struct bs_screen {
    GLuint texture, fbo;
    uint32_t width, height;  /* 0: no image yet */
};

bs_result bs_screen_create(bs_screen **out)
{
    bs_screen *s = calloc(1, sizeof *s);
    if (!s)
        return BS_ERR_NOMEM;
    glGenTextures(1, &s->texture);
    glGenFramebuffers(1, &s->fbo);
    *out = s;
    return BS_OK;
}

void bs_screen_destroy(bs_screen *s)
{
    if (!s)
        return;
    glDeleteFramebuffers(1, &s->fbo);
    glDeleteTextures(1, &s->texture);
    free(s);
}

bs_result bs_screen_set(bs_screen *s, const uint8_t *rgba, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || width > 8192 || height > 8192)
        return BS_ERR_ARGS;
    glBindTexture(GL_TEXTURE_2D, s->texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (width != s->width || height != s->height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)width, (GLsizei)height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->texture, 0);
        bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!ok)
            return BS_ERR_UNSUPPORTED;
        s->width = width;
        s->height = height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)width, (GLsizei)height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    return BS_OK;
}

bs_result bs_screen_set_bsi(bs_screen *s, const bs_bsi_image *img)
{
    if (!img || !img->pixels || img->width == 0 || img->height == 0)
        return BS_ERR_ARGS;
    size_t n = (size_t)img->width * img->height;
    uint8_t *rgba = malloc(n * 4);
    if (!rgba)
        return BS_ERR_NOMEM;
    for (size_t i = 0; i < n; i++) {
        bs_bsi_pixel_rgb(img, img->pixels[i], rgba + i * 4);
        rgba[i * 4 + 3] = 255;
    }
    bs_result r = bs_screen_set(s, rgba, img->width, img->height);
    free(rgba);
    return r;
}

/* The image's rectangle in the window (GL coordinates, origin bottom left). */
static void placement(const bs_screen *s, int width, int height, int *x, int *y, int *w, int *h)
{
    *w = width;
    *h = (int)((int64_t)width * s->height / s->width);
    if (*h > height) {
        *h = height;
        *w = (int)((int64_t)height * s->width / s->height);
    }
    *x = (width - *w) / 2;
    *y = (height - *h) / 2;
}

void bs_screen_draw(const bs_screen *s, int width, int height)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!s->width)
        return;
    int x, y, w, h;
    placement(s, width, height, &x, &y, &w, &h);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, s->fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    /* The image's first row is its top: flip vertically. */
    glBlitFramebuffer(0, 0, (GLint)s->width, (GLint)s->height, x, y + h, x + w, y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool bs_screen_to_image(const bs_screen *s, int width, int height, float wx, float wy, int *ix, int *iy)
{
    if (!s->width)
        return false;
    int x, y, w, h;
    placement(s, width, height, &x, &y, &w, &h);
    int top = height - (y + h); /* window coordinates have their origin top left */
    float fx = (wx - (float)x) / (float)w, fy = (wy - (float)top) / (float)h;
    if (fx < 0 || fy < 0 || fx >= 1 || fy >= 1)
        return false;
    *ix = (int)(fx * (float)s->width);
    *iy = (int)(fy * (float)s->height);
    return true;
}
