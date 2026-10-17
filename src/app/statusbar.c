#include "statusbar.h"

#include <stdio.h>
#include <string.h>

/* RE references are GAME.EXE addresses; see docs/re/README.md, "The interface bar". */

#define BAR_TOP 380
#define BAR_HEIGHT 100
#define VIAL_FRAMES 32
#define FULL_HEALTH_X 10            /* the vials in the full view (0x2E920) */
#define FULL_SPELL_POINTS_X 54
/* The face (0x5678A): the paper doll cut out around its eyes, from 70 left and 35 above the eyes
 * image's placement, into a 149 x 100 picture at (0, 380); the frame fram00.bsi goes over it. */
#define FACE_X 0
#define FACE_Y 380
#define FACE_W 149
#define FACE_H 100
#define FACE_LEFT_OF_EYES 70
#define FACE_ABOVE_EYES 35

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
    s->face_frame = bs_assets_image(a, "fram00.bsi");
}

void statusbar_set_face(statusbar *s, const bs_bsi_image *const *layers, int count, const bs_bsi_image *eyes)
{
    if (count > STATUSBAR_FACE_LAYERS)
        count = STATUSBAR_FACE_LAYERS;
    if (!eyes) {
        s->face_count = 0;
        return;
    }
    int left = eyes->x - FACE_LEFT_OF_EYES, top = eyes->y - FACE_ABOVE_EYES;
    s->face_dx = FACE_X - (left < 0 ? 0 : left);
    s->face_dy = FACE_Y - (top < 0 ? 0 : top);
    for (int i = 0; i < count; i++)
        s->face[i] = layers[i];
    s->face_count = count;
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
    if (s->face_count > 0) {
        bs_overlay_clip(o, FACE_X, FACE_Y, FACE_W, FACE_H);
        for (int i = 0; i < s->face_count; i++)
            bs_overlay_image(o, s->face[i], s->face_dx, s->face_dy);
        bs_overlay_clip(o, 0, 0, 0, 0);
        if (s->face_frame)
            bs_overlay_image(o, s->face_frame, 0, 0);
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
