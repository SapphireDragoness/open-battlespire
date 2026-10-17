/* Our saved games as text: everything written reads back the same; mistakes are reported by line. */

#include <stdlib.h>
#include <string.h>

#include "bs/game/game_save.h"
#include "bs_test.h"

static const char *item_name(void *ctx, uint32_t base)
{
    (void)ctx;
    return base == 2 ? "Long Sword" : base == 16 ? "Sack" : base == 22 ? "Arrow" : "Thing";
}

static const char *var_name(void *ctx, uint32_t hash)
{
    (void)ctx;
    return hash == bs_save_variable_hash("PCMale") ? "PCMale" : NULL;
}

static void make(bs_saved_game *g, bs_arena *a)
{
    memset(g, 0, sizeof *g);
    snprintf(g->save_name, sizeof g->save_name, "Before the # gate");
    snprintf(g->level_file, sizeof g->level_file, "l5house1.bs6");
    snprintf(g->campaign_file, sizeof g->campaign_file, "l5.bs6");
    g->other_level_count = 1;
    snprintf(g->other_levels[0], sizeof g->other_levels[0], "l5.bs6");
    g->level_number = 5;
    g->clock = 546885;
    g->captive_at = 0;
    g->ride = 4321;
    snprintf(g->name, sizeof g->name, "Aldric");
    g->female = true;
    g->race = 5;
    g->hair = 3;
    g->eyes = 7;
    g->mouth = 8;
    g->pos[0] = -1099.5f;
    g->pos[1] = -497.0f;
    g->pos[2] = 2719.25f;
    g->yaw = 94;
    g->pitch = -12;
    g->script_progress = 2;
    g->script_done = true;
    bs_character *c = &g->character;
    bs_character_default(c);
    c->level = 3;
    c->health = 40;
    c->health_max = 70;
    c->spell_points = 12;
    c->spell_points_max = 110;
    for (int k = 0; k < 8; k++)
        c->attributes[k] = 40 + k;
    for (int k = 0; k < BS_SKILL_COUNT; k++)
        c->skills[k] = 5 + k;
    c->skill_uses[3] = 19999;
    c->points = 345;
    snprintf(c->class_name, sizeof c->class_name, "Battlemage");
    c->class_skills[0] = 8;
    c->class_skills[1] = 15;
    c->magery = 512;
    c->resists = 2;
    c->double_healing = true;
    c->athleticism = true;
    c->shield = 7;
    c->has_anchor = true;
    c->anchor[0] = 1.5f;
    c->spells_known = 1u << 20 | 1u << 0;
    c->book[20] = (bs_spell){.name = "Cure Health", .effect = 21, .icon = 5, .element = 1, .delivery = 2, .detonation = 1};
    c->book[0] = (bs_spell){.name = "Fire", .effect = 1, .icon = 2};
    c->hotkeys[0] = 21;
    c->effects[0] = (bs_effect){.spell = 18, .icon = 4, .flags = 0x20, .until = 600000, .attributes = {5}, .pools = {0, 0, 9}};
    g->rules = (bs_equip_rules){4, 2, 6};

    g->item_count = 4;
    g->items = BS_ARENA_ARRAY(a, bs_saved_item, 4);
    memset(g->items, 0, sizeof *g->items * 4);
    g->items[0] = (bs_saved_item){.item = {.base = 2, .material = 5, .magic = 9259, .quantity = 1, .condition = 2391,
                                           .condition_max = 2400, .picture = "FWPNS602"},
                                  .id = 76933, .place = BS_SAVED_CARRIED, .worn = true, .level = -1};
    g->items[1] = (bs_saved_item){.item = {.base = 16, .quantity = 1, .condition = 20, .condition_max = 20},
                                  .id = 81503, .place = BS_SAVED_MONSTER, .ref = 0, .level = -1};
    g->items[2] = (bs_saved_item){.item = {.base = 22, .material = 2, .quantity = 35, .condition = 4, .condition_max = 4},
                                  .id = 81505, .place = BS_SAVED_IN_ITEM, .ref = 1, .level = -1};
    g->items[3] = (bs_saved_item){.item = {.base = 15, .number = 7, .quantity = 1, .condition = 10, .condition_max = 10},
                                  .id = 305, .place = BS_SAVED_FLOOR, .pos = {3663.0f, -2.0f, -32.0f}, .level = 0};
    g->monster_count = 1;
    g->monsters = BS_ARENA_ARRAY(a, bs_saved_monster, 1);
    g->monsters[0] = (bs_saved_monster){.id = 577, .type = 2, .pos = {-3648, 177, 1920}, .angle = 1537,
                                        .attributes = {65, 50, 70, 45, 50, 35, 50, 45}, .health = 100, .health_max = 100,
                                        .spell_points = 800, .spell_points_max = 800, .skill = 65, .flags = 0x80,
                                        .state = 13, .greeted = true};
    g->object_count = 1;
    g->objects = BS_ARENA_ARRAY(a, bs_saved_object, 1);
    g->objects[0] = (bs_saved_object){.id = 51, .kind = BS_SAVED_KIND_FLAT, .pos = {1784, -47, 3232},
                                      .angles = {0, 300, 0}, .flags = BS_SAVED_OBJECT_LOCKED, .time = 600000, .active = true};
    g->ctrl_count = 1;
    g->have_ctrls = true;
    g->ctrls = BS_ARENA_ARRAY(a, bs_saved_ctrl, 1);
    g->ctrls[0] = (bs_saved_ctrl){.object = 51, .kind = BS_SAVED_KIND_FLAT, .index = 1, .reversed = true,
                                  .running = true, .start_ms = 540000, .start = {1, -2, 3}};
    g->sigil_gone_count = 2;
    g->sigils_gone[0] = 296;
    g->sigils_gone[1] = 300;
    g->have_maps = true;
    g->maps[0][41] = 1;
    g->maps[0][0] = 1; /* a row starting with a seen cell isn't a comment */
    g->maps[3][1199] = 1;
    bs_dialogue_state_init(&g->dialogue);
    g->dialogue.global_count = 2;
    g->dialogue.globals[0] = (bs_dialogue_var){bs_save_variable_hash("PCMale"), 1};
    g->dialogue.globals[1] = (bs_dialogue_var){bs_save_variable_hash("ScampOath"), -3};
    g->dialogue.locals[0].id = 577;
    g->dialogue.locals[0].vars[0] = (bs_dialogue_var){bs_save_variable_hash("Gotgem"), 2};
    g->dialogue.visits[1] = 3;
    g->leaving[0].id = 643;
    g->leaving[0].at = 560000;
}

static void test_round_trip(void)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    static bs_saved_game g, h;
    make(&g, &a);
    bs_game_save_names names = {.item = item_name, .variable = var_name};
    char *text;
    size_t size;
    BS_CHECK_EQ_INT(bs_game_save_write(&g, &names, &text, &size), BS_OK);
    BS_CHECK(strstr(text, "base = 2  # Long Sword\n") != NULL);
    BS_CHECK(strstr(text, "name = \"Before the # gate\"\n") != NULL);
    BS_CHECK(strstr(text, "PCMale = 1\n") != NULL);
    BS_CHECK(strstr(text, "where = item 81503\n") != NULL);
    char err[BS_GAME_SAVE_ERROR];
    static bs_spell table[BS_SPELL_COUNT];
    table[20] = (bs_spell){.name = "Cure Health", .effect = 21, .skill = 9};
    table[0] = (bs_spell){.name = "Fire", .effect = 1, .skill = 8};
    BS_CHECK_EQ_INT(bs_game_save_read(text, size, table, &names, &a, &h, err), BS_OK);
    BS_CHECK(strcmp(h.save_name, g.save_name) == 0 && strcmp(h.level_file, "l5house1.bs6") == 0);
    BS_CHECK(strcmp(h.campaign_file, "l5.bs6") == 0 && h.other_level_count == 1);
    BS_CHECK_EQ_INT(h.level_number, 5);
    BS_CHECK_EQ_INT(h.clock, 546885);
    BS_CHECK(strcmp(h.name, "Aldric") == 0 && h.female && h.race == 5 && h.hair == 3 && h.mouth == 8);
    BS_CHECK(h.pos[0] == -1099.5f && h.pos[2] == 2719.25f && h.yaw == 94 && h.pitch == -12);
    BS_CHECK(h.script_progress == 2 && h.script_done);
    const bs_character *c = &h.character;
    BS_CHECK(c->level == 3 && c->health == 40 && c->health_max == 70 && c->spell_points == 12);
    BS_CHECK(c->attributes[7] == 47 && c->skills[20] == 25);
    BS_CHECK(c->skill_uses[3] == 19999 && c->skill_uses[4] == 0 && c->points == 345);
    BS_CHECK_EQ_INT(h.ride, 4321);
    BS_CHECK(strcmp(c->class_name, "Battlemage") == 0 && c->class_skills[1] == 15 && c->class_skills[2] == 0xFF);
    BS_CHECK(c->magery == 512 && c->resists == 2 && c->double_healing && c->athleticism && !c->acute_hearing);
    BS_CHECK(c->shield == 7 && c->has_anchor && c->anchor[0] == 1.5f);
    BS_CHECK_EQ_INT(c->spells_known, 1u << 20 | 1u << 0);
    BS_CHECK(c->book[20].icon == 5 && c->book[20].element == 1 && c->book[20].delivery == 2);
    BS_CHECK_EQ_INT(c->book[20].skill, 9); /* from the table */
    BS_CHECK_EQ_INT(c->hotkeys[0], 21);
    BS_CHECK(c->effects[0].spell == 18 && c->effects[0].flags == 0x20 && c->effects[0].until == 600000);
    BS_CHECK(c->effects[0].attributes[0] == 5 && c->effects[0].pools[2] == 9);
    BS_CHECK(h.rules.forbidden_weapons == 4 && h.rules.armour == 2 && h.rules.material == 6);

    BS_CHECK_EQ_INT(h.item_count, 4);
    for (uint32_t i = 0; i < 4; i++) {
        BS_CHECK(memcmp(&h.items[i].item, &g.items[i].item, sizeof(bs_item)) == 0);
        BS_CHECK_EQ_INT(h.items[i].id, g.items[i].id);
        BS_CHECK_EQ_INT(h.items[i].place, g.items[i].place);
        BS_CHECK_EQ_INT(h.items[i].ref, g.items[i].ref);
        BS_CHECK_EQ_INT(h.items[i].worn, g.items[i].worn);
        BS_CHECK_EQ_INT(h.items[i].level, g.items[i].level);
    }
    BS_CHECK(h.items[3].pos[0] == 3663.0f);
    BS_CHECK_EQ_INT(h.monster_count, 1);
    BS_CHECK(memcmp(&h.monsters[0], &g.monsters[0], sizeof(bs_saved_monster)) == 0);
    BS_CHECK(memcmp(&h.objects[0], &g.objects[0], sizeof(bs_saved_object)) == 0);
    BS_CHECK(h.have_ctrls && h.ctrl_count == 1 && memcmp(&h.ctrls[0], &g.ctrls[0], sizeof(bs_saved_ctrl)) == 0);
    BS_CHECK(h.sigil_gone_count == 2 && h.sigils_gone[1] == 300);
    BS_CHECK(h.have_maps && memcmp(h.maps, g.maps, sizeof h.maps) == 0);
    BS_CHECK_EQ_INT(h.dialogue.global_count, 2);
    BS_CHECK_EQ_INT(bs_dialogue_global(&h.dialogue, "ScampOath"), -3);
    BS_CHECK(h.dialogue.locals[0].id == 577 && h.dialogue.locals[0].vars[0].value == 2);
    BS_CHECK(h.dialogue.visits[1] == 3 && h.leaving[0].id == 643 && h.leaving[0].at == 560000);

    /* Written again, the same text. */
    char *again;
    size_t again_size;
    BS_CHECK_EQ_INT(bs_game_save_write(&h, &names, &again, &again_size), BS_OK);
    BS_CHECK(again_size == size && memcmp(again, text, size) == 0);
    free(again);
    free(text);
    bs_arena_free(&a);
}

/* A minimal save with one change, read: the error's line and words. */
static bs_result read_with(const char *extra, char err[BS_GAME_SAVE_ERROR])
{
    char text[1024];
    snprintf(text, sizeof text, "[save]\nversion = 1\nlevel_number = 1\n[player]\nname = A\n%s", extra);
    static bs_saved_game g;
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_game_save_names names = {.item = item_name};
    bs_result r = bs_game_save_read(text, strlen(text), NULL, &names, &a, &g, err);
    bs_arena_free(&a);
    return r;
}

static void test_errors(void)
{
    char err[BS_GAME_SAVE_ERROR];
    BS_CHECK_EQ_INT(read_with("", err), BS_OK);
    BS_CHECK_EQ_INT(read_with("[item]\nbase = long sword\nwhere = carried\n", err), BS_OK); /* by name */
    BS_CHECK_EQ_INT(read_with("strenght = 50\n", err), BS_ERR_FORMAT);
    BS_CHECK(strstr(err, "line 6") && strstr(err, "strenght"));
    BS_CHECK_EQ_INT(read_with("health = lots\n", err), BS_ERR_FORMAT);
    BS_CHECK(strstr(err, "line 6") && strstr(err, "lots"));
    BS_CHECK_EQ_INT(read_with("race = elf\n", err), BS_ERR_FORMAT);
    BS_CHECK_EQ_INT(read_with("[item]\nbase = 2\nwhere = item 99\n", err), BS_ERR_FORMAT);
    BS_CHECK(strstr(err, "99"));
    BS_CHECK_EQ_INT(read_with("[item]\nwhere = carried\n", err), BS_ERR_FORMAT);
    BS_CHECK(strstr(err, "no base"));
    BS_CHECK_EQ_INT(read_with("[item]\nbase = 2\nwhere = under the bed\n", err), BS_ERR_FORMAT);
    BS_CHECK_EQ_INT(read_with("[wizardry]\n", err), BS_ERR_FORMAT);
    BS_CHECK_EQ_INT(read_with("[automap]\nmap = 0\nrow = ##\n", err), BS_ERR_FORMAT);
    BS_CHECK_EQ_INT(read_with("pos = 1 2\n", err), BS_ERR_FORMAT);
    BS_CHECK_EQ_INT(read_with("this line is wrong\n", err), BS_ERR_FORMAT);
    BS_CHECK(strstr(err, "line 6"));
    BS_CHECK_EQ_INT(read_with("[save]\nversion = 99\n", err), BS_ERR_FORMAT);
    BS_CHECK(strstr(err, "newer"));
    bs_arena a;
    bs_arena_init(&a, 0);
    static bs_saved_game g;
    BS_CHECK_EQ_INT(bs_game_save_read("[save]\n[player]\n", 16, NULL, NULL, &a, &g, err), BS_ERR_FORMAT);
    BS_CHECK(strstr(err, "level_number"));
    BS_CHECK_EQ_INT(bs_game_save_read("[player]\n", 9, NULL, NULL, &a, &g, err), BS_ERR_FORMAT);
    BS_CHECK(strstr(err, "not a saved game"));
    bs_arena_free(&a);
}

int main(void)
{
    BS_RUN(test_round_trip);
    BS_RUN(test_errors);
    return BS_TEST_RESULT();
}
