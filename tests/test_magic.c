/* Spells: costs, casting, effects on the player, touch and missiles, resistances, reflection, traps, monsters
 * casting. */

#include <math.h>
#include <string.h>

#include "bs/formats/creatures.h"
#include "bs/game/magic.h"
#include "bs_test.h"
#include "synth.h"

static uint8_t exe[64 * 1024];
static bs_creature_tables tables;
static bs_anim_info anims[BS_CREATURE_COUNT][BS_CREATURE_IMAGES];
static bs_spell spells[BS_SPELL_COUNT];

#define MAXV 64
static bs_scene_vertex verts[MAXV];
static uint32_t nv;
static bs_scene_batch batch;
static bs_scene scene;
static bs_bs6_level empty_level;
static bs_arena arena;
static bs_collision_world world;

static void tri(const float a[3], const float b[3], const float c[3])
{
    const float *p[3] = {a, b, c};
    for (int i = 0; i < 3; i++) {
        memset(&verts[nv], 0, sizeof verts[nv]);
        memcpy(verts[nv++].pos, p[i], 12);
    }
}

static void quad(const float a[3], const float b[3], const float c[3], const float d[3])
{
    tri(a, b, c);
    tri(a, c, d);
}

/* A floor at y = 0 (up is -y) and a wall across z = 1000. */
static void build_world(void)
{
    nv = 0;
    static const float f0[3] = {-3000, 0, -3000}, f1[3] = {3000, 0, -3000}, f2[3] = {3000, 0, 3000},
                       f3[3] = {-3000, 0, 3000};
    quad(f0, f1, f2, f3);
    static const float w0[3] = {-3000, 0, 1000}, w1[3] = {-3000, -500, 1000}, w2[3] = {3000, -500, 1000},
                       w3[3] = {3000, 0, 1000};
    quad(w0, w1, w2, w3);
    batch = (bs_scene_batch){0, 0, nv, -1};
    scene = (bs_scene){.vertices = verts, .vertex_count = nv, .batches = &batch, .batch_count = 1,
                       .static_batch_count = 1};
    empty_level = (bs_bs6_level){0};
    bs_arena_free(&arena);
    bs_arena_init(&arena, 0);
    BS_CHECK_EQ_INT(bs_collision_build(&scene, &empty_level, &arena, &world), BS_OK);
}

static void setup(void)
{
    size_t n = synth_creature_exe(exe, sizeof exe);
    BS_CHECK_EQ_INT(bs_creature_tables_read(exe, n, &tables), BS_OK);
    for (int t = 0; t < BS_CREATURE_COUNT; t++)
        for (int s = 0; s < BS_CREATURE_IMAGES; s++)
            anims[t][s] = (bs_anim_info){8, 100};
    memset(spells, 0, sizeof spells);
    for (int i = 0; i < BS_SPELL_COUNT; i++) {
        spells[i].effect = (uint8_t)(i + 1);
        spells[i].cost = 40;
        spells[i].skill = 9;
        spells[i].target = BS_TARGET_EITHER;
    }
    /* As in GAME.EXE: Cause Damage 2-20 + 1-6 a level, for others, touch, Destruction; Cure Health the
     * same on oneself; Slow Fall (5 + level / 2 x 5) x 5 s with icon 7. */
    spells[17] = (bs_spell){.name = "Cause Damage", .effect = 18, .target = BS_TARGET_OTHER, .magnitude_min = 2,
                            .magnitude_max = 20, .increase_min = 1, .increase_max = 6, .magnitude_step = 1,
                            .delivery = BS_DELIVERY_TOUCH, .cost = 60, .skill = 8};
    spells[20] = (bs_spell){.name = "Cure Health", .effect = 21, .target = BS_TARGET_EITHER, .magnitude_min = 2,
                            .magnitude_max = 20, .increase_min = 1, .increase_max = 6, .magnitude_step = 1,
                            .cost = 40, .skill = 9};
    spells[7] = (bs_spell){.name = "Slow Fall", .effect = 8, .target = BS_TARGET_EITHER, .icon = 7,
                           .duration_base = 5, .duration_increase = 5, .duration_step = 2, .cost = 30, .skill = 11};
    spells[8] = (bs_spell){.name = "Continuous Damage", .effect = 9, .target = BS_TARGET_OTHER, .duration_base = 1,
                           .duration_increase = 1, .duration_step = 1, .magnitude_min = 10, .magnitude_max = 10,
                           .magnitude_step = 1, .cost = 80, .skill = 8};
    spells[28] = (bs_spell){.name = "Haste", .effect = 29, .duration_base = 5, .duration_increase = 1,
                            .duration_step = 1, .magnitude_min = 7, .magnitude_max = 7, .magnitude_step = 1,
                            .cost = 130, .skill = 13};
    spells[12] = (bs_spell){.name = "Delayed Damage", .effect = 13, .target = BS_TARGET_SELF, .duration_base = 10,
                            .duration_increase = 10, .duration_step = 1, .magnitude_min = 20, .magnitude_max = 20,
                            .magnitude_step = 1, .cost = 90, .skill = 8};
    spells[25] = (bs_spell){.name = "Shield", .effect = 26, .magnitude_min = 50, .magnitude_max = 50,
                            .magnitude_step = 1, .cost = 60, .skill = 11};
}

static bs_monsters ms;
static bs_monster_events mev;

static bs_monster_world monster_world(const bs_player *p, bs_rng *r, uint32_t now)
{
    return (bs_monster_world){
        .tables = &tables,
        .anims = (const bs_anim_info(*)[BS_CREATURE_IMAGES])anims,
        .collision = &world,
        .level_number = 1,
        .active_distance = 2500,
        .player = p,
        .rng = r,
        .now_ms = now,
        .spells = spells,
    };
}

static bs_magic_world magic_world(bs_character *c, const bs_player *p, const bs_monster_world *mw, bs_rng *r,
                                  uint32_t now)
{
    return (bs_magic_world){
        .character = c,
        .player = p,
        .collision = &world,
        .monsters = &ms,
        .monster_world = mw,
        .monster_events = &mev,
        .spells = spells,
        .tables = &tables,
        .level_number = 1,
        .now_ms = now,
        .rng = r,
    };
}

static bs_monster *add_monster(uint32_t type, float x, float z)
{
    bs_monster *m = bs_monsters_add(&ms, &tables, type, (const float[3]){x, -10, z}, &world);
    BS_CHECK(m != NULL);
    m->active = true;
    return m;
}

/* Runs the spells for ms_total in 20 ms frames. */
static void run(bs_magic_state *mg, bs_character *c, const bs_player *p, bs_rng *r, uint32_t *now, uint32_t ms_total,
                bs_magic_events *ev)
{
    for (uint32_t t = 0; t < ms_total; t += 20) {
        *now += 20;
        bs_monster_world mw = monster_world(p, r, *now);
        bs_magic_world w = magic_world(c, p, &mw, r, *now);
        mev.count = 0;
        bs_magic_update(mg, &w, 20, ev);
    }
}

static void test_element_percent(void)
{
    bs_rng r = {3};
    /* Resistance to fire: nothing; immune: nothing; a critical weakness: everything. */
    BS_CHECK_EQ_INT(bs_element_percent(BS_ELEMENT_FIRE, 1u << BS_FLAG_IMMUNE_FIRST, 0, 0, 0, 0, 30, &r), 0);
    BS_CHECK_EQ_INT(bs_element_percent(BS_ELEMENT_FIRE, 0, 0, bs_element_bit(BS_ELEMENT_FIRE), 0, 0, 30, &r), 0);
    BS_CHECK_EQ_INT(bs_element_percent(BS_ELEMENT_COLD, 0, 0, 0, 0, bs_element_bit(BS_ELEMENT_COLD), 95, &r), 100);
    BS_CHECK_EQ_INT(bs_element_percent(-1, 0, 0, 0, 0, 0, 95, &r), 100);
    /* Chance 30: a roll of 1-100 over 30 lets all through (70 %), under 10 nothing, between a share. */
    int all = 0, none = 0, n = 10000;
    for (int k = 0; k < n; k++) {
        int32_t p = bs_element_percent(BS_ELEMENT_SHOCK, 0, 0, 0, 0, 0, 30, &r);
        BS_CHECK(p >= 0 && p <= 100 && p % 5 == 0);
        all += p == 100;
        none += p == 0;
    }
    BS_CHECK(all > n * 68 / 100 && all < n * 73 / 100);
    BS_CHECK(none > n * 7 / 100 && none < n * 11 / 100);
}

static void test_cast_on_self(void)
{
    bs_magic_state mg;
    bs_magic_reset(&mg);
    bs_character c;
    bs_character_default(&c);
    bs_player p;
    bs_player_init(&p, (const float[3]){0, 0, 0}, 0);
    bs_rng r = {9};
    uint32_t now = 1000;
    bs_monster_world mw = monster_world(&p, &r, now);
    bs_magic_world w = magic_world(&c, &p, &mw, &r, now);
    bs_magic_events ev = {.count = 0};
    /* Cure Health on oneself: 19 points (40 x 60 / 100 x 80 %), healed, the hand moves. */
    c.health = 10;
    bs_spell heal = spells[20];
    BS_CHECK(bs_magic_cast_player(&mg, &w, &heal, &ev));
    BS_CHECK_EQ_INT(c.spell_points, 50 - 19);
    BS_CHECK(c.health >= 13 && c.health <= 36);
    BS_CHECK(ev.count == 1 && ev.list[0].kind == BS_MAGIC_MESSAGE && ev.list[0].value == 509);
    BS_CHECK(mg.hand_playing && !bs_magic_can_cast(&mg));
    mg.hand_playing = false;
    /* Too few spell points. */
    c.spell_points = 5;
    ev.count = 0;
    BS_CHECK(!bs_magic_cast_player(&mg, &w, &heal, &ev));
    BS_CHECK(ev.count == 1 && ev.list[0].value == 80 && c.spell_points == 5);
    /* Slow Fall: a flag and an icon for 25 s. */
    bs_magic_cast_on_player(&mg, &w, &spells[7], &ev);
    run(&mg, &c, &p, &r, &now, 100, &ev);
    BS_CHECK(c.flags & BS_FLAG_SLOW_FALL);
    BS_CHECK(bs_character_has(&c, BS_SPELL_SLOW_FALL, now));
    BS_CHECK_EQ_INT(c.effects[0].icon, 7);
    run(&mg, &c, &p, &r, &now, 25000, &ev);
    BS_CHECK(!(c.flags & BS_FLAG_SLOW_FALL));
    BS_CHECK_EQ_INT(c.effects[0].spell, 0);
    /* Haste adds to Speed while it lasts. */
    w.now_ms = now;
    bs_magic_cast_on_player(&mg, &w, &spells[28], &ev);
    run(&mg, &c, &p, &r, &now, 40, &ev);
    BS_CHECK_EQ_INT(bs_character_attribute(&c, 6), 57);
    /* Continuous Damage: 10 / 5 = 2 a second. */
    c.health = 40;
    w.now_ms = now;
    bs_magic_cast_on_player(&mg, &w, &spells[8], &ev);
    run(&mg, &c, &p, &r, &now, 3010, &ev);
    BS_CHECK(c.health == 34 || c.health == 36);
    /* Shield points go to the player. */
    bs_magic_cast_on_player(&mg, &w, &spells[25], &ev);
    BS_CHECK_EQ_INT(c.shield, 50);
    /* Ethereal: no casting. */
    c.flags |= BS_FLAG_ETHEREAL;
    c.spell_points = 50;
    ev.count = 0;
    BS_CHECK(!bs_magic_cast_player(&mg, &w, &heal, &ev));
    BS_CHECK(ev.count == 1 && ev.list[0].value == 507);
}

static void test_touch_and_missiles(void)
{
    build_world();
    bs_monsters_free(&ms);
    memset(&ms, 0, sizeof ms);
    bs_magic_state mg;
    bs_magic_reset(&mg);
    bs_character c;
    bs_character_default(&c);
    c.spell_points = 500;
    c.spell_points_max = 500;
    bs_player p;
    bs_player_init(&p, (const float[3]){0, 0, 0}, 0);
    bs_rng r = {4};
    uint32_t now = 1000;
    bs_monster_world mw = monster_world(&p, &r, now);
    bs_magic_world w = magic_world(&c, &p, &mw, &r, now);
    bs_magic_events ev = {.count = 0};
    /* A touch spell is made ready and goes on the monster used. */
    bs_monster *m = add_monster(0, 0, 80);
    m->health = m->health_max = 1000;
    bs_spell hurt = spells[17];
    BS_CHECK(bs_magic_cast_player(&mg, &w, &hurt, &ev));
    BS_CHECK_EQ_INT(c.spell_points, 500 - 32);
    BS_CHECK(mg.ready && !mg.hand_playing);
    BS_CHECK(!bs_magic_launch_ready(&mg, &w, &ev)); /* not a ranged one */
    int32_t total = 0;
    for (int k = 0; k < 40; k++) {
        mg.ready = false;
        BS_CHECK(bs_magic_cast_player(&mg, &w, &hurt, &ev));
        int32_t before = ms.list[0].health;
        BS_CHECK(bs_magic_touch(&mg, &w, 0, &ev));
        BS_CHECK(!mg.ready);
        total += before - ms.list[0].health;
        c.spell_points = 500;
    }
    /* 3-26 each, less when resisted. */
    BS_CHECK(total > 40 * 5 && total < 40 * 26);

    /* At range it flies along the view and bursts on the monster. */
    ms.list[0].body.pos[2] = 600;
    int32_t before = ms.list[0].health;
    hurt.delivery = BS_DELIVERY_RANGE;
    mg.ready = false;
    mg.hand_playing = false;
    BS_CHECK(bs_magic_cast_player(&mg, &w, &hurt, &ev));
    ev.count = 0;
    BS_CHECK(bs_magic_launch_ready(&mg, &w, &ev));
    BS_CHECK(mg.missiles[0].live && fabsf(mg.missiles[0].pos[1] + 50.0f) < 0.1f);
    BS_CHECK(ev.count >= 1 && ev.list[0].kind == BS_MAGIC_SOUND && ev.list[0].value == 0x194);
    uint32_t burst_sound = 0;
    for (int k = 0; k < 50 && mg.missiles[0].live; k++) {
        ev.count = 0;
        run(&mg, &c, &p, &r, &now, 20, &ev);
        for (uint32_t e = 0; e < ev.count; e++)
            burst_sound += ev.list[e].kind == BS_MAGIC_SOUND && ev.list[e].value == 0x199;
    }
    BS_CHECK(!mg.missiles[0].live);
    BS_CHECK_EQ_INT(burst_sound, 1);
    BS_CHECK(ms.list[0].body.pos[2] > 550 && ms.list[0].health <= before); /* hurt unless resisted */
    BS_CHECK(mg.bursts[0].live || mg.bursts[1].live);

    /* Organic bounces off the wall instead of bursting; Any bursts there. */
    ms.count = 0;
    hurt.detonation = BS_DETONATE_ORGANIC;
    mg = (bs_magic_state){0};
    BS_CHECK(bs_magic_cast_player(&mg, &w, &hurt, &ev));
    BS_CHECK(bs_magic_launch_ready(&mg, &w, &ev));
    run(&mg, &c, &p, &r, &now, 1300, &ev);
    BS_CHECK(mg.missiles[0].live && mg.missiles[0].dir[2] < 0);
    hurt.detonation = BS_DETONATE_ANY;
    mg = (bs_magic_state){0};
    BS_CHECK(bs_magic_cast_player(&mg, &w, &hurt, &ev));
    BS_CHECK(bs_magic_launch_ready(&mg, &w, &ev));
    run(&mg, &c, &p, &r, &now, 1300, &ev);
    BS_CHECK(!mg.missiles[0].live);
    bs_monsters_free(&ms);
}

static void test_reflection(void)
{
    build_world();
    memset(&ms, 0, sizeof ms);
    bs_magic_state mg;
    bs_magic_reset(&mg);
    bs_character c;
    bs_character_default(&c);
    c.health = c.health_max = 1000;
    bs_player p;
    bs_player_init(&p, (const float[3]){0, 0, 0}, 0);
    bs_rng r = {6};
    uint32_t now = 1000;
    bs_monster_world mw = monster_world(&p, &r, now);
    bs_magic_world w = magic_world(&c, &p, &mw, &r, now);
    bs_magic_events ev = {.count = 0};
    bs_monster *m = add_monster(0, 0, 600);
    m->flags |= BS_FLAG_REFLECT;
    int32_t pool = m->pools[BS_POOL_REFLECT];
    BS_CHECK_EQ_INT(pool, 150);
    bs_spell hurt = spells[17];
    hurt.delivery = BS_DELIVERY_RANGE;
    BS_CHECK(bs_magic_cast_player(&mg, &w, &hurt, &ev));
    BS_CHECK(bs_magic_launch_ready(&mg, &w, &ev));
    uint32_t hurt_events = 0;
    for (int k = 0; k < 150 && mg.missiles[0].live; k++) {
        ev.count = 0;
        run(&mg, &c, &p, &r, &now, 20, &ev);
        for (uint32_t e = 0; e < ev.count; e++)
            hurt_events += ev.list[e].kind == BS_MAGIC_PLAYER_HURT;
    }
    /* Back at the caster: the monster's pool paid for it, the player was hit (unless resisted). */
    BS_CHECK_EQ_INT(ms.list[0].pools[BS_POOL_REFLECT], pool - 36);
    BS_CHECK_EQ_INT(ms.list[0].health, ms.list[0].health_max);
    BS_CHECK(hurt_events <= 1 && !mg.missiles[0].live);
    bs_monsters_free(&ms);
}

static void test_trap(void)
{
    build_world();
    memset(&ms, 0, sizeof ms);
    bs_magic_state mg;
    bs_magic_reset(&mg);
    bs_character c;
    bs_character_default(&c);
    c.spell_points = 500;
    bs_player p;
    bs_player_init(&p, (const float[3]){0, 0, 0}, 0);
    bs_rng r = {8};
    uint32_t now = 1000;
    bs_monster_world mw = monster_world(&p, &r, now);
    bs_magic_world w = magic_world(&c, &p, &mw, &r, now);
    bs_magic_events ev = {.count = 0};
    BS_CHECK(bs_magic_cast_player(&mg, &w, &spells[12], &ev));
    BS_CHECK(mg.traps[0].live && fabsf(mg.traps[0].pos[1] + 40.0f) < 0.1f);
    /* The setter doesn't set it off; a monster coming near does. */
    run(&mg, &c, &p, &r, &now, 200, &ev);
    BS_CHECK(mg.traps[0].live);
    bs_monster *m = add_monster(0, 0, 300);
    m->health = m->health_max = 1000;
    run(&mg, &c, &p, &r, &now, 100, &ev);
    BS_CHECK(mg.traps[0].live);
    ms.list[0].body.pos[2] = 60;
    ev.count = 0;
    run(&mg, &c, &p, &r, &now, 20, &ev);
    BS_CHECK(!mg.traps[0].live);
    BS_CHECK(ev.count >= 1 && ev.list[0].kind == BS_MAGIC_SOUND && ev.list[0].value == 0x199);
    BS_CHECK(c.health == c.health_max); /* the area around its caster: not the caster */
    bs_monsters_free(&ms);
}

static void test_monsters_cast(void)
{
    build_world();
    memset(&ms, 0, sizeof ms);
    /* Creature 2 casts [18] or [9, 12]; it has 300 spell points. */
    bs_monster *m = add_monster(2, 0, 0);
    m->spell_points = m->spell_points_max = 300;
    m->attributes[1] = 50; /* every 500 ms a quarter of the time */
    bs_player p;
    bs_player_init(&p, (const float[3]){0, 0, 400}, 0);
    bs_rng r = {12};
    uint32_t now = 1000, casts = 0, spent = 0;
    int32_t sp = m->spell_points;
    for (uint32_t t = 0; t < 30000; t += 20) {
        now += 20;
        bs_monster_world mw = monster_world(&p, &r, now);
        bs_monster_events ev = {.count = 0};
        bs_monsters_update(&ms, &mw, 20, &ev);
        for (uint32_t k = 0; k < ev.count; k++) {
            if (ev.list[k].kind != BS_MONSTER_CAST)
                continue;
            int spell = ev.list[k].value & 0xFF, delivery = (ev.list[k].value >> 16) & 0xFF;
            BS_CHECK(spell == 18 || spell == 9 || spell == 12);
            BS_CHECK_EQ_INT(delivery, BS_DELIVERY_RANGE); /* 400 away */
            casts++;
            spent += (uint32_t)(sp - ms.list[0].spell_points);
            sp = ms.list[0].spell_points;
        }
    }
    /* Each cast costs twice (the casting and the caster); then it gives up and comes to fight. */
    BS_CHECK(casts >= 2);
    BS_CHECK(spent >= casts * 2 * 48);
    BS_CHECK_EQ_INT(ms.list[0].spell_points, 0);
    bs_monsters_free(&ms);
}

static void test_summoning(void)
{
    build_world();
    memset(&ms, 0, sizeof ms);
    bs_magic_state mg;
    bs_magic_reset(&mg);
    bs_character c;
    bs_character_default(&c);
    c.spell_points = 500;
    bs_player p;
    bs_player_init(&p, (const float[3]){0, 0, 0}, 0);
    bs_rng r = {10};
    uint32_t now = 1000;
    /* Nodes 100, 300 and 700 away: only the one at 300 is 128-512 from the player. */
    static bs_nav_node nodes[3] = {{.pos = {100, -10, 0}}, {.pos = {0, -10, 300}}, {.pos = {-700, -10, 0}}};
    bs_nav_graph graph = {nodes, 3};
    bs_monster_world mw = monster_world(&p, &r, now);
    bs_magic_world w = magic_world(&c, &p, &mw, &r, now);
    w.nodes = &graph;
    bs_magic_events ev = {.count = 0};
    spells[0] = (bs_spell){.name = "Monster Summoning", .effect = 1, .duration_base = 5, .duration_increase = 5,
                           .duration_step = 2, .cost = 50, .skill = 12};
    BS_CHECK(bs_magic_cast_player(&mg, &w, &spells[0], &ev));
    BS_CHECK_EQ_INT(ms.count, 1);
    BS_CHECK(ms.list[0].summoned && ms.list[0].ally);
    bool of_level = false;
    for (int k = 0; k < 4; k++)
        of_level |= ms.list[0].type == tables.level_creatures[1][k];
    BS_CHECK(of_level);
    BS_CHECK(fabsf(ms.list[0].body.pos[2] - 300) < 0.1f);
    /* The node is taken now: no room for another. */
    ms.list[0].active = true;
    ev.count = 0;
    mg.hand_playing = false;
    BS_CHECK(bs_magic_cast_player(&mg, &w, &spells[0], &ev));
    BS_CHECK(ms.count == 1 && ev.count == 1 && ev.list[0].value == 420);
    /* It leaves after 25 s. */
    run(&mg, &c, &p, &r, &now, 24900, &ev);
    BS_CHECK(!ms.list[0].gone);
    run(&mg, &c, &p, &r, &now, 200, &ev);
    BS_CHECK(ms.list[0].gone && mg.summon_count == 0);
    bs_monsters_free(&ms);
}

int main(void)
{
    setup();
    build_world();
    BS_RUN(test_element_percent);
    BS_RUN(test_cast_on_self);
    BS_RUN(test_touch_and_missiles);
    BS_RUN(test_reflection);
    BS_RUN(test_trap);
    BS_RUN(test_monsters_cast);
    BS_RUN(test_summoning);
    bs_arena_free(&arena);
    return BS_TEST_RESULT();
}
