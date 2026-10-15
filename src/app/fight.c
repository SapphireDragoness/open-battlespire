#include "fight.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/core/log.h"

/* RE references are GAME.EXE addresses; see docs/formats/creatures.md. */

#define PROP_LOOT 116
#define REACH 90.0f              /* the player's blow reaches monsters this near (0x497F3) ... */
#define REACH_ANGLE 200          /* ... this far off the facing (0x49851) */
#define MONSTER_EYE 50.0f        /* as in monsters.c (inferred) */
#define PUNCH_SOUND 0x1A2        /* a swing with bare hands (0x494FB) */
#define PUNCH_STRIKE_FRAME 5     /* the punch's blow lands at this frame (0x2A892: frame > 4) */
#define PAIN_SOUND 0x1A5         /* + 0 or 1; + 2 for a female character (0x43819) */
/* A swing's sound by the weapon in hand, -1 none (0xEF840). */
static const int16_t swing_sound[BS_ITEM_LAST_WEAPON + 1] = {418, 418, 416, 416, 416, 415, 415, 415,
                                                             -1,  -1,  -1,  419, -1,  419, 419};
#define DEATH_ENDING_MS 2000     /* (0x135DD) */

void fight_init(fight *f, const uint8_t *exe, size_t size, bool monsters_on)
{
    memset(f, 0, sizeof *f);
    f->monsters_on = monsters_on;
    f->have_tables = exe && bs_creature_tables_read(exe, size, &f->tables) == BS_OK;
    if (!f->have_tables)
        BS_WARN("no creature tables in GAME.EXE: no monsters");
}

void fight_free(fight *f)
{
    bs_monsters_free(&f->monsters);
    free(f->sprites);
    f->sprites = NULL;
    f->sprite_cap = 0;
}

static void play(const fight_env *env, int32_t id, const float pos[3])
{
    if (id > 0)
        bs_level_audio_play_at(env->audio, id, pos, env->listener, env->listener_yaw);
}

/* A message with its %s filled in. */
static void message_with(const fight_env *env, uint32_t id, const char *s)
{
    const char *fmt = bs_exe_message(env->text, id);
    char out[160];
    const char *p = strstr(fmt, "%s");
    if (p)
        snprintf(out, sizeof out, "%.*s%s%s", (int)(p - fmt), fmt, s, p + 2);
    else
        snprintf(out, sizeof out, "%s", fmt);
    if (out[0] && env->message)
        env->message(env->ctx, out);
}

/* The loot of a spawn point (STRU 116 on its flat) on the floor at pos, 4 above it as dropped items lie
 * (0x52731). Returns how many items. */
static uint32_t drop_loot(const fight_env *env, uint32_t flat, const float pos[3])
{
    if (!env->db || !env->db->tables || flat >= env->level->flat_count)
        return 0;
    bs_arena tmp;
    bs_arena_init(&tmp, 0);
    bs_bs6_logic lg;
    uint32_t n = 0;
    float from[3] = {pos[0], pos[1] - 32.0f, pos[2]}, to[3] = {pos[0], pos[1] + 2000.0f, pos[2]}, t;
    int32_t ob;
    float at[3] = {pos[0], pos[1], pos[2]};
    if (bs_collision_segment(env->collision, from, to, &t, &ob))
        at[1] = from[1] + t * (to[1] - from[1]) - 4.0f;
    if (bs_bs6_entity_logic(&env->level->flats[flat].chunk, &tmp, &lg) == BS_OK) {
        for (uint32_t k = 0; k < lg.prop_count; k++) {
            if (lg.props[k].id != PROP_LOOT)
                continue;
            int32_t v[4];
            for (uint32_t c = 0; c < 4; c++)
                v[c] = bs_bs6_record_i32(&lg.props[k], c);
            bs_item item;
            if (!bs_item_make(env->db, BS_ITEM_SPEC_CONTENT, v, env->level_number, env->multiplayer, env->rng, &item))
                continue;
            bs_item_place place = {BS_ITEM_FLOOR, bs_items_cell(env->grid_cell, at)};
            if (bs_items_add(env->items, &item, place, -1, at) != UINT32_MAX)
                n++;
        }
    }
    bs_arena_free(&tmp);
    return n;
}

/* The spells' pictures: missiles, bursts, the sparkle, the casting hands, the icons. */
static void load_spell_images(fight *f, const fight_env *env)
{
    char name[16];
    for (int e = 0; e < BS_ELEMENT_COUNT; e++) {
        snprintf(name, sizeof name, "spel%02d.bsi", e);
        f->missile_images[e] = bs_assets_image(env->assets, name);
        snprintf(name, sizeof name, "expl%02d.bsi", e);
        f->burst_images[e] = bs_assets_image(env->assets, name);
        snprintf(name, sizeof name, "cast%02d.bsi", e);
        f->hands[e] = bs_assets_image(env->assets, name);
    }
    f->sparkle = bs_assets_image(env->assets, "twinkle.bsi");
    f->punch = bs_assets_image(env->assets, "punch0.bsi");
    for (int i = 0; i < 27; i++) {
        snprintf(name, sizeof name, "symb%02d.bsi", i);
        f->icons[i] = bs_assets_image(env->assets, name);
    }
}

void fight_enter_level(fight *f, const fight_env *env, const bs_level_info *info, bs_arena *arena)
{
    bs_magic_reset(&f->magic);
    if (!f->hands[0])
        load_spell_images(f, env);
    f->monsters.count = 0;
    f->player_died = 0;
    f->punching = false;
    bs_hand_reset(&f->hand); /* raised again in the new level */
    memcpy(f->last_feet, env->player->pos, sizeof f->last_feet);
    memset(f->motion, 0, sizeof f->motion);
    f->have_nodes = false;
    memset(f->images, 0, sizeof f->images);
    memset(f->anims, 0, sizeof f->anims);
    f->monsters.player_node = UINT32_MAX;
    f->monsters.player_node_ms = 0;
    if (bs_monsters_find_points(&f->monsters, env->level) != BS_OK) {
        BS_WARN("cannot find the monsters' spawn points");
        return;
    }
    /* The navigation nodes (LEVELS.TXT field 8; "x..." for none). */
    if (info && info->nodes[0] && info->nodes[0] != 'x' && info->nodes[0] != 'X') {
        char path[64];
        snprintf(path, sizeof path, "GAMEDATA/%s", info->nodes);
        uint8_t *data;
        size_t size;
        if (bs_vfs_read(env->vfs, path, arena, &data, &size) == BS_OK &&
            bs_nodes_parse(data, size, arena, &f->nodes) == BS_OK)
            f->have_nodes = true;
        else
            BS_WARN("cannot read %s: monsters only walk straight", path);
    }
    if (!f->have_tables || !f->monsters_on) {
        /* No monsters: their loot lies at the spawn points (0x46867). */
        for (uint32_t i = 0; i < f->monsters.point_count; i++)
            if (f->monsters.points[i].loot)
                drop_loot(env, f->monsters.points[i].flat, f->monsters.points[i].pos);
        return;
    }
    int32_t percent = info ? info->monster_percent : 100;
    if (env->multiplayer)
        percent = 100;
    if (bs_monsters_spawn(&f->monsters, &f->tables, env->collision, env->level_number, percent, env->rng) != BS_OK)
        BS_WARN("cannot spawn the monsters");
    /* The images of the creatures there. */
    for (uint32_t i = 0; i < f->monsters.count; i++) {
        uint32_t t = f->monsters.list[i].type;
        if (f->images[t][0])
            continue;
        for (uint32_t s = 0; s < BS_CREATURE_IMAGES; s++) {
            const char *name = f->tables.creatures[t].images[s];
            if (!name[0])
                continue;
            const bs_bsi_image *img = bs_assets_image(env->assets, name);
            f->images[t][s] = img;
            if (img)
                f->anims[t][s] = (bs_anim_info){img->frames ? img->frames : 1,
                                                img->bhdr[8] > 0 ? (uint32_t)img->bhdr[8] : 71};
            else
                BS_WARN("no image %s for the %s", name, f->tables.creatures[t].name);
        }
    }
}

void fight_mouse(fight *f, float dx, float dy)
{
    f->motion[f->motion_at][0] += dx;
    f->motion[f->motion_at][1] += dy;
}

/* The player as a fighter: the character's attributes and skills, the weapon in hand, the armour worn. */
static void player_fighter(const fight_env *env, bs_fighter *out, uint32_t *weapon_index)
{
    const bs_character *ch = env->character;
    memset(out, 0, sizeof *out);
    out->player = true;
    out->strength = bs_character_attribute(ch, 0);
    out->agility = bs_character_attribute(ch, 3);
    out->luck = bs_character_attribute(ch, 7);
    out->dodge = ch->skills[BS_SKILL_DODGING];
    out->critical = ch->skills[BS_SKILL_CRITICAL];
    out->hand_to_hand = ch->skills[BS_SKILL_HAND_TO_HAND];
    out->health = ch->health;
    out->health_max = ch->health_max;
    out->shield = ch->shield;
    *weapon_index = 15; /* bare hands */
    const bs_weapon_stats *ws = NULL;
    uint32_t w = env->items ? bs_items_weapon(env->items) : UINT32_MAX;
    if (env->db && env->db->tables) {
        if (w != UINT32_MAX && env->items->items[w].item.base <= BS_ITEM_LAST_WEAPON) {
            const bs_item *it = &env->items->items[w].item;
            ws = &env->db->tables->weapons[it->base];
            out->armed = true;
            out->weapon_bonus = (int32_t)it->material - 1;
            *weapon_index = it->base;
        } else {
            ws = &env->db->tables->weapons[BS_ITEM_WEAPON_COUNT - 1];
        }
    }
    int32_t skill = ws && ws->skill >= 0 && ws->skill < BS_SKILL_COUNT ? ws->skill : BS_SKILL_HAND_TO_HAND;
    out->weapon_skill = ch->skills[skill];
    out->damage_min = ws ? ws->damage_min : 1;
    out->damage_max = ws ? ws->damage_max : 2;
    if (env->items)
        bs_items_armour_values(env->items, out->armour);
}

static bool uses_missiles(const fight_env *env, uint32_t weapon_index)
{
    return weapon_index < BS_ITEM_WEAPON_COUNT - 1 && env->db && env->db->tables &&
           env->db->tables->weapons[weapon_index].skill == 19;
}

/* Bare hands (0x494E7): the punch sprite plays. */
static void start_punch(fight *f, const fight_env *env, const bs_swing *s)
{
    f->swing = *s;
    f->punching = true;
    f->punch_struck = false;
    f->punch_start = env->now_ms;
    float eye[3];
    bs_player_eye(env->player, eye);
    play(env, PUNCH_SOUND, eye);
}

/* A weapon's swing (0x49521): after the wind-up, taking the time the weapon, skill, Strength and Speed
 * make (0x4963F). */
static void start_swing(fight *f, const fight_env *env, const bs_swing *s, uint32_t weapon)
{
    if (!env->db || !env->db->tables || weapon > BS_ITEM_LAST_WEAPON)
        return;
    const bs_weapon_stats *ws = &env->db->tables->weapons[weapon];
    const bs_character *ch = env->character;
    int32_t skill = ws->skill >= 0 && ws->skill < BS_SKILL_COUNT ? ch->skills[ws->skill] : 0;
    int32_t ms = bs_swing_time(s->animation, ws->unk0, skill, bs_character_attribute(ch, 0), bs_character_attribute(ch, 6));
    if (!bs_hand_swing(&f->hand, s->animation, ms))
        return;
    f->swing = *s;
    if (swing_sound[weapon] >= 0) {
        float eye[3];
        bs_player_eye(env->player, eye);
        play(env, (uint32_t)swing_sound[weapon], eye);
    }
}

static uint32_t frame_at(const bs_bsi_image *img, uint32_t elapsed, bool *done);
static void magic_events(fight *f, const fight_env *env, const bs_magic_events *ev);
static void monster_events(fight *f, const fight_env *env, bs_monster_events *ev, bool *dropped);
static const bs_monster_world *world_of(fight *f, const fight_env *env, bs_monster_world *w);
static const bs_magic_world *magic_world_of(fight *f, const fight_env *env, const bs_monster_world *mw,
                                            bs_monster_events *mev, bs_magic_world *w);

void fight_attack_pressed(fight *f, const fight_env *env)
{
    if (f->player_died)
        return;
    if (f->magic.ready && f->magic.ready_cast.spell.delivery != BS_DELIVERY_TOUCH) {
        /* A ranged spell ready flies instead (0x411F8). */
        bs_monster_world mw;
        bs_monster_events mev = {.count = 0};
        bs_magic_world w;
        bs_magic_events ev = {.count = 0};
        world_of(f, env, &mw);
        bs_magic_launch_ready(&f->magic, magic_world_of(f, env, &mw, &mev, &w), &ev);
        magic_events(f, env, &ev);
        bool dropped = false;
        monster_events(f, env, &mev, &dropped);
        return;
    }
    bs_fighter pf;
    uint32_t wi;
    player_fighter(env, &pf, &wi);
    if (pf.armed || f->punching || f->player_died)
        return;
    /* A punch: the last swing's factors (0x41265 keeps 0x16BFC8 / 0x16BFD0). */
    bs_swing s = f->swing.skill_mult ? f->swing : (bs_swing){0, 4, 8, 7};
    start_punch(f, env, &s);
}

static const bs_monster_world *world_of(fight *f, const fight_env *env, bs_monster_world *w)
{
    *w = (bs_monster_world){
        .tables = &f->tables,
        .anims = (const bs_anim_info(*)[BS_CREATURE_IMAGES])f->anims,
        .collision = env->collision,
        .nodes = f->have_nodes ? &f->nodes : NULL,
        .level_number = env->level_number,
        .active_distance = env->active_distance,
        .player = env->player,
        .player_dead = f->player_died != 0,
        .rng = env->rng,
        .now_ms = env->now_ms,
        .spells = env->spells,
        .player_flags = env->character->flags,
    };
    return w;
}

static const bs_magic_world *magic_world_of(fight *f, const fight_env *env, const bs_monster_world *mw,
                                            bs_monster_events *mev, bs_magic_world *w)
{
    *w = (bs_magic_world){
        .character = env->character,
        .player = env->player,
        .aim_yaw = env->aim_yaw,
        .aim_pitch = env->aim_pitch,
        .collision = env->collision,
        .monsters = f->have_tables ? &f->monsters : NULL,
        .monster_world = mw,
        .monster_events = mev,
        .spells = env->spells,
        .tables = f->have_tables ? &f->tables : NULL,
        .nodes = f->have_nodes ? &f->nodes : NULL,
        .level_number = env->level_number,
        .now_ms = env->now_ms,
        .rng = env->rng,
    };
    return w;
}

/* The player's blow lands (0x4975B): every monster in reach, in front and in sight. */
static void player_strikes(fight *f, const fight_env *env, bs_monster_events *ev)
{
    bs_fighter pf;
    uint32_t wi;
    player_fighter(env, &pf, &wi);
    if (uses_missiles(env, wi) || (env->character->flags & BS_FLAG_ETHEREAL))
        return; /* bows shoot arrows: not yet; an ethereal player's blows pass through (0x49771) */
    bs_monster_world w;
    world_of(f, env, &w);
    float eye[3];
    bs_player_eye(env->player, eye);
    int32_t facing = (int32_t)(env->player->yaw * 2048.0f / (2.0f * 3.14159265f)) & 0x7FF;
    for (uint32_t i = 0; i < f->monsters.count; i++) {
        bs_monster *m = &f->monsters.list[i];
        if (m->dead || m->gone)
            continue;
        float dx = m->body.pos[0] - env->player->pos[0], dy = m->body.pos[1] - env->player->pos[1],
              dz = m->body.pos[2] - env->player->pos[2];
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d > REACH)
            continue;
        if (d > 10.0f) {
            int32_t to = bs_angle_to(env->player->pos[0], env->player->pos[2], m->body.pos[0], m->body.pos[2]);
            int32_t diff = (to - facing) & 0x7FF;
            if (diff > 1024)
                diff = 2048 - diff;
            if (diff >= REACH_ANGLE)
                continue;
        }
        float meye[3] = {m->body.pos[0], m->body.pos[1] - MONSTER_EYE, m->body.pos[2]}, t;
        int32_t ob;
        if (bs_collision_segment(env->collision, eye, meye, &t, &ob))
            continue;
        bs_fighter mf;
        bs_monster_fighter(m, &f->tables.creatures[m->type], &mf);
        bs_melee hit = bs_melee_roll(&pf, &mf, f->swing.skill_mult, f->swing.damage_mult, env->rng);
        if (hit.message)
            env->message(env->ctx, bs_exe_message(env->text, hit.message));
        m->delay = -20;
        if (hit.outcome == BS_MELEE_MISS || hit.outcome == BS_MELEE_FUMBLE) {
            play(env, bs_miss_sound(m->type, env->rng), eye);
            continue;
        }
        if (hit.outcome != BS_MELEE_HIT)
            continue;
        play(env, bs_hit_sound(true, wi), eye);
        if (hit.stun)
            m->skip_attacks = 2;
        if (bs_monster_hurt(&f->monsters, i, &w, hit.damage, hit.force, env->player->pos, env->character->health, ev))
            message_with(env, BS_MSG_DIES, f->tables.creatures[m->type].name);
    }
}

/* A monster's blow at the player (0x47502 → 0x4241F). */
static void monster_strikes(fight *f, const fight_env *env, uint32_t i)
{
    bs_monster *m = &f->monsters.list[i];
    if (f->player_died || (env->character->flags & BS_FLAG_ETHEREAL))
        return; /* blows pass through an ethereal player (0x42475) */
    bs_fighter pf, mf;
    uint32_t wi;
    player_fighter(env, &pf, &wi);
    bs_monster_fighter(m, &f->tables.creatures[m->type], &mf);
    bs_melee hit = bs_melee_roll(&mf, &pf, 4, 4, env->rng);
    env->character->shield = hit.shield;
    if (hit.outcome == BS_MELEE_MISS) {
        play(env, bs_miss_sound(0, env->rng), m->body.pos);
        return;
    }
    if (hit.outcome != BS_MELEE_HIT || hit.damage <= 0)
        return;
    m->delay = -30;
    play(env, bs_hit_sound(false, m->type), m->body.pos);
    bs_character *ch = env->character;
    ch->health -= hit.damage;
    if (ch->health < 1) {
        ch->health = 0;
        f->player_died = env->now_ms ? env->now_ms : 1;
        return;
    }
    /* Fire Shield gives the attacker half the damage back (0x4384E → 0x47583). */
    int32_t back = (ch->flags & BS_FLAG_FIRE_SHIELD) ? hit.damage / 2 : 0;
    if (back >= 2) {
        bs_monster_world w;
        bs_monster_events ev = {.count = 0};
        bool dropped = false;
        if (bs_monster_hurt(&f->monsters, i, world_of(f, env, &w), back, 0, env->player->pos, 0, &ev))
            message_with(env, BS_MSG_DIES, f->tables.creatures[m->type].name);
        monster_events(f, env, &ev, &dropped);
    }
    /* A cry of pain, mostly (0x437FD: rand() < 30000). */
    if (bs_rng_range(env->rng, 0, 32767) < 30000) {
        float eye[3];
        bs_player_eye(env->player, eye);
        bool female = env->db && env->db->female;
        play(env, PAIN_SOUND + (female ? 2 : 0) + bs_rng_range(env->rng, 0, 1), eye);
    }
}

/* A message with up to three %d filled in. */
static void message_args(const fight_env *env, uint32_t id, const int32_t args[3])
{
    const char *fmt = bs_exe_message(env->text, id);
    char out[160];
    size_t n = 0;
    int k = 0;
    for (const char *p = fmt; *p && n + 12 < sizeof out; p++) {
        if (p[0] == '%' && p[1] == 'd' && k < 3) {
            n += (size_t)snprintf(out + n, sizeof out - n, "%d", (int)args[k++]);
            p++;
        } else {
            out[n++] = *p;
        }
    }
    out[n] = '\0';
    if (out[0] && env->message)
        env->message(env->ctx, out);
}

static void player_hurt(fight *f, const fight_env *env)
{
    bs_character *ch = env->character;
    if (ch->health < 1) {
        ch->health = 0;
        if (!f->player_died)
            f->player_died = env->now_ms ? env->now_ms : 1;
        return;
    }
    if (bs_rng_range(env->rng, 0, 32767) < 30000) {
        float eye[3];
        bs_player_eye(env->player, eye);
        bool female = env->db && env->db->female;
        play(env, PAIN_SOUND + (female ? 2 : 0) + bs_rng_range(env->rng, 0, 1), eye);
    }
}

static void magic_events(fight *f, const fight_env *env, const bs_magic_events *ev)
{
    for (uint32_t k = 0; k < ev->count; k++) {
        const bs_magic_event *e = &ev->list[k];
        switch (e->kind) {
        case BS_MAGIC_SOUND:
            play(env, e->value, e->pos);
            break;
        case BS_MAGIC_MESSAGE:
            message_args(env, (uint32_t)e->value, e->args);
            break;
        case BS_MAGIC_MONSTER_DIED:
            if (e->monster < f->monsters.count)
                message_with(env, BS_MSG_DIES, f->tables.creatures[f->monsters.list[e->monster].type].name);
            break;
        case BS_MAGIC_PLAYER_HURT:
            player_hurt(f, env);
            break;
        }
    }
}

/* The monsters' events; spells they cast go off here. */
static void monster_events(fight *f, const fight_env *env, bs_monster_events *ev, bool *dropped)
{
    bs_monster_world mw;
    world_of(f, env, &mw);
    for (uint32_t k = 0; k < ev->count; k++) {
        const bs_monster_event e = ev->list[k];
        if (e.monster >= f->monsters.count)
            continue;
        switch (e.kind) {
        case BS_MONSTER_SOUND:
            play(env, e.value, f->monsters.list[e.monster].body.pos);
            break;
        case BS_MONSTER_STRIKE:
            monster_strikes(f, env, e.monster);
            break;
        case BS_MONSTER_GREET:
            message_with(env, (uint32_t)e.value, f->tables.creatures[f->monsters.list[e.monster].type].name);
            break;
        case BS_MONSTER_GONE: {
            const bs_monster *m = &f->monsters.list[e.monster];
            if (m->spawn < f->monsters.point_count && f->monsters.points[m->spawn].loot)
                *dropped |= drop_loot(env, f->monsters.points[m->spawn].flat, m->body.pos) > 0;
            break;
        }
        case BS_MONSTER_CAST: {
            if (f->player_died)
                break;
            bs_magic_world w;
            bs_magic_events mev = {.count = 0};
            /* Events of monsters hurt go on the end of this list. */
            bs_magic_cast_monster(&f->magic, magic_world_of(f, env, &mw, ev, &w), e.monster, e.value & 0xFF,
                                  (e.value >> 8) & 0xFF, (e.value >> 16) & 0xFF, &mev);
            magic_events(f, env, &mev);
            break;
        }
        }
    }
}

bool fight_update(fight *f, const fight_env *env, bool attack_held, uint32_t dt_ms)
{
    bool dropped = false;
    bs_monster_events ev = {.count = 0};
    bs_fighter pf;
    uint32_t wi;
    player_fighter(env, &pf, &wi);
    /* The weapon in hand follows what's wielded: a new one is raised (0x48DF3). */
    int wield = pf.armed ? (int)wi : -1;
    if (wield != f->hand.weapon)
        bs_hand_equip(&f->hand, wield, env->now_ms);
    bool bow = pf.armed && uses_missiles(env, wi);
    /* The swing: the last eight frames' mouse motion (0x21476) while the button is held (0x41296). */
    bool ready = pf.armed ? bs_hand_can_swing(&f->hand) && !bow : !f->punching;
    if (attack_held && ready && !f->player_died) {
        float sx = 0, sy = 0;
        for (int k = 0; k < 8; k++) {
            sx += f->motion[k][0];
            sy += f->motion[k][1];
        }
        bs_swing s;
        if (bs_swing_from_motion((int32_t)sx, (int32_t)sy, &s)) {
            if (pf.armed)
                start_swing(f, env, &s, wi);
            else
                start_punch(f, env, &s);
            memset(f->motion, 0, sizeof f->motion);
        }
    }
    f->motion_at = (f->motion_at + 1) % 8;
    f->motion[f->motion_at][0] = f->motion[f->motion_at][1] = 0;
    /* The hand bobs while the player moves (0x407A6); a bow is drawn while the button is held. */
    const float *feet = env->player->pos;
    bool moving = feet[0] != f->last_feet[0] || feet[1] != f->last_feet[1] || feet[2] != f->last_feet[2];
    memcpy(f->last_feet, feet, sizeof f->last_feet);
    if (bs_hand_update(&f->hand, env->now_ms, moving, bow && attack_held && !f->player_died))
        player_strikes(f, env, &ev);
    if (f->punching) {
        bool done;
        uint32_t frame = frame_at(f->punch, env->now_ms - f->punch_start, &done);
        if (!f->punch_struck && (frame >= PUNCH_STRIKE_FRAME || done)) {
            f->punch_struck = true;
            player_strikes(f, env, &ev);
        }
        if (done)
            f->punching = false;
    }

    bs_monster_world mw;
    world_of(f, env, &mw);
    if (f->have_tables && f->monsters.count)
        bs_monsters_update(&f->monsters, &mw, dt_ms, &ev);
    /* Spells: missiles, traps, the player's effects. */
    bs_magic_world w;
    bs_magic_events mev = {.count = 0};
    bs_magic_update(&f->magic, magic_world_of(f, env, &mw, &ev, &w), dt_ms, &mev);
    magic_events(f, env, &mev);
    monster_events(f, env, &ev, &dropped);
    /* Continuous Damage, Poison or a sigil may have killed the player. */
    if (env->character->health < 1)
        player_hurt(f, env);
    return dropped;
}

void fight_cast(fight *f, const fight_env *env, const bs_spell *s)
{
    if (f->player_died || !s)
        return;
    bs_monster_world mw;
    bs_monster_events mev = {.count = 0};
    bs_magic_world w;
    bs_magic_events ev = {.count = 0};
    world_of(f, env, &mw);
    bs_magic_cast_player(&f->magic, magic_world_of(f, env, &mw, &mev, &w), s, &ev);
    magic_events(f, env, &ev);
    bool dropped = false;
    monster_events(f, env, &mev, &dropped);
}

void fight_hotkey(fight *f, const fight_env *env, int key)
{
    bs_character *ch = env->character;
    if (key < 0 || key >= BS_HOTKEYS || f->player_died)
        return;
    int spell = ch->hotkeys[key];
    if (spell < 1 || spell > BS_SPELL_COUNT || !(ch->spells_known & (1u << (spell - 1))))
        return;
    /* The key of the spell ready sends it (0x42387); otherwise a free hand casts (0x423A7). */
    if (f->magic.ready && !f->magic.hand_playing) {
        if (f->magic.ready_cast.spell.effect == spell)
            fight_attack_pressed(f, env);
        return;
    }
    if (bs_magic_can_cast(&f->magic))
        fight_cast(f, env, &ch->book[spell - 1]);
}

void fight_drink(fight *f, const fight_env *env, int spell)
{
    if (spell < 1 || spell > BS_SPELL_COUNT || !env->spells)
        return;
    bs_monster_world mw;
    bs_monster_events mev = {.count = 0};
    bs_magic_world w;
    bs_magic_events ev = {.count = 0};
    world_of(f, env, &mw);
    bs_magic_cast_on_player(&f->magic, magic_world_of(f, env, &mw, &mev, &w), &env->spells[spell - 1], &ev);
    magic_events(f, env, &ev);
    bool dropped = false;
    monster_events(f, env, &mev, &dropped);
}

bool fight_use(fight *f, const fight_env *env, float reach)
{
    if (!f->magic.ready || f->player_died)
        return false;
    if (f->magic.ready_cast.spell.delivery != BS_DELIVERY_TOUCH)
        return true; /* a ranged spell ready: using does nothing (0x4CF77) */
    float eye[3];
    bs_player_eye(env->player, eye);
    float cp = cosf(env->aim_pitch);
    float fwd[3] = {sinf(env->aim_yaw) * cp, -sinf(env->aim_pitch), cosf(env->aim_yaw) * cp};
    float to[3] = {eye[0] + fwd[0] * reach, eye[1] + fwd[1] * reach, eye[2] + fwd[2] * reach}, t;
    int32_t ob;
    float wall = bs_collision_segment(env->collision, eye, to, &t, &ob) ? t * reach : reach;
    /* The monster nearest along the view within reach, its body a 32-unit column from its feet up to its
     * eye. */
    uint32_t best = UINT32_MAX;
    float best_t = wall;
    for (uint32_t i = 0; i < f->monsters.count; i++) {
        const bs_monster *m = &f->monsters.list[i];
        if (m->dead || m->gone)
            continue;
        float c[3] = {m->body.pos[0] - eye[0], m->body.pos[1] - MONSTER_EYE * 0.5f - eye[1], m->body.pos[2] - eye[2]};
        float along = c[0] * fwd[0] + c[1] * fwd[1] + c[2] * fwd[2];
        float d2 = c[0] * c[0] + c[1] * c[1] + c[2] * c[2] - along * along;
        if (along > 0 && along < best_t && d2 <= 32.0f * 32.0f) {
            best_t = along;
            best = i;
        }
    }
    bs_monster_world mw;
    bs_monster_events mev = {.count = 0};
    bs_magic_world w;
    bs_magic_events ev = {.count = 0};
    world_of(f, env, &mw);
    if (best != UINT32_MAX) {
        bs_magic_touch(&f->magic, magic_world_of(f, env, &mw, &mev, &w), best, &ev);
    } else if (wall < reach) {
        f->magic.ready = false; /* touched something else: the spell is spent (0x27D53) */
    } else {
        return false;
    }
    magic_events(f, env, &ev);
    bool dropped = false;
    monster_events(f, env, &mev, &dropped);
    return true;
}

/* An animation's frame at a time, and whether it has played through. */
static uint32_t frame_at(const bs_bsi_image *img, uint32_t elapsed, bool *done)
{
    uint32_t frames = img && img->frames ? img->frames : 1;
    uint32_t ms = img && img->bhdr[8] > 0 ? (uint32_t)img->bhdr[8] : 71;
    uint32_t n = elapsed / ms;
    *done = n >= frames;
    return n < frames ? n : frames - 1;
}

bool fight_draw_hud(fight *f, const fight_env *env, bs_overlay *o)
{
    bool moving = false;
    /* The fist at its own place (0x2A8F8). */
    if (f->punching && f->punch) {
        bool done;
        uint32_t frame = frame_at(f->punch, env->now_ms - f->punch_start, &done);
        bs_overlay_image_frame(o, f->punch, frame, 0, 0);
        moving = true;
    }
    bs_magic_state *m = &f->magic;
    /* The casting hand (0x2A698): held up while a spell is ready, the whole movement when one goes. */
    const bs_bsi_image *hand = f->hands[m->hand_element % BS_ELEMENT_COUNT];
    if (m->hand_playing) {
        bool done;
        uint32_t frame = frame_at(hand, env->now_ms - m->hand_start, &done);
        if (done)
            m->hand_playing = false;
        else
            bs_overlay_image_frame(o, hand, frame, 0, 0);
        moving = true;
    } else if (m->ready) {
        bs_overlay_image_frame(o, hand, 0, 0, 0);
    }
    /* The effects' icons from the top left (0x29080), wrapping at x 635; then the Shield's, blinking below
     * 15 points. */
    const bs_character *ch = env->character;
    int x = 5, y = 5;
    for (uint32_t i = 0; i < BS_EFFECTS; i++) {
        const bs_effect *e = &ch->effects[i];
        const bs_bsi_image *img = e->spell && e->icon < 27 ? f->icons[e->icon] : NULL;
        if (!img)
            continue;
        if (x + img->width > 635) {
            x = 5;
            y += img->height + 20;
        }
        bs_overlay_image(o, img, x - img->x, y - img->y);
        x += img->width;
        moving = true;
    }
    if (ch->shield > 0 && f->icons[25]) {
        const bs_bsi_image *img = f->icons[25];
        if (x + img->width > 635) {
            x = 5;
            y += img->height + 20;
        }
        if (ch->shield >= 15 || env->now_ms % 1000 >= 500)
            bs_overlay_image(o, img, x - img->x, y - img->y);
        moving = moving || ch->shield < 15;
    }
    return moving || m->ready;
}

bool fight_hand(const fight *f, const fight_env *env, bs_scene_xform *out, int *weapon)
{
    bs_hand_pose p;
    if (f->player_died || !bs_hand_pose_at(&f->hand, env->now_ms, &p))
        return false;
    int32_t yaw = (int32_t)lroundf(env->player->yaw * 2048.0f / (2.0f * 3.14159265f)) & 0x7FF;
    bs_hand_rotation(yaw, p.angles, out->rot);
    float off[3];
    bs_hand_offset(yaw, p.offset, off);
    for (int k = 0; k < 3; k++)
        out->pos[k] = env->player->pos[k] + off[k];
    *weapon = f->hand.weapon;
    return true;
}

void fight_draw(fight *f, const float eye[3], uint32_t now_ms, bs_level_renderer *r)
{
    uint32_t n = 0, want = f->monsters.count + BS_MAGIC_MISSILES + BS_MAGIC_BURSTS;
    if (want > f->sprite_cap) {
        bs_level_sprite *s = realloc(f->sprites, sizeof *s * want);
        if (!s)
            return;
        f->sprites = s;
        f->sprite_cap = want;
    }
    for (uint32_t i = 0; i < f->monsters.count; i++) {
        const bs_monster *m = &f->monsters.list[i];
        if (m->gone)
            continue;
        bool mirror;
        uint32_t slot = bs_monster_image(m, eye, &mirror);
        const bs_bsi_image *img = f->images[m->type][slot];
        if (!img)
            img = f->images[m->type][0];
        if (!img)
            continue;
        bs_level_sprite *s = &f->sprites[n++];
        s->image = img;
        s->frame = m->frame;
        s->mirror = mirror;
        memcpy(s->pos, m->body.pos, sizeof s->pos);
    }
    /* Missiles (spelNN, looping) and bursts (explNN or the sparkle, once), centred on where they are. */
    for (uint32_t i = 0; i < BS_MAGIC_MISSILES; i++) {
        const bs_missile *mi = &f->magic.missiles[i];
        const bs_bsi_image *img = mi->live ? f->missile_images[mi->cast.spell.element % BS_ELEMENT_COUNT] : NULL;
        if (!img)
            continue;
        uint32_t ms = img->bhdr[8] > 0 ? (uint32_t)img->bhdr[8] : 71;
        float h = (float)img->height * (img->bhdr[11] > 0 ? (float)img->bhdr[11] / 256.0f : 1.0f);
        f->sprites[n++] = (bs_level_sprite){img, (now_ms - mi->launched) / ms, {mi->pos[0], mi->pos[1] + h * 0.5f, mi->pos[2]}, false};
    }
    for (uint32_t i = 0; i < BS_MAGIC_BURSTS; i++) {
        bs_burst *b = &f->magic.bursts[i];
        if (!b->live)
            continue;
        const bs_bsi_image *img = b->element < 0 ? f->sparkle : f->burst_images[b->element % BS_ELEMENT_COUNT];
        bool done;
        uint32_t frame = frame_at(img, now_ms - b->start, &done);
        if (!img || done) {
            b->live = false;
            continue;
        }
        float h = (float)img->height * (img->bhdr[11] > 0 ? (float)img->bhdr[11] / 256.0f : 1.0f);
        f->sprites[n++] = (bs_level_sprite){img, frame, {b->pos[0], b->pos[1] + h * 0.5f, b->pos[2]}, false};
    }
    if (bs_level_renderer_set_sprites(r, f->sprites, n) != BS_OK)
        BS_WARN("cannot draw the monsters");
}

bool fight_game_over(const fight *f, uint32_t now_ms)
{
    return f->player_died && now_ms - f->player_died >= DEATH_ENDING_MS;
}
