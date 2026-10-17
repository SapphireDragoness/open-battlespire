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
 *  0 idle, 1 walking (a bob), 2 raised (on equipping), 3 lowered (to switch weapons), 4 a wind-up
 *  before a swing, 5 and 7-11 the swings, 6 the recovery after one, 12-14 pulled back, held back
 *  and pushed out again while a wall is right ahead (0x16C028, tested at 0x405EB).
 *
 * A swing's blow lands half-way through its swing state (0x49053).
 *
 * Bows and the crossbow don't use the states (0x49A92): the bow is held at the eye, tilted with
 * the view, an arrow nocked. A press of the attack button with the bow at rest looses the arrow
 * (0x49C39); the bow then turns aside and back while the next one is nocked (0x49DD0), and turns
 * aside while a wall is right ahead.
 */

enum {
    BS_HAND_IDLE = 0,
    BS_HAND_WALK = 1,
    BS_HAND_RAISE = 2,
    BS_HAND_LOWER = 3,
    BS_HAND_WINDUP = 4,
    BS_HAND_RECOVER = 6,
    BS_HAND_PULL_BACK = 12,
    BS_HAND_PUSH_OUT = 13,
    BS_HAND_HELD_BACK = 14,
    BS_HAND_STATES = 15,
};

/* What a frame of the hand reports (bs_hand_update). */
enum {
    BS_HAND_STRUCK = 1, /* a swing's blow lands (0x49053) */
    BS_HAND_SWITCH = 2, /* lowered to switch weapons: swap them now (0x48F05 -> 0x48C9F) */
    BS_HAND_SHOOT = 4,  /* a bow looses its arrow (0x49C39) */
};

/* How a switch to the secondary weapon goes (0x48BE6). */
typedef enum bs_hand_switch {
    BS_SWITCH_NOW,       /* swap at once */
    BS_SWITCH_LOWERING,  /* the weapon in hand is lowered first: BS_HAND_SWITCH when it's down */
    BS_SWITCH_NO_ARROWS, /* the secondary is a bow and no arrows are ready */
} bs_hand_switch;

/* The input of a frame. */
typedef struct bs_hand_input {
    bool moving;          /* the player moved (0x16FFAE): the walking bob */
    bool blocked;         /* a wall within 90 units ahead (0x16C028) */
    bool attack_pressed;  /* the attack button went down this frame */
    bool ethereal;        /* the player can't shoot (player +0x175 bit 0) */
    int32_t speed;        /* the Speed attribute: how fast a bow turns aside to nock */
} bs_hand_input;

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
    uint32_t last_ms;                 /* the last update */
    /* A bow (0x49A92): extra_ms is the wait before it shows. */
    bool bow;
    int32_t bow_angle;                /* its turn aside (0xEF820): 0 at rest, 0x538 aside */
    int bow_turn;                     /* -1 turning aside, 1 back, 0 still (0x3F81D4) */
    bool loosed;                      /* an arrow just flew; the next isn't nocked yet (0x3F81C8) */
    int32_t bow_rest;                 /* the remainder of the turning's division */
} bs_hand;

/* The model's placement relative to the feet, in the body's frame (+x right, +y down, +z ahead), and
 * its angles (2048 a turn): pitch, yaw, roll. */
typedef struct bs_hand_pose {
    float offset[3];
    int32_t angles[3];
} bs_hand_pose;

/* Nothing in hand. */
void bs_hand_reset(bs_hand *h);

/* A weapon (base item 0..14) comes into the hand and is raised (0x48DF3); -1 empties it at once.
 * wait_ms (0x49738) lengthens the raising, or for a bow is how long before it shows. */
void bs_hand_equip(bs_hand *h, int weapon, uint32_t now_ms, int32_t wait_ms);

/* The secondary weapon (base item, -1 none) is wanted in hand (0x48BE6; the A key). */
bs_hand_switch bs_hand_request_switch(bs_hand *h, int secondary, bool arrows_ready, uint32_t now_ms);

/* Whether a swing can start: idle, walking or recovering (0x49521). */
bool bs_hand_can_swing(const bs_hand *h);

/* Starts a swing (animation 5 or 7..11 from the swing's direction) taking swing_ms, after the
 * wind-up (0x49545). Returns false if the hand isn't ready. */
bool bs_hand_swing(bs_hand *h, int animation, int32_t swing_ms);

/* One frame (0x48E88, or 0x49A92 for a bow). Returns BS_HAND_* bits. */
int bs_hand_update(bs_hand *h, uint32_t now_ms, const bs_hand_input *in);

/* The current pose; false with nothing in hand (or a bow not shown yet). */
bool bs_hand_pose_at(const bs_hand *h, uint32_t now_ms, bs_hand_pose *out);

/* A bow's pose and its nocked arrow's (0x49A92), the view pitched by pitch (2048 a turn, + looking
 * down as the game keeps it). Returns false if the bow isn't shown; *arrow says whether the arrow is.
 * The bow's rotation is bs_hand_bow_rotation's, the arrow's bs_hand_rotation's. */
bool bs_hand_bow_pose(const bs_hand *h, int32_t pitch, bs_hand_pose *bow, bool *arrow);

/*
 * A swing's time in ms (0x4963F): the animation's base time (0xDA58E) x 62.5, longer for a heavier
 * weapon (stats field 0) and shorter with more of skill + Strength / 5 + Speed / 5.
 */
int32_t bs_swing_time(int animation, int32_t weapon_weight, int32_t skill, int32_t strength, int32_t speed);

/* The model's rotation (row-major) for a pose with the body facing yaw (2048 a turn), as 0x49303
 * builds it: the body's yaw, then the pose's yaw, pitch and roll. */
void bs_hand_rotation(int32_t body_yaw, const int32_t angles[3], float m[9]);

/* A bow's rotation (0x49F7E): the body's yaw, then the pose's pitch and yaw. */
void bs_hand_bow_rotation(int32_t body_yaw, const int32_t angles[3], float m[9]);

/* The pose's offset turned by the body's yaw (2048 a turn): add it to the feet. */
void bs_hand_offset(int32_t body_yaw, const float offset[3], float out[3]);

#endif
