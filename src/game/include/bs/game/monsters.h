#ifndef BS_GAME_MONSTERS_H
#define BS_GAME_MONSTERS_H

#include <stdbool.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/formats/bs6.h"
#include "bs/formats/creatures.h"
#include "bs/formats/nodes.h"
#include "bs/formats/spells.h"
#include "bs/game/collision.h"
#include "bs/game/combat.h"
#include "bs/game/player.h"

/*
 * Monsters, by the rules decoded from GAME.EXE (docs/formats/creatures.md):
 *
 * - Spawning (0x461BE): flats named monster1 / monster2 are spawn points. One
 *   with a creature record (STRU 106) gets that creature, with the record's
 *   overrides; one without gets a creature from the level number's four
 *   (0xE80A1) when a roll of 0..100 is within the LEVELS.TXT monster
 *   percentage, or always when it has a dialogue code (108) or loot (116).
 * - Every frame the monsters within the level's activity distance
 *   (LEVELS.TXT field 9, 0x16C1A8) think (0x44F0B → 0x43ECF): an AI state machine
 *   chasing the player, attacking within 90 units, finding the way over the
 *   level's navigation nodes when walking straight doesn't get there.
 * - They walk at Speed x 435 / 256 + 160 units/s straight at their goal,
 *   turning 128 angle units a tick, through the player's collision.
 * - Their sprites show one of five view angles (mirrored for the other three)
 *   of the current animation: walk, attack, stand (back and forth), or the
 *   first frame of the death image while knocked back.
 *
 * Not decoded exactly: the node search and path following (0x1A660,
 * 0x1B6F6, 0x1B266) are replaced by a shortest path over the node links;
 * line of sight is a ray between eye heights.
 * - Casters (0x47645) out of reach stand and cast every (100 - Intelligence)
 *   x 10 ms a quarter of the time while they have the spell points, then
 *   come to fight (BS_MONSTER_CAST; bs/game/magic.h casts the spell).
 *
 * Not yet: dialogue (NPCs that wait to talk only greet), monsters fighting
 * each other (summoned ones stand by the player).
 */

/* AI states (the monster's +0x232). */
typedef enum bs_ai_state {
    BS_AI_ATTACK = 0,      /* in reach: attack */
    BS_AI_PATH = 1,        /* find a way over the nodes */
    BS_AI_TO_NODE = 2,     /* walk to the next node */
    BS_AI_FLEE = 4,        /* run to a node far away (0x42E2F: weaker than the attacker) */
    BS_AI_FLEE_PATH = 6,
    BS_AI_APPROACH = 8,    /* walk straight at the player */
    BS_AI_IDLE = 9,
    BS_AI_TALK = 13,       /* waits to talk: turns to the player, doesn't attack until hurt */
    BS_AI_WANDER = 14,     /* pick a random direction ... */
    BS_AI_WANDERING = 15,  /* ... and walk it for 3 s */
} bs_ai_state;

/* Animations (0x47143). */
typedef enum bs_monster_anim {
    BS_ANIM_WALK = 0,
    BS_ANIM_ATTACK = 1,
    BS_ANIM_KNOCKED = 2,   /* the death image's first frame, held */
    BS_ANIM_STAND = 8,     /* plays back and forth */
    BS_ANIM_DEATH = 9,
} bs_monster_anim;

/* A creature image's animation, from its BSI header. */
typedef struct bs_anim_info {
    uint32_t frames;
    uint32_t frame_ms;
} bs_anim_info;

typedef struct bs_spawn_point {
    uint32_t flat;
    float pos[3];
    bool has_record;
    int32_t record[13];    /* STRU 106: creature (1-based), overrides, state, flags, spells */
    char code[4];          /* STRU 108: dialogue code, "" if none */
    uint32_t loot;         /* STRU 116 records: the loot it carries */
} bs_spawn_point;

typedef struct bs_monster {
    uint32_t type;                   /* 0-based creature */
    uint32_t spawn;                  /* spawn point */
    bs_player body;                  /* feet, falling; moved like the player's */
    int32_t angle;                   /* facing (+0x1), 0..2047, 0 = +z, 512 = +x */
    int32_t attributes[8];
    int32_t health, health_max, spell_points, spell_points_max;
    int32_t skill;                   /* every skill */
    uint32_t flags;                  /* +0x172 */
    int32_t spells[5];               /* from the spawn record: cast instead of the creature's lists */
    int32_t pools[3];                /* +0x2A4.. spell pools (BS_POOL_*): 150 each (0x46BD2) */
    bool cast_stop;                  /* +0x227 bit 1: gave up casting at a reflecting target */
    bool summoned;                   /* +0x226 bit 0x10 */
    bool ally;                       /* summoned by the player: on its team, so it leaves the player be */
    bool unhurt;                     /* a level 5 Herne */
    bool has_code;                   /* a dialogue code: greets */
    bs_ai_state state;
    bs_monster_anim anim;            /* +0x299 */
    uint32_t anim_start;             /* +0x252 */
    uint32_t frame;                  /* +0x21 */
    bool anim_done;                  /* +0x14 bit 0x80: played through once */
    bool struck;                     /* +0x14 bit 0x40: this attack has hit */
    uint32_t last_attack;            /* +0x256; also when wandering began */
    int32_t skip_attacks;            /* +0x284: stunned */
    int32_t delay;                   /* +0x25F: frames to wait (> 0), or readiness (< 0) */
    bool aware;                      /* +0x226 bit 4: the player within 1024 and in sight */
    bool sidestep;                   /* +0x226 bit 0x20: walking 90 degrees off after bumping into something */
    uint32_t sidestep_start;
    bool fled;                       /* +0x172 bit 0x20: has fled once at low health */
    bool force_path;                 /* +0x236 == 1: walking straight failed */
    bool greeted;                    /* +0x13 bit 0x40 */
    bool has_goal;
    float goal[3];                   /* +0x20E */
    float progress_best;             /* the nearest to the goal lately, and since when (an addition: */
    uint32_t progress_ms;            /* sliding along a wall without getting nearer counts as blocked) */
    int32_t push_angle, push_speed;  /* knocked back (0x439B8): units per 50 ms ... */
    uint32_t push_start, push_end;   /* ... from push_start, fading out until push_end */
    uint16_t path[32];               /* nodes to walk, path[path_at] next */
    uint32_t path_len, path_at;
    uint32_t path_target;            /* the node the path leads to */
    uint32_t next_plan;              /* not before: planning a path again */
    bool active;                     /* within the activity distance this frame */
    bool dead;                       /* playing the death image */
    bool gone;                       /* removed */
    float tick;                      /* ms left over for 50 ms ticks */
} bs_monster;

typedef struct bs_monsters {
    bs_monster *list;
    uint32_t count, cap;
    bs_spawn_point *points;
    uint32_t point_count;
    /* Path search scratch, per node. */
    float *dist;
    uint32_t *prev;
    uint32_t *heap;
    uint32_t scratch_count;
    uint32_t player_node;            /* the node nearest the player, UINT32_MAX if none */
    uint32_t player_node_ms;         /* when it was found */
} bs_monsters;

/* What the monsters see of the world for an update. */
typedef struct bs_monster_world {
    const bs_creature_tables *tables;
    const bs_anim_info (*anims)[BS_CREATURE_IMAGES];   /* per creature; frames 0 if missing */
    const bs_collision_world *collision;
    const bs_nav_graph *nodes;                          /* NULL: walking straight only */
    int level_number;
    int32_t active_distance;                            /* LEVELS.TXT active_distance */
    const bs_player *player;
    bool player_dead;
    bs_rng *rng;
    uint32_t now_ms;
    const bs_spell *spells;                             /* the spell table (BS_SPELL_COUNT); NULL: no casting */
    uint32_t player_flags;                              /* the player's BS_FLAG_* (invisible, reflecting) */
    bool player_can_talk;                               /* nothing in the way of a conversation: no swing
                                                           under way, no screen open (0x495C4) */
    bool water;                                         /* the level's water: monsters float in it too */
    float water_y;                                      /* (0x3FB10) */
} bs_monster_world;

typedef enum bs_monster_event_kind {
    BS_MONSTER_SOUND,      /* sound: a SPIRE.SND id at the monster */
    BS_MONSTER_STRIKE,     /* the monster's attack reaches the player's turn to be hit */
    BS_MONSTER_GREET,      /* message: BS_MSG_GREETING + n, with the creature's name */
    BS_MONSTER_GONE,       /* the death image finished: drop the loot here */
    BS_MONSTER_CAST,       /* value: spell | element << 8 | delivery << 16, at the player (paid for) */
    BS_MONSTER_TALK,       /* the greeting starts a conversation */
} bs_monster_event_kind;

typedef struct bs_monster_event {
    bs_monster_event_kind kind;
    uint32_t monster;
    int32_t value;         /* sound id or message number */
} bs_monster_event;

#define BS_MONSTER_EVENTS 64

typedef struct bs_monster_events {
    bs_monster_event list[BS_MONSTER_EVENTS];
    uint32_t count;
} bs_monster_events;

void bs_monsters_free(bs_monsters *ms);

/* The level's spawn points: flats named monster1* / monster2* (0x25841), with their records. */
bs_result bs_monsters_find_points(bs_monsters *ms, const bs_bs6_level *level);

/*
 * Spawns the monsters at the points (0x461BE). percent: LEVELS.TXT's monster
 * percentage. Every monster stands on the floor found under its point.
 */
bs_result bs_monsters_spawn(bs_monsters *ms, const bs_creature_tables *t, const bs_collision_world *w,
                            int level_number, int32_t percent, bs_rng *r);

/* A new monster of creature `type` standing on the floor under pos (as the spawner makes them), or NULL. */
bs_monster *bs_monsters_add(bs_monsters *ms, const bs_creature_tables *t, uint32_t type, const float pos[3],
                            const bs_collision_world *w);

/* One frame of dt_ms for every monster. */
void bs_monsters_update(bs_monsters *ms, const bs_monster_world *w, uint32_t dt_ms, bs_monster_events *ev);

/* The monster as a fighter (for bs_melee_roll). */
void bs_monster_fighter(const bs_monster *m, const bs_creature *c, bs_fighter *out);

/*
 * A blow lands on monster i (0x42E2F): the attacker (at `from`, with its health) may
 * make a much weaker monster flee; the damage is taken; force pushes it back. Returns
 * true if it died (its death image starts).
 */
bool bs_monster_hurt(bs_monsters *ms, uint32_t i, const bs_monster_world *w, int32_t damage, int32_t force,
                     const float from[3], int32_t attacker_health, bs_monster_events *ev);

/* The image slot and frame to draw monster m with as seen from eye, and whether it's mirrored. */
uint32_t bs_monster_image(const bs_monster *m, const float eye[3], bool *mirror);

/* The game's angle from a to b (0x1E711): 0 = +z, 512 = +x, 0..2047. */
int32_t bs_angle_to(float ax, float az, float bx, float bz);

#endif
