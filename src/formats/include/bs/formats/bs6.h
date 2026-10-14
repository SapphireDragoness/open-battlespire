#ifndef BS_FORMATS_BS6_H
#define BS_FORMATS_BS6_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"

/*
 * BS6 levels (BS6.BSA, and loose .bs6 files in GAMEDATA). See docs/formats/bs6.md.
 *
 * A tree of chunks, each a 4-character tag and a little-endian u32 length:
 *
 *   GNRL                       the whole file
 *     TEXI { DIRN }            texture directory (editor path)
 *     BITS BBOX OFST RADI CENT level-wide values
 *     SNAP { IDTY POSI ANGS }  editor snap grid
 *     WATR                     water level
 *     VIEW { IDNB IDTY POSI ANGS AMBI }       viewpoints
 *     STRU { DIRN FILN }
 *     OBJS { DIRN LFIL OBJD... }              placed 3D models
 *       OBJD { IDNB IDFI POSI ANGS SELE [STRU CTRL LINK]... }
 *     LITS { LITD... }                        point lights
 *       LITD { IDNB IDTY POSI RADI BRIT SELE [CTRL] }
 *     FLAS { FLAD... }                        flats (sprites, markers)
 *       FLAD { IDNB FILN POSI SCAL AMBI SELE [STRU CTRL LINK]... }
 *
 * Positions are in level units with +y pointing down. STRU, CTRL and LINK
 * hold the game logic; each entity keeps its raw chunk, and
 * bs_bs6_entity_logic() reads them.
 */

#define BS_BS6_NAME_FIELD 260  /* DIRN, FILN and LFIL entries are MAX_PATH-sized */
#define BS_BS6_ANGLE_TURN 2048 /* angle units per full turn */

/* ---- Generic chunk access ---- */

typedef struct bs_bs6_chunk {
    char tag[5];
    const uint8_t *data;
    uint32_t size;
} bs_bs6_chunk;

typedef struct bs_bs6_iter {
    const uint8_t *data;
    size_t size;
    size_t pos;
    bool err;  /* set when a chunk header or length runs past the end */
} bs_bs6_iter;

/* Iterates the chunks laid out back to back in data[0..size). */
bs_bs6_iter bs_bs6_iter_make(const uint8_t *data, size_t size);
bool bs_bs6_next(bs_bs6_iter *it, bs_bs6_chunk *out);

/* First child of `parent` with the given tag. */
bool bs_bs6_find(const bs_bs6_chunk *parent, const char *tag, bs_bs6_chunk *out);

/* ---- Decoded level ---- */

typedef struct bs_bs6_object {
    uint32_t id;           /* IDNB */
    uint32_t model;        /* IDFI: index into model_names */
    int32_t pos[3];
    int32_t angles[3];     /* BS_BS6_ANGLE_TURN units; see bs_bs6_rotation() */
    uint32_t selected;     /* SELE, editor state */
    bs_bs6_chunk chunk;    /* the OBJD chunk */
} bs_bs6_object;

typedef struct bs_bs6_flat {
    uint32_t id;
    const char *name;      /* FILN: BSI image name without extension */
    int32_t pos[3];
    int32_t scale;         /* SCAL: almost always 0 */
    int32_t ambient;       /* AMBI */
    uint32_t selected;
    bs_bs6_chunk chunk;    /* the FLAD chunk */
} bs_bs6_flat;

typedef struct bs_bs6_light {
    uint32_t id;
    uint32_t type;         /* IDTY */
    int32_t pos[3];
    int32_t radius;        /* RADI */
    int32_t brightness;    /* BRIT: 11..1023; scale unknown */
    bs_bs6_chunk chunk;    /* the LITD chunk */
} bs_bs6_light;

typedef struct bs_bs6_view {
    uint32_t id;
    uint32_t type;
    int32_t pos[3];
    int32_t angles[3];
    int32_t ambient;
} bs_bs6_view;

typedef struct bs_bs6_level {
    const char **model_names;  /* LFIL, without extension, e.g. "1blok1" */
    uint32_t model_count;
    bs_bs6_object *objects;
    uint32_t object_count;
    bs_bs6_flat *flats;
    uint32_t flat_count;
    bs_bs6_light *lights;
    uint32_t light_count;
    bs_bs6_view *views;
    uint32_t view_count;
    bool has_bbox;
    int32_t bbox[6];           /* min xyz, max xyz; unreliable (min y is often garbage) */
    int32_t water;             /* WATR; 0 in most levels */
} bs_bs6_level;

/* Parses a level. Strings and arrays are allocated from arena; chunk
 * pointers point into data, which must outlive the level. */
bs_result bs_bs6_parse(const uint8_t *data, size_t size, bs_arena *arena, bs_bs6_level *out);

/*
 * Rotation matrix for object angles (row-major, applied as m * v to model
 * points): rotate about y by -angles[1], then about x by angles[0], then
 * about z by angles[2]. Inferred: this convention makes neighbouring models
 * meet in all campaign levels.
 */
void bs_bs6_rotation(const int32_t angles[3], float m[9]);

/* ---- Entity logic: STRU properties, CTRL controllers, LINK links ----
 * See docs/formats/bs6.md and docs/re/README.md ("Level logic"). */

/* A STRU property or CTRL controller: its IDNB (property id or controller
 * type) and RAWD bytes. */
typedef struct bs_bs6_record {
    uint32_t id;
    const uint8_t *data;   /* points into the level data */
    uint32_t size;
} bs_bs6_record;

typedef struct bs_bs6_link {
    uint32_t type;         /* IDTY: 0, or 2 for a teleport */
    uint32_t target;       /* IDNB: entity id */
} bs_bs6_link;

typedef struct bs_bs6_logic {
    bs_bs6_record *props;  /* STRU, in file order */
    uint32_t prop_count;
    bs_bs6_record *ctrls;  /* CTRL, in file order */
    uint32_t ctrl_count;
    bs_bs6_link *links;
    uint32_t link_count;
} bs_bs6_logic;

/* Reads the STRU, CTRL and LINK chunks of an entity (an object's, flat's or
 * light's chunk). Arrays come from arena. */
bs_result bs_bs6_entity_logic(const bs_bs6_chunk *entity, bs_arena *arena, bs_bs6_logic *out);

/* The i-th little-endian i32 of a record's data, or 0 past its end. */
int32_t bs_bs6_record_i32(const bs_bs6_record *r, uint32_t i);

/* The first property with this id, or NULL. */
const bs_bs6_record *bs_bs6_logic_prop(const bs_bs6_logic *l, uint32_t id);

#endif
