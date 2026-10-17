/* An original save turned into the engine's pieces: the character, its spells, the items wherever they are,
 * the monsters, the objects, the automap and the variables. */

#include <string.h>

#include "bs/game/save_import.h"
#include "bs_test.h"

static void put32(uint8_t *p, uint32_t v)
{
    for (int k = 0; k < 4; k++)
        p[k] = (uint8_t)(v >> (8 * k));
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void putf(uint8_t *p, float f)
{
    uint32_t v;
    memcpy(&v, &f, sizeof v);
    put32(p, v);
}

/* A record: length, then the object (a 0x3D-byte header and the body); returns the object. */
static uint8_t *record(uint8_t *f, size_t *n, uint8_t type, uint32_t id, uint32_t parent, size_t body)
{
    uint8_t *at = f + *n;
    memset(at, 0, 4 + BS_SAVE_HEADER + body);
    put32(at, (uint32_t)(BS_SAVE_HEADER + body));
    uint8_t *o = at + 4;
    o[0] = type;
    put32(o + 0x1D, id);
    put32(o + 0x39, parent);
    put16(o + 0x25, 0xFFFF);
    put16(o + 0x27, 0xFFFF);
    *n += 4 + BS_SAVE_HEADER + body;
    return o;
}

static uint8_t *item(uint8_t *f, size_t *n, uint32_t id, uint32_t parent, uint16_t base, uint16_t material)
{
    uint8_t *o = record(f, n, BS_SAVE_ITEM, id, parent, BS_SAVE_ITEM_BODY);
    uint8_t *b = o + BS_SAVE_HEADER;
    put16(b + 0x20, base);
    put16(b + 0x33, material);
    put32(b + 0x2B, 50);
    put32(b + 0x2F, 50);
    b[0x39] = 0xFF;
    return o;
}

static void test_import(void)
{
    static uint8_t f[65536];
    size_t n = 0;
    put32(f, BS_SAVE_VERSION);
    n += 4;
    uint8_t *player = record(f, &n, BS_SAVE_PLAYER, BS_SAVE_PLAYER_ID, BS_SAVE_PARENT_ROOT, BS_SAVE_PLAYER_BODY);
    put16(player + 1, 1900);                /* looking up a little */
    put16(player + 3, 512);                 /* facing +x */
    putf(player + 7, -11.0f);
    putf(player + 11, 0.0f);
    putf(player + 15, -507.0f);
    uint8_t *pb = player + BS_SAVE_HEADER;
    memcpy(pb, "Aldric", 7);
    put32(pb + 0x20, 70);                   /* Strength with an effect */
    put32(pb + 0x40, 55);                   /* ... and its base */
    put16(pb + 0x60, 12);
    put16(pb + 0x62, 82);
    put32(pb + 0x66, 40);
    put32(pb + 0x6A, 65);
    put32(pb + 0x72 + 12 * 19, 47);         /* Missile: now, uses, base */
    put32(pb + 0x76 + 12 * 19, 30);
    put32(pb + 0x7A + 12 * 19, 47);
    put32(pb + 0x20A, 125);                 /* level-up points left */
    put32(pb + 0x176, 3);
    memcpy(pb + 0x17A, "Ranger", 7);
    pb[0x1A2] = 4;                          /* no blunt weapons */
    pb[0x1A3] = 2;
    pb[0x1A5] = 6;
    pb[0x1FE] = 21;                         /* F1: Cure Health, known */
    pb[0x1FF] = 64;                         /* F2: an item */
    pb[0x200] = 5;                          /* F3: a spell not known */
    pb[0x226] = 8;                          /* female */
    put32(pb + 0x22A, 2);                   /* the player's team */
    pb[0x23E] = 4;
    pb[0x25D] = 5;
    pb[0x25E] = 3;                          /* two cogs placed */
    put32(pb + 0x264, 76935);               /* the secondary weapon */
    uint8_t *spell = record(f, &n, BS_SAVE_SPELL, 81699, BS_SAVE_PLAYER_ID, BS_SPELL_RECORD);
    memcpy(spell + BS_SAVE_HEADER, "Cure Health", 12);
    spell[BS_SAVE_HEADER + 0x20] = 21;
    uint8_t *sword = item(f, &n, 76933, BS_SAVE_PLAYER_ID, 2, 5);
    sword[0x14] = 0x30;                     /* bought, worn */
    put32(sword + BS_SAVE_HEADER + 0x2E2, 9259);
    memcpy(sword + BS_SAVE_HEADER + 0x22, "MWPNS602", 9);
    item(f, &n, 76935, BS_SAVE_PLAYER_ID, 8, 1);  /* a short bow */
    uint8_t *sack = item(f, &n, 81000, BS_SAVE_PLAYER_ID, 16, 0);
    put16(sack + BS_SAVE_HEADER + 0x42, 1);
    uint8_t *arrows = item(f, &n, 81001, 81000, 22, 3);
    put32(arrows + BS_SAVE_HEADER + 0x3E, 35);
    item(f, &n, 81594, BS_SAVE_PLAYER_ID, 31, 9030);                 /* a Sigil Amulet */
    uint8_t *potion = item(f, &n, 305, BS_SAVE_PARENT_LEVEL, 15, 15); /* a placed potion, still there */
    potion[BS_SAVE_HEADER + 0x39] = 7;
    putf(potion + 7, 3663.0f);
    item(f, &n, 297, BS_SAVE_PARENT_LEVEL, 21, 999);                 /* a scroll */
    item(f, &n, 80850, 304, 25, 0);         /* in a chest that wasn't saved */
    uint8_t *door = record(f, &n, BS_SAVE_OBJECT, 51, BS_SAVE_PARENT_LEVEL, 0x6F);
    put16(door + 0x13, 0x0008);             /* locked */
    put16(door + 3, 300);
    putf(door + 7, 1784.0f);
    put32(door + 0x21, 600000);
    item(f, &n, 80823, 51, 36, 0);          /* a cog in a saved object */
    uint8_t *map = record(f, &n, BS_SAVE_AUTOMAP, 81814, BS_SAVE_PLAYER_ID, BS_SAVE_AUTOMAP_CELLS + 5);
    map[BS_SAVE_HEADER + 41] = 1;
    uint8_t *mon = record(f, &n, BS_SAVE_MONSTER, 577, BS_SAVE_PARENT_LEVEL, BS_SAVE_PLAYER_BODY);
    mon[0x13] = 0x41;                       /* greeted */
    put16(mon + 3, 1537);
    uint8_t *mb = mon + BS_SAVE_HEADER;
    put32(mb + 0x40 + 4 * 6, 50);           /* Speed */
    put16(mb + 0x60, 800);
    put16(mb + 0x62, 800);
    put32(mb + 0x66, 70);
    put32(mb + 0x6A, 100);
    put32(mb + 0x72, 65);
    put32(mb + 0x172, 0x80);
    put32(mb + 0x22A, 8);
    put32(mb + 0x232, 13);                  /* waits to talk */
    put32(mb + 0x24E, 2);                   /* a Dremora */
    uint8_t *loot = item(f, &n, 81503, 577, 16, 0);
    put16(loot + BS_SAVE_HEADER + 0x42, 1);
    item(f, &n, 81502, 81503, 31, 9030);
    uint8_t *ally = record(f, &n, BS_SAVE_MONSTER, 81300, BS_SAVE_PARENT_LEVEL, BS_SAVE_PLAYER_BODY);
    ally[BS_SAVE_HEADER + 0x226] = 0x10;    /* summoned ... */
    put32(ally + BS_SAVE_HEADER + 0x22A, 2); /* ... by the player */
    uint8_t *reserve = record(f, &n, BS_SAVE_MONSTER, 81470, BS_SAVE_PLAYER_ID, BS_SAVE_PLAYER_BODY);
    reserve[0x14] = 2;
    item(f, &n, 81471, 81470, 16, 0);       /* the reserve's: not kept */
    put32(f + n, 0);
    n += 4;

    static uint8_t vars[BS_SAVE_VARS_SIZE];
    memset(vars, 0, sizeof vars);
    put32(vars + 1051, 1);
    put32(vars + 1055, 546885);
    put32(vars + 14364, 296);               /* sigils: one dispelled, one not */
    put32(vars + 14368, 10000);
    put32(vars + 14376, 299);
    put32(vars + 14380, (uint32_t)-491);
    put32(vars + 14364 + 64 * 12, 2);
    put32(vars + 15136, 643);               /* a speaker leaving */
    put32(vars + 15140, 560000);
    vars[15232] = 1;
    put32(vars + 15233, 2);
    put32(vars + 15237, bs_save_variable_hash("PCFemale"));
    put32(vars + 15241, 1);
    put32(vars + 15245, 1231678197u);
    put32(vars + 15249, 1);
    put32(vars + 17285, 577);
    put32(vars + 17289, bs_save_variable_hash("Gotgem"));
    put32(vars + 17293, 2);
    vars[34693] = 1;

    bs_arena a;
    bs_arena_init(&a, 0);
    static bs_save_tree t;
    static bs_save_vars v;
    BS_CHECK_EQ_INT(bs_save_tree_parse(f, n, &a, &t), BS_OK);
    BS_CHECK_EQ_INT(bs_save_vars_parse(vars, sizeof vars, &v), BS_OK);
    static bs_spell table[BS_SPELL_COUNT];
    table[20].skill = 9;
    static bs_saved_game g;
    BS_CHECK_EQ_INT(bs_save_import(&t, &v, table, &a, &g), BS_OK);

    BS_CHECK_EQ_INT(g.level_number, 1);
    BS_CHECK_EQ_INT(g.clock, 546885);
    BS_CHECK(strcmp(g.name, "Aldric") == 0);
    BS_CHECK(g.female);
    BS_CHECK_EQ_INT(g.race, 5);
    BS_CHECK_EQ_INT(g.hair, 4);
    BS_CHECK_EQ_INT(g.yaw, 512);
    BS_CHECK_EQ_INT(g.pitch, 1900 - 2048);
    BS_CHECK(g.pos[0] == -11.0f && g.pos[2] == -507.0f);
    BS_CHECK_EQ_INT(g.script_progress, 3);
    BS_CHECK(g.script_done);
    const bs_character *c = &g.character;
    BS_CHECK_EQ_INT(c->attributes[0], 55);
    BS_CHECK_EQ_INT(c->skills[19], 47);
    BS_CHECK_EQ_INT(c->skill_uses[19], 30);
    BS_CHECK_EQ_INT(c->points, 125);
    BS_CHECK_EQ_INT(c->health, 40);
    BS_CHECK_EQ_INT(c->health_max, 65);
    BS_CHECK_EQ_INT(c->spell_points, 12);
    BS_CHECK_EQ_INT(c->spell_points_max, 82);
    BS_CHECK_EQ_INT(c->level, 3);
    BS_CHECK(strcmp(c->class_name, "Ranger") == 0);
    BS_CHECK_EQ_INT(g.rules.forbidden_weapons, 4);
    BS_CHECK_EQ_INT(g.rules.armour, 2);
    BS_CHECK_EQ_INT(g.rules.material, 6);
    BS_CHECK_EQ_INT(c->spells_known, 1u << 20);
    BS_CHECK_EQ_INT(c->book[20].skill, 9);
    BS_CHECK_EQ_INT(c->hotkeys[0], 21);
    BS_CHECK_EQ_INT(c->hotkeys[1], 0);
    BS_CHECK_EQ_INT(c->hotkeys[2], 0);

    BS_CHECK_EQ_INT(g.item_count, 12);
    const bs_saved_item *it = g.items;
    /* The sword: carried, worn, enchanted. */
    BS_CHECK_EQ_INT(it[0].place, BS_SAVED_CARRIED);
    BS_CHECK(it[0].worn && !it[0].secondary);
    BS_CHECK_EQ_INT(it[0].item.base, 2);
    BS_CHECK_EQ_INT(it[0].item.material, 5);
    BS_CHECK_EQ_INT(it[0].item.magic, 9259);
    BS_CHECK(strcmp(it[0].item.picture, "MWPNS602") == 0);
    BS_CHECK(it[1].secondary);
    BS_CHECK_EQ_INT(it[3].place, BS_SAVED_IN_ITEM);  /* arrows in the sack */
    BS_CHECK_EQ_INT(it[3].ref, 2);
    BS_CHECK_EQ_INT(it[3].item.quantity, 35);
    BS_CHECK_EQ_INT(it[4].item.number, 9030);        /* the amulet's number */
    BS_CHECK_EQ_INT(it[4].item.material, 0);
    BS_CHECK_EQ_INT(it[5].place, BS_SAVED_FLOOR);
    BS_CHECK_EQ_INT(it[5].item.number, 7);           /* the potion's type */
    BS_CHECK(it[5].pos[0] == 3663.0f);
    BS_CHECK_EQ_INT(it[6].item.number, 999);         /* the scroll's text */
    BS_CHECK_EQ_INT(it[7].place, BS_SAVED_IN_OBJECT);
    BS_CHECK_EQ_INT(it[7].ref, 304);
    BS_CHECK_EQ_INT(it[8].place, BS_SAVED_IN_OBJECT);
    BS_CHECK_EQ_INT(it[8].ref, 51);
    BS_CHECK_EQ_INT(it[9].place, BS_SAVED_MONSTER);  /* the Dremora's sack ... */
    BS_CHECK_EQ_INT(it[9].ref, 0);
    BS_CHECK_EQ_INT(it[10].place, BS_SAVED_IN_ITEM); /* ... and what's in it */
    BS_CHECK_EQ_INT(it[11].place, BS_SAVED_NOWHERE); /* the reserve's */

    BS_CHECK_EQ_INT(g.monster_count, 2);
    const bs_saved_monster *m = &g.monsters[0];
    BS_CHECK_EQ_INT(m->id, 577);
    BS_CHECK_EQ_INT(m->type, 2);
    BS_CHECK_EQ_INT(m->angle, 1537);
    BS_CHECK_EQ_INT(m->health, 70);
    BS_CHECK_EQ_INT(m->health_max, 100);
    BS_CHECK_EQ_INT(m->spell_points, 800);
    BS_CHECK_EQ_INT(m->attributes[6], 50);
    BS_CHECK_EQ_INT(m->skill, 65);
    BS_CHECK_EQ_INT(m->flags, 0x80);
    BS_CHECK_EQ_INT(m->state, 13);
    BS_CHECK(m->greeted && !m->summoned && !m->ally);
    BS_CHECK(g.monsters[1].summoned && g.monsters[1].ally);

    BS_CHECK_EQ_INT(g.object_count, 1);
    BS_CHECK_EQ_INT(g.objects[0].id, 51);
    BS_CHECK(g.objects[0].flags & BS_SAVED_OBJECT_LOCKED);
    BS_CHECK_EQ_INT(g.objects[0].angles[1], 300);
    BS_CHECK_EQ_INT(g.objects[0].time, 600000);
    BS_CHECK(g.objects[0].pos[0] == 1784.0f);

    BS_CHECK(g.have_map);
    BS_CHECK_EQ_INT(g.seen[41], 1);
    BS_CHECK_EQ_INT(g.seen[40], 0);

    BS_CHECK_EQ_INT(g.dialogue.global_count, 2);
    BS_CHECK_EQ_INT(bs_dialogue_global(&g.dialogue, "PCFemale"), 1);
    BS_CHECK_EQ_INT(g.dialogue.locals[0].id, 577);
    BS_CHECK_EQ_INT(g.dialogue.locals[0].vars[0].value, 2);
    BS_CHECK_EQ_INT(g.dialogue.visits[0], 1);
    BS_CHECK_EQ_INT(g.leaving[0].id, 643);
    BS_CHECK_EQ_INT(g.leaving[0].at, 560000);
    BS_CHECK_EQ_INT(g.sigil_gone_count, 1);
    BS_CHECK_EQ_INT(g.sigils_gone[0], 296);

    /* No player: refused. */
    size_t m2 = 4;
    static uint8_t f2[256];
    put32(f2, BS_SAVE_VERSION);
    record(f2, &m2, BS_SAVE_OPTIONS, 75539, BS_SAVE_PARENT_ROOT, 5);
    put32(f2 + m2, 0);
    m2 += 4;
    static bs_save_tree t2;
    BS_CHECK_EQ_INT(bs_save_tree_parse(f2, m2, &a, &t2), BS_OK);
    BS_CHECK_EQ_INT(bs_save_import(&t2, &v, NULL, &a, &g), BS_ERR_FORMAT);
    bs_arena_free(&a);
}

int main(void)
{
    BS_RUN(test_import);
    return BS_TEST_RESULT();
}
