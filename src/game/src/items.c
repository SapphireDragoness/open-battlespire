#include "bs/game/items.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/core/log.h"

/* RE references are GAME.EXE addresses; see docs/formats/items.md. */

#define SPEC_RANDOM 10000       /* "any": a plain item (as the number) or a random base (as the base) */
#define SIGIL_FIRST 9001        /* 9001-9052: Sigil Amulets with that enchantment (0x52384) */
#define SIGIL_LAST 9052
#define POTION_RESTORATION 40   /* number 40 makes a potion on any base */
#define POTION_TYPE_SWAP 14     /* potion type 14 is made as 21 (0x51F12) */

/* The material roll's thresholds (0x51634, table at 0xEFE29). */
static const int32_t material_threshold[BS_ITEM_MATERIAL_COUNT] = {40, 65, 75, 85, 90, 95, 97, 98, 99, 200};

const bs_magic *bs_item_db_magic(const bs_item_db *db, int32_t id)
{
    for (int i = 0; i < 2; i++) {
        const bs_magic *m = db->magic[i] ? bs_magic_find(db->magic[i], id) : NULL;
        if (m)
            return m;
    }
    return NULL;
}

uint32_t bs_rng_next(bs_rng *r)
{
    /* xorshift32 */
    uint32_t x = r->state ? r->state : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->state = x;
    return x;
}

int32_t bs_rng_range(bs_rng *r, int32_t lo, int32_t hi)
{
    if (hi <= lo)
        return lo;
    return lo + (int32_t)(bs_rng_next(r) % (uint32_t)(hi - lo + 1));
}

uint16_t bs_item_material_for_roll(int32_t roll)
{
    uint16_t m = 0;
    while (m + 1 < BS_ITEM_MATERIAL_COUNT && roll > material_threshold[m])
        m++;
    return m;
}

uint16_t bs_item_roll_material(int level_number, bool multiplayer, bs_rng *r)
{
    int32_t roll = (level_number - 4) * 8 + bs_rng_range(r, 1, 100);
    if (multiplayer)
        roll += 20;
    return bs_item_material_for_roll(roll);
}

void bs_item_make_plain(const bs_item_db *db, uint32_t base, int level_number, bool multiplayer, bs_rng *r,
                        bs_item *out)
{
    memset(out, 0, sizeof *out);
    out->base = (uint16_t)(base < BS_ITEM_BASE_COUNT ? base : 0);
    out->condition = out->condition_max = db->tables->bases[out->base].condition;
    out->quantity = 1;
    out->material = bs_item_roll_material(level_number, multiplayer, r);
    if (out->base == BS_ITEM_ARROW) /* 4-8 arrows for every two levels */
        out->quantity = bs_rng_range(r, 4, 8) * (level_number / 2 + 1);
    bs_item_pick_picture(db, out, r);
}

static bool starts_with_nocase(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++) {
        char a = *s, b = *prefix;
        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = (char)(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

static int cmp_name(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Clothes' kinds (0xEF860): the icon "x" becomes one of them. */
static const char *const clothes_kinds[4] = {"armb", "cape", "pnts", "shrt"};

void bs_item_pick_picture(const bs_item_db *db, bs_item *it, bs_rng *r)
{
    it->picture[0] = '\0';
    const char *icon = db->tables->bases[it->base].icon;
    if (!icon[0] || !db->pictures)
        return;
    char sex = db->female ? 'f' : 'm';
    if (bs_item_is_weapon(it->base)) {
        snprintf(it->picture, sizeof it->picture, "%c%s", sex, icon + 1);
        return;
    }
    char prefix[16];
    int kind = bs_rng_range(r, 0, 3);
    snprintf(prefix, sizeof prefix, "%c%s", sex, icon[0] == 'x' ? clothes_kinds[kind] : icon);
    /* The game takes the matches in the archive's order; sorted here. */
    const char *found[64];
    uint32_t n = 0;
    for (uint32_t i = 0; i < db->picture_count && n < 64; i++)
        if (starts_with_nocase(db->pictures[i], prefix))
            found[n++] = db->pictures[i];
    if (!n)
        return;
    qsort(found, n, sizeof *found, cmp_name);
    snprintf(it->picture, sizeof it->picture, "%s", found[bs_rng_range(r, 0, (int32_t)n - 1)]);
}

int32_t bs_item_armour_value(const bs_item *it)
{
    int32_t m = (int32_t)it->material - 1;
    if (m > 1)
        m /= 2;
    int32_t d = ((int32_t)(uint8_t)it->picture[7] - '0') % 4; /* C remainder, as the game's idiv */
    int32_t v = d + 2 + m - 1;
    return v < 1 ? 1 : v;
}

/* Items anyone may equip (0xEF874): base and enchantment. */
static const int32_t always_equip[13][2] = {
    {7, 2017}, {37, 3013}, {37, 3012}, {37, 3011}, {0, 4012}, {3, 8011}, {24, 5010},
    {25, 5011}, {26, 5012}, {27, 5013}, {28, 5014}, {29, 5015}, {14, 5016},
};
/* Weapon skill (14..19) to the class's forbidden-weapon bit (0xEFE91). */
static const uint8_t weapon_group[6] = {0, 1, 0, 3, 2, 4};

bool bs_item_may_equip(const bs_item_db *db, const bs_item *it, const bs_equip_rules *rules)
{
    for (int i = 0; i < 13; i++)
        if (it->base == always_equip[i][0] && it->magic == always_equip[i][1])
            return true;
    if (bs_item_is_weapon(it->base)) {
        int32_t skill = db->tables->weapons[it->base].skill - 14;
        if (skill >= 0 && skill < 6 && (rules->forbidden_weapons & (1u << weapon_group[skill])))
            return false;
        return it->material <= rules->material;
    }
    if (bs_item_is_armour(it->base)) {
        int32_t type = bs_item_armour_value(it) / 2;
        if (type > 3)
            type = 3;
        return rules->armour != 0 && rules->armour >= type && it->material <= rules->material;
    }
    return true;
}

/* A random weapon, but not a sling (0x518CE). */
static uint32_t random_weapon(bs_rng *r)
{
    int32_t b;
    do
        b = bs_rng_range(r, 0, BS_ITEM_LAST_WEAPON);
    while (b == 12);
    return (uint32_t)b;
}

/* A random base for an enchantment (0x524F6): its Item field picks the kind. */
static uint32_t random_base(const bs_item_db *db, const bs_magic **m, bs_rng *r)
{
    for (int tries = 0; tries < 16; tries++) {
        switch ((*m)->item) {
        case 0:
            return BS_ITEM_CLOTHES; /* 0x520CC */
        case 1:
            return random_weapon(r);
        case 3: {
            /* Any enchantment of the general list instead, then again. */
            const bs_magic_table *t = db->magic[0] && db->magic[0]->count ? db->magic[0] : db->magic[1];
            if (!t || !t->count)
                return BS_ITEM_CLOTHES;
            *m = &t->records[bs_rng_range(r, 0, (int32_t)t->count - 1)];
            break;
        }
        default:
            return bs_rng_next(r) & 1 ? BS_ITEM_CLOTHES : random_weapon(r);
        }
    }
    return BS_ITEM_CLOTHES;
}

bool bs_item_make(const bs_item_db *db, bs_item_spec_kind kind, const int32_t v[4], int level_number,
                  bool multiplayer, bs_rng *r, bs_item *out)
{
    /* The two layouts name the same things in different places: base and
     * number are v[0] and v[3] when placed, v[1] and v[0] as contents. */
    int32_t base = kind == BS_ITEM_SPEC_PLACED ? v[0] : v[1];
    int32_t number = kind == BS_ITEM_SPEC_PLACED ? v[3] : v[0];
    if (number >= SIGIL_FIRST && number <= SIGIL_LAST) {
        bs_item_make_plain(db, BS_ITEM_SIGIL_AMULET, level_number, multiplayer, r, out);
        out->number = number;
        out->magic = bs_item_db_magic(db, number) ? number : 0;
        return true;
    }
    if (kind == BS_ITEM_SPEC_CONTENT && base == BS_ITEM_LANTERN) {
        bs_item_make_plain(db, BS_ITEM_LANTERN, level_number, multiplayer, r, out); /* 0x51701 */
        return true;
    }
    if (base == BS_ITEM_POTION || number == POTION_RESTORATION) {
        bs_item_make_plain(db, BS_ITEM_POTION, level_number, multiplayer, r, out); /* 0x51EAA */
        out->number = number == POTION_TYPE_SWAP ? 21 : number;
        return true;
    }
    if (kind == BS_ITEM_SPEC_PLACED && (base == BS_ITEM_SCROLL || base == BS_ITEM_BOOK)) {
        bs_item_make_plain(db, (uint32_t)base, level_number, multiplayer, r, out); /* 0x522D7 */
        out->number = number;
        return true;
    }
    if (number == SPEC_RANDOM) {
        if (base < 0 || base >= BS_ITEM_BASE_COUNT) {
            BS_WARN("items: unknown base item %d", base);
            return false;
        }
        bs_item_make_plain(db, (uint32_t)base, level_number, multiplayer, r, out);
        return true;
    }
    const bs_magic *m = bs_item_db_magic(db, number);
    if (!m) {
        /* The game stops here: "Unable to find magic item specified as %d_%d". */
        BS_WARN("items: unknown magic item %d on base %d", number, base);
        return false;
    }
    uint32_t b;
    if (base == SPEC_RANDOM) {
        b = random_base(db, &m, r);
    } else if (base >= 0 && base < BS_ITEM_BASE_COUNT) {
        b = (uint32_t)base;
    } else {
        BS_WARN("items: unknown base item %d", base);
        return false;
    }
    bs_item_make_plain(db, b, level_number, multiplayer, r, out); /* 0x5261A */
    out->magic = m->id;
    return true;
}

static size_t append(char *buf, size_t cap, size_t n, const char *s)
{
    if (!s[0])
        return n;
    if (n && n + 1 < cap)
        buf[n++] = ' ';
    while (*s && n + 1 < cap)
        buf[n++] = *s++;
    buf[n] = '\0';
    return n;
}

size_t bs_item_name(const bs_item_db *db, const bs_item *it, char *buf, size_t cap)
{
    if (!cap)
        return 0;
    buf[0] = '\0';
    const bs_magic *m = it->magic ? bs_item_db_magic(db, it->magic) : NULL;
    size_t n = 0;
    if (bs_item_is_weapon(it->base) || bs_item_is_armour(it->base) || it->base == BS_ITEM_ARROW)
        n = append(buf, cap, n, db->tables->materials[it->material % BS_ITEM_MATERIAL_COUNT]);
    n = append(buf, cap, n, db->tables->bases[it->base].name);
    if (m)
        n = append(buf, cap, n, m->name);
    if (it->base == BS_ITEM_ARROW && n + 1 < cap) {
        int k = snprintf(buf + n, cap - n, " (%d)", it->quantity);
        n = k > 0 ? (n + (size_t)k < cap ? n + (size_t)k : cap - 1) : n;
    }
    return n;
}

int32_t bs_item_weight(const bs_item_db *db, const bs_item *it)
{
    if (it->base == BS_ITEM_ARROW)
        return it->quantity / 2 + 1;
    return db->tables->bases[it->base].weight;
}

/* ---- where items are ---- */

#define GRID_CELLS 64

void bs_items_free(bs_items *it)
{
    free(it->items);
    free(it->containers);
    memset(it, 0, sizeof *it);
}

uint32_t bs_items_add(bs_items *it, const bs_item *item, bs_item_place place, int32_t object, const float pos[3])
{
    if (it->count == it->cap) {
        uint32_t cap = it->cap ? it->cap * 2 : 256;
        bs_world_item *n = realloc(it->items, sizeof *n * cap);
        if (!n)
            return UINT32_MAX;
        it->items = n;
        it->cap = cap;
    }
    bs_world_item *w = &it->items[it->count];
    memset(w, 0, sizeof *w);
    w->item = *item;
    w->place = place;
    w->object = object;
    if (pos)
        memcpy(w->pos, pos, sizeof w->pos);
    w->seq = it->seq++;
    return it->count++;
}

uint32_t bs_items_cell(int32_t grid_cell, const float pos[3])
{
    if (grid_cell <= 0)
        return BS_ITEM_CELL_OUTSIDE;
    /* The game converts to integers and divides, truncating towards 0. */
    int32_t cx = (int32_t)pos[0] / grid_cell + GRID_CELLS / 2, cz = (int32_t)pos[2] / grid_cell + GRID_CELLS / 2;
    if (cx < 0 || cx >= GRID_CELLS || cz < 0 || cz >= GRID_CELLS)
        return BS_ITEM_CELL_OUTSIDE;
    return (uint32_t)(cx * GRID_CELLS + cz);
}

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

bs_result bs_items_add_level(bs_items *it, const bs_bs6_level *level, const bs_item_db *db, int level_number,
                             bool multiplayer, int32_t grid_cell, bs_rng *r)
{
    it->grid_cell = grid_cell;
    bs_arena tmp;
    bs_arena_init(&tmp, 0);
    bs_result res = BS_OK;
    uint32_t *containers = calloc(level->object_count + 1, sizeof *containers);
    uint32_t ncont = 0;
    if (!containers)
        res = BS_ERR_NOMEM;
    for (uint32_t i = 0; i < level->object_count && res == BS_OK; i++) {
        bs_bs6_logic lg;
        bs_arena_mark mark = bs_arena_get_mark(&tmp);
        res = bs_bs6_entity_logic(&level->objects[i].chunk, &tmp, &lg);
        const bs_bs6_object *o = &level->objects[i];
        float pos[3] = {(float)o->pos[0], (float)o->pos[1], (float)o->pos[2]};
        bool container = false;
        /* In the game's order (0x2475B): properties as they come. */
        for (uint32_t p = 0; res == BS_OK && p < lg.prop_count; p++) {
            const bs_bs6_record *rec = &lg.props[p];
            int32_t v[4] = {0, 0, 0, 0};
            bs_item_spec_kind kind = BS_ITEM_SPEC_PLACED;
            if (rec->id == BS_BS6_PROP_ITEM || rec->id == BS_BS6_PROP_CONTENTS) {
                for (uint32_t k = 0; k < 4; k++)
                    v[k] = bs_bs6_record_i32(rec, k);
                kind = rec->id == BS_BS6_PROP_CONTENTS ? BS_ITEM_SPEC_CONTENT : BS_ITEM_SPEC_PLACED;
            } else if (rec->id == BS_BS6_PROP_BOOK || rec->id == BS_BS6_PROP_POTION) {
                v[0] = rec->id == BS_BS6_PROP_BOOK ? BS_ITEM_BOOK : BS_ITEM_POTION;
                v[3] = bs_bs6_record_i32(rec, 0);
            } else {
                continue;
            }
            bs_item item;
            if (!bs_item_make(db, kind, v, level_number, multiplayer, r, &item))
                continue;
            bs_item_place place = {BS_ITEM_FLOOR, bs_items_cell(grid_cell, pos)};
            if (kind == BS_ITEM_SPEC_CONTENT) {
                place = (bs_item_place){BS_ITEM_IN_OBJECT, i};
                container = true;
            }
            if (bs_items_add(it, &item, place, kind == BS_ITEM_SPEC_CONTENT ? -1 : (int32_t)i, pos) == UINT32_MAX)
                res = BS_ERR_NOMEM;
        }
        if (container)
            containers[ncont++] = i;
        bs_arena_rewind(&tmp, mark);
    }
    bs_arena_free(&tmp);
    if (res != BS_OK) {
        free(containers);
        return res;
    }
    free(it->containers);
    it->containers = containers;
    it->container_count = ncont;
    qsort(it->containers, ncont, sizeof *containers, cmp_u32);
    return BS_OK;
}

/* The place an item is ultimately in: up through the containers holding it. */
static bs_item_place root_place(const bs_items *it, bs_item_place p)
{
    for (uint32_t guard = 0; p.where == BS_ITEM_IN_ITEM && p.ref < it->count && guard <= it->count; guard++)
        p = it->items[p.ref].place;
    return p;
}

bool bs_items_place_carried(const bs_items *it, bs_item_place place)
{
    return root_place(it, place).where == BS_ITEM_CARRIED;
}

void bs_items_keep_carried(bs_items *it)
{
    uint32_t *map = malloc(sizeof *map * (it->count + 1));
    if (!map)
        return;
    uint32_t n = 0;
    for (uint32_t i = 0; i < it->count; i++)
        map[i] = it->items[i].place.where != BS_ITEM_GONE && bs_items_place_carried(it, it->items[i].place)
                     ? n++
                     : UINT32_MAX;
    for (uint32_t i = 0; i < it->count; i++) {
        if (map[i] == UINT32_MAX)
            continue;
        bs_world_item w = it->items[i];
        if (w.place.where == BS_ITEM_IN_ITEM)
            w.place.ref = map[w.place.ref];
        it->items[map[i]] = w;
    }
    it->count = n;
    it->container_count = 0;
    free(map);
}

bs_result bs_items_take_level(bs_items *it, bs_items *out)
{
    memset(out, 0, sizeof *out);
    uint32_t *map = malloc(sizeof *map * (it->count + 1));
    if (!map)
        return BS_ERR_NOMEM;
    uint32_t n = 0;
    for (uint32_t i = 0; i < it->count; i++)
        map[i] = it->items[i].place.where != BS_ITEM_GONE && !bs_items_place_carried(it, it->items[i].place)
                     ? n++
                     : UINT32_MAX;
    for (uint32_t i = 0; i < it->count; i++) {
        if (map[i] == UINT32_MAX)
            continue;
        bs_world_item w = it->items[i];
        if (w.place.where == BS_ITEM_IN_ITEM)
            w.place.ref = map[w.place.ref];
        if (bs_items_add(out, &w.item, w.place, w.object, w.pos) == UINT32_MAX) {
            free(map);
            bs_items_free(out);
            return BS_ERR_NOMEM;
        }
        out->items[out->count - 1] = w;
    }
    free(map);
    out->seq = it->seq;
    out->grid_cell = it->grid_cell;
    if (it->container_count) {
        out->containers = malloc(sizeof *out->containers * it->container_count);
        if (!out->containers) {
            bs_items_free(out);
            return BS_ERR_NOMEM;
        }
        memcpy(out->containers, it->containers, sizeof *out->containers * it->container_count);
        out->container_count = it->container_count;
    }
    bs_items_keep_carried(it);
    return BS_OK;
}

bs_result bs_items_put_level(bs_items *it, const bs_items *level)
{
    uint32_t base = it->count;
    for (uint32_t i = 0; i < level->count; i++) {
        bs_world_item w = level->items[i];
        if (w.place.where == BS_ITEM_IN_ITEM)
            w.place.ref += base;
        if (bs_items_add(it, &w.item, w.place, w.object, w.pos) == UINT32_MAX)
            return BS_ERR_NOMEM;
        it->items[it->count - 1] = w;
    }
    if (level->seq > it->seq)
        it->seq = level->seq;
    it->grid_cell = level->grid_cell;
    free(it->containers);
    it->containers = NULL;
    it->container_count = 0;
    if (level->container_count) {
        it->containers = malloc(sizeof *it->containers * level->container_count);
        if (!it->containers)
            return BS_ERR_NOMEM;
        memcpy(it->containers, level->containers, sizeof *it->containers * level->container_count);
        it->container_count = level->container_count;
    }
    return BS_OK;
}

static bool same_place(bs_item_place a, bs_item_place b)
{
    return a.where == b.where && (a.where == BS_ITEM_CARRIED || a.ref == b.ref);
}

/* Insertion sort by seq: lists are short. */
uint32_t bs_items_list(const bs_items *it, bs_item_place place, uint32_t *out, uint32_t max)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < it->count; i++) {
        if (!same_place(it->items[i].place, place) || it->items[i].place.where == BS_ITEM_GONE)
            continue;
        if (n < max) {
            uint32_t k = n;
            while (k > 0 && it->items[out[k - 1]].seq > it->items[i].seq) {
                out[k] = out[k - 1];
                k--;
            }
            out[k] = i;
        }
        n++;
    }
    return n;
}

bool bs_items_is_container_object(const bs_items *it, uint32_t object)
{
    uint32_t lo = 0, hi = it->container_count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (it->containers[mid] < object)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < it->container_count && it->containers[lo] == object;
}

uint32_t bs_items_on_object(const bs_items *it, uint32_t object)
{
    for (uint32_t i = 0; i < it->count; i++)
        if (it->items[i].place.where == BS_ITEM_FLOOR && it->items[i].object == (int32_t)object)
            return i;
    return UINT32_MAX;
}

bool bs_items_is_container(const bs_item_db *db, const bs_items *it, uint32_t i)
{
    return i < it->count && db->tables->bases[it->items[i].item.base].container;
}

int32_t bs_items_weight(const bs_item_db *db, const bs_items *it, uint32_t i)
{
    int32_t w = bs_item_weight(db, &it->items[i].item);
    for (uint32_t k = 0; k < it->count; k++)
        if (k != i && it->items[k].place.where == BS_ITEM_IN_ITEM && it->items[k].place.ref == i)
            w += bs_items_weight(db, it, k);
    return w;
}

int32_t bs_items_carried_weight(const bs_item_db *db, const bs_items *it)
{
    int32_t w = 0;
    for (uint32_t i = 0; i < it->count; i++)
        if (it->items[i].place.where != BS_ITEM_GONE && bs_items_place_carried(it, it->items[i].place))
            w += bs_item_weight(db, &it->items[i].item);
    return w;
}

bool bs_items_encumbered(const bs_item_db *db, const bs_items *it, int32_t strength)
{
    return bs_items_carried_weight(db, it) > strength * 3;
}

/* Whether place is i itself or inside it. */
static bool inside(const bs_items *it, bs_item_place place, uint32_t i)
{
    for (uint32_t guard = 0; place.where == BS_ITEM_IN_ITEM && guard <= it->count; guard++) {
        if (place.ref == i)
            return true;
        if (place.ref >= it->count)
            return false;
        place = it->items[place.ref].place;
    }
    return false;
}

static bool stacks_with(const bs_item *a, const bs_item *b)
{
    if (a->base != b->base)
        return false;
    if (a->base == BS_ITEM_ARROW)
        return a->material == b->material && a->magic == b->magic;
    if (a->base == BS_ITEM_POTION)
        return a->number == b->number;
    return false;
}

bs_item_move bs_items_move(bs_items *it, const bs_item_db *db, uint32_t i, bs_item_place to,
                           const float floor_pos[3], int32_t strength)
{
    if (i >= it->count || it->items[i].place.where == BS_ITEM_GONE || to.where == BS_ITEM_GONE)
        return BS_MOVE_INVALID;
    if (to.where == BS_ITEM_IN_ITEM && (to.ref >= it->count || !bs_items_is_container(db, it, to.ref)))
        return BS_MOVE_INVALID;
    if (inside(it, to, i))
        return BS_MOVE_INTO_ITSELF;
    bs_world_item *w = &it->items[i];
    if (bs_items_place_carried(it, to) && !bs_items_place_carried(it, w->place) &&
        bs_items_carried_weight(db, it) + bs_items_weight(db, it, i) > strength * 4)
        return BS_MOVE_TOO_HEAVY;
    if (to.where == BS_ITEM_FLOOR) {
        if (floor_pos)
            memcpy(w->pos, floor_pos, sizeof w->pos);
        to.ref = bs_items_cell(it->grid_cell, w->pos);
        w->object = -1;
    }
    /* Stacks join. */
    if (w->item.base == BS_ITEM_ARROW || w->item.base == BS_ITEM_POTION) {
        for (uint32_t k = 0; k < it->count; k++) {
            bs_world_item *o = &it->items[k];
            if (k == i || o->place.where == BS_ITEM_GONE || !same_place(o->place, to) || !stacks_with(&o->item, &w->item))
                continue;
            o->item.quantity += w->item.quantity;
            w->place.where = BS_ITEM_GONE;
            return BS_MOVE_JOINED;
        }
    }
    w->place = to;
    w->seq = it->seq++;
    w->equipped = w->secondary = false;
    return BS_MOVE_OK;
}

/* ---- equipping ---- */

const uint16_t bs_armour_places[BS_ARMOUR_PLACES] = {24, 26, 25, 27, 29, 28}; /* 0xEFD56 */

static bool is_bow(uint32_t base)
{
    return base == 8 || base == 9 || base == 10;
}

static bool live_carried(const bs_items *it, uint32_t i)
{
    return it->items[i].place.where != BS_ITEM_GONE && bs_items_place_carried(it, it->items[i].place);
}

static uint32_t find_equipped(const bs_items *it, bool (*match)(const bs_item *, int32_t), int32_t arg)
{
    for (uint32_t i = 0; i < it->count; i++)
        if (it->items[i].equipped && live_carried(it, i) && match(&it->items[i].item, arg))
            return i;
    return UINT32_MAX;
}

static bool match_base(const bs_item *x, int32_t base)
{
    return x->base == base;
}

static bool match_weapon(const bs_item *x, int32_t unused)
{
    (void)unused;
    return bs_item_is_weapon(x->base);
}

uint32_t bs_items_worn(const bs_items *it, int32_t base)
{
    return find_equipped(it, match_base, base);
}

uint32_t bs_items_weapon(const bs_items *it)
{
    return find_equipped(it, match_weapon, 0);
}

uint32_t bs_items_arrows(const bs_items *it)
{
    return find_equipped(it, match_base, BS_ITEM_ARROW);
}

uint32_t bs_items_secondary(const bs_items *it)
{
    for (uint32_t i = 0; i < it->count; i++)
        if (it->items[i].secondary && live_carried(it, i))
            return i;
    return UINT32_MAX;
}

void bs_items_unequip(bs_items *it, uint32_t i)
{
    if (i < it->count)
        it->items[i].equipped = false;
}

static bool same_part(const char *a, const char *b)
{
    for (int k = 0; k < 5; k++) {
        char x = a[k], y = b[k];
        if (x >= 'A' && x <= 'Z')
            x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z')
            y = (char)(y - 'A' + 'a');
        if (x != y)
            return false;
        if (!x)
            return true;
    }
    return true;
}

/* Puts an item into the inventory if it isn't there, checking the weight. Arrows may join a stack
 * there (0x53EDD): *i becomes that stack. */
static bs_equip carry(bs_items *it, const bs_item_db *db, uint32_t *i, int32_t strength)
{
    if (live_carried(it, *i))
        return BS_EQUIP_OK;
    bs_item item = it->items[*i].item;
    bs_item_place carried = {BS_ITEM_CARRIED, 0};
    bs_item_move m = bs_items_move(it, db, *i, carried, NULL, strength);
    if (m == BS_MOVE_JOINED) {
        for (uint32_t k = 0; k < it->count; k++)
            if (it->items[k].place.where == BS_ITEM_CARRIED && stacks_with(&it->items[k].item, &item)) {
                *i = k;
                return BS_EQUIP_OK;
            }
    }
    return m == BS_MOVE_TOO_HEAVY ? BS_EQUIP_TOO_HEAVY : m == BS_MOVE_OK ? BS_EQUIP_OK : BS_EQUIP_INVALID;
}

bs_equip bs_items_equip(bs_items *it, const bs_item_db *db, uint32_t i, const bs_equip_rules *rules,
                        int32_t strength)
{
    if (i >= it->count || it->items[i].place.where == BS_ITEM_GONE)
        return BS_EQUIP_INVALID;
    const bs_item *x = &it->items[i].item;
    if (!bs_item_may_equip(db, x, rules))
        return BS_EQUIP_NOT_ALLOWED;
    if (is_bow(x->base) && bs_items_arrows(it) == UINT32_MAX)
        return BS_EQUIP_NO_ARROWS;
    if (!bs_item_is_weapon(x->base) && x->base != BS_ITEM_CLOTHES && x->base != BS_ITEM_ARROW &&
        !bs_item_is_armour(x->base))
        return BS_EQUIP_NOT_ALLOWED;
    bs_equip e = carry(it, db, &i, strength);
    if (e != BS_EQUIP_OK)
        return e;
    x = &it->items[i].item;
    /* Off with what was worn in its place (0x527F9). */
    for (uint32_t k = 0; k < it->count; k++) {
        bs_world_item *o = &it->items[k];
        if (k == i || !o->equipped || !live_carried(it, k))
            continue;
        bool clash = bs_item_is_weapon(x->base) ? bs_item_is_weapon(o->item.base)
                                                : !bs_item_is_weapon(o->item.base) && same_part(o->item.picture, x->picture);
        if (clash)
            o->equipped = false;
    }
    it->items[i].equipped = true;
    it->items[i].secondary = false;
    return BS_EQUIP_OK;
}

bs_equip bs_items_set_secondary(bs_items *it, const bs_item_db *db, uint32_t i, const bs_equip_rules *rules,
                                int32_t strength)
{
    if (i >= it->count || it->items[i].place.where == BS_ITEM_GONE || !bs_item_is_weapon(it->items[i].item.base))
        return BS_EQUIP_INVALID;
    if (!bs_item_may_equip(db, &it->items[i].item, rules))
        return BS_EQUIP_NOT_ALLOWED;
    bs_equip e = carry(it, db, &i, strength);
    if (e != BS_EQUIP_OK)
        return e;
    for (uint32_t k = 0; k < it->count; k++)
        it->items[k].secondary = false;
    it->items[i].secondary = true;
    it->items[i].equipped = false;
    return BS_EQUIP_OK;
}

bool bs_items_check_bow(bs_items *it)
{
    uint32_t w = bs_items_weapon(it);
    if (w == UINT32_MAX || !is_bow(it->items[w].item.base) || bs_items_arrows(it) != UINT32_MAX)
        return false;
    it->items[w].equipped = false;
    return true;
}

void bs_items_armour_values(const bs_items *it, int32_t out[BS_ARMOUR_PLACES])
{
    for (int k = 0; k < BS_ARMOUR_PLACES; k++) {
        uint32_t i = bs_items_worn(it, bs_armour_places[k]);
        out[k] = i == UINT32_MAX ? 0 : bs_item_armour_value(&it->items[i].item);
    }
}
