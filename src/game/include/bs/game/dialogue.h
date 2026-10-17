#ifndef BS_GAME_DIALOGUE_H
#define BS_GAME_DIALOGUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bs/formats/talk.h"
#include "bs/game/items.h"

/*
 * Conversations (GAME.EXE 0x584C6 and on; docs/re/README.md, "Dialogue").
 *
 * A speaker (a monster, or a level object or flat with a dialogue code) has a three-letter stem: its
 * BS6 STRU 108 code, or for a monster without one its level's default for its creature (0xF1CC6). The
 * stem names its tables in TXT.BSA: <stem>M or <stem>F (or <stem>B) to pick the opening line, and
 * <stem>T, the script.
 *
 * - The opening (0x584C6): the first "talkcheck" row whose condition holds, else the "talktable" row for
 *   how many times this creature type has been spoken to (the first, second, third, then the fourth on;
 *   "GotoRandom" rolls d100 against the "random" rows). A line in [[ ]] is only a message.
 * - The script: a line, and the replies whose checks hold: the rows around it whose reply codes share its
 *   say code's 4th and 5th letters (0x5922C). A reply sets a variable, runs its actions and goes to
 *   another line, a random one, or the end (0x59BFD).
 * - Variables are 32-bit hashes of their names (bs_save_variable_hash): each speaker keeps up to 8 of its
 *   own (those its opening conditions name that aren't global), the rest are global. Some globals follow
 *   the player: their sex, and what they carry (0xF185C).
 *
 * The state that lasts (the variables, the visits) is laid out as saves keep it (SAVEVARS.DAT).
 */

#define BS_DIALOGUE_GLOBALS 256
#define BS_DIALOGUE_LOCALS 256
#define BS_DIALOGUE_LOCAL_VARS 8
#define BS_DIALOGUE_CREATURES 16
#define BS_DIALOGUE_REPLIES 16
#define BS_DIALOGUE_HISTORY 64
#define BS_DIALOGUE_ACTIONS 8
#define BS_DIALOGUE_VISITS_MAX 3     /* the fourth visit and on use the same row */

typedef struct bs_dialogue_var {
    uint32_t hash;
    int32_t value;
} bs_dialogue_var;

typedef struct bs_dialogue_local {
    uint32_t id;                      /* the speaker's record id (a monster's spawn flat, an object's BS6 id); 0 free */
    bs_dialogue_var vars[BS_DIALOGUE_LOCAL_VARS];
} bs_dialogue_local;

typedef struct bs_dialogue_state {
    bs_dialogue_var globals[BS_DIALOGUE_GLOBALS];
    uint32_t global_count;
    bs_dialogue_local locals[BS_DIALOGUE_LOCALS];
    uint8_t visits[BS_DIALOGUE_CREATURES];   /* per creature type (0x401D44) */
} bs_dialogue_state;

void bs_dialogue_state_init(bs_dialogue_state *s);

/* Whether the player carries an item of a base with a number (or enchantment), -1 any. */
typedef bool (*bs_dialogue_has_fn)(void *ctx, int32_t base, int32_t number);

/* The globals that follow the player (0x59935, 0x5AE3D): PCMale, PCFemale, SkelServe (always false), and
 * those for items carried: the Sigil Amulets (Tayem, WearAmuletD ...), the War Axe, the Moon Reiver, the
 * spear case, the Voidguide, the armour of protection. */
void bs_dialogue_update_vars(bs_dialogue_state *s, bool female, bs_dialogue_has_fn has, void *ctx);

/* A global's value by name (0 if not set). */
int32_t bs_dialogue_global(const bs_dialogue_state *s, const char *name);

/* The two-letter code of a creature (0xF1C86): "SK", "VM" ... as its talking heads' names begin. */
const char *bs_dialogue_creature_code(int32_t creature);

/* A speaker's stem (0x583E1): its own code, or a monster's level default by creature. */
bool bs_dialogue_stem(const char *code, int32_t creature, int level_number, char out[4]);

/* The tables for a stem among TXT.BSA's names, in the archive's order (0x584C6): the last <stem>?T.TXT for the
 * script, the last with M or F (the player's sex) or B for the opening. -1 if none. */
void bs_dialogue_files(const char *const *names, uint32_t count, const char *stem, bool female, int32_t *group,
                       int32_t *talk);

typedef enum bs_dialogue_action_kind {
    BS_DLG_DROP_WEAPON,               /* DROPREADYWEAPON: "You comply and drop your weapon on the ground." */
    BS_DLG_STATE,                     /* the speaker's AI state a: MONSTERATTACK, -SEEK 8, -WANDER, -FLEE 14,
                                         -WAIT 13, COMMAND XX_n */
    BS_DLG_RALLY,                     /* MONSTERRALLY: 4 of the speaker's creature come */
    BS_DLG_HEALTH_HALF,               /* HEALTHHALF */
    BS_DLG_KILL,                      /* KILL: the speaker goes when the conversation ends */
    BS_DLG_PC_TEAM,                   /* PCTEAM: the speaker joins the player */
    BS_DLG_PC_TEAM_ALL,               /* PCTEAMALL a b (unused by the tables) */
    BS_DLG_SUMMON,                    /* SUMMON XX_n: n of creature a by the speaker */
    BS_DLG_COMMAND_ALL,               /* COMMANDALL XX_n: every monster of creature a gets AI state b */
    BS_DLG_PUT,                       /* PUT a_b: the player is given an item of base a, number b */
    BS_DLG_DROP,                      /* DROP a_b: an item at the speaker's feet */
    BS_DLG_TAKE,                      /* TAKE a_b: the item is taken from the player */
    BS_DLG_LINK,                      /* DIALOGLINK n: the objects with STRU 104 = n are used */
    BS_DLG_END_FRAME,                 /* ENDFRAME n: the end, CHOP2 and text n */
    BS_DLG_TELEPORT,                  /* TELEPORT n: the speaker vanishes, now (0) or after n seconds */
} bs_dialogue_action_kind;

typedef struct bs_dialogue_action {
    bs_dialogue_action_kind kind;
    int32_t a, b;
} bs_dialogue_action;

/* An action cell (0x59BFD, 0x59D62): actions joined by AND; names the game doesn't know do nothing. */
uint32_t bs_dialogue_parse_actions(const char *cell, bs_dialogue_action *out, uint32_t max);

typedef struct bs_dialogue {
    const bs_talk_file *group, *talk;
    bs_dialogue_state *state;
    uint32_t local;                   /* the speaker's record in state->locals */
    int32_t creature;                 /* the speaker's creature, -1 if not a monster */
    int32_t say_row;                  /* the speaker's line in the script; -1 when it's over */
    uint32_t replies[BS_DIALOGUE_REPLIES];   /* script rows */
    uint32_t reply_count;
    const char *history[BS_DIALOGUE_HISTORY];   /* the opening line, then the replies chosen (0x5ABF0) */
    uint32_t history_count, history_at;
    bool kill;                        /* KILL was done */
} bs_dialogue;

typedef enum bs_dialogue_opening {
    BS_DIALOGUE_NOTHING,              /* no tables, or nothing to say */
    BS_DIALOGUE_MESSAGE,              /* a line in [[ ]]: shown as a message, no conversation */
    BS_DIALOGUE_TALK,
} bs_dialogue_opening;

/* Starts a conversation (0x584C6). creature: the speaker's, -1 if it isn't a monster. A message goes to
 * message. */
bs_dialogue_opening bs_dialogue_start(bs_dialogue *d, bs_dialogue_state *s, const bs_talk_file *group,
                                      const bs_talk_file *talk, uint32_t speaker_id, int32_t creature, bs_rng *r,
                                      char *message, size_t cap);

/* The speaker's line and its say code (the voice clip's name), NULL when it's over. */
const char *bs_dialogue_line(const bs_dialogue *d);
const char *bs_dialogue_say_code(const bs_dialogue *d);

/* Finds the replies whose checks hold now (0x5922C). */
void bs_dialogue_update_replies(bs_dialogue *d);
const char *bs_dialogue_reply(const bs_dialogue *d, uint32_t i);

/* Chooses a reply (0x58359, 0x59BFD): its variable is set, its actions returned, and the line it goes to
 * becomes the speaker's. Returns how many actions. */
uint32_t bs_dialogue_choose(bs_dialogue *d, uint32_t i, bs_rng *r, bs_dialogue_action *out, uint32_t max);

static inline bool bs_dialogue_over(const bs_dialogue *d)
{
    return d->say_row < 0;
}

/* Whether a condition holds (0x58D4E), for tests. */
bool bs_dialogue_check(const bs_dialogue *d, const char *cond);

#endif
