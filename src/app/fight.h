#ifndef OPENBATTLESPIRE_FIGHT_H
#define OPENBATTLESPIRE_FIGHT_H

#include <stdbool.h>
#include <stdint.h>

#include "bs/assets/assets.h"
#include "bs/audio/level_audio.h"
#include "bs/formats/creatures.h"
#include "bs/formats/exe_text.h"
#include "bs/formats/levels.h"
#include "bs/formats/nodes.h"
#include "bs/game/combat.h"
#include "bs/game/hand.h"
#include "bs/game/items.h"
#include "bs/game/magic.h"
#include "bs/game/monsters.h"
#include "bs/game/use.h"
#include "bs/render/level_renderer.h"
#include "bs/render/overlay.h"

/*
 * The monsters of a level and the fighting (docs/formats/creatures.md): spawns
 * them when a level loads, runs them every frame, draws their sprites, and
 * plays out blows both ways:
 *
 * - The player swings by holding the attack button and moving the mouse
 *   (0x41296): the direction of the motion picks the swing (bs_swing). A
 *   weapon in hand is a 3D model whose swing runs through its keyframed
 *   states (bs/game/hand.h): the blow lands half-way through, on every
 *   monster within 90 units and 200 angle units of the facing, in sight
 *   (0x4975B). Bare hands punch: the punch0.bsi sprite, the blow after its
 *   fifth frame (0x2A892).
 * - A monster's blow is rolled when its attack animation reaches frame 4.
 * - A dead monster drops its loot (its spawn point's STRU 116) where it fell.
 * - The player dies at 0 health: 2 s later the captivity ending plays.
 * - Spells (bs/game/magic.h): the player's from the spell book, its hot
 *   keys and potions, the monsters' at the player; their missiles and bursts
 *   drawn as sprites; the casting hand and the effects' icons over the view.
 *
 * Not yet: arrows (a bow is only drawn and loosed), shoving, switching to the
 * secondary weapon.
 */

typedef struct fight_env {
    bs_assets *assets;
    bs_vfs *vfs;
    bs_level_audio *audio;
    const bs_exe_text *text;
    bs_items *items;
    const bs_item_db *db;
    bs_character *character;
    bs_player *player;
    const float *listener;           /* the camera, for sounds */
    float listener_yaw;
    float aim_yaw, aim_pitch;        /* where the player's spells fly: the view */
    const bs_spell *spells;          /* the spell table (BS_SPELL_COUNT), or NULL */
    const bs_collision_world *collision;
    const bs_bs6_level *level;
    bs_rng *rng;
    uint32_t now_ms;                 /* the level clock */
    int level_number;
    bool multiplayer;
    int32_t active_distance;
    int32_t grid_cell;
    void (*message)(void *ctx, const char *text);
    void *ctx;
} fight_env;

typedef struct fight {
    bs_creature_tables tables;
    bool have_tables;
    bool monsters_on;
    bs_monsters monsters;
    bs_nav_graph nodes;
    bool have_nodes;
    const bs_bsi_image *images[BS_CREATURE_COUNT][BS_CREATURE_IMAGES];
    bs_anim_info anims[BS_CREATURE_COUNT][BS_CREATURE_IMAGES];
    bs_level_sprite *sprites;
    uint32_t sprite_cap;
    /* The player's swing. */
    float motion[8][2];              /* the last mouse motions while attacking (0x3B47C4) */
    uint32_t motion_at;
    bs_swing swing;                  /* the last swing's factors */
    bs_hand hand;                    /* the weapon in hand */
    float last_feet[3];              /* for whether the player moves (0x16FFAE) */
    bool punching, punch_struck;     /* bare hands (0x16C260) */
    uint32_t punch_start;
    const bs_bsi_image *punch;       /* punch0.bsi */
    uint32_t player_died;            /* the level clock when the player died (0x16C258), 0 alive */
    /* Spells. */
    bs_magic_state magic;
    const bs_bsi_image *missile_images[BS_ELEMENT_COUNT];   /* spel00-04 */
    const bs_bsi_image *burst_images[BS_ELEMENT_COUNT];     /* expl00-04 */
    const bs_bsi_image *sparkle;                            /* twinkle.bsi */
    const bs_bsi_image *hands[BS_ELEMENT_COUNT];            /* cast00-04 */
    const bs_bsi_image *icons[27];                          /* symb00-26 */
} fight;

/* Reads the creature tables from GAME.EXE's bytes. */
void fight_init(fight *f, const uint8_t *exe, size_t size, bool monsters_on);
void fight_free(fight *f);

/* A level loaded: its spawn points, navigation nodes, monsters and their images. Allocations of the level
 * (the nodes) go in arena. */
void fight_enter_level(fight *f, const fight_env *env, const bs_level_info *info, bs_arena *arena);

/* Mouse motion while the attack button is held (window pixels, + right and down). */
void fight_mouse(fight *f, float dx, float dy);

/* The attack button was pressed: a ready ranged spell flies (0x411F8), or a punch with bare hands (0x41247). */
void fight_attack_pressed(fight *f, const fight_env *env);

/* The player casts a spell of the book (the Cast button, 0x2A4D6). */
void fight_cast(fight *f, const fight_env *env, const bs_spell *s);

/* F1-F8 in play (0x422F0): casts the key's spell if the hand is free, or sends a ready ranged one. */
void fight_hotkey(fight *f, const fight_env *env, int key);

/* A potion's spell on the player (0x5042C). */
void fight_drink(fight *f, const fight_env *env, int spell);

/*
 * The use button with a touch spell ready (0x4CF0A → 0x27CE5): it takes
 * effect on the monster in front within reach, or is lost on anything else
 * there. Returns true if the use went to the spell.
 */
bool fight_use(fight *f, const fight_env *env, float reach);

/* Draws the punching fist, the casting hand and the effects' icons into the overlay (0x2A837, 0x2A698,
 * 0x29080). Returns true while something there moves (the overlay must be drawn again next frame). */
bool fight_draw_hud(fight *f, const fight_env *env, bs_overlay *o);

/* One frame. Returns true if items were dropped (the world's items changed). */
bool fight_update(fight *f, const fight_env *env, bool attack_held, uint32_t dt_ms);

/* The weapon in hand's placement in the level (0x49303) and its base item (0..14), or false with none. */
bool fight_hand(const fight *f, const fight_env *env, bs_scene_xform *out, int *weapon);

/* Hands the monsters', missiles' and bursts' sprites, as seen from eye, to the renderer. */
void fight_draw(fight *f, const float eye[3], uint32_t now_ms, bs_level_renderer *r);

/* Whether the player died and the 2 s since have passed (time for the ending). */
bool fight_game_over(const fight *f, uint32_t now_ms);

#endif
