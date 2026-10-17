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
 * - Water (0x3FB10): with the feet 50 or more below the surface the player
 *   swims: no gravity, a fall into the water stops 50 below the surface
 *   (a splash), the speeds halve and the Swimming skill adds to the forward
 *   one, Page Up and Down rise to the surface and sink at 100 units/s, and
 *   carrying more than 1.5 x Strength sinks. A swimmer stands only on floors
 *   the feet reach; one blocked by a ledge is tried 35 units further on with
 *   the floor looked for from 110 above the feet, which climbs out.
 * - Riding (0x16C064, 0x16C214): a boat goes the way it heads, the balloon
 *   rises and sinks at 160 units/s with Page Up and Down; neither falls, and
 *   their own spheres (a boat's three along its length, the balloon's
 *   envelope and basket) stop all movement when they touch anything.
 *
 * Not yet: the slide-plane choice exactly as the game makes it.
 */

#define BS_PLAYER_EYE 72
#define BS_PLAYER_EYE_DUCKED 35

typedef struct bs_player_stats {
    int32_t speed;        /* Speed, the attribute at player record +0x38 (walking, jumping) */
    int32_t strength;     /* Strength, at +0x20 (jumping; carrying, see bs_items_move) */
    int32_t jump_skill;   /* the skill at +0x7E */
    int32_t swim_skill;   /* the skill at +0xA2: adds to the forward speed in water */
    bool deathmatch;      /* multiplayer levels move faster */
} bs_player_stats;

/* What the player rides (the game's 0x16C064, 0x16C214). */
typedef enum bs_player_ride { BS_RIDE_NONE, BS_RIDE_BOAT, BS_RIDE_BALLOON } bs_player_ride;

typedef struct bs_player_input {
    float forward;        /* -1..1 */
    float strafe;         /* -1..1, + to the right */
    bool jump;            /* held: charges a jump; released: jumps */
    bool duck;            /* the eye drops to 35 */
    bool sneak;           /* halves the walking speeds (the game's +0x226 bit 0x40, set by Sneak Mode) */
    bool run;             /* the player record flag +0x199: the "Running" spell's effect (the game has
                             no run key) */
    bool jumping;         /* +0x174 bit 0x40: the "Jumping" spell's effect, a jump's forward speed x 330/256 */
    bool slow_fall;       /* +0x173 bit 1: the "Slow Fall" spell's effect, falls at most 60 units/s */
    float walk_speed;     /* if > 0, the forward speed in units/s instead of the one from Speed (monsters walk
                             at their own, 0x45A2C) */
    bool up, down;        /* actions 21, 22 (Page Up, Page Down): swimming and the balloon */
    bool water;           /* the level has water (the game's 0x16C23C isn't 10000) */
    float water_y;        /* its surface (+y down) */
    float dive_max;       /* how far below the surface sinking stops (level 5: 256); 0 none */
    bool heavy;           /* carrying more than 3 x Strength in half units: no staying afloat (0x5493B) */
    bs_player_ride ride;
    float ride_heading;   /* a boat: the way it goes (radians, as yaw) */
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
    bool swimming;        /* the game's 0x16FFAF: the feet 50 or more below the water's surface */
} bs_player;

typedef struct bs_player_events {
    bool floor_changed;   /* floor_object changed: the game's step event */
    bool touch_changed;   /* touch_object changed: the game's bump event */
    bool jumped, landed;
    int32_t fall_speed;   /* on landing: the vertical speed (1/256 units/s) */
    bool splashed;        /* fell into the water (sound 86) */
    bool slow_fall;       /* Slow Fall began to hold the fall ("Slowfalling active.") */
    bool ride_blocked;    /* the boat or balloon touched something and didn't move */
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
