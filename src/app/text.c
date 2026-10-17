#include "text.h"

#include <string.h>

/* Windows-1252 to the fonts' code page 437, where there is an equivalent. */
void ui_cp437(const char *in, char *out, size_t cap)
{
    size_t n = 0;
    for (const uint8_t *p = (const uint8_t *)in; *p && n + 1 < cap; p++) {
        uint8_t c = *p;
        if (c == 0x91 || c == 0x92)
            c = '\'';
        else if (c == 0x93 || c == 0x94)
            c = '"';
        else if (c == 0x96 || c == 0x97)
            c = '-';
        else if (c >= 0x80)
            c = c == 0xE9 ? 130 : '?';
        out[n++] = (char)c;
    }
    out[n] = '\0';
}

/* A TEXT.RSC record's first version as text, '$' for its line breaks. */
void ui_rsc_text(const bs_rsc *rsc, int32_t id, char *out, size_t cap)
{
    out[0] = '\0';
    const bs_rsc_record *rec = rsc && id >= 0 && id < 0xFFFF ? bs_rsc_find(rsc, (uint16_t)id) : NULL;
    if (!rec)
        return;
    size_t n = 0;
    for (size_t i = 0; i < rec->size && n + 1 < cap; i++) {
        uint8_t c = rec->data[i];
        if (c == 0xFF || c == BS_RSC_END)
            break; /* another version follows */
        if (c == BS_RSC_CODE)
            i++;
        else if (c == BS_RSC_NEWLINE || c == BS_RSC_NEWLINE_C)
            out[n++] = '$';
        else if (c >= ' ')
            out[n++] = (char)c;
    }
    out[n] = '\0';
}

/* Text wrapped at a width, each line centred on cx (0x597DB): a line breaks at the last space before the
 * text gets too wide, or at a '$'. */
int ui_text_box(bs_overlay *o, const bs_font *f, const char *s, int cx, int y, int width)
{
    if (!f)
        return y;
    int last = y;
    char shown[2048];
    ui_cp437(s, shown, sizeof shown);
    int lh = bs_text_height(f);
    const char *p = shown;
    while (*p) {
        const char *end = p, *space = NULL;
        int w = 0;
        while (*end && *end != '$') {
            w += bs_text_advance(f, (uint8_t)*end);
            if (w > width && end > p)
                break;
            if (*end == ' ')
                space = end;
            end++;
        }
        const char *next;
        if (*end && *end != '$' && space && space > p) {
            end = space;
            next = space + 1;
        } else {
            next = *end ? end + 1 : end;
        }
        char line[256];
        size_t n = (size_t)(end - p) < sizeof line - 1 ? (size_t)(end - p) : sizeof line - 1;
        memcpy(line, p, n);
        line[n] = '\0';
        bs_overlay_text(o, f, cx - bs_text_width(f, line) / 2, y, line);
        last = y;
        y += lh;
        p = next;
    }
    return last;
}

