#include "bs/game/dialogue.h"

#include <stdlib.h>
#include <string.h>

#include "bs/formats/save.h"

/* RE references are GAME.EXE addresses; see docs/re/README.md, "Dialogue". */

/* The creatures' two-letter codes (0xF1C86). */
static const char *const creature_codes[BS_DIALOGUE_CREATURES] = {
    "SK", "VM", "DR", "SD", "SN", "GH", "WR", "MD", "FR", "FT", "MA", "CF", "SR", "DS", "ML", "DL",
};

/* Each level's default stems by creature (0xF1CC6): five per level. */
static const struct {
    uint8_t creature;
    char stem[4];
} default_stems[8][5] = {
    {{0, "SK1"}, {1, "VM1"}, {2, "DR1"}, {2, "DR1"}, {2, "DR1"}},
    {{0, "SK2"}, {1, "VM2"}, {2, "DR2"}, {3, "SD1"}, {3, "SD1"}},
    {{4, "SN1"}, {5, "GH1"}, {6, "WR1"}, {7, "MD1"}, {7, "MD1"}},
    {{8, "FR1"}, {9, "FT1"}, {2, "DR4"}, {12, "SR1"}, {13, "DS5"}},
    {{8, "FR2"}, {9, "FT2"}, {13, "DS1"}, {10, "MA1"}, {10, "MA1"}},
    {{11, "CF1"}, {10, "MA3"}, {12, "SR5"}, {14, "ML1"}, {14, "ML1"}},
    {{14, "ML2"}, {15, "DL1"}, {13, "DS3"}, {6, "WR2"}, {6, "WR2"}},
    {{14, "ML2"}, {15, "DL1"}, {13, "DS3"}, {6, "WR2"}, {6, "WR2"}},
};

/* The variables for what the player carries (0xF185C): true when both items (or the first alone) are
 * carried; a row without a name joins the next one. */
static const struct {
    int32_t base[2], number[2];
    const char *name;
    bool invert;
} carried_vars[26] = {
    {{31, 31}, {9020, 9046}, "Tayem", false},     {{31, 31}, {9003, 9029}, "Cess", false},
    {{31, 31}, {9016, 9042}, "Payem", false},     {{31, 31}, {9009, 9035}, "Iya", false},
    {{6, -1}, {9412, -1}, "WarAxe", false},       {{0, -1}, {9405, -1}, "Dagger", false},
    {{3, -1}, {8011, -1}, "Reiver", false},       {{3, -1}, {8011, -1}, "NotReiver", true},
    {{40, -1}, {-1, -1}, "Case", false},          {{31, 31}, {9004, 9030}, "WearAmuletD", false},
    {{31, 31}, {9013, 9039}, "WearAmuletM", false}, {{31, 31}, {9018, 9044}, "WearAmuletR", false},
    {{31, 31}, {9026, 9052}, "WearAmuletZ", false}, {{31, 31}, {9023, 9049}, "WearAmuletW", false},
    {{31, 31}, {9008, 9034}, "WearAmuletH", false}, {{31, 31}, {9015, 9041}, "WearAmuletO", false},
    {{31, 31}, {9021, 9047}, "WearAmuletU", false}, {{7, -1}, {2017, -1}, "Scourge", false},
    {{30, -1}, {-1, -1}, "InvVoidguide1", false},  {{24, 25}, {5010, 5011}, "", false},
    {{26, 27}, {5012, 5013}, "", false},          {{28, 29}, {5014, 5015}, "Armor", false},
    {{24, 25}, {5010, 5011}, "", false},          {{26, 27}, {5012, 5013}, "", false},
    {{28, 29}, {5014, 5015}, "NotArmor", true},   {{26, -1}, {1011, -1}, "Pauldron", false},
};

/* Script columns. */
enum {
    COL_SAY_CODE = 1, COL_LINE = 2, COL_REPLY_CODE = 3, COL_CHECK = 4, COL_REPLY = 7, COL_GOTO = 8,
    COL_ACTIONS = 9, COL_SET = 10
};

static const char *skip_space(const char *p)
{
    while (*p && (unsigned char)*p <= ' ')
        p++;
    return p;
}

static const char *skip_word(const char *p)
{
    while ((unsigned char)*p > ' ')
        p++;
    return p;
}

void bs_dialogue_state_init(bs_dialogue_state *s)
{
    memset(s, 0, sizeof *s);
}

/* ---- variables ---- */

static bs_dialogue_var *find_global(bs_dialogue_state *s, uint32_t hash)
{
    for (uint32_t i = 0; i < s->global_count; i++)
        if (s->globals[i].hash == hash)
            return &s->globals[i];
    return NULL;
}

static void set_global(bs_dialogue_state *s, uint32_t hash, int32_t value)
{
    bs_dialogue_var *v = find_global(s, hash);
    if (!v && s->global_count < BS_DIALOGUE_GLOBALS) {
        v = &s->globals[s->global_count++];
        v->hash = hash;
    }
    if (v)
        v->value = value;
}

int32_t bs_dialogue_global(const bs_dialogue_state *s, const char *name)
{
    uint32_t h = bs_save_variable_hash(name);
    for (uint32_t i = 0; i < s->global_count; i++)
        if (s->globals[i].hash == h)
            return s->globals[i].value;
    return 0;
}

void bs_dialogue_update_vars(bs_dialogue_state *s, bool female, bs_dialogue_has_fn has, void *ctx)
{
    bool chain = false;
    for (int i = 0; i < 26; i++) {
        if (i == 0 || carried_vars[i - 1].name[0])
            chain = true;
        bool v = has && has(ctx, carried_vars[i].base[0], carried_vars[i].number[0]);
        if (carried_vars[i].base[1] != -1)
            v = v && has && has(ctx, carried_vars[i].base[1], carried_vars[i].number[1]);
        v = chain && v;
        chain = v;
        if (carried_vars[i].name[0]) {
            if (carried_vars[i].invert)
                v = !v;
            chain = false;
            set_global(s, bs_save_variable_hash(carried_vars[i].name), v);
        }
    }
    /* 0x5AE3D */
    set_global(s, bs_save_variable_hash("PCMale"), !female);
    set_global(s, bs_save_variable_hash("PCFemale"), female);
    set_global(s, bs_save_variable_hash("SkelServe"), 0);
}

/* A variable's value: the speaker's own first, then the globals (0x59457). */
static int32_t value_of(const bs_dialogue *d, uint32_t hash)
{
    const bs_dialogue_local *l = &d->state->locals[d->local];
    for (int i = 0; i < BS_DIALOGUE_LOCAL_VARS; i++)
        if (l->vars[i].hash == hash)
            return l->vars[i].value;
    const bs_dialogue_var *g = find_global(d->state, hash);
    return g ? g->value : 0;
}

static bool exists(const bs_dialogue *d, uint32_t hash)
{
    const bs_dialogue_local *l = &d->state->locals[d->local];
    for (int i = 0; i < BS_DIALOGUE_LOCAL_VARS; i++)
        if (l->vars[i].hash == hash)
            return true;
    return find_global(d->state, hash) != NULL;
}

/* A word of a cell as the hash takes it: to the first space, '&' left out. */
static uint32_t word_hash(const char *p)
{
    char w[64];
    size_t n = 0;
    for (; (unsigned char)*p > ' ' && n + 1 < sizeof w; p++)
        if (*p != '&')
            w[n++] = *p;
    w[n] = '\0';
    return bs_save_variable_hash(w);
}

/* The variables an opening condition names become the speaker's own unless they're global (0x58B17).
 * After "and", "or" or "not" the next word is taken whatever it is. */
static void declare(bs_dialogue *d, const char *cond)
{
    bs_dialogue_local *l = &d->state->locals[d->local];
    const char *p = cond;
    while (*p) {
        p = skip_space(p);
        if (bs_talk_prefix(p, "and", 3) || bs_talk_prefix(p, "or", 2) || bs_talk_prefix(p, "not", 3))
            p = skip_word(p);
        p = skip_space(p);
        uint32_t h = word_hash(p);
        p = skip_word(p);
        if (exists(d, h))
            continue;
        for (int i = 0; i < BS_DIALOGUE_LOCAL_VARS; i++)
            if (!l->vars[i].hash) {
                l->vars[i].hash = h;
                l->vars[i].value = 0;
                break;
            }
    }
}

bool bs_dialogue_check(const bs_dialogue *d, const char *cond)
{
    const char *p = skip_space(cond);
    if (!*p)
        return true;
    bool result = false, negate = false;
    int op = -1; /* -1 first, 0 or, 1 and */
    while (*p) {
        if (*p == '&' || bs_talk_prefix(p, "true", 4)) {
            const char *w = *p == '&' ? p + 1 : p;
            bool v = bs_talk_prefix(w, "true", 4) || value_of(d, word_hash(w)) != 0;
            if (negate)
                v = !v;
            result = op == -1 ? v : op == 0 ? (v || result) : (v && result);
            negate = false;
        } else if (bs_talk_prefix(p, "or", 2)) {
            op = 0;
        } else if (bs_talk_prefix(p, "and", 3)) {
            op = 1;
        } else if (bs_talk_prefix(p, "not", 3)) {
            negate = true;
        }
        p = skip_space(skip_word(p));
    }
    return result;
}

/* SET1 (0x595AC): the speaker's own variable if it has it, else a global. */
static void set_var(bs_dialogue *d, const char *cell)
{
    uint32_t h = word_hash(skip_space(cell));
    bs_dialogue_local *l = &d->state->locals[d->local];
    for (int i = 0; i < BS_DIALOGUE_LOCAL_VARS; i++)
        if (l->vars[i].hash == h) {
            l->vars[i].value = 1;
            return;
        }
    set_global(d->state, h, 1);
}

/* ---- speakers ---- */

const char *bs_dialogue_creature_code(int32_t creature)
{
    return creature >= 0 && creature < BS_DIALOGUE_CREATURES ? creature_codes[creature] : "";
}

bool bs_dialogue_stem(const char *code, int32_t creature, int level_number, char out[4])
{
    if (code && code[0]) {
        memcpy(out, code, 3);
        out[3] = '\0';
        return true;
    }
    if (creature < 0 || level_number < 1 || level_number > 8)
        return false;
    for (int i = 0; i < 5; i++)
        if (default_stems[level_number - 1][i].creature == creature) {
            memcpy(out, default_stems[level_number - 1][i].stem, 4);
            return true;
        }
    return false;
}

void bs_dialogue_files(const char *const *names, uint32_t count, const char *stem, bool female, int32_t *group,
                       int32_t *talk)
{
    *group = *talk = -1;
    size_t n = strlen(stem);
    for (uint32_t i = 0; i < count; i++) {
        const char *s = names[i];
        if (!bs_talk_prefix(s, stem, n) || strlen(s) <= n)
            continue;
        const char *ext = strchr(s, '.');
        if (!ext || !bs_talk_prefix(ext, ".txt", 5))
            continue;
        char kind = s[3] >= 'a' && s[3] <= 'z' ? (char)(s[3] - 32) : s[3];
        if (kind == 'T')
            *talk = (int32_t)i;
        else if (kind == (female ? 'F' : 'M') || kind == 'B')
            *group = (int32_t)i;
    }
}

/* ---- actions ---- */

static int32_t creature_at(const char *p)
{
    int32_t c = -1;
    for (int i = 0; i < BS_DIALOGUE_CREATURES; i++)
        if (bs_talk_prefix(p, creature_codes[i], 2))
            c = i;
    return c;
}

/* One action (0x59D62); returns where the next "AND" may be. */
static const char *parse_action(const char *p, bs_dialogue_action *a, bool *known)
{
    char name[32];
    size_t n = 0;
    while (*p && *p != '_' && (unsigned char)*p > ' ') {
        if (n + 1 < sizeof name)
            name[n++] = *p;
        p++;
    }
    name[n] = '\0';
    p = skip_space(p);
    int32_t first = 0, second = 0;
    bool with_creature = bs_talk_prefix(name, "SUMMON", 6) || bs_talk_prefix(name, "COMMAND", 7) ||
                         bs_talk_prefix(name, "COMMANDALL", 10);
    if (with_creature) {
        first = creature_at(p);
        if (*p)
            p++;
    } else if (*p == '_' || *p == ' ') {
        first = atoi(p + 1);
    } else if (*p >= '0' && *p <= '9') {
        first = atoi(p);
    }
    if (*p && !bs_talk_prefix(p, "AND", 3)) {
        /* A second number after the next '_' or space ("PUT 21_033", "Command SK_4"). */
        const char *prev = p;
        for (;;) {
            prev = p++;
            if (!*p || (unsigned char)*p <= ' ' || *p == '_')
                break;
        }
        if (*p == '_' || *p == ' ') {
            second = atoi(prev + 2);
            p = prev + 2;
            while (*p && (unsigned char)*p > ' ' && *p != '_')
                p++;
        }
    }
    *known = true;
    a->a = first;
    a->b = 0;
    if (bs_talk_prefix(name, "DROPREADYWEAPON", 15)) {
        a->kind = BS_DLG_DROP_WEAPON;
    } else if (bs_talk_prefix(name, "PCATTACK", 8)) {
        *known = false; /* nothing (0x59D62) */
    } else if (bs_talk_prefix(name, "MONSTERATTACK", 13) || bs_talk_prefix(name, "MONSTERSEEK", 11)) {
        a->kind = BS_DLG_STATE;
        a->a = 8;
    } else if (bs_talk_prefix(name, "MONSTERWANDER", 13) || bs_talk_prefix(name, "MONSTERFLEE", 11)) {
        a->kind = BS_DLG_STATE;
        a->a = 14;
    } else if (bs_talk_prefix(name, "MONSTERRALLY", 12)) {
        a->kind = BS_DLG_RALLY;
        a->a = 4; /* whatever the number */
    } else if (bs_talk_prefix(name, "MONSTERWAIT", 11)) {
        a->kind = BS_DLG_STATE;
        a->a = 13;
    } else if (bs_talk_prefix(name, "MONSTERGUARD", 12) || bs_talk_prefix(name, "MONSTERLEAD", 11)) {
        *known = false;
    } else if (bs_talk_prefix(name, "HEALTHHALF", 10)) {
        a->kind = BS_DLG_HEALTH_HALF;
    } else if (bs_talk_prefix(name, "PAINSFX", 7)) {
        *known = false;
    } else if (bs_talk_prefix(name, "KILL", 4)) {
        a->kind = BS_DLG_KILL;
    } else if (bs_talk_prefix(name, "PCTEAM", 4)) {
        a->kind = BS_DLG_PC_TEAM; /* (the game compares four letters, so PCTEAMALL is this too) */
    } else if (bs_talk_prefix(name, "SUMMON", 6)) {
        a->kind = BS_DLG_SUMMON;
        a->b = second;
    } else if (bs_talk_prefix(name, "COMMANDALL", 10)) {
        a->kind = BS_DLG_COMMAND_ALL;
        a->b = second;
    } else if (bs_talk_prefix(name, "COMMAND", 7)) {
        a->kind = BS_DLG_STATE;
        a->a = second;
    } else if (bs_talk_prefix(name, "PUT", 3)) {
        a->kind = BS_DLG_PUT;
        a->b = second;
    } else if (bs_talk_prefix(name, "DROP", 4)) {
        a->kind = BS_DLG_DROP;
        a->b = second;
    } else if (bs_talk_prefix(name, "TAKE", 4)) {
        a->kind = BS_DLG_TAKE;
        a->b = second;
    } else if (bs_talk_prefix(name, "DIALOGLINK", 10)) {
        a->kind = BS_DLG_LINK;
    } else if (bs_talk_prefix(name, "ENDFRAME", 8)) {
        a->kind = BS_DLG_END_FRAME;
    } else if (bs_talk_prefix(name, "TELEPORT", 8)) {
        a->kind = BS_DLG_TELEPORT;
    } else {
        *known = false;
    }
    if (with_creature && a->a < 0)
        *known = false; /* no creature code: the game would use a stale value */
    return p;
}

uint32_t bs_dialogue_parse_actions(const char *cell, bs_dialogue_action *out, uint32_t max)
{
    uint32_t n = 0;
    const char *p = skip_space(cell);
    while (*p && n < max) {
        bool known;
        bs_dialogue_action a;
        p = skip_space(parse_action(p, &a, &known));
        if (known)
            out[n++] = a;
        if (!bs_talk_prefix(p, "AND", 3))
            break;
        p = skip_space(p + 3);
    }
    return n;
}

/* ---- the conversation ---- */

static void push_history(bs_dialogue *d, const char *text)
{
    if (d->history_count == BS_DIALOGUE_HISTORY) {
        memmove(d->history, d->history + 1, sizeof d->history[0] * (BS_DIALOGUE_HISTORY - 1));
        d->history_count--;
    }
    d->history_at = d->history_count;
    d->history[d->history_count++] = text;
}

/* The speaker's own record (0x584C6): found by id, or the last free one. */
static uint32_t local_record(bs_dialogue_state *s, uint32_t id)
{
    uint32_t found = UINT32_MAX, free_slot = BS_DIALOGUE_LOCALS - 1;
    for (uint32_t i = 0; i < BS_DIALOGUE_LOCALS; i++) {
        if (s->locals[i].id == id && id)
            found = i;
        if (!s->locals[i].id)
            free_slot = i;
    }
    if (found != UINT32_MAX)
        return found;
    s->locals[free_slot].id = id;
    return free_slot;
}

/* The opening line's say code from the group table, or NULL. */
static const char *opening(bs_dialogue *d, bs_rng *r)
{
    const bs_talk_file *g = d->group;
    int32_t check = bs_talk_find(g, 0, 1, "talkcheck");
    if (check >= 0) {
        for (uint32_t row = (uint32_t)check + 1; bs_talk_cell(g, row, 1)[0]; row++)
            declare(d, bs_talk_cell(g, row, 1));
        for (uint32_t row = (uint32_t)check + 1; bs_talk_cell(g, row, 1)[0]; row++)
            if (bs_dialogue_check(d, bs_talk_cell(g, row, 1)))
                return bs_talk_cell(g, row, 2);
    }
    int32_t table = bs_talk_find(g, 0, 1, "talktable");
    if (table < 0)
        return NULL;
    uint32_t row = (uint32_t)table + 1;
    if (d->creature >= 0 && d->creature < BS_DIALOGUE_CREATURES)
        row += d->state->visits[d->creature];
    const char *code = bs_talk_cell(g, row, 2);
    if (bs_talk_prefix(code, "GotoRandom", 10)) {
        int32_t roll = bs_rng_range(r, 1, 100);
        int32_t random = bs_talk_find(g, row, 1, "random");
        if (random < 0)
            return NULL;
        for (uint32_t k = (uint32_t)random + 2; k < g->count; k++)
            if (bs_talk_in_range(bs_talk_cell(g, k, 1), roll))
                return bs_talk_cell(g, k, 2);
        return NULL;
    }
    return code;
}

bs_dialogue_opening bs_dialogue_start(bs_dialogue *d, bs_dialogue_state *s, const bs_talk_file *group,
                                      const bs_talk_file *talk, uint32_t speaker_id, int32_t creature, bs_rng *r,
                                      char *message, size_t cap)
{
    memset(d, 0, sizeof *d);
    d->group = group;
    d->talk = talk;
    d->state = s;
    d->creature = creature;
    d->say_row = -1;
    if (cap)
        message[0] = '\0';
    d->local = local_record(s, speaker_id);
    const char *code = opening(d, r);
    if (creature >= 0 && creature < BS_DIALOGUE_CREATURES && s->visits[creature] < BS_DIALOGUE_VISITS_MAX)
        s->visits[creature]++;
    if (!code || !code[0])
        return BS_DIALOGUE_NOTHING;
    int32_t row = bs_talk_find(talk, 0, COL_SAY_CODE, code);
    if (row < 0)
        return BS_DIALOGUE_NOTHING;
    /* A line in [[ ]] is only a message. */
    const char *line = skip_space(bs_talk_cell(talk, (uint32_t)row, COL_LINE));
    bool quoted = false;
    while (*line == '"') {
        line++;
        quoted = true;
    }
    if (line[0] == '[') {
        if (cap) {
            /* Without the "[[" and the "]]" (and a closing quote, if it opened with one). */
            size_t n = 0;
            for (const char *q = line + 2; *q && n + 1 < cap; q++)
                message[n++] = *q;
            message[n] = '\0';
            size_t cut = quoted ? 3 : 2;
            message[n >= cut ? n - cut : 0] = '\0';
        }
        return BS_DIALOGUE_MESSAGE;
    }
    d->say_row = row;
    push_history(d, bs_talk_cell(talk, (uint32_t)row, COL_LINE));
    bs_dialogue_update_replies(d);
    return BS_DIALOGUE_TALK;
}

const char *bs_dialogue_line(const bs_dialogue *d)
{
    return d->say_row < 0 ? NULL : bs_talk_cell(d->talk, (uint32_t)d->say_row, COL_LINE);
}

const char *bs_dialogue_say_code(const bs_dialogue *d)
{
    return d->say_row < 0 ? NULL : bs_talk_cell(d->talk, (uint32_t)d->say_row, COL_SAY_CODE);
}

/* Whether a row's reply code goes with the line's say code: the same 4th and 5th letters. */
static bool same_group(const char *say, const char *reply)
{
    if (strlen(say) < 5 || strlen(reply) < 5)
        return false;
    return say[3] == reply[3] && say[4] == reply[4];
}

void bs_dialogue_update_replies(bs_dialogue *d)
{
    d->reply_count = 0;
    if (d->say_row < 0)
        return;
    const char *say = bs_dialogue_say_code(d);
    uint32_t first = (uint32_t)d->say_row;
    while (first > 0 && same_group(say, bs_talk_cell(d->talk, first - 1, COL_REPLY_CODE)))
        first--;
    for (uint32_t row = first; row < d->talk->count && same_group(say, bs_talk_cell(d->talk, row, COL_REPLY_CODE));
         row++) {
        bool ok = true;
        for (int k = 0; k < 3 && ok; k++)
            ok = bs_dialogue_check(d, bs_talk_cell(d->talk, row, (uint32_t)(COL_CHECK + k)));
        if (ok && d->reply_count < BS_DIALOGUE_REPLIES)
            d->replies[d->reply_count++] = row;
    }
}

const char *bs_dialogue_reply(const bs_dialogue *d, uint32_t i)
{
    return i < d->reply_count ? bs_talk_cell(d->talk, d->replies[i], COL_REPLY) : "";
}

/* Where a reply goes: a say code, "end", or ranges of d100 "01-50: XX1A21 51-00: XX1A31" (0x59BFD). */
static const char *goto_code(const char *cell, bs_rng *r, char *buf, size_t cap)
{
    const char *p = skip_space(cell);
    if (!strchr(p, ':'))
        return p;
    int32_t roll = bs_rng_range(r, 1, 100);
    while (*p) {
        const char *colon = strchr(p, ':');
        if (!colon)
            break;
        bool hit = bs_talk_in_range(p, roll);
        const char *code = skip_space(colon + 1);
        const char *end = skip_word(code);
        if (hit) {
            size_t n = (size_t)(end - code) < cap - 1 ? (size_t)(end - code) : cap - 1;
            memcpy(buf, code, n);
            buf[n] = '\0';
            return buf;
        }
        p = skip_space(end);
    }
    return "end"; /* no range holds the roll: the game would read past the cell */
}

uint32_t bs_dialogue_choose(bs_dialogue *d, uint32_t i, bs_rng *r, bs_dialogue_action *out, uint32_t max)
{
    if (i >= d->reply_count || d->say_row < 0)
        return 0;
    uint32_t row = d->replies[i];
    push_history(d, bs_talk_cell(d->talk, row, COL_REPLY));
    const char *set = bs_talk_cell(d->talk, row, COL_SET);
    if (set[0])
        set_var(d, set);
    uint32_t n = bs_dialogue_parse_actions(bs_talk_cell(d->talk, row, COL_ACTIONS), out, max);
    for (uint32_t k = 0; k < n; k++)
        if (out[k].kind == BS_DLG_KILL)
            d->kill = true;
    char buf[32];
    const char *next = goto_code(bs_talk_cell(d->talk, row, COL_GOTO), r, buf, sizeof buf);
    d->say_row = bs_talk_prefix(next, "end", 3) || !next[0] ? -1 : bs_talk_find(d->talk, 0, COL_SAY_CODE, next);
    bs_dialogue_update_replies(d);
    return n;
}
