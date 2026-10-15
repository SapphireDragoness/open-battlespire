#include "statusbar.h"

#include <stdio.h>
#include <string.h>

/* RE references are GAME.EXE addresses; see docs/re/README.md, "The interface bar". */

#define BAR_TOP 380
#define BAR_HEIGHT 100
#define VIAL_FRAMES 32
#define FULL_HEALTH_X 10            /* the vials in the full view (0x2E920) */
#define FULL_SPELL_POINTS_X 54
/* Where the face goes (measured from two screenshots, a female High Elf's and a male Dark Elf's: the
 * game's rule isn't found): the top of the head 10 below the bar's top, its right edge at x 107. */
#define FACE_TOP 390
#define FACE_RIGHT 107
#define HEAD_ROWS 60

/* The buttons (0xED838: x0, y0, x1, y1, inclusive). */
static const int buttons[STATUSBAR_BUTTONS][4] = {
    {33, 380, 167, 479},  /* the face: the character sheet (0x558DC) */
    {229, 380, 327, 429}, /* the spell book (0x2988B) */
    {330, 380, 417, 428}, /* the inventory (0x4E4C1) */
    {229, 430, 327, 479}, /* the automap (0x57415) */
    {330, 430, 417, 479}, /* the options (0x2EA54) */
    {500, 380, 639, 479}, /* the compass: the hot key list (0x551C5) */
    {9, 455, 32, 477},    /* help (0x2EA28) */
};

void statusbar_load(statusbar *s, bs_assets *a)
{
    s->panel = bs_assets_bsi(a, "panel.bsi");
    s->health = bs_assets_image(a, "engy01.bsi");
    s->spell_points = bs_assets_image(a, "engy02.bsi");
    s->compass = bs_assets_image(a, "comp00.bsi");
    s->compass_full = bs_assets_image(a, "comp01.bsi");
}

/* The head in the body image: its top row and the right edge of its first rows, in screen pixels. */
static bool head_of(const bs_bsi_image *body, int *top, int *right)
{
    if (!body || !body->pixels || !body->transparent)
        return false;
    int t = -1, r = -1;
    for (uint32_t j = 0; j < body->height && (t < 0 || (int)j < t + HEAD_ROWS); j++) {
        const uint8_t *row = body->pixels + (size_t)j * body->width;
        for (uint32_t i = 0; i < body->width; i++)
            if (row[i]) {
                if (t < 0)
                    t = (int)j;
                if ((int)i > r)
                    r = (int)i;
            }
    }
    if (t < 0)
        return false;
    *top = body->y + t;
    *right = body->x + r;
    return true;
}

void statusbar_set_face(statusbar *s, bs_assets *a, char sex, const char *race, int hair, int eyes, int mouth)
{
    char name[24];
    snprintf(name, sizeof name, "%c%sbd001.bsi", sex, race);
    s->face[0] = bs_assets_image(a, name);
    snprintf(name, sizeof name, "%c%sey%03d.bsi", sex, race, eyes);
    s->face[1] = bs_assets_image(a, name);
    snprintf(name, sizeof name, "%c%smt%03d.bsi", sex, race, mouth);
    s->face[2] = bs_assets_image(a, name);
    snprintf(name, sizeof name, "%c%shr%03d.bsi", sex, race, hair);
    s->face[3] = bs_assets_image(a, name);
    int top, right;
    if (head_of(s->face[0], &top, &right)) {
        s->face_dx = FACE_RIGHT - right;
        s->face_dy = FACE_TOP - top;
    } else {
        s->face[0] = NULL;
    }
}

/* A vial's frame (0x2E939): full is 0, empty 31. */
static uint32_t vial_frame(int32_t value, int32_t max)
{
    if (max <= 0)
        return VIAL_FRAMES - 1;
    float f = 32.0f - (float)value * 32.0f / (float)max;
    int n = (int)f;
    return n < 0 ? 0 : n > VIAL_FRAMES - 1 ? VIAL_FRAMES - 1 : (uint32_t)n;
}

void statusbar_draw(const statusbar *s, bs_overlay *o, const bs_character *c, int32_t yaw)
{
    /* The compass's frame (0x2E6AB): the yaw in tenths of 568. */
    uint32_t heading = (uint32_t)((yaw & 0x7FF) * 10 / 568);
    uint32_t health = vial_frame(c->health, c->health_max);
    uint32_t points = vial_frame(c->spell_points, c->spell_points_max);
    if (s->full_view) {
        if (s->health)
            bs_overlay_image_frame(o, s->health, health, FULL_HEALTH_X - s->health->x, 0);
        if (s->spell_points)
            bs_overlay_image_frame(o, s->spell_points, points, FULL_SPELL_POINTS_X - s->spell_points->x, 0);
        if (s->compass_full)
            bs_overlay_image_frame(o, s->compass_full, heading, 0, 0);
        return;
    }
    /* The face shows through the bar's hole. */
    if (s->face[0]) {
        bs_overlay_clip(o, 0, BAR_TOP, BS_OVERLAY_WIDTH, BAR_HEIGHT);
        for (int i = 0; i < 4; i++)
            if (s->face[i])
                bs_overlay_image(o, s->face[i], s->face_dx, s->face_dy);
        bs_overlay_clip(o, 0, 0, 0, 0);
    }
    if (s->panel && s->panel->count >= 6) {
        bs_overlay_image(o, &s->panel->images[1], 0, 0);
        for (size_t i = 2; i < 6; i++)
            bs_overlay_image(o, &s->panel->images[i], 0, 0);
    }
    if (s->health)
        bs_overlay_image_frame(o, s->health, health, 0, 0);
    if (s->spell_points)
        bs_overlay_image_frame(o, s->spell_points, points, 0, 0);
    if (s->compass)
        bs_overlay_image_frame(o, s->compass, heading, 0, 0);
}

statusbar_button statusbar_at(const statusbar *s, int x, int y)
{
    if (s->full_view)
        return STATUSBAR_NONE;
    for (int b = 0; b < STATUSBAR_BUTTONS; b++) {
        const int *r = buttons[b];
        if (x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3])
            return (statusbar_button)b;
    }
    return STATUSBAR_NONE;
}

bool statusbar_covers(const statusbar *s, int x, int y)
{
    (void)x;
    return !s->full_view && y >= BAR_TOP;
}
