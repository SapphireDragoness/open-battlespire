/* bstool save: the original game's saved games (docs/formats/save.md). */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/core/fs.h"
#include "bs/core/png.h"
#include "bs/formats/save.h"
#include "bs/formats/spells.h"
#include "commands.h"

typedef struct save_files {
    uint8_t *name, *image, *tree, *vars;
    size_t name_size, image_size, tree_size, vars_size;
} save_files;

/* A save folder: a host path, or SAVEn / n in the install. */
static void save_dir(const char *arg, char *out, size_t cap)
{
    const char *data = bstool_data_dir();
    bool slot = (arg[0] >= '0' && arg[0] <= '9' && !arg[1]) ||
                (bs_strncasecmp(arg, "save", 4) == 0 && arg[4] >= '0' && arg[4] <= '9' && !arg[5]);
    if (slot && data && !bs_fs_is_dir(arg))
        snprintf(out, cap, "%s/SAVE%c", data, arg[strlen(arg) - 1]);
    else
        snprintf(out, cap, "%s", arg);
}

/* Reads a file of the folder, its name in upper or lower case. */
static bool read_file(const char *dir, const char *name, bs_arena *a, uint8_t **data, size_t *size)
{
    char path[1024], lower[32];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (bs_fs_read_file(path, a, data, size) == BS_OK)
        return true;
    size_t i = 0;
    for (; name[i] && i + 1 < sizeof lower; i++)
        lower[i] = (char)(name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 32 : name[i]);
    lower[i] = '\0';
    snprintf(path, sizeof path, "%s/%s", dir, lower);
    return bs_fs_read_file(path, a, data, size) == BS_OK;
}

static bool load(const char *dir, bs_arena *a, save_files *f)
{
    memset(f, 0, sizeof *f);
    read_file(dir, "SAVENAME.DAT", a, &f->name, &f->name_size);
    read_file(dir, "IMAGE.RAW", a, &f->image, &f->image_size);
    bool tree = read_file(dir, "SAVETREE.DAT", a, &f->tree, &f->tree_size);
    bool vars = read_file(dir, "SAVEVARS.DAT", a, &f->vars, &f->vars_size);
    if (!tree || !vars)
        fprintf(stderr, "save: %s: no SAVETREE.DAT or SAVEVARS.DAT\n", dir);
    return tree && vars;
}

static const char *type_name(uint8_t t)
{
    switch (t) {
    case BS_SAVE_ITEM: return "item";
    case BS_SAVE_PLAYER: return "player";
    case BS_SAVE_OBJECT: return "object";
    case BS_SAVE_PROJECTILE: return "projectile";
    case BS_SAVE_SPELL: return "spell";
    case BS_SAVE_MONSTER: return "monster";
    case BS_SAVE_OPTIONS: return "options";
    case BS_SAVE_AUTOMAP: return "automap";
    case BS_SAVE_EFFECT: return "effect";
    default: return "?";
    }
}

static int depth_of(const bs_save_tree *t, uint32_t i)
{
    int d = 0;
    for (int32_t p = t->records[i].parent_index; p >= 0 && d < 16; p = t->records[p].parent_index)
        d++;
    return d;
}

/* What a record is, in a few words. */
static void describe(const bs_save_record *r, char *out, size_t cap)
{
    out[0] = '\0';
    switch (r->type) {
    case BS_SAVE_PLAYER:
    case BS_SAVE_MONSTER: {
        static bs_save_character c;
        bs_save_character_parse(r->body, r->body_size, &c);
        if (r->type == BS_SAVE_PLAYER)
            snprintf(out, cap, "\"%s\" %s, race %d, level %d, health %d/%d, sp %d/%d", c.name, c.cls.class_name,
                     c.race, c.level, (int)c.wounds, (int)c.wounds_max, (int)c.spell_points, (int)c.spell_points_max);
        else
            snprintf(out, cap, "creature %d, health %d/%d", (int)c.creature, (int)c.wounds, (int)c.wounds_max);
        break;
    }
    case BS_SAVE_ITEM: {
        bs_scl_item it;
        bs_save_item_parse(r, &it);
        snprintf(out, cap, "\"%s\" base %u material %u cond %d/%d qty %d magic %d%s%s", it.name, it.base,
                 it.material, (int)it.condition, (int)it.condition_max, (int)it.quantity, (int)it.magic,
                 (it.flags & 0x20) ? " worn" : "", it.container ? " container" : "");
        break;
    }
    case BS_SAVE_SPELL:
    case BS_SAVE_EFFECT: {
        char name[33];
        size_t n = 0;
        while (n < 32 && n < r->body_size && r->body[n])
            n++;
        memcpy(name, r->body, n);
        name[n] = '\0';
        snprintf(out, cap, "\"%s\"", name);
        break;
    }
    case BS_SAVE_OBJECT:
        snprintf(out, cap, "model %" PRIu32, r->file_id);
        break;
    case BS_SAVE_AUTOMAP: {
        uint32_t seen = 0;
        for (uint32_t k = 0; k < r->body_size && k < BS_SAVE_AUTOMAP_CELLS; k++)
            seen += r->body[k] != 0;
        snprintf(out, cap, "%ux%u, %" PRIu32 " cells seen", r->angles[0], r->angles[1], seen);
        break;
    }
    default:
        break;
    }
}

static void print_tree(const bs_save_tree *t)
{
    printf("SAVETREE.DAT: version 0x%" PRIx32 ", %" PRIu32 " records, %zu bytes of controller state\n", t->version,
           t->count, t->controls_size);
    for (uint32_t i = 0; i < t->count; i++) {
        const bs_save_record *r = &t->records[i];
        char parent[16], what[256];
        if (r->reserve)
            snprintf(parent, sizeof parent, "reserve");
        else if (r->parent == BS_SAVE_PARENT_ROOT)
            snprintf(parent, sizeof parent, "root");
        else if (r->parent == BS_SAVE_PARENT_LEVEL)
            snprintf(parent, sizeof parent, "level");
        else
            snprintf(parent, sizeof parent, "%" PRIu32 "%s", r->parent, r->parent_index < 0 ? "?" : "");
        describe(r, what, sizeof what);
        printf("%4" PRIu32 " %*s%-10s id %-8" PRIu32 " in %-8s at %8.1f %8.1f %8.1f  flags %04x links %d %d  %s\n", i,
               2 * depth_of(t, i), "", type_name(r->type), r->id, parent, (double)r->pos[0], (double)r->pos[1],
               (double)r->pos[2], r->flags, r->link, r->link2, what);
    }
}

static void print_vars(const bs_save_vars *v)
{
    printf("SAVEVARS.DAT: level %d, clock %" PRIu32 " ms, music %d, sound %d, misc %d %d %d %d\n", (int)v->level_number,
           v->clock, (int)v->music_volume, (int)v->sound_volume, (int)v->misc[0], (int)v->misc[1], (int)v->misc[2],
           (int)v->misc[3]);
    printf("  conversations %" PRIu32 ", monster overrides %" PRIu32 ", gems %" PRIu32 ", sigils %" PRIu32
           ", list 0x100984 %" PRIu32 ", globals %" PRIu32 "\n",
           v->conversation_count, v->monster_count, v->gem_count, v->sigil_count, v->list_100984_count,
           v->global_count);
    for (uint32_t i = 0; i < v->link_count; i++)
        printf("  dialogue link: id %" PRIu32 " %d\n", v->links[i].id, (int)v->links[i].link);
    for (int i = 0; i < 8; i++)
        if (v->leaving[i].id)
            printf("  leaving: id %" PRIu32 " at %" PRIu32 "\n", v->leaving[i].id, v->leaving[i].at);
    printf("  visits:");
    for (int i = 0; i < BS_SAVE_CREATURES; i++)
        printf(" %u", v->visits[i]);
    printf("\n");
    for (uint32_t i = 0; i < v->conversation_count; i++)
        printf("  conversation: id %" PRIu32 " %s\n", v->conversations[i].id, v->conversations[i].file);
    for (uint32_t i = 0; i < v->monster_count; i++)
        printf("  monster: id %" PRIu32 " creature %d health %d goal %d\n", v->monsters[i].id,
               (int)v->monsters[i].creature, (int)v->monsters[i].health, (int)v->monsters[i].goal);
    for (uint32_t i = 0; i < v->gem_count; i++)
        printf("  gem: id %" PRIu32 " %+d %s\n", v->gems[i].id, (int)v->gems[i].amount,
               v->gems[i].spell_points ? "spell points" : "health");
    for (uint32_t i = 0; i < v->sigil_count; i++)
        printf("  sigil: id %" PRIu32 " y %d\n", v->sigils[i].id, (int)v->sigils[i].y);
    static const char *const known[] = {"PCMale", "PCFemale", "SkelServe", "Journal", "QuestStart"};
    for (uint32_t i = 0; i < v->global_count; i++) {
        const char *name = "";
        for (size_t k = 0; k < sizeof known / sizeof known[0]; k++)
            if (bs_save_variable_hash(known[k]) == v->globals[i].hash)
                name = known[k];
        printf("  global: %10" PRIu32 " %-10s = %d\n", v->globals[i].hash, name, (int)v->globals[i].value);
    }
    for (int i = 0; i < BS_SAVE_LOCALS; i++) {
        if (!v->locals[i].id)
            continue;
        printf("  locals of %" PRIu32 ":", v->locals[i].id);
        for (int k = 0; k < BS_SAVE_LOCAL_VARS; k++)
            if (v->locals[i].vars[k].hash)
                printf(" %" PRIu32 "=%d", v->locals[i].vars[k].hash, (int)v->locals[i].vars[k].value);
        printf("\n");
    }
    /* The log: NUL-terminated messages, each missing its first letter. */
    for (int at = 0; at < BS_SAVE_LOG_SIZE;) {
        int len = 0;
        while (at + len < BS_SAVE_LOG_SIZE && v->log[at + len])
            len++;
        if (len)
            printf("  log @%d: %.*s\n", at, len, v->log + at);
        at += len + 1;
    }
}

static void hexdump(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i += 16) {
        printf("  %04zx:", i);
        for (size_t k = i; k < i + 16 && k < n; k++)
            printf(" %02x", p[k]);
        printf("\n");
    }
}

/* Bytes that differ, as ranges. */
static void diff_bytes(const char *what, const uint8_t *a, size_t na, const uint8_t *b, size_t nb)
{
    size_t n = na < nb ? na : nb;
    if (na != nb)
        printf("%s: sizes %zu and %zu\n", what, na, nb);
    for (size_t i = 0; i < n;) {
        if (a[i] == b[i]) {
            i++;
            continue;
        }
        size_t j = i;
        while (j < n && a[j] != b[j])
            j++;
        printf("%s: +0x%zx..0x%zx:", what, i, j - 1);
        for (size_t k = i; k < j && k < i + 12; k++)
            printf(" %02x>%02x", a[k], b[k]);
        printf("%s\n", j - i > 12 ? " ..." : "");
        i = j;
    }
}

static void diff(const bs_save_tree *ta, const save_files *fa, const bs_save_tree *tb, const save_files *fb)
{
    for (uint32_t i = 0; i < ta->count; i++) {
        const bs_save_record *r = &ta->records[i];
        int32_t j = bs_save_find(tb, r->id);
        char what[64];
        snprintf(what, sizeof what, "%s %" PRIu32, type_name(r->type), r->id);
        if (j < 0) {
            printf("%s: only in the first\n", what);
            continue;
        }
        diff_bytes(what, r->object, r->size, tb->records[j].object, tb->records[j].size);
    }
    for (uint32_t j = 0; j < tb->count; j++)
        if (bs_save_find(ta, tb->records[j].id) < 0)
            printf("%s %" PRIu32 ": only in the second\n", type_name(tb->records[j].type), tb->records[j].id);
    diff_bytes("controllers", ta->controls, ta->controls_size, tb->controls, tb->controls_size);
    diff_bytes("SAVEVARS", fa->vars, fa->vars_size, fb->vars, fb->vars_size);
}

int cmd_save(int argc, char **argv)
{
    const char *slot = NULL, *png = NULL, *other = NULL;
    bool show_vars = false;
    long hex_id = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            png = argv[++i];
        else if (strcmp(argv[i], "--vars") == 0)
            show_vars = true;
        else if (strcmp(argv[i], "--hex") == 0 && i + 1 < argc)
            hex_id = strtol(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "--diff") == 0 && i + 1 < argc)
            other = argv[++i];
        else if (!slot)
            slot = argv[i];
        else {
            fprintf(stderr, "usage: bstool save <SAVEn|n|dir> [--vars] [--hex record-id] [-o image.png] [--diff dir]\n");
            return 2;
        }
    }
    if (!slot) {
        fprintf(stderr, "usage: bstool save <SAVEn|n|dir> [--vars] [--hex record-id] [-o image.png] [--diff dir]\n");
        return 2;
    }
    char dir[1024];
    save_dir(slot, dir, sizeof dir);
    bs_arena a;
    bs_arena_init(&a, 0);
    int status = 1;
    save_files f;
    bs_save_tree t;
    static bs_save_vars v;
    if (!load(dir, &a, &f))
        goto done;
    if (bs_save_tree_parse(f.tree, f.tree_size, &a, &t) != BS_OK) {
        fprintf(stderr, "save: %s: SAVETREE.DAT is malformed\n", dir);
        goto done;
    }
    if (bs_save_vars_parse(f.vars, f.vars_size, &v) != BS_OK) {
        fprintf(stderr, "save: %s: SAVEVARS.DAT is malformed (%zu bytes)\n", dir, f.vars_size);
        goto done;
    }
    if (other) {
        char dir2[1024];
        save_dir(other, dir2, sizeof dir2);
        save_files f2;
        bs_save_tree t2;
        if (!load(dir2, &a, &f2) || bs_save_tree_parse(f2.tree, f2.tree_size, &a, &t2) != BS_OK)
            goto done;
        diff(&t, &f, &t2, &f2);
        status = 0;
        goto done;
    }
    char name[BS_SAVE_NAME_SIZE + 1] = "";
    if (f.name)
        memcpy(name, f.name, f.name_size < BS_SAVE_NAME_SIZE ? f.name_size : BS_SAVE_NAME_SIZE);
    printf("%s: \"%s\"\n", dir, name);
    print_tree(&t);
    if (show_vars)
        print_vars(&v);
    else
        printf("SAVEVARS.DAT: level %d, clock %" PRIu32 " ms (--vars for the rest)\n", (int)v.level_number, v.clock);
    if (hex_id >= 0) {
        int32_t i = bs_save_find(&t, (uint32_t)hex_id);
        if (i < 0) {
            fprintf(stderr, "save: no record %ld\n", hex_id);
        } else {
            printf("record %ld (%s), body:\n", hex_id, type_name(t.records[i].type));
            hexdump(t.records[i].body, t.records[i].body_size);
        }
    }
    if (png) {
        if (!f.image || f.image_size < BS_SAVE_IMAGE_SIZE) {
            fprintf(stderr, "save: %s: no IMAGE.RAW\n", dir);
            goto done;
        }
        static uint8_t rgb[BS_SAVE_IMAGE_WIDTH * BS_SAVE_IMAGE_HEIGHT * 3];
        bs_save_image_rgb(f.image, rgb);
        if (bs_png_write_rgb(png, BS_SAVE_IMAGE_WIDTH, BS_SAVE_IMAGE_HEIGHT, rgb) != BS_OK) {
            fprintf(stderr, "save: cannot write %s\n", png);
            goto done;
        }
    }
    status = 0;
done:
    bs_arena_free(&a);
    return status;
}
