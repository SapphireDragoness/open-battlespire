#ifndef BS_GAME_MAGIC_H
#define BS_GAME_MAGIC_H

#include <stdbool.h>
#include <stdint.h>

#include "bs/formats/creatures.h"
#include "bs/formats/spells.h"
#include "bs/game/collision.h"
#include "bs/game/monsters.h"
#include "bs/game/player.h"
#include "bs/game/use.h"

/*
 * Spells, by the rules decoded from GAME.EXE (docs/formats/spells.md):
 *
 * - Casting (0x27AEB) costs base x (110 - the school's skill) / 100 x the
 *   delivery's 80-120 %, at least 5 (0x29791). A spell on the caster takes
 *   effect at once (0x296C4); the player's touch and ranged spells are made
 *   ready (0x16BFA4) and go with the next use of a creature (touch) or press
 *   of the attack button (range); a monster's go at once at its target.
 * - Ranged spells fly as missiles (0x274BE, 0x28AD0): 900 units/s (1000 on
 *   level 5), for at most 10 s and 3072 units from the caster. They burst on
 *   the first creature within 150 units or, by their detonation, on the
 *   level; area spells and bursts on the level reach every creature within
 *   192 (0x2786F). Resistance, absorption and reflection (0x27DAC) come
 *   first: spells costing no more than the target's pool.
 * - Effects (0x27F4C): damage through the elements' tolerances (0x2AA5D),
 *   healing, and timed effects on the player (0x295A0) with their icons.
 *   Most timed effects only work on the player.
 *
 * Casting, a potion's spell included, counts for the school's skill
 * (bs/game/levelling.h).
 *
 * Not yet: Dispel Magic's list, Detect spells' displays, what Confusion does
 * to the controls, summoned monsters fighting for the player, multiplayer's
 * Dispel Sigil.
 */

#define BS_MAGIC_PLAYER (-1)           /* a caster or target that is the player, not a monster */
#define BS_MAGIC_NOBODY (-2)
#define BS_MAGIC_MISSILES 64
#define BS_MAGIC_TRAPS 32
#define BS_MAGIC_BURSTS 32
#define BS_MAGIC_SUMMONS 20            /* the hidden reserve: 255 - spawn points, at most 20 (0x461BE) */

/* A spell as cast: the record with the caster's choices, its cost and its rolls (the game's spell
 * object, type 9). */
typedef struct bs_spell_cast {
    bs_spell spell;
    int32_t cost;                      /* +0x63 */
    uint32_t duration_ms;              /* +0x43 */
    int32_t magnitude;                 /* +0x47 */
    int32_t owner;                     /* +0x4F: BS_MAGIC_PLAYER or a monster */
} bs_spell_cast;

typedef struct bs_missile {
    bs_spell_cast cast;
    float pos[3];
    float dir[3];                      /* +0x53: unit */
    float speed;                       /* +0x73: units/s */
    float fall;                        /* +0x6F: Organic Drop's falling speed, units/s, + down */
    uint32_t launched;                 /* +0x21 */
    bool cleared;                      /* has been further than 150 from its caster (see magic.c) */
    bool live;
} bs_missile;

/* Delayed Damage's trap (0x28445): bursts as Cause Damage when someone else comes within 120. */
typedef struct bs_trap {
    bs_spell_cast cast;
    float pos[3];
    uint32_t until;
    bool live;
} bs_trap;

/* A burst's picture (explNN.bsi), or a sparkle (twinkle.bsi) at a monster casting on itself. */
typedef struct bs_burst {
    float pos[3];
    int8_t element;                    /* -1: the sparkle */
    uint32_t start;
    bool live;
} bs_burst;

typedef struct bs_magic_state {
    bs_missile missiles[BS_MAGIC_MISSILES];
    bs_trap traps[BS_MAGIC_TRAPS];
    bs_burst bursts[BS_MAGIC_BURSTS];
    /* The player's spell made ready (0x16BFA4), paid for. */
    bool ready;
    bs_spell_cast ready_cast;
    /* The casting hand (castNN.bsi, 0x2A698): while a spell is ready its first frame; when one goes, the
     * whole animation from hand_start (0x3DA334). */
    bool hand_playing;
    uint32_t hand_start;
    uint8_t hand_element;              /* 0x3DA348 */
    /* Summoned monsters (0x47B33) and when they leave (+0x2A0). */
    uint32_t summoned[BS_MAGIC_SUMMONS];
    uint32_t summoned_until[BS_MAGIC_SUMMONS];
    uint32_t summon_count;
    bool teleport_screen;              /* Teleport was cast: the anchor screen (0x2B1FC) */
} bs_magic_state;

typedef enum bs_magic_event_kind {
    BS_MAGIC_SOUND,        /* value: a SPIRE.SND id at pos */
    BS_MAGIC_MESSAGE,      /* value: a game message; args fill its %d */
    BS_MAGIC_MONSTER_DIED, /* monster: killed by a spell ("%s dies...") */
    BS_MAGIC_PLAYER_HURT,  /* value: damage the player took */
} bs_magic_event_kind;

typedef struct bs_magic_event {
    bs_magic_event_kind kind;
    int32_t value;
    int32_t args[3];
    uint32_t monster;
    float pos[3];
} bs_magic_event;

#define BS_MAGIC_EVENTS 64
typedef struct bs_magic_events {
    bs_magic_event list[BS_MAGIC_EVENTS];
    uint32_t count;
} bs_magic_events;

/* What spells reach. */
typedef struct bs_magic_world {
    bs_character *character;           /* the player */
    const bs_player *player;           /* the player's body: feet */
    float aim_yaw, aim_pitch;          /* where the player's spells fly (radians, as bs_player) */
    const bs_collision_world *collision;
    bs_monsters *monsters;             /* NULL: none */
    const bs_monster_world *monster_world;
    bs_monster_events *monster_events; /* sounds and deaths of monsters hurt */
    const bs_spell *spells;            /* the table, BS_SPELL_COUNT, for traps and summoning */
    const bs_creature_tables *tables;
    const bs_nav_graph *nodes;         /* where summoned monsters appear; NULL: none */
    int level_number;
    uint32_t now_ms;
    bs_rng *rng;
} bs_magic_world;

/* Forgets the missiles, traps, bursts, the ready spell and summoned monsters (a level starts). */
void bs_magic_reset(bs_magic_state *m);

/* Whether the player may cast now: no spell ready and the hand still (0x2A960). */
bool bs_magic_can_cast(const bs_magic_state *m);

/*
 * The player casts a spell of the book (0x27AEB): not while ethereal
 * (message 507), nor without the spell points (80). On the caster it takes
 * effect; otherwise it is made ready. Returns whether it was cast.
 */
bool bs_magic_cast_player(bs_magic_state *m, const bs_magic_world *w, const bs_spell *s, bs_magic_events *ev);

/* A potion's spell on its drinker (0x296C4 from 0x5042C): free, with the table's settings. */
void bs_magic_cast_on_player(bs_magic_state *m, const bs_magic_world *w, const bs_spell *s, bs_magic_events *ev);

/* The player uses monster i with a touch spell ready (0x27CE5): it takes effect on it. Returns false if no
 * touch spell is ready. */
bool bs_magic_touch(bs_magic_state *m, const bs_magic_world *w, uint32_t monster, bs_magic_events *ev);

/* The attack button with a ranged spell ready (0x411F8): it flies where the player aims. Returns false if
 * none is ready. */
bool bs_magic_launch_ready(bs_magic_state *m, const bs_magic_world *w, bs_magic_events *ev);

/* Monster i casts spell n (1-31) with an element and delivery at the player (0x47645 → 0x27AEB). */
void bs_magic_cast_monster(bs_magic_state *m, const bs_magic_world *w, uint32_t monster, int spell, int element,
                           int delivery, bs_magic_events *ev);

/*
 * One frame: missiles fly and burst, traps wait, bursts play out, summoned
 * monsters leave, and the player's effects run (0x28F11): their flags,
 * pools and attribute changes, Continuous Damage and Poison every second,
 * the end of each.
 */
void bs_magic_update(bs_magic_state *m, const bs_magic_world *w, uint32_t dt_ms, bs_magic_events *ev);

/* The percentage of an element's damage that gets through (0x2AA5D): a roll against chance, changed by the
 * tolerances; flags: the target's (Resistance makes it 0). */
int32_t bs_element_percent(int element, uint32_t flags, uint8_t resists, uint8_t immune, uint8_t low,
                           uint8_t critical, int32_t chance, bs_rng *r);

/* The player stops being the target of a teleport: moves to the anchor or drops it (the anchor screen's
 * buttons, 0x2B22F). Returns the message shown. */
uint32_t bs_magic_anchor(bs_character *c, bs_player *body, bool drop);

#endif
