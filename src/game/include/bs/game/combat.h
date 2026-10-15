#ifndef BS_GAME_COMBAT_H
#define BS_GAME_COMBAT_H

#include <stdbool.h>
#include <stdint.h>

#include "bs/game/items.h"

/*
 * Melee combat, by the rules decoded from GAME.EXE (docs/formats/creatures.md,
 * "Combat"): one blow of an attacker at a defender (0x4241F), the same for
 * the player and monsters.
 *
 * - A hit location: 0 head .. 5 feet, the player's six armour places, picked
 *   with chances 11, 20, 40, 15, 10, 4 %.
 * - The damage: the weapon's roll (doubled for monsters; for the player
 *   scaled by the swing, or with bare hands plus hand-to-hand / 7), less what
 *   a shield absorbs, less the armour at the location.
 * - The chance to hit: (attack - defence) / 2 + 50, attack being the weapon
 *   skill + (Luck - 50) / 5 + Agility / 5 (scaled by the swing), defence the
 *   defender's skill with its own weapon + Luck / 10 + Agility / 5; less the
 *   defender's dodge skill / 5 %; kept within 5 .. 95.
 * - The player's critical blows (skill 20 / 7 + chance / 20) do triple the
 *   weapon's maximum or stun; fumbles (the top (100 - chance) / 20) hurt the
 *   player's weapon or balance.
 * - A hit then scales the damage by (weapon skill / 100)^2 x 2.5 and adds
 *   (Strength - 50) / 10, at least 1.
 */

#define BS_HIT_LOCATIONS 6

typedef struct bs_fighter {
    bool player;
    bool armed;                 /* the player has a weapon in hand (+0x260) */
    int32_t strength, agility, luck;   /* attributes +0x20, +0x2C, +0x3C */
    int32_t weapon_skill;       /* the skill its weapon uses, attacking and defending */
    int32_t dodge;              /* skill 6 (+0xBA): takes its fifth, in %, off the attacker's chance */
    int32_t critical;           /* skill 20 (+0x162): the player's critical blows */
    int32_t hand_to_hand;       /* the player's +0x132, without a weapon */
    int32_t damage_min, damage_max;
    int32_t weapon_bonus;       /* the player's weapon: its +0x33 - 1, added to a hit's damage */
    int32_t armour[BS_HIT_LOCATIONS];
    int32_t health, health_max;
    int32_t shield;             /* +0x16E: absorbs damage before the armour */
    bool wraith;                /* a Wraith's blows scale with its health */
    bool unhurt;                /* a level 5 Herne: no damage */
} bs_fighter;

typedef enum bs_melee_outcome {
    BS_MELEE_MISS,
    BS_MELEE_HIT,
    BS_MELEE_ABSORBED,          /* the shield took it all */
    BS_MELEE_FUMBLE,            /* the player fumbled (see message) */
} bs_melee_outcome;

typedef struct bs_melee {
    bs_melee_outcome outcome;
    int32_t damage;
    int32_t force;              /* 1, or 2 for a good blow: scales the push (0x3F7F80) */
    int location;
    uint32_t message;           /* a message for the player (BS_MSG_*), or 0 */
    bool stun;                  /* the defender skips its next two attacks (+0x284 = 2) */
    int32_t shield;             /* the defender's shield left */
} bs_melee;

/* skill_mult and damage_mult are in quarters: 4 is x1 (monsters always pass 4). */
bs_melee bs_melee_roll(const bs_fighter *att, const bs_fighter *def, int32_t skill_mult, int32_t damage_mult,
                       bs_rng *r);

/* The swing the player's mouse makes while the attack button is held (0x41296): the direction of
 * the last motion picks the skill and damage factors and the weapon animation. dx, dy: mickeys,
 * + right and down. Returns false for less than 100. */
typedef struct bs_swing {
    int direction;              /* 0..15, 0 pulling down */
    int32_t skill_mult, damage_mult;   /* quarters */
    uint8_t animation;          /* the weapon animation (0xDA53E): 5 .. 11 */
} bs_swing;
bool bs_swing_from_motion(int32_t dx, int32_t dy, bs_swing *out);

/* The push a blow gives (0x439B8): force (capped at 400) against mass. Returns the speed in units per
 * 50 ms tick and its duration in ms, or 0 for none. */
int32_t bs_knockback(int32_t force, int32_t mass, int32_t distance, uint32_t *duration_ms);

/* The sound of a blow that lands (0x43E0C): by the player's weapon (0..14, 15 bare hands) or the
 * attacking creature (0-based). */
int32_t bs_hit_sound(bool player, uint32_t weapon_or_creature);

/* The sound of a miss (0x43E6A), by the defending creature (the player counts as 0). */
int32_t bs_miss_sound(uint32_t defender_creature, bs_rng *r);

#endif
