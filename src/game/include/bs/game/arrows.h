#ifndef BS_GAME_ARROWS_H
#define BS_GAME_ARROWS_H

#include <stdbool.h>
#include <stdint.h>

#include "bs/game/collision.h"
#include "bs/game/items.h"
#include "bs/game/monsters.h"

/*
 * Arrows the player shoots (GAME.EXE 0x4A4E1, 0x4A127; docs/re/README.md, "Arrows"). One arrow
 * leaves the arrows ready and flies from where it was nocked, straight (no drop) at 1600 units a
 * second, its model turned as it was on the bow:
 *
 * - each frame it is tested against the level along its move (30 units higher, 0x2B7E3): a wall
 *   takes it;
 * - ten points along the move are tested for a creature within 80 units (0x2B502): the first
 *   one hit takes the damage, and half the time the arrow sticks in it (carried, so dropped with
 *   its loot), otherwise it's gone;
 * - more than 3072 units from the player it's gone.
 *
 * The damage (0x4A38A) is the bow's roll, scaled by the Missile skill, plus the bow's and the
 * arrow's materials, plus Strength for the bows (not the crossbow), less the armour where it hits.
 * There's no roll to hit.
 */

#define BS_ARROWS 16
#define BS_ARROW_SPEED 1600      /* units a second (0x4A1C5) */
#define BS_ARROW_RANGE 3072.0f   /* from the player (0x4A191) */
#define BS_ARROW_REACH 80.0f     /* a creature this near a point of the move is hit (0x4A262) */
#define BS_ARROW_HIT_SOUND 0x1B0 /* at the creature hit (0x4A30C) */
#define BS_ARROW_SHOOT_SOUND 0x19E

typedef struct bs_arrow {
    bool live;
    bs_item item;            /* the arrow, one */
    float pos[3];            /* +y down */
    float dir[3];            /* unit */
    float rot[9];            /* the model's rotation, as it was nocked */
} bs_arrow;

typedef struct bs_arrows {
    bs_arrow list[BS_ARROWS];
} bs_arrows;

/* What hit what in a frame. */
typedef struct bs_arrow_hit {
    uint32_t monster;
    bs_item item;            /* the arrow */
    bool stuck;              /* it sticks in the monster */
} bs_arrow_hit;

#define BS_ARROW_HITS 8
typedef struct bs_arrow_hits {
    bs_arrow_hit list[BS_ARROW_HITS];
    uint32_t count;
} bs_arrow_hits;

/* The shooter's numbers for the damage. */
typedef struct bs_arrow_shot {
    uint32_t bow;            /* base item 8, 9 or 10 */
    int32_t damage_min, damage_max;  /* the bow's roll (its weapon stats) */
    int32_t skill;           /* Missile Weapons (player +0x156) */
    int32_t bow_material;
    int32_t strength;
} bs_arrow_shot;

void bs_arrows_clear(bs_arrows *a);

/*
 * Looses an arrow from pos with the model rotation rot, towards the view's pitch and yaw (2048 a
 * turn, pitch + down): the direction is (sin yaw, sin pitch, cos yaw) made unit, as 0x1B9A0 and
 * 0x1E7A2 make it. Returns false if all arrows are in the air (the oldest is not replaced).
 */
bool bs_arrows_launch(bs_arrows *a, const bs_item *arrow, const float pos[3], int32_t pitch, int32_t yaw,
                      const float rot[9]);

/* One frame of dt_ms: the arrows fly; hits go in out. Only monsters that act are hit. */
void bs_arrows_update(bs_arrows *a, uint32_t dt_ms, const bs_collision_world *w, const bs_monsters *ms,
                      const float player_pos[3], bs_rng *r, bs_arrow_hits *out);

/* An arrow's damage (0x4A38A) to a creature with `armour` where it was hit. */
int32_t bs_arrow_damage(const bs_arrow_shot *s, const bs_item *arrow, int32_t armour, bs_rng *r);

#endif
