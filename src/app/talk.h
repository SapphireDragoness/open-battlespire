#ifndef OPENBATTLESPIRE_TALK_H
#define OPENBATTLESPIRE_TALK_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#include "bs/formats/bsa.h"
#include "bs/formats/exe_text.h"
#include "bs/formats/flic.h"
#include "bs/formats/font.h"
#include "bs/formats/talk.h"
#include "bs/game/dialogue.h"
#include "bs/render/overlay.h"
#include "bs/render/screen.h"
#include "menus.h"

/*
 * The conversation screen (0x58043, drawn by 0x58226; docs/re/README.md, "Dialogue"): the speaker's talking
 * head, a full-screen FLC animation ("<XX>talk<nn>.flc" while their voice plays, "<XX>wait<nn>.flc" while
 * they wait), their line across the top, the player's replies on the right, and what was last said on the
 * scroll below (its arrows go back through the conversation). Exit leaves once there's nothing left to
 * reply; until then covr00.bsi covers it.
 *
 * Each line is spoken: WAVES.BSA (on the CD) has "<say code>.WAV" for most. Without a voice the speaker
 * talks for 4 s.
 */

typedef struct talk_env {
    bs_overlay *overlay;
    bs_screen *screen;
    const bs_font *fonts[MENU_FONTS];
    bs_assets *assets;
    const bs_exe_text *text;
    bs_bsa *txt;                      /* TXT.BSA: the tables */
    bs_bsa *heads;                    /* FLC.BSA: the talking heads; NULL: none */
    uint64_t now_ms;
    bs_rng *rng;
    void *ctx;
    void (*message)(void *ctx, const char *text);
    /* Starts the voice clip of a say code; false if there's none. */
    bool (*voice)(void *ctx, const char *code);
    bool (*voice_playing)(void *ctx);
    void (*voice_stop)(void *ctx);
} talk_env;

#define TALK_HEADS 10

typedef struct talk {
    bool open;
    bs_dialogue d;
    bs_talk_file group, script;
    bs_arena arena;                   /* the tables, while open */
    char heads[3];                    /* the talking heads' two letters */
    int talk_count, wait_count;       /* how many of each there are */
    bs_arena head_arena;              /* the animation playing */
    bs_flic flic;
    bs_flic_player player;
    bool have_head;
    uint64_t frame_due;
    uint8_t *rgba;
    bool talking;                     /* the voice is playing, or 4 s haven't passed */
    uint64_t talk_until;
    bool new_line;                    /* the line's voice is still to start */
    int reply_top[BS_DIALOGUE_REPLIES], reply_bottom[BS_DIALOGUE_REPLIES];
    int hover;                        /* the reply under the mouse, or -1 */
    bool told_over;                   /* "Conversation is done." shown */
    uint32_t speaker;                 /* the caller's: which monster or object talks */
    bool speaker_monster;
    char stem[4];
} talk;

void talk_init(talk *t);

/* Starts a conversation (0x584C6): stem's tables from TXT.BSA, heads the talking heads' two letters
 * ("SK"). A [[ ]] line comes back as a message. */
bs_dialogue_opening talk_open(talk *t, const talk_env *env, bs_dialogue_state *s, const char *stem,
                              const char *heads, bool female, uint32_t speaker_id, int32_t creature, char *message,
                              size_t cap);

typedef enum talk_result {
    TALK_NOTHING,
    TALK_CHOSE,                       /* a reply was chosen: its actions are in out */
    TALK_CLOSED,                      /* Exit */
} talk_result;

/* A key or mouse event; x, y in the 640x480 screen. */
talk_result talk_event(talk *t, const talk_env *env, const SDL_Event *e, int x, int y, bs_dialogue_action *out,
                       uint32_t *count);

/* Plays the head and the voice, and draws the screen. */
void talk_draw(talk *t, const talk_env *env, int window_w, int window_h);

void talk_close(talk *t, const talk_env *env);

#endif
