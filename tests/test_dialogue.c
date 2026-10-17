/* Conversations: the talk tables and how a conversation runs (bs_dialogue). */

#include <string.h>

#include "bs/formats/save.h"
#include "bs/formats/talk.h"
#include "bs/game/dialogue.h"
#include "bs_test.h"

/* A group table for a male player, modelled on the scamps'. */
static const char group_text[] =
    "\tRandom Talk Tables\t\t\r\n"
    "\ttalkcheck\t\t\r\n"
    "Scamp\t&SKNoTalk\tSK1A42\tlocal\r\n"
    "\t&ScampMad\tSK1A31\t\r\n"
    "\t\t\t\r\n"
    "\ttalktable\t\t\r\n"
    "\tFirst\tSK1A21\tglobal\r\n"
    "\tSecond\tSK1A21\t\r\n"
    "\tThird\tSK1A31\t\r\n"
    "\tFourth +\tGotoRandom\t\r\n"
    "\t\t\t\r\n"
    "\tRandom\tMale PC\t\r\n"
    "\t\tTalkGroup\tNotes\r\n"
    "\t01-50\tSK1A41\t\r\n"
    "\t51-00\tSK1A42\t\r\n";

/* Its script. */
static const char talk_text[] =
    "\t\tScamp Oathkin\t\t\t\t\t\t\t\t\t\t\t\r\n"
    "\tSaycode\tNPC SAY\tReplycode\tCHECK1\tCHECK2\tCHECK3\tPC REPLY\tGoTo\tDO THIS ACTION\tSET1\tSET2\tSET3\t\r\n"
    "\tSK1A21\tWhat? Manflesh?\tSK1A2A\tTRUE\t\t\tGive me the keys.\tSK1A91\t\t\t\t\t\r\n"
    "\tSK1A22\tWHAT!! What want?\tSK1A2B\t&PCFemale\t\t\tWhere is Kaid?\tEND\t\t\t\t\t\r\n"
    "\t\t\tSK1A2C\t&PCMale\t\t\tWhere is Trenelle?\tEND\tPUT 21_033 AND Command SK_4\t&ScampMad\t\t\t\r\n"
    "\t\t\tSK1A2D\tTRUE\tnot &Armor\t\tGoodbye.\t01-50: SK1A31 51-00: SK1A91\t\t\t\t\t\r\n"
    "\tSK1A31\tNASTY mankin! Die!\tSK1A3A\tTRUE\t\t\tNo, thank you.\tEND\tMONSTERATTACK\t\t\t\t\r\n"
    "\tSK1A41\t[[It sneers and refuses to speak with you.]]\t\t\t\t\t\t\t\t\t\t\t\r\n"
    "\tSK1A42\t\"[[It hisses and spits at you.]]\"\t\t\t\t\t\t\t\t\t\t\t\r\n"
    "\tSK1A91\tKeys in dark, NASTY place.\tSK1A9A\tTRUE\t\t\tNo, thank you.\tEND\t\t\t\t\t\r\n";

static void test_talk_file(void)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_talk_file f;
    BS_CHECK_EQ_INT(bs_talk_parse((const uint8_t *)talk_text, sizeof talk_text - 1, &a, &f), BS_OK);
    BS_CHECK_EQ_INT(f.count, 10);
    BS_CHECK(strcmp(bs_talk_cell(&f, 2, 1), "SK1A21") == 0);
    BS_CHECK(strcmp(bs_talk_cell(&f, 2, 7), "Give me the keys.") == 0);
    BS_CHECK(strcmp(bs_talk_cell(&f, 2, 15), "") == 0);
    BS_CHECK(strcmp(bs_talk_cell(&f, 99, 1), "") == 0);
    BS_CHECK_EQ_INT(bs_talk_find(&f, 0, 1, "sk1a31"), 6);
    BS_CHECK_EQ_INT(bs_talk_find(&f, 0, 1, "\"SK1A91\""), 9);
    BS_CHECK_EQ_INT(bs_talk_find(&f, 0, 1, "SK1A77"), -1);
    BS_CHECK(bs_talk_in_range("01-10", 1) && bs_talk_in_range("01-10", 10) && !bs_talk_in_range("01-10", 11));
    BS_CHECK(bs_talk_in_range("91-00", 100) && !bs_talk_in_range("91-00", 90));
    /* 0x92 is an apostrophe. */
    static const uint8_t quote[] = {'\t', 'I', 0x92, 'm', '\r', '\n'};
    BS_CHECK_EQ_INT(bs_talk_parse(quote, sizeof quote, &a, &f), BS_OK);
    BS_CHECK(strcmp(bs_talk_cell(&f, 0, 1), "I'm") == 0);
    bs_arena_free(&a);
}

static bool carries(void *ctx, int32_t base, int32_t number)
{
    const int32_t *have = ctx;
    for (int i = 0; have[i] >= 0; i += 2)
        if (have[i] == base && (number < 0 || have[i + 1] == number))
            return true;
    return false;
}

static void test_conversation(void)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_talk_file group, talk;
    BS_CHECK_EQ_INT(bs_talk_parse((const uint8_t *)group_text, sizeof group_text - 1, &a, &group), BS_OK);
    BS_CHECK_EQ_INT(bs_talk_parse((const uint8_t *)talk_text, sizeof talk_text - 1, &a, &talk), BS_OK);
    static bs_dialogue_state s;
    bs_dialogue_state_init(&s);
    const int32_t nothing[] = {-1};
    bs_dialogue_update_vars(&s, false, carries, (void *)nothing);
    BS_CHECK_EQ_INT(bs_dialogue_global(&s, "PCMale"), 1);
    BS_CHECK_EQ_INT(bs_dialogue_global(&s, "PCFemale"), 0);
    bs_rng r = {12345};
    static bs_dialogue d;
    char msg[128];

    /* The first visit: the talkcheck variables are the speaker's own, unset, so the talktable's first row. */
    BS_CHECK_EQ_INT(bs_dialogue_start(&d, &s, &group, &talk, 577, 0, &r, msg, sizeof msg), BS_DIALOGUE_TALK);
    BS_CHECK(strcmp(bs_dialogue_say_code(&d), "SK1A21") == 0);
    BS_CHECK_EQ_INT(s.visits[0], 1);
    BS_CHECK_EQ_INT(s.locals[BS_DIALOGUE_LOCALS - 1].id, 577); /* the last free record */
    BS_CHECK_EQ_INT(s.locals[BS_DIALOGUE_LOCALS - 1].vars[0].hash, bs_save_variable_hash("SKNoTalk"));
    BS_CHECK_EQ_INT(s.locals[BS_DIALOGUE_LOCALS - 1].vars[1].hash, bs_save_variable_hash("ScampMad"));
    BS_CHECK_EQ_INT(d.history_count, 1);
    /* The replies around it sharing "A2": not the female one; the last needs no armour of protection. */
    BS_CHECK_EQ_INT(d.reply_count, 3);
    BS_CHECK(strcmp(bs_dialogue_reply(&d, 0), "Give me the keys.") == 0);
    BS_CHECK(strcmp(bs_dialogue_reply(&d, 1), "Where is Trenelle?") == 0);
    BS_CHECK(strcmp(bs_dialogue_reply(&d, 2), "Goodbye.") == 0);
    /* Choosing the second: its actions, its variable (the speaker's own), the end. */
    bs_dialogue_action act[BS_DIALOGUE_ACTIONS];
    uint32_t n = bs_dialogue_choose(&d, 1, &r, act, BS_DIALOGUE_ACTIONS);
    BS_CHECK_EQ_INT(n, 2);
    BS_CHECK(act[0].kind == BS_DLG_PUT && act[0].a == 21 && act[0].b == 33);
    BS_CHECK(act[1].kind == BS_DLG_STATE && act[1].a == 4);
    BS_CHECK_EQ_INT(s.locals[BS_DIALOGUE_LOCALS - 1].vars[1].value, 1);
    BS_CHECK_EQ_INT(bs_dialogue_global(&s, "ScampMad"), 0);
    BS_CHECK(bs_dialogue_over(&d));
    BS_CHECK(bs_dialogue_line(&d) == NULL);
    BS_CHECK_EQ_INT(d.history_count, 2);

    /* Now ScampMad holds for this scamp: the talkcheck picks SK1A31. Another scamp starts fresh. */
    BS_CHECK_EQ_INT(bs_dialogue_start(&d, &s, &group, &talk, 577, 0, &r, msg, sizeof msg), BS_DIALOGUE_TALK);
    BS_CHECK(strcmp(bs_dialogue_say_code(&d), "SK1A31") == 0);
    n = bs_dialogue_choose(&d, 0, &r, act, BS_DIALOGUE_ACTIONS);
    BS_CHECK(n == 1 && act[0].kind == BS_DLG_STATE && act[0].a == 8);
    BS_CHECK_EQ_INT(bs_dialogue_start(&d, &s, &group, &talk, 578, 0, &r, msg, sizeof msg), BS_DIALOGUE_TALK);
    BS_CHECK(strcmp(bs_dialogue_say_code(&d), "SK1A31") == 0); /* the third visit by a scamp */
    BS_CHECK_EQ_INT(s.visits[0], 3);
    /* The fourth on: GotoRandom, the lines in [[ ]] being messages. */
    for (int k = 0; k < 4; k++) {
        bs_dialogue_opening o = bs_dialogue_start(&d, &s, &group, &talk, 579, 0, &r, msg, sizeof msg);
        BS_CHECK_EQ_INT(o, BS_DIALOGUE_MESSAGE);
        BS_CHECK(strcmp(msg, "It sneers and refuses to speak with you.") == 0 ||
                 strcmp(msg, "It hisses and spits at you.") == 0);
    }
    BS_CHECK_EQ_INT(s.visits[0], 3);

    /* Wearing the armour of protection hides "Goodbye."; a random goto lands on one of its two lines. */
    const int32_t armour[] = {24, 5010, 25, 5011, 26, 5012, 27, 5013, 28, 5014, 29, 5015, -1};
    bs_dialogue_update_vars(&s, false, carries, (void *)armour);
    BS_CHECK_EQ_INT(bs_dialogue_global(&s, "Armor"), 1);
    BS_CHECK_EQ_INT(bs_dialogue_global(&s, "NotArmor"), 0);
    s.visits[1] = 0;
    BS_CHECK_EQ_INT(bs_dialogue_start(&d, &s, &group, &talk, 600, 1, &r, msg, sizeof msg), BS_DIALOGUE_TALK);
    BS_CHECK_EQ_INT(d.reply_count, 2);
    bs_dialogue_update_vars(&s, false, carries, (void *)nothing);
    bs_dialogue_update_replies(&d);
    BS_CHECK_EQ_INT(d.reply_count, 3);
    bs_dialogue_choose(&d, 2, &r, act, BS_DIALOGUE_ACTIONS);
    BS_CHECK(!bs_dialogue_over(&d));
    BS_CHECK(strcmp(bs_dialogue_say_code(&d), "SK1A31") == 0 || strcmp(bs_dialogue_say_code(&d), "SK1A91") == 0);
    bs_arena_free(&a);
}

static void test_checks(void)
{
    static bs_dialogue_state s;
    bs_dialogue_state_init(&s);
    static bs_dialogue d;
    memset(&d, 0, sizeof d);
    d.state = &s;
    s.globals[0] = (bs_dialogue_var){bs_save_variable_hash("A"), 1};
    s.globals[1] = (bs_dialogue_var){bs_save_variable_hash("B"), 0};
    s.global_count = 2;
    BS_CHECK(bs_dialogue_check(&d, ""));
    BS_CHECK(bs_dialogue_check(&d, "TRUE"));
    BS_CHECK(bs_dialogue_check(&d, "&A"));
    BS_CHECK(!bs_dialogue_check(&d, "&B"));
    BS_CHECK(bs_dialogue_check(&d, "&A OR &B"));
    BS_CHECK(!bs_dialogue_check(&d, "&A AND &B"));
    BS_CHECK(bs_dialogue_check(&d, "&A AND NOT &B"));
    BS_CHECK(bs_dialogue_check(&d, "NOT &B"));
    BS_CHECK(!bs_dialogue_check(&d, "PCMale")); /* a name without '&' is skipped: nothing holds */
    BS_CHECK(!bs_dialogue_check(&d, "&Unknown"));
}

static void test_actions(void)
{
    bs_dialogue_action a[BS_DIALOGUE_ACTIONS];
    BS_CHECK_EQ_INT(bs_dialogue_parse_actions("Summon CF_2", a, 8), 1);
    BS_CHECK(a[0].kind == BS_DLG_SUMMON && a[0].a == 11 && a[0].b == 2);
    BS_CHECK_EQ_INT(bs_dialogue_parse_actions("MonsterWait_60", a, 8), 1);
    BS_CHECK(a[0].kind == BS_DLG_STATE && a[0].a == 13);
    BS_CHECK_EQ_INT(bs_dialogue_parse_actions("ENDFRAME_1057", a, 8), 1);
    BS_CHECK(a[0].kind == BS_DLG_END_FRAME && a[0].a == 1057);
    BS_CHECK_EQ_INT(bs_dialogue_parse_actions("DialogLink_13", a, 8), 1);
    BS_CHECK(a[0].kind == BS_DLG_LINK && a[0].a == 13);
    BS_CHECK_EQ_INT(bs_dialogue_parse_actions("Teleport_0", a, 8), 1);
    BS_CHECK(a[0].kind == BS_DLG_TELEPORT && a[0].a == 0);
    BS_CHECK_EQ_INT(bs_dialogue_parse_actions("DROP 35_10000", a, 8), 1);
    BS_CHECK(a[0].kind == BS_DLG_DROP && a[0].a == 35 && a[0].b == 10000);
    BS_CHECK_EQ_INT(bs_dialogue_parse_actions("Take 30_10000", a, 8), 1);
    BS_CHECK(a[0].kind == BS_DLG_TAKE && a[0].a == 30);
    BS_CHECK_EQ_INT(bs_dialogue_parse_actions("Kill", a, 8), 1);
    BS_CHECK(a[0].kind == BS_DLG_KILL);
    BS_CHECK_EQ_INT(bs_dialogue_parse_actions("PCTeam", a, 8), 1);
    BS_CHECK(a[0].kind == BS_DLG_PC_TEAM);
    BS_CHECK_EQ_INT(bs_dialogue_parse_actions("MONSTERFLEE_120", a, 8), 1);
    BS_CHECK(a[0].kind == BS_DLG_STATE && a[0].a == 14);
    BS_CHECK_EQ_INT(bs_dialogue_parse_actions("MONSTERRALLY_2", a, 8), 1);
    BS_CHECK(a[0].kind == BS_DLG_RALLY && a[0].a == 4);
    BS_CHECK_EQ_INT(bs_dialogue_parse_actions("HealthHalf, PainSFX", a, 8), 1);
    BS_CHECK(a[0].kind == BS_DLG_HEALTH_HALF);
    /* The tables' slips do nothing, as in the game. */
    BS_CHECK_EQ_INT(bs_dialogue_parse_actions("MonsteraAttack", a, 8), 0);
    BS_CHECK_EQ_INT(bs_dialogue_parse_actions("PainSFX", a, 8), 0);
    BS_CHECK_EQ_INT(bs_dialogue_parse_actions("PCATTACK _100", a, 8), 0);
    BS_CHECK_EQ_INT(bs_dialogue_parse_actions("SummonVM_2", a, 8), 0);
    BS_CHECK_EQ_INT(bs_dialogue_parse_actions("", a, 8), 0);
}

static void test_speakers(void)
{
    char stem[4];
    BS_CHECK(bs_dialogue_stem("dm1", 2, 1, stem) && strcmp(stem, "dm1") == 0);
    BS_CHECK(bs_dialogue_stem("", 0, 1, stem) && strcmp(stem, "SK1") == 0);
    BS_CHECK(bs_dialogue_stem(NULL, 13, 5, stem) && strcmp(stem, "DS1") == 0);
    BS_CHECK(!bs_dialogue_stem("", 15, 1, stem));
    BS_CHECK(!bs_dialogue_stem("", -1, 1, stem));
    BS_CHECK(strcmp(bs_dialogue_creature_code(1), "VM") == 0);
    static const char *const names[] = {"SK1F.TXT", "SK1M.TXT", "SK1T.TXT", "SK2B.TXT", "SK2T.TXT", "BK1_001.TXT"};
    int32_t g, t;
    bs_dialogue_files(names, 6, "sk1", false, &g, &t);
    BS_CHECK(g == 1 && t == 2);
    bs_dialogue_files(names, 6, "SK1", true, &g, &t);
    BS_CHECK(g == 0 && t == 2);
    bs_dialogue_files(names, 6, "SK2", true, &g, &t);
    BS_CHECK(g == 3 && t == 4);
    bs_dialogue_files(names, 6, "XX1", true, &g, &t);
    BS_CHECK(g == -1 && t == -1);
}

int main(void)
{
    BS_RUN(test_talk_file);
    BS_RUN(test_conversation);
    BS_RUN(test_checks);
    BS_RUN(test_actions);
    BS_RUN(test_speakers);
    return BS_TEST_RESULT();
}
