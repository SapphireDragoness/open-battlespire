#include "bs/game/level_spaces.h"

#include <stdio.h>
#include <string.h>

#include "bs/core/fs.h"
#include "bs/game/level_logic.h"

/* RE references are GAME.EXE addresses; see docs/formats/save.md, "Interiors". */

#define PROP_DOOR 100                 /* a 14-byte file name, then the arrival point (0x24817) */
#define DOOR_NAME 14
#define ID_GAP 100                    /* what each door adds to the id counter */

static uint32_t last_id(const bs_bs6_level *lv)
{
    uint32_t last = 0;
    for (uint32_t i = 0; i < lv->object_count; i++)
        last = lv->objects[i].id > last ? lv->objects[i].id : last;
    for (uint32_t i = 0; i < lv->flat_count; i++)
        last = lv->flats[i].id > last ? lv->flats[i].id : last;
    for (uint32_t i = 0; i < lv->light_count; i++)
        last = lv->lights[i].id > last ? lv->lights[i].id : last;
    return last;
}

/* The file an object's door leads to ("" for a door without a name); false if it isn't a door. */
static bool door_file(const bs_bs6_object *o, bs_arena *arena, char file[32])
{
    bs_arena_mark mark = bs_arena_get_mark(arena);
    bs_bs6_logic lg;
    const bs_bs6_record *rec = NULL;
    if (bs_bs6_entity_logic(&o->chunk, arena, &lg) == BS_OK)
        rec = bs_bs6_logic_prop(&lg, PROP_DOOR);
    bool door = rec != NULL;
    file[0] = '\0';
    if (rec) {
        size_t n = 0;
        while (n < DOOR_NAME && n < rec->size && rec->data[n])
            n++;
        if (n)
            snprintf(file, 32, "%.*s.bs6", (int)n, (const char *)rec->data);
    }
    bs_arena_rewind(arena, mark);
    return door;
}

bs_result bs_level_spaces_build(const char *file, const bs_bs6_level *level, bs_level_read_fn read, void *ctx,
                                bs_arena *arena, bs_level_spaces *out)
{
    bs_bs6_level levels[BS_LEVEL_SPACES_MAX];
    memset(out, 0, sizeof *out);
    bs_level_space *s0 = &out->spaces[0];
    snprintf(s0->file, sizeof s0->file, "%s", file);
    s0->last_id = last_id(level);
    levels[0] = *level;
    out->count = 1;
    /* The doors, level by level in load order (0x22ECC). */
    uint32_t counter = s0->last_id + ID_GAP;
    for (uint32_t s = 0; s < out->count; s++) {
        const bs_bs6_level *lv = &levels[s];
        for (uint32_t i = 0; i < lv->object_count; i++) {
            char f[32];
            if (!door_file(&lv->objects[i], arena, f))
                continue;
            bool known = !f[0];
            for (uint32_t k = 0; k < out->count && !known; k++)
                known = bs_strcasecmp(out->spaces[k].file, f) == 0;
            if (known || out->count == BS_LEVEL_SPACES_MAX || read(ctx, f, arena, &levels[out->count]) != BS_OK) {
                counter += ID_GAP;
                continue;
            }
            bs_level_space *n = &out->spaces[out->count];
            snprintf(n->file, sizeof n->file, "%s", f);
            n->door = out->spaces[s].base + lv->objects[i].id;
            n->base = counter;
            n->last_id = last_id(&levels[out->count]);
            counter += n->last_id + ID_GAP;
            out->count++;
        }
    }
    /* The controller chains: each space's in its logic's order. */
    bs_level_logic lgs[BS_LEVEL_SPACES_MAX];
    uint32_t total = 0;
    for (uint32_t s = 0; s < out->count; s++) {
        bs_result r = bs_level_logic_build(&levels[s], arena, &lgs[s]);
        if (r != BS_OK)
            return r;
        out->spaces[s].first_chain = total;
        for (uint32_t i = 0; i < lgs[s].object_count; i++)
            out->spaces[s].chain_count += lgs[s].objects[i].ctrl_count != 0;
        total += out->spaces[s].chain_count;
    }
    out->chain_lengths = BS_ARENA_ARRAY(arena, uint32_t, total + 1);
    if (!out->chain_lengths)
        return BS_ERR_NOMEM;
    for (uint32_t s = 0; s < out->count; s++)
        for (uint32_t i = 0; i < lgs[s].object_count; i++)
            if (lgs[s].objects[i].ctrl_count)
                out->chain_lengths[out->chain_count++] = lgs[s].objects[i].ctrl_count;
    return BS_OK;
}

int bs_level_spaces_by_door(const bs_level_spaces *s, uint32_t door)
{
    for (uint32_t k = 0; k < s->count; k++)
        if (s->spaces[k].door == door)
            return (int)k;
    return -1;
}

int bs_level_spaces_of_id(const bs_level_spaces *s, uint32_t id, uint32_t *local)
{
    for (uint32_t k = 0; k < s->count; k++)
        if (id >= s->spaces[k].base && id - s->spaces[k].base <= s->spaces[k].last_id) {
            *local = id - s->spaces[k].base;
            return (int)k;
        }
    return -1;
}
