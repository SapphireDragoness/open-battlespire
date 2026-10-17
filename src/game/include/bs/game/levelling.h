#ifndef BS_GAME_LEVELLING_H
#define BS_GAME_LEVELLING_H

#include <stdbool.h>
#include <stdint.h>

#include "bs/game/use.h"

/*
 * Levelling, by the rules decoded from GAME.EXE (docs/re/README.md,
 * "Levelling"). Battlespire has no experience: a level done gives points to
 * spend on the character sheet, and skills rise with use.
 *
 * - Completing a level (0x268A1) opens the sheet as the level-up screen
 *   (0x558DC with points, cscr01.bsi) with the level's points (0xED524) and
 *   those left from before (+0x20A, at first the CharacterMaker's).
 *   Opening it raises the health's maximum by 307 / 256 and fills health and
 *   spell points.
 * - On the sheet an attribute rises by one for 40 + v / 2 points (0x56453) up
 *   to 100, and may go back down to what it was when the sheet opened, unless
 *   a skill it governs is as high (0x573A8). A skill rises for 10 + v x
 *   factor / 100 points (0x57238), the factor 50 for the class's primary
 *   skills, 60 major, 70 minor and 80 the rest, while it is below its
 *   attribute; it may go back down to what it was. Points left stay for the
 *   next time (0x55ADC).
 * - Using a skill counts (0x1E4FB, at most 20000); when the count reaches
 *   v x rate x 2 / 5 + 1, with the skill's rate (0xED4E3), it starts again
 *   and the skill rises by one if it is below 95 and below its attribute
 *   (0x1E3DF, every frame).
 * - Every 5 s (0x40C9A) moving while sneaking counts for Stealth, moving
 *   while swimming for Swimming, and the regeneration advantages add one
 *   health or spell point.
 */

/* The points for completing the campaign level of a number (0xED524): 600 for level 1 .. 1900 for 6; 0 for
 * others. */
int32_t bs_level_up_points(int level_number);

/* The level-up screen while it is open (0x3FBE90, 0x3FB2A4, 0x3FBD90). */
typedef struct bs_level_up {
    int32_t points;                         /* to spend */
    int32_t attributes_floor[8];            /* the attributes when it opened */
    int32_t skills_floor[BS_SKILL_COUNT];   /* the skills when it opened */
} bs_level_up;

/* Opens it with `points` more (0x558DC): the health's maximum x 307 / 256, health and spell points full. */
void bs_level_up_begin(bs_level_up *lu, bs_character *c, int32_t points);

/* Closes it (0x55ADC): the points left are the character's. */
void bs_level_up_end(const bs_level_up *lu, bs_character *c);

/* What raising an attribute or a skill from v costs (0x56453, 0x57238); lowering it to v gives as much. */
int32_t bs_level_up_attribute_cost(int32_t v);
int32_t bs_level_up_skill_cost(int32_t v, int32_t factor);

/* A skill's factor (0x55E04): 50 for the class's primary skills, 60 major, 70 minor, 80 the rest. */
int32_t bs_level_up_skill_factor(const bs_character *c, int skill);

/* What a change left to say: 0, or a game message (BS_MSG_*). For BS_MSG_SKILL_ABOVE the attribute
 * is the one in the way ("%s %s." with BS_MSG_ATTRIBUTES + attribute). */
typedef struct bs_level_up_result {
    uint32_t message;
    int attribute;
} bs_level_up_result;

/*
 * A click on an attribute (0x562D0), `steps` times (10 with Shift): on its
 * upper half it rises when below 100 and the points are there; otherwise
 * (and so also on the upper half without the points, as the game has it) it
 * goes down if above its floor. Spell points follow Intelligence.
 */
bs_level_up_result bs_level_up_attribute(bs_level_up *lu, bs_character *c, int attribute, bool upper, int steps);

/* A click right of a skill (raise, 0x57238) or left of it (lower, 0x57184), `steps` times. */
bs_level_up_result bs_level_up_skill(bs_level_up *lu, bs_character *c, int skill, bool raise, int steps);

/* ---- skills rising with use ---- */

/* Counts a use of a skill (0x1E4FB). */
void bs_skill_used(bs_character *c, int skill, int32_t amount);

/* The skills whose counts have come round (0x1E3DF): those that rose are listed in `rose` (for "%s skill
 * has increased...", BS_MSG_SKILL_INCREASED). Returns how many. */
int bs_skills_rise(bs_character *c, int rose[BS_SKILL_COUNT]);

/* The skill a spell's school counts for (0xDA60A). */
int bs_school_skill(int school);

/* What happens every 5 s (0x40C9A): moved is whether the player has gone 128 units or more since. */
void bs_character_five_seconds(bs_character *c, bool sneaking, bool swimming, bool moved);

#endif
