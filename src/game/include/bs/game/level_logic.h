#ifndef BS_GAME_LEVEL_LOGIC_H
#define BS_GAME_LEVEL_LOGIC_H

#include <stdbool.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"
#include "bs/formats/bs6.h"

/*
 * A level's moving parts, run as GAME.EXE runs its BS6 CTRL and LINK chunks
 * (docs/re/README.md, "Level logic"):
 *
 * - An object is activated with an event (use, step onto, bump into). If one
 *   of its controllers is a trigger for that event, its whole controller
 *   chain starts, and then, unless the chain has a timer, every object it
 *   links to starts too.
 * - Each frame, running chains advance: moves and rotations are linear over
 *   their time; option "r" makes the next run go back, "c" repeats for ever;
 *   timers start their linked objects when their period has passed.
 * - Objects that a teleport link points at move the player instead: the
 *   player is put at the object's position and its move carries them on.
 *
 * Times are milliseconds on a clock the caller supplies.
 */

enum {
    BS_LOGIC_EVENT_BUMP = 0,    /* touched from the side */
    BS_LOGIC_EVENT_STEP = 5,    /* stepped onto */
    BS_LOGIC_EVENT_USE = 16,    /* used */
};

enum {
    BS_LOGIC_CTRL_TIMER = 0x95,  /* 149 */
    BS_LOGIC_CTRL_MOVE = 0x105,  /* 261 */
    BS_LOGIC_CTRL_ROTATE = 0x200 /* 512 */
};

typedef enum bs_logic_kind { BS_LOGIC_OBJECT, BS_LOGIC_FLAT, BS_LOGIC_LIGHT } bs_logic_kind;

typedef struct bs_logic_ctrl {
    uint32_t type;           /* event number for triggers, or BS_LOGIC_CTRL_* */
    int32_t v[6];            /* the data's first six i32 */
    bool reversible;         /* option "r" */
    bool continuous;         /* option "c" */
    int32_t sound;           /* SPIRE.SND id played when it starts, or -1 */
    /* state */
    bool running, reversed;
    uint32_t start_ms;
    int32_t start[3];        /* position or angles when the chain started */
} bs_logic_ctrl;

typedef struct bs_logic_object {
    bs_logic_kind kind;
    uint32_t index;          /* into the level's objects, flats or lights */
    uint32_t entity_id;
    float pos[3];            /* current position (level units, +y down) */
    int32_t angles[3];       /* current angles (2048 per turn) */
    float delta[3];          /* movement in the last update, for carrying what stands on it */
    uint32_t first_ctrl, ctrl_count;
    uint32_t first_link, link_count;
    bool teleport_target;    /* a type-2 link points here: its controllers move the player */
    bool active;
    bool moved;              /* has moved or turned since the level was built */
} bs_logic_object;

/* What happened in a call, for the caller to act on. */
#define BS_LOGIC_MAX_SOUNDS 16
typedef struct bs_logic_output {
    uint32_t sound_count;
    struct {
        int32_t id;          /* SPIRE.SND */
        uint32_t object;     /* where it plays */
    } sounds[BS_LOGIC_MAX_SOUNDS];
    bool player_moved;       /* the player was teleported or carried by a teleport move */
} bs_logic_output;

typedef struct bs_level_logic {
    bs_logic_object *objects;    /* every object, flat and light, in that order */
    uint32_t object_count;
    bs_logic_ctrl *ctrls;
    uint32_t ctrl_count;
    uint32_t *links;             /* object indices */
    uint32_t link_count;
    uint32_t *active;            /* objects with a running chain, in start order */
    uint32_t active_count;
    uint32_t now_ms;
    int32_t target[3];           /* absolute move target set by triggers; 0 = none (per axis) */
    float player[3];             /* the player's position: set before calls, read after */
} bs_level_logic;

/* Builds the logic for a parsed level. Links to missing entities are ignored. */
bs_result bs_level_logic_build(const bs_bs6_level *level, bs_arena *arena, bs_level_logic *out);

/* The logic object of an entity kind and index, or UINT32_MAX. */
uint32_t bs_level_logic_find(const bs_level_logic *lg, bs_logic_kind kind, uint32_t index);

/* Activates an object with an event (BS_LOGIC_EVENT_*) at time now_ms. */
void bs_level_logic_activate(bs_level_logic *lg, uint32_t object, uint32_t event, uint32_t now_ms,
                             bs_logic_output *out);

/* Advances every running chain to now_ms. Clears and refills each object's delta. */
void bs_level_logic_update(bs_level_logic *lg, uint32_t now_ms, bs_logic_output *out);

/* Whether an object can react to anything (has controllers or links). */
static inline bool bs_level_logic_interactive(const bs_level_logic *lg, uint32_t object)
{
    return object < lg->object_count && (lg->objects[object].ctrl_count || lg->objects[object].link_count);
}

#endif
