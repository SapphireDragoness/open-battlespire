/* Monsters: the creature tables, spawning, the AI chasing and attacking, being hurt, sprites. */

#include <math.h>
#include <string.h>

#include "bs/formats/creatures.h"
#include "bs/game/monsters.h"
#include "bs_test.h"
#include "synth.h"

static uint8_t exe[64 * 1024];
static bs_creature_tables tables;
static bs_anim_info anims[BS_CREATURE_COUNT][BS_CREATURE_IMAGES];

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

/* A floor at y = 0 (up is -y); with_wall: a wall at x = 200 from z = -1000 to 300. */
static void build_world(bool with_wall)
{
    nv = 0;
    static const float f0[3] = {-3000, 0, -3000}, f1[3] = {3000, 0, -3000}, f2[3] = {3000, 0, 3000},
                       f3[3] = {-3000, 0, 3000};
    quad(f0, f1, f2, f3);
    if (with_wall) {
        static const float w0[3] = {200, 0, -1000}, w1[3] = {200, -300, -1000}, w2[3] = {200, -300, 300},
                           w3[3] = {200, 0, 300};
        quad(w0, w1, w2, w3);
    }
    batch = (bs_scene_batch){0, 0, nv, -1};
    scene = (bs_scene){.vertices = verts, .vertex_count = nv, .batches = &batch, .batch_count = 1,
                       .static_batch_count = 1};
    empty_level = (bs_bs6_level){0};
    bs_arena_free(&arena);
    bs_arena_init(&arena, 0);
    BS_CHECK_EQ_INT(bs_collision_build(&scene, &empty_level, &arena, &world), BS_OK);
}

static void setup_tables(void)
{
    size_t n = synth_creature_exe(exe, sizeof exe);
    BS_CHECK(n > 0);
    BS_CHECK_EQ_INT(bs_creature_tables_read(exe, n, &tables), BS_OK);
    for (int t = 0; t < BS_CREATURE_COUNT; t++)
        for (int s = 0; s < BS_CREATURE_IMAGES; s++)
            anims[t][s] = (bs_anim_info){s == BS_CREATURE_IMAGE_DEATH ? 4u : 8u, s == BS_CREATURE_IMAGE_DEATH ? 57u : 100u};
}

static void test_tables(void)
{
    setup_tables();
    BS_CHECK(strcmp(tables.creatures[0].name, "Scamp") == 0);
    BS_CHECK(strcmp(tables.creatures[1].name, "Vermai") == 0);
    BS_CHECK_EQ_INT(tables.creatures[3].health, 60);
    BS_CHECK_EQ_INT(tables.creatures[3].attributes[0], 23);
    BS_CHECK_EQ_INT(tables.creatures[3].attributes[6], 65);
    BS_CHECK_EQ_INT(tables.creatures[13].flags, 0x18000);
    BS_CHECK_EQ_INT(tables.creatures[0].armour, 2);
    BS_CHECK_EQ_INT(tables.creatures[0].unk_7c, 20);
    BS_CHECK_EQ_INT(tables.creatures[0].weapon.damage_min, 1);
    BS_CHECK_EQ_INT(tables.creatures[0].weapon.damage_max, 6);
    BS_CHECK_EQ_INT(tables.creatures[0].weapon.skill, 16);
    BS_CHECK(tables.creatures[2].spell_caster && !tables.creatures[3].spell_caster);
    BS_CHECK(tables.creatures[2].spells[0][0] == 18 && tables.creatures[2].spells[0][1] == 0);
    BS_CHECK(tables.creatures[2].spells[1][0] == 9 && tables.creatures[2].spells[1][1] == 12);
    BS_CHECK(tables.creatures[13].spells[0][0] == 18 && tables.creatures[13].spells[1][0] == 0);
    BS_CHECK(tables.creatures[8].immune == 2 && tables.creatures[8].critical_weakness == 4);
    BS_CHECK(strcmp(tables.creatures[0].images[0], "smpw00.bsi") == 0);
    BS_CHECK(strcmp(tables.creatures[1].images[BS_CREATURE_IMAGE_ATTACK + 4], "vera04.bsi") == 0);
    BS_CHECK(strcmp(tables.creatures[12].images[BS_CREATURE_IMAGE_DEATH], "c12d00.bsi") == 0);
    BS_CHECK_EQ_INT(tables.creatures[0].images[10][0], 0);
    BS_CHECK_EQ_INT(tables.level_creatures[2][3], 5);
    BS_CHECK_EQ_INT(tables.sounds[1][BS_CREATURE_SOUND_HURT], 406);
    /* Not there: no tables. */
    BS_CHECK_EQ_INT(bs_creature_tables_read(exe, 200, &tables), BS_ERR_NOT_FOUND);
    setup_tables();
}

static bs_bs6_level level;
static uint8_t bs6[16 * 1024];

static void load_level(bs_monsters *ms)
{
    size_t n = synth_bs6_monsters(bs6, sizeof bs6);
    BS_CHECK_EQ_INT(bs_bs6_parse(bs6, n, &arena, &level), BS_OK);
    memset(ms, 0, sizeof *ms);
    BS_CHECK_EQ_INT(bs_monsters_find_points(ms, &level), BS_OK);
}

static void test_spawning(void)
{
    setup_tables();
    build_world(false);
    bs_monsters ms;
    load_level(&ms);
    BS_CHECK_EQ_INT(ms.point_count, 6);
    BS_CHECK(ms.points[0].has_record && ms.points[0].record[0] == 3 && ms.points[0].record[4] == 77);
    BS_CHECK(!ms.points[1].has_record && !ms.points[1].code[0] && !ms.points[1].loot);
    BS_CHECK(strcmp(ms.points[2].code, "ab1") == 0);
    BS_CHECK_EQ_INT(ms.points[3].loot, 1);
    BS_CHECK(strcmp(ms.points[5].code, "DS5") == 0 && ms.points[5].record[6] == 13);

    /* None by the percentage: the records, the dialogue code and the loot still spawn. */
    bs_rng r = {77};
    BS_CHECK_EQ_INT(bs_monsters_spawn(&ms, &tables, &world, 2, 0, &r), BS_OK);
    BS_CHECK_EQ_INT(ms.count, 4);
    const bs_monster *d = &ms.list[0];
    BS_CHECK_EQ_INT(d->spawn, 0);
    BS_CHECK_EQ_INT(d->type, 2);
    BS_CHECK_EQ_INT(d->health, 77);
    BS_CHECK_EQ_INT(d->health_max, 77);
    BS_CHECK_EQ_INT(d->flags, 0x8000);
    BS_CHECK_EQ_INT(d->state, BS_AI_IDLE);
    BS_CHECK(fabsf(d->body.pos[1] + 4.0f) < 0.01f); /* 4 above the floor */
    BS_CHECK(ms.list[1].spawn == 2 && ms.list[1].type >= 2 && ms.list[1].type <= 5 && ms.list[1].has_code);
    BS_CHECK(ms.list[2].spawn == 3 && ms.list[2].type >= 2 && ms.list[2].type <= 5);
    BS_CHECK(ms.list[3].spawn == 5 && ms.list[3].type == 11 && ms.list[3].state == BS_AI_TALK);

    /* All of them: the random point too. */
    BS_CHECK_EQ_INT(bs_monsters_spawn(&ms, &tables, &world, 2, 100, &r), BS_OK);
    BS_CHECK_EQ_INT(ms.count, 5);
    BS_CHECK_EQ_INT(ms.list[1].spawn, 1);
    /* A level 5 Herne can't be hurt by weapons. */
    BS_CHECK_EQ_INT(bs_monsters_spawn(&ms, &tables, &world, 5, 0, &r), BS_OK);
    BS_CHECK(!ms.list[0].unhurt);
    bs_monsters_free(&ms);
}

static bs_monster_world make_world(const bs_player *p, bs_rng *r, uint32_t now, const bs_nav_graph *nodes)
{
    return (bs_monster_world){
        .tables = &tables,
        .anims = (const bs_anim_info(*)[BS_CREATURE_IMAGES])anims,
        .collision = &world,
        .nodes = nodes,
        .level_number = 1,
        .active_distance = 2500,
        .player = p,
        .rng = r,
        .now_ms = now,
    };
}

/* Runs the monsters for ms milliseconds in 20 ms frames, counting events of a kind. */
static uint32_t run(bs_monsters *ms, const bs_player *p, bs_rng *r, uint32_t *now, uint32_t ms_total,
                    const bs_nav_graph *nodes, bs_monster_event_kind kind)
{
    uint32_t n = 0;
    for (uint32_t t = 0; t < ms_total; t += 20) {
        *now += 20;
        bs_monster_world w = make_world(p, r, *now, nodes);
        bs_monster_events ev = {.count = 0};
        bs_monsters_update(ms, &w, 20, &ev);
        for (uint32_t k = 0; k < ev.count; k++)
            n += ev.list[k].kind == kind;
    }
    return n;
}

static float distance2d(const float a[3], const float b[3])
{
    return hypotf(a[0] - b[0], a[2] - b[2]);
}

static void test_chase_and_attack(void)
{
    setup_tables();
    build_world(false);
    bs_monsters ms;
    load_level(&ms);
    bs_rng r = {5};
    BS_CHECK_EQ_INT(bs_monsters_spawn(&ms, &tables, &world, 1, 0, &r), BS_OK);
    ms.count = 1; /* the Dremora at (0, 0, 0) alone */
    bs_player p;
    bs_player_init(&p, (const float[3]){400, 0, 0}, 0);
    uint32_t now = 1000;
    /* 270 units/s: in reach of the player within 1.5 s. */
    run(&ms, &p, &r, &now, 1500, NULL, BS_MONSTER_STRIKE);
    const bs_monster *m = &ms.list[0];
    BS_CHECK(m->aware);
    BS_CHECK(distance2d(m->body.pos, p.pos) <= 90.5f);
    BS_CHECK(distance2d(m->body.pos, p.pos) >= 40.0f); /* not into the player */
    BS_CHECK(m->state == BS_AI_ATTACK);
    BS_CHECK_EQ_INT(m->angle, 512); /* facing +x */
    /* An attack every 800 ms animation (the (100 - 65) x 5 ms pause has passed by then), 85 % of the time. */
    uint32_t strikes = run(&ms, &p, &r, &now, 10000, NULL, BS_MONSTER_STRIKE);
    BS_CHECK(strikes >= 8 && strikes <= 13);
    /* The player moves off: it follows. */
    p.pos[0] = 1200;
    run(&ms, &p, &r, &now, 4000, NULL, BS_MONSTER_STRIKE);
    BS_CHECK(distance2d(m->body.pos, p.pos) <= 90.5f);
    bs_monsters_free(&ms);
}

static void test_inactive_far_away(void)
{
    setup_tables();
    build_world(false);
    bs_monsters ms;
    load_level(&ms);
    bs_rng r = {5};
    BS_CHECK_EQ_INT(bs_monsters_spawn(&ms, &tables, &world, 1, 0, &r), BS_OK);
    ms.count = 1;
    bs_player p;
    bs_player_init(&p, (const float[3]){2800, 0, 0}, 0);
    uint32_t now = 1000;
    run(&ms, &p, &r, &now, 2000, NULL, BS_MONSTER_STRIKE);
    BS_CHECK(!ms.list[0].active);
    BS_CHECK(fabsf(ms.list[0].body.pos[0]) < 0.01f && fabsf(ms.list[0].body.pos[1]) < 0.01f);
    bs_monsters_free(&ms);
}

static void test_talker(void)
{
    setup_tables();
    build_world(false);
    bs_monsters ms;
    load_level(&ms);
    bs_rng r = {9};
    BS_CHECK_EQ_INT(bs_monsters_spawn(&ms, &tables, &world, 1, 0, &r), BS_OK);
    ms.list[0] = ms.list[3]; /* the Clannfear at (1500, 0, 0) that waits to talk */
    ms.count = 1;
    bs_player p;
    /* Looking away: no greeting yet; it turns to face the player and stays. */
    bs_player_init(&p, (const float[3]){1500, 0, 400}, 0);
    uint32_t now = 1000;
    BS_CHECK_EQ_INT(run(&ms, &p, &r, &now, 2000, NULL, BS_MONSTER_GREET), 0);
    const bs_monster *m = &ms.list[0];
    BS_CHECK_EQ_INT(m->state, BS_AI_TALK);
    BS_CHECK_EQ_INT(m->anim, BS_ANIM_STAND);
    BS_CHECK(fabsf(m->body.pos[0] - 1500) < 0.01f && fabsf(m->body.pos[2]) < 0.01f);
    BS_CHECK(m->angle == 0 || m->angle == 2047); /* towards +z */
    /* Facing it: one greeting. */
    p.yaw = 3.14159265f;
    BS_CHECK_EQ_INT(run(&ms, &p, &r, &now, 2000, NULL, BS_MONSTER_GREET), 1);
    /* Hurt, it fights. */
    bs_monster_world w = make_world(&p, &r, now, NULL);
    bs_monster_events ev = {.count = 0};
    BS_CHECK(!bs_monster_hurt(&ms, 0, &w, 5, 1, p.pos, 50, &ev));
    BS_CHECK_EQ_INT(ms.list[0].state, BS_AI_APPROACH);
    BS_CHECK(ev.count == 1 && ev.list[0].kind == BS_MONSTER_SOUND && ev.list[0].value == 400 + 4 * 11 + 2);
    bs_monsters_free(&ms);
}

static void test_hurt_and_death(void)
{
    setup_tables();
    build_world(false);
    bs_monsters ms;
    load_level(&ms);
    bs_rng r = {3};
    BS_CHECK_EQ_INT(bs_monsters_spawn(&ms, &tables, &world, 1, 0, &r), BS_OK);
    ms.count = 1;
    bs_player p;
    bs_player_init(&p, (const float[3]){-60, 0, 0}, 0);
    uint32_t now = 1000;
    run(&ms, &p, &r, &now, 100, NULL, BS_MONSTER_STRIKE);
    float x0 = ms.list[0].body.pos[0];
    bs_monster_world w = make_world(&p, &r, now, NULL);
    bs_monster_events ev = {.count = 0};
    /* 10 damage: pushed away from the player for a while, showing the death image's first frame. */
    BS_CHECK(!bs_monster_hurt(&ms, 0, &w, 10, 1, p.pos, 50, &ev));
    BS_CHECK_EQ_INT(ms.list[0].health, 67);
    BS_CHECK(ms.list[0].push_end > now);
    run(&ms, &p, &r, &now, 60, NULL, BS_MONSTER_STRIKE);
    BS_CHECK_EQ_INT(ms.list[0].anim, BS_ANIM_KNOCKED);
    BS_CHECK_EQ_INT(ms.list[0].frame, 0);
    run(&ms, &p, &r, &now, 400, NULL, BS_MONSTER_STRIKE);
    BS_CHECK(ms.list[0].body.pos[0] > x0 + 20.0f);
    /* Killed: the death image plays (4 x 57 ms), then it's gone. */
    w = make_world(&p, &r, now, NULL);
    ev.count = 0;
    BS_CHECK(bs_monster_hurt(&ms, 0, &w, 100, 1, p.pos, 50, &ev));
    BS_CHECK(ms.list[0].dead && !ms.list[0].gone);
    BS_CHECK(ev.count == 1 && ev.list[0].value == 400 + 4 * 2 + 3);
    BS_CHECK_EQ_INT(run(&ms, &p, &r, &now, 100, NULL, BS_MONSTER_GONE), 0);
    BS_CHECK_EQ_INT(ms.list[0].anim, BS_ANIM_DEATH);
    BS_CHECK_EQ_INT(run(&ms, &p, &r, &now, 300, NULL, BS_MONSTER_GONE), 1);
    BS_CHECK(ms.list[0].gone);
    BS_CHECK_EQ_INT(run(&ms, &p, &r, &now, 300, NULL, BS_MONSTER_GONE), 0);
    bs_monsters_free(&ms);
}

static void test_views(void)
{
    bs_monster m;
    memset(&m, 0, sizeof m);
    m.anim = BS_ANIM_WALK;
    m.angle = 0; /* facing +z */
    bool mirror;
    BS_CHECK_EQ_INT(bs_monster_image(&m, (const float[3]){0, -50, 300}, &mirror), 0);
    BS_CHECK(!mirror);
    BS_CHECK_EQ_INT(bs_monster_image(&m, (const float[3]){0, -50, -300}, &mirror), 4);
    BS_CHECK(!mirror);
    BS_CHECK_EQ_INT(bs_monster_image(&m, (const float[3]){-300, -50, 0}, &mirror), 2);
    BS_CHECK(!mirror);
    BS_CHECK_EQ_INT(bs_monster_image(&m, (const float[3]){300, -50, 0}, &mirror), 2);
    BS_CHECK(mirror);
    m.anim = BS_ANIM_ATTACK;
    BS_CHECK_EQ_INT(bs_monster_image(&m, (const float[3]){300, -50, 300}, &mirror), BS_CREATURE_IMAGE_ATTACK + 1);
    BS_CHECK(mirror);
    m.anim = BS_ANIM_STAND;
    BS_CHECK_EQ_INT(bs_monster_image(&m, (const float[3]){0, -50, 300}, &mirror), BS_CREATURE_IMAGE_STAND);
    m.anim = BS_ANIM_DEATH;
    BS_CHECK_EQ_INT(bs_monster_image(&m, (const float[3]){300, -50, 0}, &mirror), BS_CREATURE_IMAGE_DEATH);
    BS_CHECK(!mirror);
    BS_CHECK_EQ_INT(bs_angle_to(0, 0, 0, 10), 2047); /* the game mirrors dx <= 0: straight ahead is 2047 */
    BS_CHECK_EQ_INT(bs_angle_to(0, 0, 1, 10), 32);
    BS_CHECK_EQ_INT(bs_angle_to(0, 0, 10, 0), 512);
    BS_CHECK_EQ_INT(bs_angle_to(0, 0, -10, 0), 2047 - 512);
}

/* Round a wall over the nodes: monster at (0, 0, 0), the wall at x = 200 up to z = 300, the player
 * behind it at (400, 0, 0); nodes 0 (0, 0, 0), 1 (100, 0, 500), 2 (300, 0, 500), 3 (400, 0, 0). */
static void test_path_round_a_wall(void)
{
    setup_tables();
    build_world(true);
    bs_monsters ms;
    load_level(&ms);
    bs_rng r = {11};
    BS_CHECK_EQ_INT(bs_monsters_spawn(&ms, &tables, &world, 1, 0, &r), BS_OK);
    ms.count = 1;
    static bs_nav_link l0[1] = {{510, 1}}, l1[2] = {{510, 0}, {200, 2}}, l2[2] = {{200, 1}, {510, 3}},
                       l3[1] = {{510, 2}};
    static bs_nav_node nodes[4];
    nodes[0] = (bs_nav_node){{0, 0, 0}, 0, 0, 1, 1, l0};
    nodes[1] = (bs_nav_node){{100, 0, 500}, 0, 0, 1, 2, l1};
    nodes[2] = (bs_nav_node){{300, 0, 500}, 0, 0, 1, 2, l2};
    nodes[3] = (bs_nav_node){{400, 0, 0}, 0, 0, 1, 1, l3};
    bs_nav_graph g = {nodes, 4};
    bs_player p;
    bs_player_init(&p, (const float[3]){400, 0, 0}, 0);
    uint32_t now = 1000;
    run(&ms, &p, &r, &now, 300, &g, BS_MONSTER_STRIKE);
    BS_CHECK(!ms.list[0].aware);
    uint32_t strikes = run(&ms, &p, &r, &now, 12000, &g, BS_MONSTER_STRIKE);
    BS_CHECK(strikes > 0);
    BS_CHECK(distance2d(ms.list[0].body.pos, p.pos) <= 90.5f);
    BS_CHECK(ms.list[0].body.pos[0] > 200);
    bs_monsters_free(&ms);
}

int main(void)
{
    BS_RUN(test_tables);
    BS_RUN(test_spawning);
    BS_RUN(test_chase_and_attack);
    BS_RUN(test_inactive_far_away);
    BS_RUN(test_talker);
    BS_RUN(test_hurt_and_death);
    BS_RUN(test_views);
    BS_RUN(test_path_round_a_wall);
    bs_arena_free(&arena);
    return BS_TEST_RESULT();
}
