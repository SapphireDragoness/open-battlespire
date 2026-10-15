#include "bs/game/level_script.h"

#include <string.h>

/* RE references are GAME.EXE addresses; see docs/re/README.md, "Keys, sigils and gems". */

enum {
    BASE_VOIDGUIDE = 30, BASE_GATEKEY_6 = 35, BASE_COG = 36, BASE_ROD = 37, BASE_GATEKEY_5 = 38,
};
enum {
    MSG_TEMPLE_SIX = 2, MSG_GATE_INACTIVE = 124, MSG_GATE_OPENED = 125, MSG_ROD_PLACED = 126,
    MSG_KEY_PLACED = 127, MSG_TEMPLE_THREE = 129,
};

static const char *const counter1[] = {"1cnt01", "1cnt02", "1cnt03", "1cnt04", "1cnt05", "1cnt06"};
static const char *const counter2[] = {"alta01", "alta02", "alta03", "alta04", "alta05"};

uint32_t bs_level_script_counter(int level_number, const char *const **names)
{
    if (level_number == 1) {
        *names = counter1;
        return 6;
    }
    if (level_number == 2) {
        *names = counter2;
        return 5;
    }
    *names = NULL;
    return 0;
}

void bs_level_script_init(bs_level_script *s, int level_number)
{
    memset(s, 0, sizeof *s);
    s->level_number = level_number;
}

/* Whether a model name starts with p, ignoring case (the game's 0x870A9). */
static bool prefix(const char *model, const char *p)
{
    for (; *p; model++, p++) {
        char a = *model, b = *p;
        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

/* Takes one carried item of a base (0x53B5D, 0x22782): false if there is none. */
static bool take(bs_items *it, uint32_t base)
{
    for (uint32_t i = 0; i < it->count; i++) {
        bs_world_item *w = &it->items[i];
        if (w->item.base != base || w->place.where == BS_ITEM_GONE || !bs_items_place_carried(it, w->place))
            continue;
        if (w->item.quantity > 1)
            w->item.quantity--;
        else
            w->place.where = BS_ITEM_GONE;
        return true;
    }
    return false;
}

static uint32_t carried(const bs_items *it, uint32_t base)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < it->count; i++) {
        const bs_world_item *w = &it->items[i];
        if (w->item.base == base && w->place.where != BS_ITEM_GONE && bs_items_place_carried(it, w->place))
            n += w->item.quantity > 0 ? (uint32_t)w->item.quantity : 1;
    }
    return n;
}

static void say(bs_script_result *r, uint32_t id)
{
    if (r->message_count < BS_SCRIPT_MESSAGES)
        r->messages[r->message_count++] = id;
}

bs_script_result bs_level_script_use(bs_level_script *s, bs_items *it, const char *model, bool locked,
                                     bs_script_moved moved, void *ctx)
{
    bs_script_result r;
    memset(&r, 0, sizeof r);
    r.counter = -1;
    r.locked = locked;
    switch (s->level_number) {
    case 1:
    case 2: {
        /* Cogs (level 1) or Voidguides (level 2) at the locked objects (0x3E596, 0x3E6F5). */
        if (!locked || !take(it, s->level_number == 1 ? BASE_COG : BASE_VOIDGUIDE))
            break;
        const char *const *names;
        uint32_t n = bs_level_script_counter(s->level_number, &names);
        /* The counter moves on one image; the last opens the way (0x3E4C7). */
        bool was = s->done;
        if (s->progress + 1u < n) {
            s->progress++;
            r.counter = s->progress;
        }
        s->done = s->progress + 1u >= n;
        if (s->done && !was)
            r.activate = true;
        break;
    }
    case 3:
        /* The gates (l2dr1, l2dr2) stay shut until the spool takes three rods with both buttons pressed (0x3E7DA). */
        if (prefix(model, "l2dr2") || prefix(model, "l2dr1")) {
            if (!s->done)
                say(&r, MSG_GATE_INACTIVE);
            r.locked = !s->done;
        } else if (prefix(model, "l2spool")) {
            r.locked = true;
            if (carried(it, BASE_ROD) == 0 || !moved || !moved(ctx, "l2but4") || !moved(ctx, "l2but05"))
                break;
            take(it, BASE_ROD);
            if (++s->progress == 3) {
                r.activate = true;
                say(&r, MSG_GATE_OPENED);
                s->done = true;
            } else {
                say(&r, MSG_ROD_PLACED);
            }
        }
        break;
    case 5:
        /* The temple door takes all six Gatekeys at once (0x4D42A). */
        if (prefix(model, "tempd")) {
            if (carried(it, BASE_GATEKEY_5) >= 6) {
                for (int k = 0; k < 6; k++)
                    take(it, BASE_GATEKEY_5);
                r.activate = true;
            } else {
                say(&r, MSG_TEMPLE_SIX);
                r.locked = true;
            }
        }
        break;
    case 6:
        /* Three keys to open the temple (0x3EA54). */
        if (prefix(model, "l6phone")) {
            r.locked = true;
            if (take(it, BASE_GATEKEY_6)) {
                say(&r, MSG_KEY_PLACED);
                if (++s->progress == 3) {
                    r.activate = true;
                    say(&r, MSG_GATE_OPENED);
                }
            } else {
                say(&r, MSG_TEMPLE_THREE);
            }
        }
        break;
    default:
        break;
    }
    return r;
}

/* ---- level exits ---- */

static bool same_name(const char *a, const char *b)
{
    for (; *a || *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z')
            x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z')
            y = (char)(y - 'A' + 'a');
        if (x != y)
            return false;
    }
    return true;
}

bool bs_level_script_exit(const bs_level_script *s, const bs_exit_query *q)
{
    const char *model = q->floor_model, *tex = q->floor_texture ? q->floor_texture : "";
    if (!model)
        return false;
    switch (s->level_number) {
    case 1: /* 0x3E695 */
        return prefix(model, "1zoom") && s->done && q->doorpans_open >= 5;
    case 2: /* 0x3E78E */
        return same_name(tex, "port03") && s->done;
    case 3: /* 0x3E931 */
        return prefix(model, "l2a46") && same_name(tex, "black");
    case 4:
    case 5: /* 0x3E98A, 0x3EA22 */
        return q->floor_locked;
    case 6: /* 0x3EB28 */
        return q->floor_locked && s->progress == 3;
    default:
        return false;
    }
}

/* ---- sigils ---- */

#define SIGIL_REACH 300.0f       /* across (0x5B580) */
#define SIGIL_HEIGHT 160.0f      /* up or down (0x5B5A9) */
#define SIGIL_BLOCK 120.0f       /* blocking (0x5B690) */
#define SIGIL_FIRST 9001

bool bs_sigil_model(const char *model, char *letter)
{
    if (!prefix(model, "sigil") || !model[5])
        return false;
    char c = model[5];
    if (c >= 'A' && c <= 'Z')
        c = (char)(c - 'A' + 'a');
    if (c < 'a' || c > 'z')
        return false;
    *letter = c;
    return true;
}

bool bs_items_have_sigil(const bs_items *it, char letter)
{
    for (uint32_t i = 0; i < it->count; i++) {
        const bs_world_item *w = &it->items[i];
        if (w->item.base == BS_ITEM_SIGIL_AMULET && w->item.magic - SIGIL_FIRST == letter - 'a' &&
            w->place.where != BS_ITEM_GONE && bs_items_place_carried(it, w->place))
            return true;
    }
    return false;
}

bs_sigil_event bs_sigils_check(bs_sigil *sigils, uint32_t count, const bs_items *it, const float pos[3],
                               uint32_t frame, uint32_t *which, bool *hurt)
{
    *hurt = false;
    for (uint32_t i = 0; i < count; i++) {
        bs_sigil *s = &sigils[i];
        if (s->gone)
            continue;
        float dx = s->pos[0] - pos[0], dz = s->pos[2] - pos[2];
        float across = dx * dx + dz * dz, dy = s->pos[1] - pos[1];
        if (across > SIGIL_REACH * SIGIL_REACH || dy > SIGIL_HEIGHT || dy < -SIGIL_HEIGHT)
            continue;
        *which = i;
        if (bs_items_have_sigil(it, s->letter)) {
            s->gone = true;
            return BS_SIGIL_OPENED;
        }
        if (across <= SIGIL_BLOCK * SIGIL_BLOCK) {
            *hurt = s->last_hurt + 1 != frame;
            s->last_hurt = frame;
            return BS_SIGIL_BLOCKED;
        }
    }
    return BS_SIGIL_NONE;
}

/* ---- gems ---- */

int32_t bs_gem_use(bs_character *c, bool spell_points, int32_t percent)
{
    int32_t *v = spell_points ? &c->spell_points : &c->health;
    int32_t max = spell_points ? c->spell_points_max : c->health_max;
    int32_t change = max * percent / 100;
    int32_t was = *v;
    *v += change;
    if (*v > max)
        *v = max;
    if (*v < 0)
        *v = 0;
    return *v - was;
}
