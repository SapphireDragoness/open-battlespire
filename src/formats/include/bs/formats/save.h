#ifndef BS_FORMATS_SAVE_H
#define BS_FORMATS_SAVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"
#include "bs/formats/scl.h"

/*
 * The original game's saved games, SAVE0 .. SAVE9 (docs/formats/save.md; GAME.EXE writes them at 0x30307
 * and reads them at 0x303FE):
 *
 * - SAVENAME.DAT: the save's name, 32 bytes.
 * - IMAGE.RAW: a picture of the view, 80 x 50 RGB555 (8000 bytes).
 * - SAVETREE.DAT: a version (0x100), the game's objects as records (the player, its spells and items,
 *   the level's changed objects, monsters, the automap ...), a record of length 0, then the state of the
 *   level's controllers.
 * - SAVEVARS.DAT: the player record again, the clock and level number, the message log, the level's
 *   tables (conversations, monsters' overrides, gems, sigils) and the dialogue variables: 34805 bytes.
 *
 * A record is a 32-bit length and the object as the game holds it: a 0x3D-byte header (below), then
 * the body. Class files (scl.h) are the same records with no version.
 */

#define BS_SAVE_VERSION 0x100u
#define BS_SAVE_HEADER 0x3D
#define BS_SAVE_NAME_SIZE 32
#define BS_SAVE_IMAGE_WIDTH 80
#define BS_SAVE_IMAGE_HEIGHT 50
#define BS_SAVE_IMAGE_SIZE (BS_SAVE_IMAGE_WIDTH * BS_SAVE_IMAGE_HEIGHT * 2)
#define BS_SAVE_PLAYER_ID 50000u      /* the player's record id (0x312CA) */
#define BS_SAVE_PLAYER_BODY 0x317     /* the player and monster records' size in memory */
#define BS_SAVE_ITEM_BODY 0x2F3
#define BS_SAVE_AUTOMAP_CELLS (40 * 30)

/* Object types (byte 0 of the header). */
enum {
    BS_SAVE_ITEM = 2,
    BS_SAVE_PLAYER = 3,
    BS_SAVE_OBJECT = 6,               /* a level's placed model */
    BS_SAVE_PROJECTILE = 7,
    BS_SAVE_SPELL = 9,
    BS_SAVE_MONSTER = 18,
    BS_SAVE_OPTIONS = 23,
    BS_SAVE_AUTOMAP = 51,
    BS_SAVE_EFFECT = 66,
};

/* A record's parent id: the root, the level (placed by position), or another record's id. */
#define BS_SAVE_PARENT_ROOT 0u
#define BS_SAVE_PARENT_LEVEL 1u

typedef struct bs_save_record {
    uint8_t type;
    uint16_t angles[3];               /* +0x01: pitch, yaw, roll, 2048 a turn (the automap: its grid size) */
    float pos[3];                     /* +0x07 */
    uint16_t flags;                   /* +0x13: byte +0x14 bit 0x10 bought with the class, 0x20 worn */
    uint32_t file_id;                 /* +0x15: a level object's model */
    uint32_t id;                      /* +0x1D: the record id; a level object's is its BS6 entity id */
    uint32_t time;                    /* +0x21: a time on the game's clock (a used gem's return) */
    int16_t link, link2;              /* +0x25, +0x27: -1 none */
    uint32_t parent;                  /* +0x39: BS_SAVE_PARENT_* or the parent record's id */
    int32_t parent_index;             /* the parent record's index here, or -1: the root, the level, a level
                                         object that wasn't saved (a chest: its BS6 id), or the reserve */
    bool reserve;                     /* a hidden reserve monster (+0x14 bit 2): its parent doesn't count */
    const uint8_t *object;            /* the whole object, header first */
    uint32_t size;
    const uint8_t *body;              /* object + BS_SAVE_HEADER */
    uint32_t body_size;
} bs_save_record;

/* Reads records until the data ends or a record of length 0 (which is read too). *used: the bytes read.
 * Parents are found by id, as loading does (0x2FC18): reserve monsters go to the reserve whatever their
 * parent says. */
bs_result bs_save_records_read(const uint8_t *data, size_t size, bs_arena *arena, bs_save_record **out,
                               uint32_t *count, size_t *used);

typedef struct bs_save_tree {
    uint32_t version;
    bs_save_record *records;
    uint32_t count;
    /* The level's controllers (0x4C20B): their layout comes from the level (bs_save_controllers_read). */
    const uint8_t *controls;
    size_t controls_size;
} bs_save_tree;

bs_result bs_save_tree_parse(const uint8_t *data, size_t size, bs_arena *arena, bs_save_tree *out);

/* The index of the record with an id, or -1. */
int32_t bs_save_find(const bs_save_tree *t, uint32_t id);

/*
 * A level controller's saved state: for each controller of each chain, in the level's order (the BS6
 * objects' CTRL entries, chain by chain), a flags byte; if bit 0 (running) is set, its object's id, three
 * 12-byte values (start position or angles, and the two after it) and its start time.
 */
typedef struct bs_save_controller {
    uint8_t flags;
    uint32_t object;
    uint8_t values[36];               /* controller +0x14, +0x20, +0x2C */
    uint32_t start;                   /* controller +0x10 */
} bs_save_controller;

/* Reads the controllers of chains of the given lengths, then the running chains (indices into the chain
 * table). */
bs_result bs_save_controllers_read(const uint8_t *data, size_t size, const uint32_t *chain_lengths, uint32_t chains,
                                   bs_arena *arena, bs_save_controller **out, uint32_t *count, uint8_t **active,
                                   uint32_t *active_count);

/* ---- typed records ---- */

/* A player or monster record (type 3 or 18): offsets into the body. */
typedef struct bs_save_character {
    char name[33];                    /* +0x00 */
    int32_t attributes[8];            /* +0x20, with effects */
    int32_t attributes_base[8];       /* +0x40 */
    int32_t spell_points, spell_points_max, spell_points_base;   /* +0x60, +0x62, +0x64 (16-bit) */
    int32_t wounds, wounds_max, wounds_base;                     /* +0x66, +0x6A, +0x6E */
    int32_t skills[BS_SCL_SKILLS], skill_counts[BS_SCL_SKILLS], skills_base[BS_SCL_SKILLS]; /* +0x72 + 12 n */
    int32_t shield;                   /* +0x16E: the Shield spell's points */
    uint32_t effects;                 /* +0x172: bit n spell n's effect */
    int32_t monster_level;            /* +0x176 */
    uint8_t hotkeys[8];               /* +0x1FE: a spell's number, or 64 + n for hotkey_items[n] */
    int32_t points;                   /* +0x20A: level-up points not spent */
    uint32_t flags;                   /* +0x226: 8 female */
    int32_t music_volume;             /* +0x236 */
    uint8_t hair, eyes, mouth;        /* +0x23E */
    int32_t sound_volume;             /* +0x24A */
    int32_t creature;                 /* +0x24E */
    uint8_t race;                     /* +0x25D */
    uint32_t weapon, secondary;       /* +0x260, +0x264: record ids */
    int32_t armour[7];                /* +0x268: helmet, pauldrons, cuirass, greaves, gauntlets, boots, shield */
    int32_t level;                    /* +0x29F (a byte in class files) */
    float anchor[3];                  /* +0x2B0: Teleport's anchor */
    uint32_t water_breathing_until;   /* +0x2C0 */
    uint8_t turn_rate;                /* +0x2E0 */
    uint32_t hotkey_items[8];         /* +0x2E7: record ids */
    uint32_t confused_until;          /* +0x30B */
    bs_class cls;                     /* the class fields (+0x17A ..), as a class file has them */
} bs_save_character;

/* The body may be shorter than BS_SAVE_PLAYER_BODY (some class files'); what's missing reads as 0. */
void bs_save_character_parse(const uint8_t *body, size_t size, bs_save_character *out);

/* An item record (type 2); flags from the header. */
void bs_save_item_parse(const bs_save_record *r, bs_scl_item *out);

/* ---- SAVEVARS.DAT ---- */

#define BS_SAVE_VARS_SIZE 34805
#define BS_SAVE_LOG_SIZE 0x500
#define BS_SAVE_TABLE 128
#define BS_SAVE_SIGILS 64
#define BS_SAVE_GLOBALS 256
#define BS_SAVE_LOCALS 256
#define BS_SAVE_LOCAL_VARS 8
#define BS_SAVE_CREATURES 16

typedef struct bs_save_variable {
    uint32_t hash;                    /* the name's hash (docs/formats/save.md) */
    int32_t value;
} bs_save_variable;

typedef struct bs_save_vars {
    bs_save_character player;         /* a copy of the tree's */
    int32_t level_number;             /* the campaign level's number */
    uint32_t clock;                   /* the game's millisecond clock (0x421FBC) */
    int32_t misc[4];                  /* 0x16C024, 0x16C008, 0x16BFC8, 0x16BFD0 */
    char log[BS_SAVE_LOG_SIZE + 1];   /* messages shown, each NUL-terminated, missing its first letter */
    int32_t music_volume, sound_volume;
    struct {
        uint32_t id;                  /* a record id */
        int32_t link;                 /* STRU 104: what the dialogue's DIALOGLINK n uses */
    } links[BS_SAVE_TABLE];           /* dialogue links (0x100BE4) */
    uint32_t link_count;
    struct {
        uint32_t id;                  /* a record id */
        char file[5];                 /* the conversation's file, its first three letters */
    } conversations[BS_SAVE_TABLE];
    uint32_t conversation_count;
    struct {
        uint32_t id;
        int32_t creature;             /* BS6 STRU 106's: 1 + the creature, 0 random */
        int32_t speed, strength, spell_points, health, skill, goal;
        uint32_t effects;
        int32_t spells[5];
    } monsters[BS_SAVE_TABLE];        /* the level's placed monsters' overrides */
    uint32_t monster_count;
    struct {
        uint32_t id;
        int32_t amount;
        bool spell_points;
    } gems[BS_SAVE_TABLE];            /* health and spell point gems (STRU 110, 111) */
    uint32_t gem_count;
    uint32_t list_100984[BS_SAVE_TABLE];   /* record ids; purpose unknown */
    uint32_t list_100984_count;
    struct {
        uint32_t id;
        int32_t y;
        uint32_t unknown;
    } sigils[BS_SAVE_SIGILS];
    uint32_t sigil_count;
    struct {
        uint32_t id;                  /* a speaker leaving after a while (the dialogue's TELEPORT n, 0x5A41C) */
        uint32_t at, start;           /* when it goes, when it was told to (game clock) */
    } leaving[8];
    uint32_t global_count;
    bs_save_variable globals[BS_SAVE_GLOBALS];
    struct {
        uint32_t id;                  /* the monster's record id, or an NPC's BS6 id; 0 unused */
        bs_save_variable vars[BS_SAVE_LOCAL_VARS];
    } locals[BS_SAVE_LOCALS];
    uint8_t visits[BS_SAVE_CREATURES];  /* conversations so far per creature type, at most 3 (0x401D44) */
    const uint8_t *data;              /* the file, for what isn't decoded (offsets in save.md) */
} bs_save_vars;

bs_result bs_save_vars_parse(const uint8_t *data, size_t size, bs_save_vars *out);

/* A variable name's hash (0x1BAE9): leading spaces and one '&' skipped, then for each character to the
 * first space or control, uppercased: h = (h rotated left 4) + c. */
uint32_t bs_save_variable_hash(const char *name);

/* IMAGE.RAW as 24-bit RGB (80 x 50 x 3 bytes). */
void bs_save_image_rgb(const uint8_t raw[BS_SAVE_IMAGE_SIZE], uint8_t *rgb);

#endif
