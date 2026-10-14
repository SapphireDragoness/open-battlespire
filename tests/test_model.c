/* 3D models, BS6 levels and NODES graphs on synthetic data. */

#include <math.h>
#include <string.h>

#include "bs/core/strmap.h"
#include "bs/formats/bs6.h"
#include "bs/formats/model.h"
#include "bs/formats/nodes.h"
#include "bs_test.h"
#include "synth.h"

static uint8_t buf[4096];

static bool near(float a, float b)
{
    return fabsf(a - b) < 1e-3f;
}

static void test_texture_names(void)
{
    char name[BS_MODEL_TEX_NAME_MAX];
    BS_CHECK(bs_model_texture_name(synth_base40("WALL96"), name) && strcmp(name, "WALL96") == 0);
    BS_CHECK(bs_model_texture_name(synth_base40("BED_00"), name) && strcmp(name, "BED_00") == 0);
    BS_CHECK(bs_model_texture_name(synth_base40("BELL"), name) && strcmp(name, "BELL") == 0);
    BS_CHECK(bs_model_texture_name(synth_base40("1PLAT1"), name) && strcmp(name, "1PLAT1") == 0);
    /* Real values seen in the data. */
    BS_CHECK(bs_model_texture_name(0xB33E66C0u, name) && strcmp(name, "TERR00") == 0);
    BS_CHECK(bs_model_texture_name(0x455B6B7Fu, name) && strcmp(name, "BELL") == 0);
    /* Padding before a character, digit 36, and values past 40^6 are invalid. */
    BS_CHECK(!bs_model_texture_name(39u * 40 * 40 * 40 * 40 * 40 + 10, name));
    BS_CHECK(!bs_model_texture_name(36, name));
    BS_CHECK(!bs_model_texture_name(0xFFFF632Au, name));

    BS_CHECK(bs_model_texture_kind(synth_base40("WALL96")) == BS_MODEL_TEX_IMAGE);
    BS_CHECK(bs_model_texture_kind(0xFFFF632Au) == BS_MODEL_TEX_COLOR);
    BS_CHECK(bs_model_texture_kind(BS_MODEL_TEX_INVISIBLE) == BS_MODEL_TEX_NONE);
    BS_CHECK(bs_model_texture_kind(0xFFF00000u) == BS_MODEL_TEX_NONE);
}

static void test_model_parse(void)
{
    size_t n = synth_model(buf, sizeof buf);
    BS_CHECK(n > 0);
    bs_arena arena;
    bs_arena_init(&arena, 0);
    bs_model m;
    BS_CHECK(bs_model_parse(buf, n, &arena, &m) == BS_OK);
    BS_CHECK(strcmp(m.version, "v2.7") == 0);
    BS_CHECK_EQ_INT(m.point_count, 4);
    BS_CHECK_EQ_INT(m.face_count, 3);
    BS_CHECK_EQ_INT(m.vertex_count, 10);
    BS_CHECK_EQ_INT(m.radius, 100);
    BS_CHECK_EQ_INT(m.points[2].x, 512);
    BS_CHECK_EQ_INT(m.points[2].y, 512);
    BS_CHECK_EQ_INT(m.faces[0].vertex_count, 4);
    BS_CHECK_EQ_INT(m.faces[0].light, 6);
    BS_CHECK_EQ_INT(m.faces[0].normal[2], 256);
    BS_CHECK_EQ_INT(m.faces[1].first_vertex, 4);
    BS_CHECK_EQ_INT(m.faces[1].texture, 0xFFFFF800u);
    BS_CHECK_EQ_INT(m.vertices[7].point, 0);
    BS_CHECK_EQ_INT(m.vertices[9].point, 3);

    float uv[4][2];
    bs_model_face_uvs(&m, &m.faces[0], uv);
    BS_CHECK(near(uv[0][0], 1) && near(uv[0][1], 2));
    BS_CHECK(near(uv[1][0], 11) && near(uv[1][1], 2));
    BS_CHECK(near(uv[2][0], 11) && near(uv[2][1], 12));
    BS_CHECK(near(uv[3][0], 1) && near(uv[3][1], 12)); /* derived, not the stored 999 */
    bs_arena_free(&arena);
}

static void test_model_rejects(void)
{
    size_t n = synth_model(buf, sizeof buf);
    bs_arena arena;
    bs_arena_init(&arena, 0);
    bs_model m;
    /* Every truncation fails cleanly. */
    for (size_t len = 0; len < n - 72; len++)
        BS_CHECK(bs_model_parse(buf, len, &arena, &m) != BS_OK);
    /* Version. */
    buf[3] = '5';
    BS_CHECK(bs_model_parse(buf, n, &arena, &m) == BS_ERR_UNSUPPORTED);
    buf[3] = '7';
    /* A vertex point offset that isn't a multiple of 12, or is out of range. */
    size_t first_vertex = 64 + 48 + 10;
    buf[first_vertex] = 4;
    BS_CHECK(bs_model_parse(buf, n, &arena, &m) == BS_ERR_FORMAT);
    buf[first_vertex] = 48;
    BS_CHECK(bs_model_parse(buf, n, &arena, &m) == BS_ERR_FORMAT);
    buf[first_vertex] = 0;
    BS_CHECK(bs_model_parse(buf, n, &arena, &m) == BS_OK);
    bs_arena_free(&arena);
}

static void test_bs6_parse(void)
{
    size_t n = synth_bs6(buf, sizeof buf);
    BS_CHECK(n > 0);
    bs_arena arena;
    bs_arena_init(&arena, 0);
    bs_bs6_level l;
    BS_CHECK(bs_bs6_parse(buf, n, &arena, &l) == BS_OK);
    BS_CHECK_EQ_INT(l.model_count, 2);
    BS_CHECK(strcmp(l.model_names[0], "box") == 0 && strcmp(l.model_names[1], "rug") == 0);
    BS_CHECK_EQ_INT(l.object_count, 2);
    BS_CHECK_EQ_INT(l.objects[0].id, 7);
    BS_CHECK_EQ_INT(l.objects[0].model, 1);
    BS_CHECK_EQ_INT(l.objects[0].pos[2], 30);
    BS_CHECK_EQ_INT(l.objects[0].angles[0], 512);
    BS_CHECK_EQ_INT(l.objects[1].angles[1], 1024);
    BS_CHECK_EQ_INT(l.light_count, 1);
    BS_CHECK_EQ_INT(l.lights[0].radius, 300);
    BS_CHECK_EQ_INT(l.lights[0].brightness, 40);
    BS_CHECK_EQ_INT(l.lights[0].pos[1], -40);
    BS_CHECK_EQ_INT(l.flat_count, 1);
    BS_CHECK(strcmp(l.flats[0].name, "start") == 0);
    BS_CHECK_EQ_INT(l.flats[0].pos[0], 4);
    BS_CHECK_EQ_INT(l.view_count, 1);
    BS_CHECK_EQ_INT(l.views[0].type, 6);
    BS_CHECK_EQ_INT(l.views[0].ambient, 16000);
    BS_CHECK(l.has_bbox && l.bbox[3] == 100);
    BS_CHECK_EQ_INT(l.water, 91);

    /* The entity keeps its raw chunk: find the CTRL inside the first object. */
    bs_bs6_chunk ctrl, rawd;
    BS_CHECK(bs_bs6_find(&l.objects[0].chunk, "CTRL", &ctrl));
    BS_CHECK(bs_bs6_find(&ctrl, "RAWD", &rawd) && rawd.size == 8 && rawd.data[0] == 0xAB);
    BS_CHECK(!bs_bs6_find(&l.objects[1].chunk, "CTRL", &ctrl));
    bs_arena_free(&arena);
}

static void test_bs6_rejects(void)
{
    size_t n = synth_bs6(buf, sizeof buf);
    bs_arena arena;
    bs_arena_init(&arena, 0);
    bs_bs6_level l;
    for (size_t len = 0; len < n; len++)
        BS_CHECK(bs_bs6_parse(buf, len, &arena, &l) != BS_OK);
    /* An object referring past the model list. */
    static const uint8_t idfi[] = {'I', 'D', 'F', 'I', 4, 0, 0, 0, 1};
    size_t at = 0;
    while (at + sizeof idfi <= n && memcmp(buf + at, idfi, sizeof idfi) != 0)
        at++;
    BS_CHECK(at + sizeof idfi <= n);
    buf[at + 8] = 2;
    BS_CHECK(bs_bs6_parse(buf, n, &arena, &l) == BS_ERR_FORMAT);
    buf[at + 8] = 1;
    BS_CHECK(bs_bs6_parse(buf, n, &arena, &l) == BS_OK);
    /* A fixed-size chunk with the wrong size. */
    static const uint8_t radi[] = {'R', 'A', 'D', 'I', 4, 0, 0, 0};
    for (at = 0; at + sizeof radi <= n && memcmp(buf + at, radi, sizeof radi) != 0;)
        at++;
    BS_CHECK(at + sizeof radi <= n);
    memcpy(buf + at, "BRIT", 4); /* two BRIT chunks: still valid, last one wins */
    BS_CHECK(bs_bs6_parse(buf, n, &arena, &l) == BS_OK);
    bs_arena_free(&arena);
}

static void test_rotation(void)
{
    float m[9];
    float v[3];
    /* angles[0] = 512 (90 degrees about x): model +z becomes level -y (up). */
    bs_bs6_rotation((const int32_t[3]){512, 0, 0}, m);
    v[0] = m[2], v[1] = m[5], v[2] = m[8];
    BS_CHECK(near(v[0], 0) && near(v[1], -1) && near(v[2], 0));
    /* angles[1] = 512: rotation about y by -90 degrees, so +x goes to +z. */
    bs_bs6_rotation((const int32_t[3]){0, 512, 0}, m);
    BS_CHECK(near(m[0], 0) && near(m[3], 0) && near(m[6], 1));
    /* angles[2] = 512: about z by +90 degrees, so +x goes to +y. */
    bs_bs6_rotation((const int32_t[3]){0, 0, 512}, m);
    BS_CHECK(near(m[0], 0) && near(m[3], 1) && near(m[6], 0));
    /* Order: about y first, then x. +x -> (y by -90) +z -> (x by +90) -y. */
    bs_bs6_rotation((const int32_t[3]){512, 512, 0}, m);
    BS_CHECK(near(m[0], 0) && near(m[3], -1) && near(m[6], 0));
    /* Extreme values (found by fuzzing): INT32_MIN is a whole number of turns. */
    bs_bs6_rotation((const int32_t[3]){INT32_MIN, INT32_MIN, INT32_MAX}, m); /* = (0, 0, 2047) */
    BS_CHECK(near(m[0], 1) && near(m[8], 1));
    bs_bs6_rotation((const int32_t[3]){0, INT32_MIN, 0}, m);
    BS_CHECK(near(m[0], 1) && near(m[4], 1) && near(m[8], 1));
}

static void test_bs6_logic(void)
{
    static uint8_t lv[8192];
    size_t n = synth_bs6_logic(lv, sizeof lv);
    BS_CHECK(n > 0);
    bs_arena arena;
    bs_arena_init(&arena, 0);
    bs_bs6_level l;
    BS_CHECK_EQ_INT(bs_bs6_parse(lv, n, &arena, &l), BS_OK);
    BS_CHECK_EQ_INT(l.object_count, 8);
    bs_bs6_logic g;
    BS_CHECK_EQ_INT(bs_bs6_entity_logic(&l.objects[0].chunk, &arena, &g), BS_OK);
    BS_CHECK_EQ_INT(g.ctrl_count, 1);
    BS_CHECK_EQ_INT(g.ctrls[0].id, 16);
    BS_CHECK_EQ_INT(g.ctrls[0].size, 24);
    BS_CHECK_EQ_INT(g.prop_count, 1);
    const bs_bs6_record *snd = bs_bs6_logic_prop(&g, 121);
    BS_CHECK(snd && bs_bs6_record_i32(snd, 0) == 72 && bs_bs6_record_i32(snd, 1) == 1);
    BS_CHECK_EQ_INT(bs_bs6_record_i32(snd, 2), 0); /* past the end */
    BS_CHECK(bs_bs6_logic_prop(&g, 999) == NULL);
    BS_CHECK_EQ_INT(g.link_count, 1);
    BS_CHECK(g.links[0].type == 0 && g.links[0].target == 11);

    BS_CHECK_EQ_INT(bs_bs6_entity_logic(&l.objects[1].chunk, &arena, &g), BS_OK);
    BS_CHECK(g.ctrl_count == 1 && g.ctrls[0].id == 512 && g.ctrls[0].size == 32);
    BS_CHECK_EQ_INT(bs_bs6_record_i32(&g.ctrls[0], 1), -512);
    BS_CHECK_EQ_INT(bs_bs6_record_i32(&g.ctrls[0], 3), 1000);
    BS_CHECK(g.ctrls[0].data[16] == 'r' && g.ctrls[0].data[17] == 0);

    BS_CHECK_EQ_INT(bs_bs6_entity_logic(&l.flats[0].chunk, &arena, &g), BS_OK);
    BS_CHECK(g.ctrl_count == 1 && g.ctrls[0].id == 261 && bs_bs6_record_i32(&g.ctrls[0], 0) == 50);

    /* An entity with no logic. */
    BS_CHECK_EQ_INT(bs_bs6_entity_logic(&l.flats[1].chunk, &arena, &g), BS_OK);
    BS_CHECK(g.prop_count == 0 && g.ctrl_count == 0 && g.link_count == 0);

    /* A CTRL without RAWD is malformed. */
    static const uint8_t bad[] = {'C', 'T', 'R', 'L', 12, 0, 0, 0, 'I', 'D', 'N', 'B', 4, 0, 0, 0, 5, 0, 0, 0};
    bs_bs6_chunk ent = {"OBJD", bad, sizeof bad};
    BS_CHECK_EQ_INT(bs_bs6_entity_logic(&ent, &arena, &g), BS_ERR_FORMAT);
    bs_arena_free(&arena);
}

static void test_nodes(void)
{
    size_t n = synth_nodes(buf, sizeof buf);
    BS_CHECK(n > 0);
    bs_arena arena;
    bs_arena_init(&arena, 0);
    bs_nav_graph g;
    BS_CHECK_EQ_INT(bs_nodes_parse(buf, n, &arena, &g), BS_OK);
    BS_CHECK_EQ_INT(g.count, 3);
    BS_CHECK(near(g.nodes[0].pos[0], 1) && near(g.nodes[0].pos[1], -2) && near(g.nodes[0].pos[2], 3));
    BS_CHECK(near(g.nodes[2].pos[2], 23));
    BS_CHECK_EQ_INT(g.nodes[0].group, 7);
    BS_CHECK_EQ_INT(g.nodes[2].group, 9);
    BS_CHECK_EQ_INT(g.nodes[0].unk_14, 123);
    BS_CHECK_EQ_INT(g.nodes[0].link_count, 2);
    BS_CHECK_EQ_INT(g.nodes[0].links[1].target, 2);
    BS_CHECK_EQ_INT(g.nodes[0].links[1].distance, 20);
    BS_CHECK_EQ_INT(g.nodes[1].link_count, 0);
    BS_CHECK_EQ_INT(g.nodes[2].links[0].target, 0);

    for (size_t len = 0; len < n; len++)
        BS_CHECK(bs_nodes_parse(buf, len, &arena, &g) != BS_OK);
    /* Trailing bytes. */
    buf[n] = 0;
    BS_CHECK_EQ_INT(bs_nodes_parse(buf, n + 1, &arena, &g), BS_ERR_FORMAT);
    /* A link past the last node: node 2's link target is the file's last u16. */
    buf[n - 2] = 3;
    BS_CHECK_EQ_INT(bs_nodes_parse(buf, n, &arena, &g), BS_ERR_FORMAT);
    buf[n - 2] = 0;
    /* A record size that disagrees with its link count (node 0's size field). */
    buf[4] = 0x25 + 4;
    BS_CHECK_EQ_INT(bs_nodes_parse(buf, n, &arena, &g), BS_ERR_FORMAT);
    bs_arena_free(&arena);
}

static void test_strmap(void)
{
    bs_strmap m;
    bs_strmap_init(&m);
    static int values[1000];
    char key[32];
    for (int i = 0; i < 1000; i++) {
        snprintf(key, sizeof key, "Key%d.BSI", i);
        BS_CHECK(bs_strmap_set(&m, key, &values[i]) == BS_OK);
    }
    BS_CHECK_EQ_INT(m.count, 1000);
    void *v;
    BS_CHECK(bs_strmap_get(&m, "key500.bsi", &v) && v == &values[500]);
    BS_CHECK(bs_strmap_get(&m, "KEY999.BSI", &v) && v == &values[999]);
    BS_CHECK(!bs_strmap_get(&m, "key1000.bsi", &v));
    BS_CHECK(bs_strmap_set(&m, "KEY1.bsi", NULL) == BS_OK);
    BS_CHECK(bs_strmap_get(&m, "key1.BSI", &v) && v == NULL);
    BS_CHECK_EQ_INT(m.count, 1000);
    bs_strmap_free(&m);
}

int main(void)
{
    BS_RUN(test_texture_names);
    BS_RUN(test_model_parse);
    BS_RUN(test_model_rejects);
    BS_RUN(test_bs6_parse);
    BS_RUN(test_bs6_rejects);
    BS_RUN(test_rotation);
    BS_RUN(test_bs6_logic);
    BS_RUN(test_nodes);
    BS_RUN(test_strmap);
    return BS_TEST_RESULT();
}
