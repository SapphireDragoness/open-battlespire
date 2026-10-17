#include "talk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "text.h"

/* RE references are GAME.EXE addresses; see docs/re/README.md, "Dialogue". */

/* Buttons (0xF1800): the replies, the history's arrows, Exit. */
static const int replies_area[4] = {320, 100, 639, 370};
static const int history_up[4] = {537, 386, 558, 427};
static const int history_down[4] = {537, 432, 558, 472};
static const int exit_button[4] = {584, 425, 631, 474};
#define LINE_X 320                    /* the speaker's line: centred, wrapped (0x5922C) */
#define LINE_Y 5
#define LINE_W 639
#define LINE_FONT 9
#define REPLY_X 480                   /* the replies, one under another (0x58C66) */
#define REPLY_Y 100
#define REPLY_W 300
#define REPLY_FONT 8
#define HISTORY_X 272                 /* what was last said (0x5AC2D) */
#define HISTORY_Y 398
#define HISTORY_W 456
#define HISTORY_FONT 6
#define END_Y 15                      /* "[ END OF DIALOG ]" */
#define HOVER_COLOR 0xC00000FFu       /* 0x6000 */
#define SILENT_TALK_MS 4000           /* without a voice (0x1CE6A) */
enum { MSG_DONE = 118, MSG_END = 123 };

static bool in(const int r[4], int x, int y)
{
    return x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3];
}

void talk_init(talk *t)
{
    memset(t, 0, sizeof *t);
    t->hover = -1;
}

/* How many "<XX>talk0?.flc" or "<XX>wait0?.flc" there are (0x5A6D6). */
static int count_heads(const talk_env *env, const char *heads, const char *kind)
{
    int n = 0;
    for (int i = 0; env->heads && i < TALK_HEADS; i++) {
        char name[32];
        snprintf(name, sizeof name, "%s%s%02d.FLC", heads, kind, i);
        if (bs_bsa_find(env->heads, name))
            n = i + 1;
    }
    return n;
}

/* Starts a head animation. */
static void play_head(talk *t, const talk_env *env, const char *kind, int number)
{
    t->have_head = false;
    if (!env->heads)
        return;
    char name[32];
    snprintf(name, sizeof name, "%s%s%02d.FLC", t->heads, kind, number);
    const bs_bsa_record *rec = bs_bsa_find(env->heads, name);
    if (!rec)
        return;
    bs_arena_free(&t->head_arena);
    bs_arena_init(&t->head_arena, 0);
    uint8_t *data;
    size_t size;
    if (bs_bsa_read(env->heads, rec, &t->head_arena, &data, &size) != BS_OK ||
        bs_flic_open(data, size, &t->flic) != BS_OK ||
        bs_flic_player_init(&t->player, &t->flic, &t->head_arena) != BS_OK)
        return;
    t->rgba = BS_ARENA_ARRAY(&t->head_arena, uint8_t, (size_t)t->flic.width * t->flic.height * 4);
    if (!t->rgba)
        return;
    t->have_head = true;
    t->frame_due = env->now_ms;
}

/* A random one of the talking or waiting animations (0x58226). */
static void next_head(talk *t, const talk_env *env)
{
    int n = t->talking ? t->talk_count : t->wait_count;
    if (n > 0)
        play_head(t, env, t->talking ? "TALK" : "WAIT", bs_rng_range(env->rng, 0, n - 1));
}

/* A new line: its voice, unless it's in [[ ]] or one of Jaciel's first two (0x5922C). */
static void speak(talk *t, const talk_env *env)
{
    t->new_line = false;
    const char *line = bs_dialogue_line(&t->d), *code = bs_dialogue_say_code(&t->d);
    if (!line || !code)
        return;
    if (bs_talk_prefix(line, "[[", 2) || bs_talk_prefix(code, "jm1a11", 6) || bs_talk_prefix(code, "jm1a21", 6))
        return;
    bool voiced = env->voice && env->voice(env->ctx, code);
    t->talk_until = voiced ? 0 : env->now_ms + SILENT_TALK_MS;
}

bs_dialogue_opening talk_open(talk *t, const talk_env *env, bs_dialogue_state *s, const char *stem,
                              const char *heads, bool female, uint32_t speaker_id, int32_t creature, char *message,
                              size_t cap)
{
    talk_close(t, env);
    if (cap)
        message[0] = '\0';
    if (!env->txt)
        return BS_DIALOGUE_NOTHING;
    const char **names = malloc(sizeof *names * (env->txt->count + 1));
    if (!names)
        return BS_DIALOGUE_NOTHING;
    for (uint32_t i = 0; i < env->txt->count; i++)
        names[i] = env->txt->records[i].name;
    int32_t gi, ti;
    bs_dialogue_files(names, env->txt->count, stem, female, &gi, &ti);
    free(names);
    if (gi < 0 || ti < 0)
        return BS_DIALOGUE_NOTHING;
    bs_arena_init(&t->arena, 0);
    uint8_t *data;
    size_t size;
    if (bs_bsa_read(env->txt, &env->txt->records[gi], &t->arena, &data, &size) != BS_OK ||
        bs_talk_parse(data, size, &t->arena, &t->group) != BS_OK ||
        bs_bsa_read(env->txt, &env->txt->records[ti], &t->arena, &data, &size) != BS_OK ||
        bs_talk_parse(data, size, &t->arena, &t->script) != BS_OK) {
        bs_arena_free(&t->arena);
        return BS_DIALOGUE_NOTHING;
    }
    bs_dialogue_opening o =
        bs_dialogue_start(&t->d, s, &t->group, &t->script, speaker_id, creature, env->rng, message, cap);
    if (o != BS_DIALOGUE_TALK) {
        bs_arena_free(&t->arena);
        return o;
    }
    t->open = true;
    snprintf(t->stem, sizeof t->stem, "%s", stem);
    t->heads[0] = (char)(heads[0] >= 'a' && heads[0] <= 'z' ? heads[0] - 32 : heads[0]);
    t->heads[1] = (char)(heads[1] >= 'a' && heads[1] <= 'z' ? heads[1] - 32 : heads[1]);
    t->heads[2] = '\0';
    t->talk_count = count_heads(env, t->heads, "TALK");
    t->wait_count = count_heads(env, t->heads, "WAIT");
    t->hover = -1;
    t->told_over = false;
    t->new_line = true;
    t->talking = true;
    play_head(t, env, "TALK", 0); /* (0x58043) */
    return BS_DIALOGUE_TALK;
}

void talk_close(talk *t, const talk_env *env)
{
    if (t->open && env && env->voice_stop)
        env->voice_stop(env->ctx);
    bs_arena_free(&t->arena);
    bs_arena_free(&t->head_arena);
    uint32_t speaker = t->speaker;
    bool monster = t->speaker_monster;
    talk_init(t);
    t->speaker = speaker;
    t->speaker_monster = monster;
}

talk_result talk_event(talk *t, const talk_env *env, const SDL_Event *e, int x, int y, bs_dialogue_action *out,
                       uint32_t *count)
{
    *count = 0;
    if (!t->open)
        return TALK_NOTHING;
    bool can_leave = t->d.reply_count == 0 || bs_dialogue_over(&t->d); /* (0x58324) */
    if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
        if (e->key.key == SDLK_ESCAPE && can_leave)
            return TALK_CLOSED;
        if (e->key.key >= SDLK_1 && e->key.key <= SDLK_9 && !bs_dialogue_over(&t->d)) {
            uint32_t i = (uint32_t)(e->key.key - SDLK_1);
            if (i < t->d.reply_count) {
                *count = bs_dialogue_choose(&t->d, i, env->rng, out, BS_DIALOGUE_ACTIONS);
                t->new_line = true;
                return TALK_CHOSE;
            }
        }
        return TALK_NOTHING;
    }
    if (e->type == SDL_EVENT_MOUSE_MOTION) {
        t->hover = -1;
        if (in(replies_area, x, y))
            for (uint32_t i = 0; i < t->d.reply_count; i++)
                if (y >= t->reply_top[i] && y <= t->reply_bottom[i])
                    t->hover = (int)i;
        return TALK_NOTHING;
    }
    if (e->type != SDL_EVENT_MOUSE_BUTTON_DOWN || e->button.button != SDL_BUTTON_LEFT)
        return TALK_NOTHING;
    if (in(replies_area, x, y) && !bs_dialogue_over(&t->d)) {
        for (uint32_t i = 0; i < t->d.reply_count; i++)
            if (y >= t->reply_top[i] && y <= t->reply_bottom[i]) {
                *count = bs_dialogue_choose(&t->d, i, env->rng, out, BS_DIALOGUE_ACTIONS);
                t->new_line = true;
                t->hover = -1;
                return TALK_CHOSE;
            }
    } else if (in(history_up, x, y)) {
        if (t->d.history_at > 0)
            t->d.history_at--;
    } else if (in(history_down, x, y)) {
        if (t->d.history_at + 1 < t->d.history_count)
            t->d.history_at++;
    } else if (in(exit_button, x, y) && can_leave) {
        return TALK_CLOSED;
    }
    return TALK_NOTHING;
}

/* The head's next frame when it's due; a finished animation gives way to another (0x5C1B3). */
static void advance_head(talk *t, const talk_env *env)
{
    if (!t->have_head)
        return;
    uint32_t speed = t->flic.speed ? t->flic.speed : 71;
    bool changed = false;
    int guard = 0;
    while (env->now_ms >= t->frame_due && guard++ < 8) {
        bool ended = false;
        if (t->player.frame >= t->flic.frames ||
            bs_flic_next_frame(&t->player, &ended) != BS_OK || ended) {
            next_head(t, env);
            if (!t->have_head)
                return;
            continue;
        }
        changed = true;
        t->frame_due += speed;
    }
    if (env->now_ms > t->frame_due + 1000)
        t->frame_due = env->now_ms;
    if (!changed && t->player.frame)
        return;
    size_t n = (size_t)t->flic.width * t->flic.height;
    for (size_t i = 0; i < n; i++) {
        const uint8_t *c = &t->player.palette[3 * t->player.pixels[i]];
        t->rgba[4 * i] = c[0];
        t->rgba[4 * i + 1] = c[1];
        t->rgba[4 * i + 2] = c[2];
        t->rgba[4 * i + 3] = 255;
    }
    bs_screen_set(env->screen, t->rgba, t->flic.width, t->flic.height);
}

void talk_draw(talk *t, const talk_env *env, int ww, int wh)
{
    if (!t->open)
        return;
    if (t->new_line && !bs_dialogue_over(&t->d))
        speak(t, env);
    t->talking = (env->voice_playing && env->voice_playing(env->ctx)) || env->now_ms < t->talk_until;
    advance_head(t, env);
    bs_screen_draw(env->screen, ww, wh);
    bs_overlay *o = env->overlay;
    bs_overlay_clear(o);
    bool over = bs_dialogue_over(&t->d);
    if (over) {
        if (!t->told_over && env->message && env->text) {
            env->message(env->ctx, bs_exe_message(env->text, MSG_DONE));
            t->told_over = true;
        }
        const bs_font *f = env->fonts[LINE_FONT];
        const char *end = env->text ? bs_exe_message(env->text, MSG_END) : "[ END OF DIALOG ]";
        if (f)
            bs_overlay_text(o, f, LINE_X - bs_text_width(f, end) / 2, END_Y, end);
    } else {
        bs_overlay_image(o, bs_assets_image(env->assets, "covr00.bsi"), 0, 0);
        ui_text_box(o, env->fonts[LINE_FONT], bs_dialogue_line(&t->d), LINE_X, LINE_Y, LINE_W);
        /* The replies; the one under the mouse framed (0x5AD2C). */
        bs_dialogue_update_replies(&t->d);
        const bs_font *f = env->fonts[REPLY_FONT];
        int lh = f ? bs_text_height(f) : 10, y = REPLY_Y;
        for (uint32_t i = 0; i < t->d.reply_count; i++) {
            t->reply_top[i] = y;
            int last = ui_text_box(o, f, bs_dialogue_reply(&t->d, i), REPLY_X, y, REPLY_W);
            y = last + 2 * lh;
            t->reply_bottom[i] = y - 5;
            if ((int)i == t->hover)
                bs_overlay_frame(o, replies_area[0], t->reply_top[i] - 5, replies_area[2] - replies_area[0] + 1,
                                 t->reply_bottom[i] - t->reply_top[i] + 6, HOVER_COLOR);
        }
    }
    if (t->d.history_at < t->d.history_count)
        ui_text_box(o, env->fonts[HISTORY_FONT], t->d.history[t->d.history_at], HISTORY_X, HISTORY_Y, HISTORY_W);
}
