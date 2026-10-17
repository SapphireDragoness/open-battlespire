/*
 * bssave: shows and edits saved games (docs/formats/save.md).
 *
 * The original game's saves (SAVE0 .. SAVE9 in its install, or any folder holding SAVETREE.DAT and
 * SAVEVARS.DAT) are changed field by field: everything not touched stays as it was, and the result is written
 * to a new folder (or over the same one with --in-place).
 *
 * OpenBattlespire's own saves are text (bs/game/game_save.h): any editor changes them, check finds mistakes,
 * and convert makes one from an original save.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/core/arena.h"
#include "bs/core/fs.h"
#include "bs/core/install.h"
#include "bs/core/vfs.h"
#include "bs/formats/bsa.h"
#include "bs/formats/creatures.h"
#include "bs/formats/items.h"
#include "bs/formats/levels.h"
#include "bs/formats/spells.h"
#include "bs/formats/save.h"
#include "bs/game/game_save.h"
#include "bs/game/items.h"
#include "bs/game/save_edit.h"
#include "bs/game/save_import.h"
#include "bs/game/use.h"

static void usage(FILE *out)
{
    fprintf(out,
            "usage: bssave [-d install-dir] <command> [args]\n"
            "\n"
            "commands:\n"
            "  show <save>                       the save's player, what it carries, and its variables\n"
            "  fields                            the player's fields that edit's set takes\n"
            "  convert <save> -o <file>          an original save as one of ours (text)\n"
            "  check <file>                      reads one of ours and says what's wrong, if anything\n"
            "  edit <save> -o <dir> [--in-place] <changes...>\n"
            "                                    changes an original save and writes it to dir\n"
            "\n"
            "<save> is a slot number 0-9 (the install's SAVE0-SAVE9) or a save folder; show also takes\n"
            "one of our save files.\n"
            "\n"
            "changes (any number, in order):\n"
            "  set KEY=VALUE...                  the player's fields: name=Aldric health=70 strength=80\n"
            "                                    long_blade=60 female=yes race=dk x=-11 ... (see fields)\n"
            "  give BASE [OPTION=VALUE...]       a new item carried, by base item name or number:\n"
            "                                    give \"Long Sword\" material=5   give Arrow quantity=40\n"
            "                                    options: material, quantity, number (a potion's type,\n"
            "                                    a scroll's text, a sigil's id), condition, magic (an\n"
            "                                    enchantment some item in the save has), into=ID (a sack),\n"
            "                                    worn=yes\n"
            "  take ID                           removes an item (with what's in it) or a monster\n"
            "  item ID KEY=VALUE...              an item's condition, condition_max, quantity, material,\n"
            "                                    number, worn=yes|no, secondary=yes|no\n"
            "  var NAME=VALUE...                 a dialogue variable (added if it isn't there)\n"
            "  rename TEXT                       the save's name on the load screen\n"
            "\n"
            "The install directory defaults to BS_DATA_DIR; give needs it (GAME.EXE's items).\n");
}

/* ---- files ---- */

typedef struct save_files {
    char dir[1024];
    uint8_t *name, *image, *tree, *vars;
    size_t name_size, image_size, tree_size, vars_size;
} save_files;

static bool read_file(const char *dir, const char *name, bs_arena *a, uint8_t **data, size_t *size)
{
    char path[1100], lower[32];
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

static void save_dir(const char *data_dir, const char *arg, char *out, size_t cap)
{
    if (arg[0] >= '0' && arg[0] <= '9' && !arg[1] && data_dir && !bs_fs_is_dir(arg))
        snprintf(out, cap, "%s/SAVE%c", data_dir, arg[0]);
    else
        snprintf(out, cap, "%s", arg);
}

static bool load(const char *data_dir, const char *arg, bs_arena *a, save_files *f)
{
    memset(f, 0, sizeof *f);
    save_dir(data_dir, arg, f->dir, sizeof f->dir);
    read_file(f->dir, "SAVENAME.DAT", a, &f->name, &f->name_size);
    read_file(f->dir, "IMAGE.RAW", a, &f->image, &f->image_size);
    if (!read_file(f->dir, "SAVETREE.DAT", a, &f->tree, &f->tree_size) ||
        !read_file(f->dir, "SAVEVARS.DAT", a, &f->vars, &f->vars_size)) {
        fprintf(stderr, "bssave: %s: no SAVETREE.DAT or SAVEVARS.DAT\n", f->dir);
        return false;
    }
    return true;
}

/* ---- the install, for items and variable names ---- */

typedef struct install {
    bool open;
    bs_install inst;
    bs_vfs vfs;
    bs_cdimage *cd;
    bs_arena arena;
    uint8_t *exe;
    size_t exe_size;
    bs_item_tables tables;
    bool have_tables;
    const char **pictures;
    uint32_t picture_count;
    /* Dialogue variable names by hash: every word of the dialogue tables. */
    uint32_t *hashes;
    char **words;
    uint32_t word_count;
    bs_creature_tables creatures;
    bool have_creatures;
    bs_spell spells[BS_SPELL_COUNT];
    bool have_spells;
    bs_level_table levels;
    bool have_levels;
} install;

static bool install_open(install *in, const char *dir)
{
    memset(in, 0, sizeof *in);
    bs_arena_init(&in->arena, 0);
    if (!dir || bs_install_detect(dir, &in->inst) != BS_OK)
        return false;
    bs_vfs_init(&in->vfs);
    if (bs_install_mount(&in->inst, &in->vfs, &in->cd) != BS_OK)
        return false;
    in->open = true;
    if (bs_vfs_read(&in->vfs, "GAME.EXE", &in->arena, &in->exe, &in->exe_size) == BS_OK) {
        in->have_tables = bs_item_tables_read(in->exe, in->exe_size, &in->tables) == BS_OK;
        in->have_creatures = bs_creature_tables_read(in->exe, in->exe_size, &in->creatures) == BS_OK;
        in->have_spells = bs_spells_read(in->exe, in->exe_size, in->spells) == BS_OK;
    }
    uint8_t *lt;
    size_t lt_size;
    if (bs_vfs_read(&in->vfs, "GAMEDATA/LEVELS.TXT", &in->arena, &lt, &lt_size) == BS_OK)
        in->have_levels = bs_levels_parse(lt, lt_size, &in->arena, &in->levels) == BS_OK;
    bs_bsa *bsa;
    if (bs_bsa_open_vfs(&in->vfs, "GAMEDATA/BSI.BSA", &bsa) == BS_OK) {
        in->pictures = BS_ARENA_ARRAY(&in->arena, const char *, bsa->count + 1);
        for (uint32_t i = 0; in->pictures && i < bsa->count; i++) {
            char name[16];
            snprintf(name, sizeof name, "%s", bsa->records[i].name);
            char *dot = strrchr(name, '.');
            if (dot)
                *dot = '\0';
            in->pictures[in->picture_count++] = bs_arena_strdup(&in->arena, name);
        }
        bs_bsa_close(bsa);
    }
    return true;
}

static void install_close(install *in)
{
    if (in->open)
        bs_cdimage_close(in->cd);
    free(in->hashes);
    for (uint32_t i = 0; i < in->word_count; i++)
        free(in->words[i]);
    free(in->words);
    bs_arena_free(&in->arena);
}

static void add_word(install *in, const char *w, size_t n, uint32_t *cap)
{
    char buf[40];
    if (n < 2 || n >= sizeof buf)
        return;
    memcpy(buf, w, n);
    buf[n] = '\0';
    uint32_t h = bs_save_variable_hash(buf);
    for (uint32_t i = 0; i < in->word_count; i++)
        if (in->hashes[i] == h)
            return;
    if (in->word_count == *cap) {
        *cap = *cap * 2 + 256;
        uint32_t *hs = realloc(in->hashes, sizeof *hs * *cap);
        if (hs)
            in->hashes = hs;
        char **ws = realloc(in->words, sizeof *ws * *cap);
        if (ws)
            in->words = ws;
        if (!hs || !ws)
            return;
    }
    char *copy = malloc(n + 1);
    if (!copy)
        return;
    memcpy(copy, buf, n + 1);
    in->hashes[in->word_count] = h;
    in->words[in->word_count++] = copy;
}

static void load_words(install *in)
{
    if (!in->open || in->word_count)
        return;
    bs_bsa *txt;
    if (bs_bsa_open_vfs(&in->vfs, "GAMEDATA/TXT.BSA", &txt) != BS_OK)
        return;
    uint32_t cap = 0;
    add_word(in, "PCMale", 6, &cap);
    add_word(in, "PCFemale", 8, &cap);
    add_word(in, "SkelServe", 9, &cap);
    bs_arena a;
    bs_arena_init(&a, 0);
    for (uint32_t r = 0; r < txt->count; r++) {
        uint8_t *d;
        size_t n;
        bs_arena_reset(&a);
        if (bs_bsa_read(txt, &txt->records[r], &a, &d, &n) != BS_OK)
            continue;
        for (size_t i = 0; i < n;) {
            bool word = (d[i] >= 'A' && d[i] <= 'Z') || (d[i] >= 'a' && d[i] <= 'z') || d[i] == '_';
            if (!word) {
                i++;
                continue;
            }
            size_t j = i;
            while (j < n && ((d[j] >= 'A' && d[j] <= 'Z') || (d[j] >= 'a' && d[j] <= 'z') || d[j] == '_' ||
                             (d[j] >= '0' && d[j] <= '9')))
                j++;
            add_word(in, (const char *)d + i, j - i, &cap);
            i = j;
        }
    }
    bs_arena_free(&a);
    bs_bsa_close(txt);
}

static const char *word_for(const install *in, uint32_t hash)
{
    for (uint32_t i = 0; i < in->word_count; i++)
        if (in->hashes[i] == hash)
            return in->words[i];
    return NULL;
}

/* Names for our files' comments, and items by name. */
static const char *item_name(void *ctx, uint32_t base)
{
    const install *in = ctx;
    return in->have_tables && base < BS_ITEM_BASE_COUNT ? in->tables.bases[base].name : NULL;
}

static const char *creature_name(void *ctx, uint32_t type)
{
    const install *in = ctx;
    return in->have_creatures && type < BS_CREATURE_COUNT ? in->creatures.creatures[type].name : NULL;
}

static const char *variable_name(void *ctx, uint32_t hash)
{
    return word_for(ctx, hash);
}

static bs_game_save_names names_of(install *in)
{
    return (bs_game_save_names){item_name, creature_name, variable_name, in};
}

/* ---- show ---- */

static void show_item(const bs_save_tree *t, uint32_t i, int depth, uint32_t weapon, uint32_t secondary)
{
    const bs_save_record *r = &t->records[i];
    bs_scl_item it;
    bs_save_item_parse(r, &it);
    printf("  %*s%-6u %s", depth * 2, "", (unsigned)r->id, it.name);
    bool numbered = it.base == BS_ITEM_SCROLL || it.base == BS_ITEM_BOOK || it.base == BS_ITEM_SIGIL_AMULET;
    printf("  (base %u", (unsigned)it.base);
    if (numbered)
        printf(", number %u", (unsigned)it.material);
    else if (it.base == BS_ITEM_POTION)
        printf(", number %d", (int)it.number);
    else if (bs_item_is_weapon(it.base) || bs_item_is_armour(it.base))
        printf(", material %u", (unsigned)it.material);
    if (it.magic)
        printf(", magic %d", (int)it.magic);
    printf(")");
    if (it.base == BS_ITEM_ARROW)
        printf(" x%d", (int)it.quantity);
    if (it.condition_max)
        printf("  condition %d/%d", (int)it.condition, (int)it.condition_max);
    if (r->id == weapon)
        printf("  in hand");
    else if (it.flags & 0x20)
        printf("  worn");
    if (r->id == secondary)
        printf("  secondary");
    printf("\n");
    for (uint32_t k = 0; k < t->count; k++)
        if (t->records[k].parent_index == (int32_t)i && t->records[k].type == BS_SAVE_ITEM)
            show_item(t, k, depth + 1, weapon, secondary);
}

/* Reads one of our files; prints what's wrong. */
static bool read_ours(install *in, const char *path, bs_arena *a, bs_saved_game *g)
{
    uint8_t *text;
    size_t size;
    if (bs_fs_read_file(path, a, &text, &size) != BS_OK) {
        fprintf(stderr, "bssave: cannot read %s\n", path);
        return false;
    }
    bs_game_save_names names = names_of(in);
    char err[BS_GAME_SAVE_ERROR];
    bs_result r = bs_game_save_read((const char *)text, size, in->have_spells ? in->spells : NULL, &names, a, g, err);
    if (r != BS_OK) {
        fprintf(stderr, "%s: %s\n", path, r == BS_ERR_FORMAT ? err : bs_result_str(r));
        return false;
    }
    return true;
}

static int show_ours(const char *data_dir, const char *path)
{
    install in;
    install_open(&in, data_dir);
    load_words(&in);
    bs_arena a;
    bs_arena_init(&a, 0);
    static bs_saved_game g;
    bool ok = read_ours(&in, path, &a, &g);
    if (ok) {
        const bs_character *c = &g.character;
        printf("%s: \"%s\", %s (level %d), %u:%02u into the game\n", path, g.save_name, g.level_file,
               g.level_number, (unsigned)(g.clock / 60000), (unsigned)(g.clock / 1000 % 60));
        printf("%s, %s, level %d: health %d/%d, spell points %d/%d\n", g.name, c->class_name, (int)c->level,
               (int)c->health, (int)c->health_max, (int)c->spell_points, (int)c->spell_points_max);
        uint32_t carried = 0, spells = 0;
        for (uint32_t i = 0; i < g.item_count; i++)
            carried += g.items[i].place == BS_SAVED_CARRIED;
        for (int k = 0; k < BS_SPELL_COUNT; k++)
            spells += (c->spells_known >> k) & 1;
        printf("%u spells, %u items (%u carried), %u monsters, %u variables\n", (unsigned)spells,
               (unsigned)g.item_count, (unsigned)carried, (unsigned)g.monster_count,
               (unsigned)g.dialogue.global_count);
    }
    install_close(&in);
    bs_arena_free(&a);
    return ok ? 0 : 1;
}

static int cmd_check(const char *data_dir, int argc, char **argv)
{
    if (argc != 1) {
        usage(stderr);
        return 2;
    }
    install in;
    install_open(&in, data_dir);
    bs_arena a;
    bs_arena_init(&a, 0);
    static bs_saved_game g;
    bool ok = read_ours(&in, argv[0], &a, &g);
    if (ok)
        printf("%s: fine\n", argv[0]);
    install_close(&in);
    bs_arena_free(&a);
    return ok ? 0 : 1;
}

static int cmd_convert(const char *data_dir, int argc, char **argv)
{
    const char *src = NULL, *out = NULL;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc)
            out = argv[++i];
        else if (!src)
            src = argv[i];
        else {
            usage(stderr);
            return 2;
        }
    }
    if (!src || !out) {
        usage(stderr);
        return 2;
    }
    bs_arena a;
    bs_arena_init(&a, 0);
    save_files f;
    if (!load(data_dir, src, &a, &f))
        return 1;
    install in;
    install_open(&in, data_dir);
    load_words(&in);
    static bs_save_tree t;
    static bs_save_vars v;
    static bs_saved_game g;
    bool ok = bs_save_tree_parse(f.tree, f.tree_size, &a, &t) == BS_OK &&
              bs_save_vars_parse(f.vars, f.vars_size, &v) == BS_OK &&
              bs_save_import(&t, &v, in.have_spells ? in.spells : NULL, &a, &g) == BS_OK;
    if (!ok) {
        fprintf(stderr, "bssave: %s: not a saved game\n", f.dir);
    } else {
        for (size_t k = 0; f.name && k < f.name_size && k < sizeof g.save_name - 1 && f.name[k]; k++)
            g.save_name[k] = (char)f.name[k];
        for (size_t i = 0; in.have_levels && i < in.levels.count && !g.level_file[0]; i++)
            if (bs_level_number(&in.levels.levels[i]) == g.level_number &&
                bs_level_game_type(&in.levels.levels[i]) == BS_GAME_CAMPAIGN)
                snprintf(g.level_file, sizeof g.level_file, "%s", in.levels.levels[i].file);
        if (!g.level_file[0])
            fprintf(stderr, "bssave: no install: the level file is left out (the game finds it by number)\n");
        bs_game_save_names names = names_of(&in);
        char *text;
        size_t size;
        ok = bs_game_save_write(&g, &names, &text, &size) == BS_OK;
        if (ok) {
            ok = bs_fs_write_file(out, text, size) == BS_OK;
            if (!ok)
                fprintf(stderr, "bssave: cannot write %s\n", out);
            else
                printf("wrote %s\n", out);
            free(text);
        }
    }
    install_close(&in);
    bs_arena_free(&a);
    return ok ? 0 : 1;
}

static int cmd_show(const char *data_dir, int argc, char **argv)
{
    if (argc != 1) {
        usage(stderr);
        return 2;
    }
    bs_arena a;
    bs_arena_init(&a, 0);
    if (bs_fs_is_file(argv[0]))
        return show_ours(data_dir, argv[0]);
    save_files f;
    if (!load(data_dir, argv[0], &a, &f))
        return 1;
    static bs_save_edit e;
    bs_save_tree t;
    static bs_save_vars v;
    if (bs_save_edit_open(&e, f.tree, f.tree_size, f.vars, f.vars_size) != BS_OK ||
        bs_save_tree_parse(f.tree, f.tree_size, &a, &t) != BS_OK || bs_save_vars_parse(f.vars, f.vars_size, &v) != BS_OK) {
        fprintf(stderr, "bssave: %s: not a saved game\n", f.dir);
        return 1;
    }
    char name[33] = "";
    for (size_t k = 0; k < f.name_size && k < 32 && f.name[k]; k++)
        name[k] = (char)f.name[k];
    printf("%s: \"%s\", level %d, %u:%02u into the game\n", f.dir, name, (int)v.level_number,
           (unsigned)(v.clock / 60000), (unsigned)(v.clock / 1000 % 60));
    printf("\n[player]\n");
    char val[64];
    for (uint32_t k = 0; bs_save_edit_field(k); k++)
        if (bs_save_edit_get(&e, bs_save_edit_field(k), val, sizeof val))
            printf("%-16s = %s\n", bs_save_edit_field(k), val);
    int32_t p = bs_save_find(&t, BS_SAVE_PLAYER_ID);
    if (p >= 0) {
        bs_save_character pc;
        bs_save_character_parse(t.records[p].body, t.records[p].body_size, &pc);
        printf("\n[spells]\n");
        for (uint32_t k = 0; k < t.count; k++)
            if (t.records[k].parent_index == p && t.records[k].type == BS_SAVE_SPELL)
                printf("  %.32s\n", (const char *)t.records[k].body);
        printf("\n[carried]  (ids for take and item)\n");
        for (uint32_t k = 0; k < t.count; k++)
            if (t.records[k].parent_index == p && t.records[k].type == BS_SAVE_ITEM)
                show_item(&t, k, 0, pc.weapon, pc.secondary);
    }
    uint32_t lying = 0, in_objects = 0, monsters = 0, held = 0;
    for (uint32_t k = 0; k < t.count; k++) {
        const bs_save_record *r = &t.records[k];
        if (r->type == BS_SAVE_MONSTER && !r->reserve)
            monsters++;
        if (r->type != BS_SAVE_ITEM)
            continue;
        if (r->parent == BS_SAVE_PARENT_LEVEL)
            lying++;
        else if (r->parent_index < 0 || t.records[r->parent_index].type == BS_SAVE_OBJECT)
            in_objects++;
        else if (t.records[r->parent_index].type == BS_SAVE_MONSTER)
            held++;
    }
    printf("\n[level]\n%u items lying about, %u in chests, %u monsters alive carrying %u sacks\n", (unsigned)lying,
           (unsigned)in_objects, (unsigned)monsters, (unsigned)held);
    install in;
    install_open(&in, data_dir);
    load_words(&in);
    printf("\n[variables]\n");
    for (uint32_t k = 0; k < v.global_count; k++) {
        const char *w = word_for(&in, v.globals[k].hash);
        if (w)
            printf("%-16s = %d\n", w, (int)v.globals[k].value);
        else
            printf("%-16u = %d\n", (unsigned)v.globals[k].hash, (int)v.globals[k].value);
    }
    install_close(&in);
    bs_save_edit_free(&e);
    bs_arena_free(&a);
    return 0;
}

static int cmd_fields(void)
{
    for (uint32_t k = 0; bs_save_edit_field(k); k++)
        printf("%s\n", bs_save_edit_field(k));
    return 0;
}

/* ---- edit ---- */

static bool is_op(const char *s)
{
    return !strcmp(s, "set") || !strcmp(s, "give") || !strcmp(s, "take") || !strcmp(s, "item") ||
           !strcmp(s, "var") || !strcmp(s, "rename");
}

/* Splits "key=value" in place. */
static bool split(char *s, char **key, char **value)
{
    char *eq = strchr(s, '=');
    if (!eq)
        return false;
    *eq = '\0';
    *key = s;
    *value = eq + 1;
    return true;
}

static bool parse_u32(const char *s, uint32_t *out)
{
    char *end;
    unsigned long v = strtoul(s, &end, 10);
    if (end == s || *end || v > 0xFFFFFFFFul)
        return false;
    *out = (uint32_t)v;
    return true;
}

static bool give(bs_save_edit *e, install *in, const char *base_arg, char **opts, int n)
{
    if (!in->open || !in->have_tables) {
        fprintf(stderr, "bssave: give needs the install (-d or BS_DATA_DIR) for GAME.EXE's items\n");
        return false;
    }
    uint32_t base = UINT32_MAX;
    if (!parse_u32(base_arg, &base) || base >= BS_ITEM_BASE_COUNT) {
        base = UINT32_MAX;
        for (uint32_t b = 0; b < BS_ITEM_BASE_COUNT && base == UINT32_MAX; b++)
            if (bs_strcasecmp(in->tables.bases[b].name, base_arg) == 0)
                base = b;
    }
    if (base == UINT32_MAX) {
        fprintf(stderr, "bssave: no base item '%s'\n", base_arg);
        return false;
    }
    const uint8_t *rec = bs_item_base_record(in->exe, in->exe_size, base);
    if (!rec)
        return false;
    bs_item it;
    memset(&it, 0, sizeof it);
    it.base = (uint16_t)base;
    it.condition = it.condition_max = in->tables.bases[base].condition;
    it.quantity = base == BS_ITEM_ARROW ? 10 : 1;
    uint32_t into = BS_SAVE_PLAYER_ID, v;
    bool worn = false;
    for (int k = 0; k < n; k++) {
        if (!strcmp(opts[k], "worn") || !strcmp(opts[k], "worn=yes")) {
            worn = true;
            continue;
        }
        char *key = NULL, *val = NULL;
        if (!split(opts[k], &key, &val) || !parse_u32(val, &v)) {
            fprintf(stderr, "bssave: give: bad option '%s'\n", opts[k]);
            return false;
        }
        if (!strcmp(key, "material"))
            it.material = (uint16_t)v;
        else if (!strcmp(key, "quantity"))
            it.quantity = (int32_t)v;
        else if (!strcmp(key, "number"))
            it.number = (int32_t)v;
        else if (!strcmp(key, "condition"))
            it.condition = it.condition_max = (int32_t)v;
        else if (!strcmp(key, "magic"))
            it.magic = (int32_t)v;
        else if (!strcmp(key, "into"))
            into = v;
        else {
            fprintf(stderr, "bssave: give: no option '%s'\n", key);
            return false;
        }
    }
    const uint8_t *magic = NULL;
    if (it.magic) {
        magic = bs_save_edit_find_magic(e, it.magic);
        if (!magic) {
            fprintf(stderr, "bssave: no item in the save has enchantment %d to copy\n", (int)it.magic);
            return false;
        }
    }
    /* The picture worn items show, for the player's sex (0x52CB4). */
    char female[8];
    bs_item_db db = {.tables = &in->tables, .pictures = in->pictures, .picture_count = in->picture_count};
    db.female = bs_save_edit_get(e, "female", female, sizeof female) && !strcmp(female, "yes");
    bs_rng rng = {(uint32_t)(base * 2654435761u + 12345u)};
    bs_item_pick_picture(&db, &it, &rng);
    uint8_t body[BS_SAVE_ITEM_BODY];
    bs_save_item_body(rec, &it, magic, body);
    uint32_t id = bs_save_edit_give(e, body, into);
    if (!id) {
        fprintf(stderr, "bssave: give: no player or container %u\n", (unsigned)into);
        return false;
    }
    printf("gave %s as %u\n", in->tables.bases[base].name, (unsigned)id);
    char err[BS_SAVE_EDIT_ERROR];
    if (worn && !bs_save_edit_item_set(e, id, "worn", "yes", err)) {
        fprintf(stderr, "bssave: give: %s\n", err);
        return false;
    }
    return true;
}

static bool write_file(const char *dir, const char *name, const void *data, size_t size)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (bs_fs_write_file(path, data, size) != BS_OK) {
        fprintf(stderr, "bssave: cannot write %s\n", path);
        return false;
    }
    return true;
}

static int cmd_edit(const char *data_dir, int argc, char **argv)
{
    const char *src = NULL, *out = NULL;
    bool in_place = false;
    int first = argc;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc)
            out = argv[++i];
        else if (!strcmp(argv[i], "--in-place"))
            in_place = true;
        else if (!src)
            src = argv[i];
        else {
            first = i;
            break;
        }
    }
    if (!src || (!out && !in_place) || first == argc || !is_op(argv[first])) {
        usage(stderr);
        return 2;
    }
    bs_arena a;
    bs_arena_init(&a, 0);
    save_files f;
    if (!load(data_dir, src, &a, &f))
        return 1;
    char out_dir[1024];
    if (in_place)
        snprintf(out_dir, sizeof out_dir, "%s", f.dir);
    else
        save_dir(data_dir, out, out_dir, sizeof out_dir);
    static bs_save_edit e;
    if (bs_save_edit_open(&e, f.tree, f.tree_size, f.vars, f.vars_size) != BS_OK) {
        fprintf(stderr, "bssave: %s: not a saved game\n", f.dir);
        return 1;
    }
    char save_name[32];
    memset(save_name, 0, sizeof save_name);
    if (f.name)
        memcpy(save_name, f.name, f.name_size < sizeof save_name ? f.name_size : sizeof save_name);
    install in;
    install_open(&in, data_dir);
    bool ok = true;
    char err[BS_SAVE_EDIT_ERROR];
    for (int i = first; ok && i < argc;) {
        const char *op = argv[i++];
        int n = 0;
        while (i + n < argc && !is_op(argv[i + n]))
            n++;
        char **args = argv + i;
        i += n;
        if (!strcmp(op, "set") || !strcmp(op, "var")) {
            for (int k = 0; ok && k < n; k++) {
                char *key = NULL, *val = NULL;
                err[0] = '\0';
                if (!split(args[k], &key, &val)) {
                    fprintf(stderr, "bssave: %s: expected KEY=VALUE, not '%s'\n", op, args[k]);
                    ok = false;
                } else if (!strcmp(op, "set")) {
                    ok = bs_save_edit_set(&e, key, val, err);
                } else {
                    char *end;
                    long v = strtol(val, &end, 10);
                    ok = end != val && !*end;
                    if (!ok)
                        snprintf(err, sizeof err, "bad value '%s'", val);
                    else
                        ok = bs_save_edit_set_var(&e, key, (int32_t)v, err);
                }
                if (!ok && err[0])
                    fprintf(stderr, "bssave: %s %s: %s\n", op, key ? key : "", err);
                err[0] = '\0';
            }
        } else if (!strcmp(op, "give")) {
            ok = n >= 1 && give(&e, &in, args[0], args + 1, n - 1);
        } else if (!strcmp(op, "take")) {
            uint32_t id;
            snprintf(err, sizeof err, "not an id: '%s'", n == 1 ? args[0] : "");
            ok = n == 1 && parse_u32(args[0], &id) && bs_save_edit_remove(&e, id, err);
            if (!ok)
                fprintf(stderr, "bssave: take: %s\n", n == 1 ? err : "one id");
        } else if (!strcmp(op, "item")) {
            uint32_t id;
            ok = n >= 2 && parse_u32(args[0], &id);
            for (int k = 1; ok && k < n; k++) {
                char *key = NULL, *val = NULL;
                snprintf(err, sizeof err, "expected KEY=VALUE, not '%s'", args[k]);
                ok = split(args[k], &key, &val) && bs_save_edit_item_set(&e, id, key, val, err);
                if (!ok)
                    fprintf(stderr, "bssave: item %s: %s\n", args[0], err);
            }
            if (n < 2)
                fprintf(stderr, "bssave: item ID KEY=VALUE...\n");
        } else if (!strcmp(op, "rename")) {
            ok = n == 1 && strlen(args[0]) < sizeof save_name;
            if (ok) {
                memset(save_name, 0, sizeof save_name);
                memcpy(save_name, args[0], strlen(args[0]));
            } else {
                fprintf(stderr, "bssave: rename: one name of at most 31 characters\n");
            }
        }
    }
    uint8_t *tree;
    size_t tree_size;
    if (ok && bs_save_edit_write(&e, &tree, &tree_size) == BS_OK) {
        if (bs_fs_mkdirs(out_dir) != BS_OK) {
            fprintf(stderr, "bssave: cannot make %s\n", out_dir);
            ok = false;
        }
        ok = ok && write_file(out_dir, "SAVENAME.DAT", save_name, sizeof save_name);
        if (ok && f.image)
            ok = write_file(out_dir, "IMAGE.RAW", f.image, f.image_size);
        ok = ok && write_file(out_dir, "SAVEVARS.DAT", e.vars, sizeof e.vars);
        ok = ok && write_file(out_dir, "SAVETREE.DAT", tree, tree_size);
        free(tree);
        if (ok)
            printf("wrote %s\n", out_dir);
    } else {
        ok = false;
    }
    install_close(&in);
    bs_save_edit_free(&e);
    bs_arena_free(&a);
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *data_dir = getenv("BS_DATA_DIR");
    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        if ((!strcmp(argv[i], "-d") || !strcmp(argv[i], "--data")) && i + 1 < argc) {
            data_dir = argv[i + 1];
            i += 2;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(stdout);
            return 0;
        } else {
            usage(stderr);
            return 2;
        }
    }
    if (data_dir && !*data_dir)
        data_dir = NULL;
    if (i >= argc) {
        usage(stderr);
        return 2;
    }
    const char *cmd = argv[i++];
    if (!strcmp(cmd, "show"))
        return cmd_show(data_dir, argc - i, argv + i);
    if (!strcmp(cmd, "fields"))
        return cmd_fields();
    if (!strcmp(cmd, "edit"))
        return cmd_edit(data_dir, argc - i, argv + i);
    if (!strcmp(cmd, "convert"))
        return cmd_convert(data_dir, argc - i, argv + i);
    if (!strcmp(cmd, "check"))
        return cmd_check(data_dir, argc - i, argv + i);
    fprintf(stderr, "bssave: no command '%s'\n\n", cmd);
    usage(stderr);
    return 2;
}
