#include "bs/formats/exe_text.h"

#include <stdbool.h>
#include <string.h>

#define FIRST_MESSAGE "Exit the Automap."
#define MAX_MESSAGES 4096

/* The NUL-terminated string at a virtual address, or NULL. */
static const char *string_at(const bs_le_image *img, uint32_t va)
{
    for (uint32_t i = 0; i < img->object_count; i++) {
        const bs_le_object *o = &img->objects[i];
        if (va >= o->base && va - o->base < o->mem_size) {
            const uint8_t *p = o->data + (va - o->base);
            return memchr(p, 0, o->mem_size - (va - o->base)) ? (const char *)p : NULL;
        }
    }
    return NULL;
}

static uint32_t u32_at(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bs_result bs_exe_string_table(const bs_le_image *img, const char *first, uint32_t max, bs_arena *arena,
                              bs_exe_text *out)
{
    memset(out, 0, sizeof *out);
    /* The first string's address... */
    uint32_t first_va = 0;
    size_t len = strlen(first) + 1;
    for (uint32_t i = 0; i < img->object_count && !first_va; i++) {
        const bs_le_object *o = &img->objects[i];
        for (uint32_t k = 1; k + len <= o->mem_size; k++)
            if (o->data[k - 1] == 0 && memcmp(o->data + k, first, len) == 0) {
                first_va = o->base + k;
                break;
            }
    }
    if (!first_va)
        return BS_ERR_NOT_FOUND;
    /* ...then a pointer to it followed by more string pointers. */
    for (uint32_t i = 0; i < img->object_count; i++) {
        const bs_le_object *o = &img->objects[i];
        for (uint32_t k = 0; k + 4 <= o->mem_size; k++) {
            if (u32_at(o->data + k) != first_va)
                continue;
            uint32_t n = 0;
            while (k + 4 * (n + 1) <= o->mem_size && n < max && string_at(img, u32_at(o->data + k + 4 * n)))
                n++;
            if (n < 2)
                continue;
            out->messages = BS_ARENA_ARRAY(arena, const char *, n);
            if (!out->messages)
                return BS_ERR_NOMEM;
            for (uint32_t m = 0; m < n; m++)
                out->messages[m] = string_at(img, u32_at(o->data + k + 4 * m));
            out->count = n;
            return BS_OK;
        }
    }
    return BS_ERR_NOT_FOUND;
}

bs_result bs_exe_text_read(const bs_le_image *img, bs_arena *arena, bs_exe_text *out)
{
    return bs_exe_string_table(img, FIRST_MESSAGE, MAX_MESSAGES, arena, out);
}

const char *bs_exe_message(const bs_exe_text *t, uint32_t i)
{
    return t && i < t->count ? t->messages[i] : "";
}
