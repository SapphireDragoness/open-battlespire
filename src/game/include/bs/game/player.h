#ifndef BS_GAME_PLAYER_H
#define BS_GAME_PLAYER_H

#include <stdbool.h>
#include <stdint.h>

#include "bs/game/collision.h"

/*
 * The player's movement and collision, by the rules decoded from GAME.EXE
 * (docs/re/README.md, "Player movement and collision"):
 *
 * - Position is the feet (+y down); the eye is 72 above (35 ducking).
 * - The body is three spheres above the feet (34/7, 41/11, 65/16): anything
 *   lower than 27 is a step. Walls stop the body; it slides along them.
 * - A probe from 32 above the feet to 200 below finds the floor; a floor less
 *   than 25 below (or above) is stood on.
 * - Where the probe finds nothing to stand on, a sphere of radius 16 at the
 *   feet that still touches something keeps the player from falling (plank
 *   bridges, ledges).
 * - Gravity 392 units/s^2, falling at most 1000 units/s; walking speeds come
 *   from the character's attributes (forward, backward and sideways differ).
 * - Jumps are charged by holding the key (up to 2 s standing, 1 s moving):
 *   the forward speed grows with the time held; the jump happens on release.
 *
 * Not yet: swimming, levitation, the slide-plane choice exactly as the game
 * makes it.
 */

#define BS_PLAYER_EYE 72
#define BS_PLAYER_EYE_DUCKED 35

typedef struct bs_player_stats {
    int32_t speed;        /* the attribute at player record +0x38 (walking, jumping) */
    int32_t attr_20;      /* the attribute at +0x20 (jumping) */
    int32_t jump_skill;   /* the skill at +0x7E */
    bool deathmatch;      /* multiplayer levels move faster */
} bs_player_stats;

typedef struct bs_player_input {
    float forward;        /* -1..1 */
    float strafe;         /* -1..1, + to the right */
    bool jump;            /* held: charges a jump; released: jumps */
    bool duck;            /* the eye drops to 35 */
    bool sneak;           /* halves the walking speeds (the game's +0x226 bit 0x40, set by Sneak Mode) */
    bool run;             /* the player record flag +0x199: very likely the unused "Running"
                             spell's effect (the game has no run key); nothing sets it */
} bs_player_input;

typedef struct bs_player {
    float pos[3];         /* feet */
    float yaw, pitch;     /* radians; 0 faces +z, + yaw turns towards +x; + pitch looks up */
    int32_t vy;           /* vertical speed, 1/256 units/s, + down */
    float eye;            /* eye height above the feet */
    bool on_ground;
    bool jumping;         /* in the air from a jump: keeps the jump's forward speed */
    bool charging;        /* the jump key is held on the ground */
    uint32_t charge_ms;
    bool moved;           /* moved horizontally in the last frame (the game's 0x16FFAE) */
    float jump_distance;  /* while charging: the predicted distance, for the pointer */
    float jump_speed, jump_yaw;
    int32_t floor_object; /* level object stood on, or -1 */
    int32_t touch_object; /* level object the body touches from the side, or -1 */
} bs_player;

typedef struct bs_player_events {
    bool floor_changed;   /* floor_object changed: the game's step event */
    bool touch_changed;   /* touch_object changed: the game's bump event */
    bool jumped, landed;
    int32_t fall_speed;   /* on landing: the vertical speed (1/256 units/s) */
} bs_player_events;

void bs_player_init(bs_player *p, const float feet[3], float yaw);

/* Moves by one frame of dt_ms milliseconds. */
void bs_player_move(bs_player *p, const bs_player_stats *s, const bs_collision_world *w, const bs_player_input *in,
                    uint32_t dt_ms, bs_player_events *ev);

/* The camera position (eye). */
void bs_player_eye(const bs_player *p, float out[3]);

/* While charging, where the game shows the jump pointer: jump_distance ahead
 * of the feet in the facing direction, 50 units up. Returns false otherwise. */
bool bs_player_jump_target(const bs_player *p, float out[3]);

#endif
