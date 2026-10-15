#ifndef BS_GAME_HAND_H
#define BS_GAME_HAND_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The weapon in the player's hand (GAME.EXE 0x48A6A .. 0x4975B; docs/re/README.md, "The weapon in
 * hand"). A wielded weapon is a 3D model (`edagger.3d` ..., table 0xE34AD by base item) carried
 * by the body: it turns with the player's yaw but not with the view's pitch. Its pose runs through
 * keyframed states (0xEF7D4): each lasts a time and moves the model along sine curves and its
 * three angles linearly from a start to an end. Bare hands are a 2D sprite instead (punch0.bsi,
 * in the app).
 *
 *  0 idle, 1 walking (a bob), 2 raised (on equipping), 3 lowered, 4 a wind-up before a swing,
 *  5 and 7-11 the swings, 6 the recovery after one, 12-14 a bow drawn, held and loosed.
 *
 * A swing's blow lands half-way through its swing state (0x49053).
 */

enum {
    BS_HAND_IDLE = 0,
    BS_HAND_WALK = 1,
    BS_HAND_RAISE = 2,
    BS_HAND_LOWER = 3,
    BS_HAND_WINDUP = 4,
    BS_HAND_RECOVER = 6,
    BS_HAND_DRAW = 12,
    BS_HAND_LOOSE = 13,
    BS_HAND_HOLD = 14,
    BS_HAND_STATES = 15,
};

typedef struct bs_hand {
    int weapon;                       /* base item 0..14 in hand, -1 none (no model shown) */
    int state;
    uint32_t start;                   /* the level clock when the state began */
    bool cut;                         /* the state ends at the next update (0x495A9) */
    int pending;                      /* the state to go to when this one ends, -1 none (0x3F81C0 - 1) */
    int32_t extra_ms;                 /* added to the state's time (0x3F81CC) */
    bool struck;                      /* past the half-way point (0x3F81D0) */
    int32_t swing_ms[BS_HAND_STATES]; /* the swings' times, set per swing (0x4957F) */
    int windup_next;                  /* the swing the wind-up leads to (0xEF498) */
} bs_hand;

/* The model's placement relative to the feet, in the body's frame (+x right, +y down, +z ahead), and
 * its angles (2048 a turn): pitch, yaw, roll. */
typedef struct bs_hand_pose {
    float offset[3];
    int32_t angles[3];
} bs_hand_pose;

/* Nothing in hand. */
void bs_hand_reset(bs_hand *h);

/* A weapon (base item 0..14) comes into the hand and is raised (0x48DF3); -1 empties it at once. */
void bs_hand_equip(bs_hand *h, int weapon, uint32_t now_ms);

/* Whether a swing can start: idle, walking or recovering (0x49521). */
bool bs_hand_can_swing(const bs_hand *h);

/* Starts a swing (animation 5 or 7..11 from the swing's direction) taking swing_ms, after the
 * wind-up (0x49545). Returns false if the hand isn't ready. */
bool bs_hand_swing(bs_hand *h, int animation, int32_t swing_ms);

/* One frame: moving picks the walking bob; drawing holds a bow drawn. Returns true when a swing's
 * blow lands (0x48E88). */
bool bs_hand_update(bs_hand *h, uint32_t now_ms, bool moving, bool drawing);

/* The current pose; false with nothing in hand. */
bool bs_hand_pose_at(const bs_hand *h, uint32_t now_ms, bs_hand_pose *out);

/*
 * A swing's time in ms (0x4963F): the animation's base time (0xDA58E) x 62.5, longer for a heavier
 * weapon (stats field 0) and shorter with more of skill + Strength / 5 + Speed / 5.
 */
int32_t bs_swing_time(int animation, int32_t weapon_weight, int32_t skill, int32_t strength, int32_t speed);

/* The model's rotation (row-major) for a pose with the body facing yaw (2048 a turn), as 0x49303
 * builds it: the body's yaw, then the pose's yaw, pitch and roll. */
void bs_hand_rotation(int32_t body_yaw, const int32_t angles[3], float m[9]);

/* The pose's offset turned by the body's yaw (2048 a turn): add it to the feet. */
void bs_hand_offset(int32_t body_yaw, const float offset[3], float out[3]);

#endif
