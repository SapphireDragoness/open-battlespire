#ifndef BS_FORMATS_EXE_TEXT_H
#define BS_FORMATS_EXE_TEXT_H

#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"
#include "bs/formats/le.h"

/*
 * The game's built-in messages: a table of 543 string pointers in GAME.EXE's
 * data (0xE82B9 in v1.5), from "Exit the Automap." to "LCK". The game shows
 * them by index (0x1D211 queues one on screen). Found by content, so any
 * version with the same table works. Windows-1252.
 */

enum {
    BS_MSG_TOO_FAR = 1,             /* "You are too far away." */
    BS_MSG_OFF_BALANCE = 3,         /* "Your opponent's dodge caught you off balance." */
    BS_MSG_GREETING = 6,            /* 6-11: "The %s invites you to speak." ... */
    BS_MSG_SKILL_INCREASED = 88,    /* "%s skill has increased..." */
    BS_MSG_NO_ARROWS = 106,         /* "You have no arrows ready." */
    BS_MSG_ARROW_HITS = 107,        /* "Arrow hits!" */
    BS_MSG_NO_MORE_ARROWS = 108,    /* "You have no more arrows ready." */
    BS_MSG_WEAPON_DAMAGED = 109,    /* "Your weapon is damaged." */
    BS_MSG_WEAPON_KNOCKED = 110,    /* "Your weapon is knocked from your hand!" */
    BS_MSG_TELLING_BLOW = 111,      /* "You land a telling blow." */
    BS_MSG_STUN = 112,              /* "Your blow stuns your opponent." */
    BS_MSG_REELING = 113,           /* "You send your opponent reeling." */
    BS_MSG_NO_DAMAGE = 114,         /* "You dont seem to be doing any damage." */
    BS_MSG_DIES = 115,              /* "%s dies..." */
    BS_MSG_POTION_RESTORATION = 240,     /* "Potion of Restoration (%d)" */
    BS_MSG_POTION_WATER_BREATHING = 241, /* "Potion of Water Breathing (%d)" */
    BS_MSG_POTION_OF = 242,         /* "%s of %s (%d)": a potion, its spell, how many */
    BS_MSG_CANNOT_CARRY = 251,      /* "You cannot carry any more stuff." */
    BS_MSG_NOT_ALLOWED = 254,       /* "You are not allowed to equip this." */
    BS_MSG_DESTROYED = 256,         /* "%s is destroyed..." */
    BS_MSG_NOT_ENOUGH_POINTS = 431, /* "You do not have enough points." */
    BS_MSG_DAMAGE = 510,
    BS_MSG_CONDITION = 511,
    BS_MSG_NUMBER = 512,
    BS_MSG_WEIGHT = 519,
    BS_MSG_COMBINE = 520,           /* "You combine that item with what you already have." */
    BS_MSG_SKILL_ABOVE = 525,       /* "Skill cannot be increased higher than your", then an attribute */
    BS_MSG_YOU_SEE = 526,           /* "You see %s." */
    BS_MSG_ATTRIBUTES = 527,        /* 527-534: the eight attribute names, strength .. luck */
    BS_MSG_DEPENDENT_SKILL = 565,   /* "You cannot decrease this below the highest dependent skill." */
};

typedef struct bs_exe_text {
    const char **messages;  /* NUL-terminated, pointing into the image */
    uint32_t count;
} bs_exe_text;

/* Finds the message table in a loaded GAME.EXE (bs_le_load). The image must outlive out. */
bs_result bs_exe_text_read(const bs_le_image *img, bs_arena *arena, bs_exe_text *out);

/*
 * Any table of string pointers in a loaded executable: the one whose first
 * entry points to the string `first` (which must follow a NUL), up to max
 * entries while they point to strings.
 */
bs_result bs_exe_string_table(const bs_le_image *img, const char *first, uint32_t max, bs_arena *arena,
                              bs_exe_text *out);

/* Message i, or "" if there is none. */
const char *bs_exe_message(const bs_exe_text *t, uint32_t i);

#endif
