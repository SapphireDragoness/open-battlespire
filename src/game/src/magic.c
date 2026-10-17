#include "bs/game/magic.h"

#include <math.h>
#include <string.h>

#include "bs/game/levelling.h"

/* RE references are GAME.EXE addresses; see docs/formats/spells.md. */

#define CAST_HEIGHT 50.0f        /* spells start this far above the caster's feet (0x27BF1) */
#define MISSILE_SPEED 900.0f     /* units/s (0x275B8) ... */
#define MISSILE_SPEED_L5 1000.0f /* ... on level 5 (0x275D7) */
#define MISSILE_LIFE_MS 10000    /* (0x28BEF) */
#define MISSILE_RANGE 3072.0f    /* from the caster (0x28BDB) */
#define MISSILE_SLOWEST 120.0f   /* (0x28BFB) */
#define DROP_SLOWING 150.0f      /* Organic Drop loses this much speed a second (0x28B9C) */
#define GRAVITY 392.0f           /* units/s^2: Organic Drop falls as the player does (0x3FA97) */
#define PROBE_DROP 30.0f         /* missiles meet the level 30 lower than they fly (0x28C7E) */
#define NEAR_CASTER 100.0f       /* a missile this near its caster doesn't hit it (0x28B74) */
#define HIT_RADIUS 150.0f        /* creatures this near a missile are hit (0x28E3C) */
#define LAUNCH_RADIUS 65.0f      /* ... and this near the way out of the caster's hand (0x277D7) */
#define AREA_RADIUS 192.0f       /* a burst reaches this far (0x27904) */
#define TRAP_RADIUS 120.0f       /* a trap goes off for others this near (0x5B755) */
#define TRAP_HEIGHT 40.0f        /* above the feet (0x284DA) */
#define FLOOR_NORMAL (-0.5f)     /* Organic Drop bounces up off faces facing up more than this (0x28DC7) */
#define TICK_MS 1000             /* Continuous Damage and Poison every second (0x282B5) */
#define POOL_PER_MAGNITUDE 10    /* (0x28537) */
#define SUMMON_NEAR 128.0f       /* summoned monsters appear at a node this far from the player ... */
#define SUMMON_FAR 512.0f        /* ... and no further (0x47B4A) */
#define SUMMON_ROOM 42.0f        /* where no one stands this near (0x47C6A) */

/* Messages. */
enum {
    MSG_NOT_ENOUGH = 80, MSG_ANCHOR_DROPPED = 84, MSG_NO_ANCHOR = 85, MSG_NO_SUMMONS = 420,
    MSG_ETHEREAL = 507, MSG_HEALED = 509, MSG_CONFUSED = 583,
};

/* SPIRE.SND: a missile flying and bursting, by element (0x275E9, 0x27899). */
#define SOUND_LAUNCH 0x194
#define SOUND_BURST 0x199
static const uint8_t element_sound[BS_ELEMENT_COUNT] = {0, 1, 3, 2, 4}; /* 0xED69A */

static void emit(bs_magic_events *ev, bs_magic_event e)
{
    if (ev && ev->count < BS_MAGIC_EVENTS)
        ev->list[ev->count++] = e;
}

static void say(bs_magic_events *ev, int32_t id, int32_t a, int32_t b, int32_t c)
{
    emit(ev, (bs_magic_event){.kind = BS_MAGIC_MESSAGE, .value = id, .args = {a, b, c}});
}

static void sound(bs_magic_events *ev, int32_t id, const float pos[3])
{
    bs_magic_event e = {.kind = BS_MAGIC_SOUND, .value = id};
    memcpy(e.pos, pos, sizeof e.pos);
    emit(ev, e);
}

static float dist3(const float a[3], const float b[3])
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* ---- Who is where ---- */

static bs_monster *monster(const bs_magic_world *w, int32_t who)
{
    if (who < 0 || !w->monsters || (uint32_t)who >= w->monsters->count)
        return NULL;
    bs_monster *m = &w->monsters->list[who];
    return m->dead || m->gone ? NULL : m;
}

/* A caster's or target's feet; false if it's gone. */
static bool feet_of(const bs_magic_world *w, int32_t who, float out[3])
{
    if (who == BS_MAGIC_PLAYER) {
        memcpy(out, w->player->pos, sizeof w->player->pos);
        return true;
    }
    const bs_monster *m = monster(w, who);
    if (!m)
        return false;
    memcpy(out, m->body.pos, sizeof m->body.pos);
    return true;
}

static bool player_ethereal(const bs_magic_world *w)
{
    return (w->character->flags & BS_FLAG_ETHEREAL) != 0;
}

/* The first creature (the player first, unless ethereal) other than `ignore` within radius of p
 * (0x2B502). Only monsters that act count. */
static bool creature_near(const bs_magic_world *w, const float p[3], float radius, int32_t ignore, int32_t *out)
{
    if (ignore != BS_MAGIC_PLAYER && !player_ethereal(w) && dist3(w->player->pos, p) <= radius) {
        *out = BS_MAGIC_PLAYER;
        return true;
    }
    for (uint32_t i = 0; w->monsters && i < w->monsters->count; i++) {
        const bs_monster *m = &w->monsters->list[i];
        if ((int32_t)i == ignore || m->dead || m->gone || !m->active)
            continue;
        if (dist3(m->body.pos, p) <= radius) {
            *out = (int32_t)i;
            return true;
        }
    }
    return false;
}

/* ---- Rolls and costs ---- */

static int32_t caster_level(const bs_magic_world *w, int32_t who)
{
    const bs_monster *m = monster(w, who);
    if (who == BS_MAGIC_PLAYER || !m || !w->tables)
        return w->character->level;
    return w->tables->creatures[m->type].level;
}

/* The rolls (0x28A1E): duration by the caster's level, magnitude by the level number. */
static void roll(const bs_magic_world *w, bs_spell_cast *c)
{
    c->duration_ms = bs_spell_duration(&c->spell, caster_level(w, c->owner));
    c->magnitude = bs_spell_magnitude(&c->spell, w->level_number, w->rng);
}

static int32_t player_skill(const bs_character *ch, const bs_spell *s)
{
    return s->skill >= 0 && s->skill < BS_SKILL_COUNT ? ch->skills[s->skill] : 0;
}

/* ---- Damage ---- */

int32_t bs_element_percent(int element, uint32_t flags, uint8_t resists, uint8_t immune, uint8_t low,
                           uint8_t critical, int32_t chance, bs_rng *r)
{
    if (element < 0 || element >= BS_ELEMENT_COUNT)
        return 100;
    if (flags & (1u << (BS_FLAG_IMMUNE_FIRST + element)))
        return 0;
    uint8_t bit = bs_element_bit(element);
    if (immune & bit)
        return 0;
    if (critical & bit)
        return 100;
    if (low & bit)
        chance >>= 1;
    if (resists & bit)
        chance += chance >> 1;
    chance = chance < 5 ? 5 : chance > 95 ? 95 : chance;
    int32_t roll_ = bs_rng_range(r, 1, 100);
    if (roll_ > chance)
        return 100;
    if (chance - 20 > roll_)
        return 0;
    return (chance - roll_) * 5;
}

/* A spell's damage to a creature (0x42E2F with an element): the tolerances first. */
static void damage(const bs_magic_world *w, int32_t target, int32_t amount, int element, const float from[3],
                   bs_magic_events *ev)
{
    if (amount <= 0)
        return;
    if (target == BS_MAGIC_PLAYER) {
        bs_character *c = w->character;
        int32_t chance = 40 + (bs_character_attribute(c, 2) - 50) / 2; /* Willpower (0x2AAB6) */
        int32_t pct = bs_element_percent(element, c->flags, c->resists, c->immune, c->low_tolerance,
                                         c->critical_weakness, chance, w->rng);
        int32_t d = amount * pct / 100;
        if (d <= 0)
            return;
        c->health -= d;
        emit(ev, (bs_magic_event){.kind = BS_MAGIC_PLAYER_HURT, .value = d});
        return;
    }
    bs_monster *m = monster(w, target);
    if (!m || m->unhurt || !w->tables || !w->monster_world)
        return; /* level 5's Hernes take no harm from spells either (0x4348C) */
    const bs_creature *c = &w->tables->creatures[m->type];
    int32_t pct = bs_element_percent(element, m->flags, c->resists, c->immune, c->low_tolerance,
                                     c->critical_weakness, 30, w->rng);
    int32_t d = amount * pct / 100;
    if (d <= 0)
        return;
    /* Not the attacker's health: spells don't make monsters flee (0x433B0 asks a creature). */
    if (bs_monster_hurt(w->monsters, (uint32_t)target, w->monster_world, d, 1, from, 0, w->monster_events))
        emit(ev, (bs_magic_event){.kind = BS_MAGIC_MONSTER_DIED, .monster = (uint32_t)target});
}

/* ---- Effects ---- */

/* A timed effect on the player, made or lengthened (0x295A0): one per spell. */
static bs_effect *add_effect(const bs_magic_world *w, const bs_spell_cast *c, uint32_t flags)
{
    bs_character *ch = w->character;
    bs_effect *free_slot = NULL;
    for (uint32_t i = 0; i < BS_EFFECTS; i++) {
        bs_effect *e = &ch->effects[i];
        if (e->spell == c->spell.effect) {
            e->until += c->duration_ms;
            e->flags |= flags;
            return e;
        }
        if (!e->spell && !free_slot)
            free_slot = e;
    }
    if (!free_slot)
        return NULL;
    memset(free_slot, 0, sizeof *free_slot);
    free_slot->spell = c->spell.effect;
    free_slot->icon = c->spell.icon;
    free_slot->until = w->now_ms + c->duration_ms;
    free_slot->flags = flags;
    return free_slot;
}

/* Effects with flags that only work on the player (0x280F6 ..): spell number → flag. */
static uint32_t player_flag(int spell)
{
    switch (spell) {
    case BS_SPELL_DETECT_SPELL: return BS_FLAG_DETECT_SPELL;
    case BS_SPELL_DETECT_ENEMY: return BS_FLAG_DETECT_ENEMY;
    case BS_SPELL_DETECT_INVISIBILITY: return BS_FLAG_DETECT_INVISIBILITY;
    case BS_SPELL_INVISIBILITY: return BS_FLAG_INVISIBLE;
    case BS_SPELL_SHADOW: return BS_FLAG_SHADOW;
    case BS_SPELL_CHAMELEON: return BS_FLAG_CHAMELEON;
    case BS_SPELL_SLOW_FALL: return BS_FLAG_SLOW_FALL;
    case BS_SPELL_REFLECTION: return BS_FLAG_REFLECT;
    case BS_SPELL_RESISTANCE: return BS_FLAG_RESIST_SPELLS;
    case BS_SPELL_ABSORPTION: return BS_FLAG_ABSORB;
    case BS_SPELL_FIRE_SHIELD: return BS_FLAG_FIRE_SHIELD;
    case BS_SPELL_JUMPING: return BS_FLAG_JUMPING;
    case BS_SPELL_RUNNING: return BS_FLAG_RUNNING;
    case BS_SPELL_ETHEREALNESS: return BS_FLAG_ETHEREAL;
    default: return 0;
    }
}

static void summon(bs_magic_state *mg, const bs_magic_world *w, const bs_spell_cast *c, bs_magic_events *ev);
static void add_burst(bs_magic_state *mg, const bs_magic_world *w, const float pos[3], int element);
static void set_trap(bs_magic_state *mg, const bs_magic_world *w, const bs_spell_cast *c, int32_t target);

/* A spell takes effect on a creature (0x27F4C). from: where it came from, for pushes. */
static void effect(bs_magic_state *mg, const bs_magic_world *w, const bs_spell_cast *c, int32_t target, const float from[3],
                   bs_magic_events *ev)
{
    bool on_player = target == BS_MAGIC_PLAYER;
    bs_character *ch = w->character;
    bs_monster *m = monster(w, target);
    if (!on_player && !m)
        return;
    int spell = c->spell.effect;
    int32_t mag = c->magnitude;
    uint32_t flag = player_flag(spell);
    if (flag) {
        if (on_player) {
            bs_effect *e = add_effect(w, c, flag);
            if (e && spell == BS_SPELL_REFLECTION)
                e->pools[BS_POOL_REFLECT] = mag * POOL_PER_MAGNITUDE;
            else if (e && spell == BS_SPELL_RESISTANCE)
                e->pools[BS_POOL_RESIST] = mag * POOL_PER_MAGNITUDE;
            else if (e && spell == BS_SPELL_ABSORPTION)
                e->pools[BS_POOL_ABSORB] = mag * POOL_PER_MAGNITUDE;
        } else if (spell == BS_SPELL_ETHEREALNESS) {
            mg->teleport_screen = true; /* falls into Teleport's case (0x28779) */
        }
        return;
    }
    switch (spell) {
    case BS_SPELL_MONSTER_SUMMONING:
        summon(mg, w, c, ev);
        break;
    case BS_SPELL_CONTINUOUS_DAMAGE:
    case BS_SPELL_POISON:
        if (on_player) {
            bs_effect *e = add_effect(w, c, spell == BS_SPELL_POISON ? BS_FLAG_POISONED : BS_FLAG_CONTINUOUS_DAMAGE);
            if (e) {
                e->tick_damage = -(mag / 5);
                e->tick_last = w->now_ms;
                e->tick_ms = TICK_MS;
            }
        } else {
            damage(w, target, mag, c->spell.element, from, ev); /* all at once (0x282C1) */
        }
        break;
    case BS_SPELL_CONFUSION:
        if (on_player) {
            ch->confused_until = w->now_ms + c->duration_ms;
            say(ev, MSG_CONFUSED, 0, 0, 0);
        } else {
            m->state = BS_AI_WANDER; /* (0x283D9) */
        }
        break;
    case BS_SPELL_VAMPIRIC_DRAIN:
        /* The player gains, whoever cast it (0x283E8). */
        ch->health = ch->health + mag > ch->health_max ? ch->health_max : ch->health + mag;
        damage(w, target, mag, c->spell.element, from, ev);
        break;
    case BS_SPELL_DELAYED_DAMAGE:
        set_trap(mg, w, c, target);
        break;
    case BS_SPELL_DISPEL_MAGIC:
        break; /* a list of spells to end (0x2AD30): not yet */
    case BS_SPELL_CAUSE_DAMAGE:
        damage(w, target, mag, c->spell.element, from, ev);
        break;
    case BS_SPELL_CURE_POISON:
        if (on_player)
            for (uint32_t i = 0; i < BS_EFFECTS; i++)
                if (ch->effects[i].spell && (ch->effects[i].flags & BS_FLAG_POISONED))
                    ch->effects[i].spell = 0;
        break;
    case BS_SPELL_CURE_HEALTH: {
        /* The player's double healing counts for whoever is healed (0x2867A). */
        int32_t heal = ch->double_healing ? mag * 2 : mag;
        if (on_player) {
            ch->health = ch->health + heal > ch->health_max ? ch->health_max : ch->health + heal;
            say(ev, MSG_HEALED, heal, ch->health, ch->health_max);
        } else {
            m->health = m->health + heal > m->health_max ? m->health_max : m->health + heal;
        }
        break;
    }
    case BS_SPELL_TELEPORT:
        mg->teleport_screen = true;
        break;
    case BS_SPELL_SHIELD:
        ch->shield += mag; /* the player's, whoever it was cast on (0x287D4) */
        break;
    case BS_SPELL_ELEMENT_RESISTANCE:
        if (on_player && c->spell.element < BS_ELEMENT_COUNT)
            add_effect(w, c, 1u << (BS_FLAG_IMMUNE_FIRST + c->spell.element));
        break;
    case BS_SPELL_SLOW:
    case BS_SPELL_HASTE:
    case BS_SPELL_STRENGTH:
        if (on_player) {
            bs_effect *e = add_effect(w, c, 0);
            if (e) {
                int a = spell == BS_SPELL_STRENGTH ? 0 : 6;
                e->attributes[a] = spell == BS_SPELL_SLOW ? -mag : mag;
            }
        }
        break;
    default:
        break; /* Dispel Sigil: multiplayer */
    }
}

/* A spell on a creature straight from its caster (0x296C4): the hand or a sparkle, then the rolls and the
 * effect. */
static void apply(bs_magic_state *mg, const bs_magic_world *w, bs_spell_cast *c, int32_t target, bs_magic_events *ev)
{
    float from[3] = {0, 0, 0};
    feet_of(w, c->owner, from);
    if (c->owner == BS_MAGIC_PLAYER) {
        mg->hand_playing = true;
        mg->hand_start = w->now_ms;
        mg->hand_element = c->spell.element;
        bs_skill_used(w->character, bs_school_skill(c->spell.school), 1); /* (0x29713) */
    } else {
        float at[3] = {from[0], from[1] - CAST_HEIGHT, from[2]};
        add_burst(mg, w, at, -1);
    }
    roll(w, c);
    effect(mg, w, c, target, from, ev);
}

/* ---- Resisting, absorbing, reflecting (0x27DAC) ---- */

enum { REACHES, STOPPED, REFLECTED };

static int defend(const bs_magic_world *w, int32_t target, bs_missile *mi)
{
    bs_spell_cast *c = &mi->cast;
    uint32_t flags;
    int32_t *pools, *sp, sp_max;
    bool absorbs;
    int32_t willpower;
    if (target == BS_MAGIC_PLAYER) {
        bs_character *ch = w->character;
        flags = ch->flags;
        pools = ch->pools;
        sp = &ch->spell_points;
        sp_max = ch->spell_points_max;
        absorbs = ch->absorbs_spells;
        willpower = bs_character_attribute(ch, 2);
    } else {
        bs_monster *m = monster(w, target);
        if (!m)
            return STOPPED;
        flags = m->flags;
        pools = m->pools;
        sp = &m->spell_points;
        sp_max = m->spell_points_max;
        absorbs = false;
        willpower = m->attributes[2];
    }
    if ((flags & BS_FLAG_RESIST_SPELLS) && c->cost <= pools[BS_POOL_RESIST]) {
        pools[BS_POOL_RESIST] -= c->cost;
        return STOPPED;
    }
    bool own = c->owner == target;
    if (((flags & BS_FLAG_ABSORB) || absorbs) && !own) {
        bool take = absorbs ? bs_rng_range(w->rng, 1, 100) <= willpower || c->cost <= pools[BS_POOL_ABSORB]
                            : c->cost <= pools[BS_POOL_ABSORB];
        if (take) {
            if (!absorbs)
                pools[BS_POOL_ABSORB] -= c->cost;
            if (*sp + c->cost > sp_max)
                return REACHES; /* no room for the spell points: it hits after all (0x27E9C) */
            *sp += c->cost;
            return STOPPED;
        }
    }
    if ((flags & BS_FLAG_REFLECT) && c->cost <= pools[BS_POOL_REFLECT] && !own) {
        pools[BS_POOL_REFLECT] -= c->cost;
        for (int k = 0; k < 3; k++)
            mi->dir[k] = -mi->dir[k];
        c->owner = target;
        return REFLECTED;
    }
    return REACHES;
}

/* ---- Missiles ---- */

static void add_burst(bs_magic_state *mg, const bs_magic_world *w, const float pos[3], int element)
{
    bs_burst *b = &mg->bursts[w->now_ms % BS_MAGIC_BURSTS];
    for (uint32_t i = 0; i < BS_MAGIC_BURSTS; i++)
        if (!mg->bursts[i].live) {
            b = &mg->bursts[i];
            break;
        }
    *b = (bs_burst){.pos = {pos[0], pos[1], pos[2]}, .element = (int8_t)element, .start = w->now_ms, .live = true};
}

/* A missile or trap bursts (0x2786F), on `hit` or on whoever is near. Returns true if it was reflected. */
static bool burst(bs_magic_state *mg, const bs_magic_world *w, bs_missile *mi, int32_t hit, bs_magic_events *ev)
{
    bs_spell_cast *c = &mi->cast;
    sound(ev, SOUND_BURST + element_sound[c->spell.element % BS_ELEMENT_COUNT], mi->pos);
    roll(w, c);
    if (c->spell.delivery == BS_DELIVERY_RANGE && hit != BS_MAGIC_NOBODY) {
        if (hit == BS_MAGIC_PLAYER && player_ethereal(w))
            return false;
        int r = defend(w, hit, mi);
        if (r == REACHES)
            effect(mg, w, c, hit, mi->pos, ev);
        return r == REFLECTED;
    }
    /* Everyone within reach, but the caster of a spell on the area around it. */
    int32_t skip = c->spell.delivery == BS_DELIVERY_AREA_CASTER ? c->owner : BS_MAGIC_NOBODY;
    int32_t list[1 + 256];
    uint32_t n = 0;
    if (skip != BS_MAGIC_PLAYER && dist3(w->player->pos, mi->pos) <= AREA_RADIUS)
        list[n++] = BS_MAGIC_PLAYER;
    for (uint32_t i = 0; w->monsters && i < w->monsters->count && n < 1 + 256; i++) {
        const bs_monster *m = &w->monsters->list[i];
        if ((int32_t)i != skip && !m->dead && !m->gone && m->active && dist3(m->body.pos, mi->pos) <= AREA_RADIUS)
            list[n++] = (int32_t)i;
    }
    for (uint32_t k = 0; k < n; k++) {
        int r = defend(w, list[k], mi);
        if (!(list[k] == BS_MAGIC_PLAYER && player_ethereal(w)) && r == REACHES) {
            effect(mg, w, c, list[k], mi->pos, ev);
            add_burst(mg, w, mi->pos, c->spell.element);
        }
        if (r == REFLECTED)
            return true;
    }
    return false;
}

static void step(bs_missile *mi, float dist)
{
    for (int k = 0; k < 3; k++)
        mi->pos[k] += mi->dir[k] * dist;
}

/* One frame of a missile (0x28AD0). Returns false when it's gone. */
static bool fly(bs_magic_state *mg, const bs_magic_world *w, bs_missile *mi, int32_t ignore, uint32_t dt_ms,
                bs_magic_events *ev)
{
    float caster[3];
    if (!feet_of(w, mi->cast.owner, caster))
        memcpy(caster, mi->pos, sizeof caster);
    float d = dist3(caster, mi->pos);
    /* The game ignores the caster within 100 (0x28B74) but hits anyone within 150: whether a missile
     * passes its own caster depends on how far it moves a frame. Here the caster is ignored until the
     * missile has once been clear of it (an addition). */
    if (d > HIT_RADIUS)
        mi->cleared = true;
    if ((d <= NEAR_CASTER || !mi->cleared) && ignore == BS_MAGIC_NOBODY)
        ignore = mi->cast.owner;
    float dt = (float)dt_ms / 1000.0f;
    if (mi->cast.spell.detonation == BS_DETONATE_ORGANIC_DROP)
        mi->speed -= DROP_SLOWING * dt;
    if (d > MISSILE_RANGE || w->now_ms - mi->launched > MISSILE_LIFE_MS || mi->speed < MISSILE_SLOWEST)
        return false;
    float to[3];
    for (int k = 0; k < 3; k++)
        to[k] = mi->pos[k] + mi->dir[k] * mi->speed * dt;
    if (mi->cast.spell.detonation == BS_DETONATE_ORGANIC_DROP) {
        mi->fall += GRAVITY * dt;
        to[1] += mi->fall * dt;
    }
    float a[3] = {mi->pos[0], mi->pos[1] + PROBE_DROP, mi->pos[2]}, b[3] = {to[0], to[1] + PROBE_DROP, to[2]}, t;
    int32_t object;
    if (w->collision && bs_collision_segment(w->collision, a, b, &t, &object)) {
        if (mi->cast.spell.detonation == BS_DETONATE_ANY) {
            if (mi->cast.spell.delivery != BS_DELIVERY_TOUCH && burst(mg, w, mi, BS_MAGIC_NOBODY, ev)) {
                step(mi, mi->speed * dt);
                step(mi, mi->speed * dt);
                return true;
            }
            add_burst(mg, w, mi->pos, mi->cast.spell.element);
            return false;
        }
        /* Organic and Organic Drop bounce off the face (0x2A9BB); Organic Drop also up off floors. */
        float hitp[3];
        for (int k = 0; k < 3; k++)
            hitp[k] = a[k] + (b[k] - a[k]) * t;
        float back[3];
        for (int k = 0; k < 3; k++)
            back[k] = hitp[k] - mi->dir[k] * 4.0f;
        bs_collision_hit h;
        float n[3] = {-mi->dir[0], -mi->dir[1], -mi->dir[2]};
        if (bs_collision_sphere(w->collision, back, 8.0f, &h, 1))
            memcpy(n, h.normal, sizeof n);
        if (mi->cast.spell.detonation == BS_DETONATE_ORGANIC_DROP && n[1] < FLOOR_NORMAL)
            mi->fall = -mi->fall;
        float dot = mi->dir[0] * n[0] + mi->dir[1] * n[1] + mi->dir[2] * n[2];
        for (int k = 0; k < 3; k++)
            mi->dir[k] -= 2.0f * dot * n[k];
        return true;
    }
    memcpy(mi->pos, to, sizeof mi->pos);
    int32_t hit;
    if (creature_near(w, mi->pos, HIT_RADIUS, ignore, &hit)) {
        if (burst(mg, w, mi, hit, ev)) {
            step(mi, mi->speed * dt);
            step(mi, mi->speed * dt);
            return true;
        }
        add_burst(mg, w, mi->pos, mi->cast.spell.element);
        return false;
    }
    return true;
}

/* A missile leaves its caster (0x274BE): at `target`'s feet if one, else where the player aims. */
static void launch(bs_magic_state *mg, const bs_magic_world *w, bs_spell_cast *c, const float target[3],
                   bs_magic_events *ev)
{
    float from[3];
    if (!feet_of(w, c->owner, from))
        return;
    bs_missile *mi = NULL;
    for (uint32_t i = 0; i < BS_MAGIC_MISSILES && !mi; i++)
        if (!mg->missiles[i].live)
            mi = &mg->missiles[i];
    if (!mi)
        return;
    if (c->owner == BS_MAGIC_PLAYER)
        bs_skill_used(w->character, bs_school_skill(c->spell.school), 1); /* (0x2752B) */
    memset(mi, 0, sizeof *mi);
    mi->cast = *c;
    mi->live = true;
    mi->pos[0] = from[0];
    mi->pos[1] = from[1] - CAST_HEIGHT;
    mi->pos[2] = from[2];
    mi->launched = w->now_ms;
    mi->speed = w->level_number == 5 ? MISSILE_SPEED_L5 : MISSILE_SPEED;
    sound(ev, SOUND_LAUNCH + element_sound[c->spell.element % BS_ELEMENT_COUNT], mi->pos);
    roll(w, &mi->cast);
    float dir[3];
    if (target) {
        for (int k = 0; k < 3; k++)
            dir[k] = target[k] - mi->pos[k];
    } else {
        /* 1024 ahead along the view (0x276C2). */
        float cp = cosf(w->aim_pitch);
        dir[0] = sinf(w->aim_yaw) * cp;
        dir[1] = -sinf(w->aim_pitch);
        dir[2] = cosf(w->aim_yaw) * cp;
    }
    float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len <= 0) {
        dir[0] = dir[1] = 0;
        dir[2] = 1;
        len = 1;
    }
    for (int k = 0; k < 3; k++)
        mi->dir[k] = dir[k] / len;
    if (c->owner == BS_MAGIC_PLAYER) {
        mg->hand_playing = true;
        mg->hand_start = w->now_ms;
        mg->hand_element = c->spell.element;
    }
    /* Someone right at the caster's hand is hit at once (0x277D7); else the first step. */
    float mid[3] = {(from[0] + mi->pos[0]) * 0.5f, (from[1] + mi->pos[1]) * 0.5f, (from[2] + mi->pos[2]) * 0.5f};
    int32_t hit;
    if (creature_near(w, mid, LAUNCH_RADIUS, c->owner, &hit)) {
        burst(mg, w, mi, hit, ev);
        mi->live = false;
        return;
    }
    if (!fly(mg, w, mi, c->owner, 0, ev))
        mi->live = false;
}

/* ---- Traps and summoning ---- */

static void set_trap(bs_magic_state *mg, const bs_magic_world *w, const bs_spell_cast *c, int32_t target)
{
    float at[3];
    if (!feet_of(w, target, at))
        return;
    for (uint32_t i = 0; i < BS_MAGIC_TRAPS; i++) {
        bs_trap *t = &mg->traps[i];
        if (t->live)
            continue;
        t->cast = *c;
        t->cast.spell.delivery = BS_DELIVERY_AREA_CASTER; /* (0x28491) */
        t->pos[0] = at[0];
        t->pos[1] = at[1] - TRAP_HEIGHT;
        t->pos[2] = at[2];
        t->until = w->now_ms + c->duration_ms;
        t->live = true;
        return;
    }
}

/* Monster Summoning (0x2804A → 0x47B33): one of the level's four creatures from the hidden reserve, at a
 * free node 128-512 from the player, for the spell's duration. */
static void summon(bs_magic_state *mg, const bs_magic_world *w, const bs_spell_cast *c, bs_magic_events *ev)
{
    bool by_player = c->owner == BS_MAGIC_PLAYER;
    uint32_t reserve = 0;
    if (w->monsters && w->monsters->point_count < 255)
        reserve = 255 - w->monsters->point_count;
    reserve = reserve > BS_MAGIC_SUMMONS ? BS_MAGIC_SUMMONS : reserve;
    if (!w->monsters || !w->tables || !w->nodes || mg->summon_count >= reserve) {
        if (by_player)
            say(ev, MSG_NO_SUMMONS, 0, 0, 0);
        return;
    }
    uint32_t near[256], n = 0;
    for (uint32_t i = 0; i < w->nodes->count && n < 256; i++) {
        float d = dist3(w->nodes->nodes[i].pos, w->player->pos);
        if (d >= SUMMON_NEAR && d <= SUMMON_FAR)
            near[n++] = i;
    }
    uint32_t first = n ? (uint32_t)bs_rng_range(w->rng, 0, (int32_t)n - 1) : 0;
    for (uint32_t k = 0; k < n; k++) {
        const float *p = w->nodes->nodes[near[(first + k) % n]].pos;
        int32_t who;
        if (creature_near(w, p, SUMMON_ROOM, BS_MAGIC_NOBODY, &who))
            continue;
        int row = w->level_number >= 0 && w->level_number < BS_CREATURE_LEVELS ? w->level_number : 0;
        uint32_t type = w->tables->level_creatures[row][bs_rng_range(w->rng, 0, 3)];
        bs_monster *m = bs_monsters_add(w->monsters, w->tables, type, p, w->collision);
        if (!m)
            break;
        m->summoned = true;
        m->ally = by_player;
        mg->summoned[mg->summon_count] = w->monsters->count - 1;
        mg->summoned_until[mg->summon_count] = w->now_ms + c->duration_ms;
        mg->summon_count++;
        return;
    }
    if (by_player)
        say(ev, MSG_NO_SUMMONS, 0, 0, 0);
}

/* ---- Casting ---- */

void bs_magic_reset(bs_magic_state *m)
{
    memset(m, 0, sizeof *m);
}

bool bs_magic_can_cast(const bs_magic_state *m)
{
    return !m->ready && !m->hand_playing;
}

/* The casting (0x27AEB) of a paid spell by its caster. */
static void cast(bs_magic_state *mg, const bs_magic_world *w, bs_spell_cast *c, int32_t target, bs_magic_events *ev)
{
    switch (c->spell.delivery) {
    case BS_DELIVERY_CASTER:
        apply(mg, w, c, c->owner, ev);
        break;
    case BS_DELIVERY_TOUCH:
        if (c->owner == BS_MAGIC_PLAYER) {
            mg->ready = true;
            mg->ready_cast = *c;
        } else {
            apply(mg, w, c, target, ev);
        }
        break;
    default:
        if (c->owner == BS_MAGIC_PLAYER) {
            mg->ready = true;
            mg->ready_cast = *c;
        } else {
            float at[3];
            if (feet_of(w, target, at))
                launch(mg, w, c, at, ev);
        }
        break;
    }
}

bool bs_magic_cast_player(bs_magic_state *m, const bs_magic_world *w, const bs_spell *s, bs_magic_events *ev)
{
    bs_character *ch = w->character;
    if (player_ethereal(w)) {
        say(ev, MSG_ETHEREAL, 0, 0, 0);
        return false;
    }
    int32_t cost = bs_spell_cost(s, player_skill(ch, s));
    if (ch->spell_points < cost) {
        say(ev, MSG_NOT_ENOUGH, 0, 0, 0);
        return false;
    }
    ch->spell_points -= cost;
    bs_spell_cast c = {.spell = *s, .cost = cost, .owner = BS_MAGIC_PLAYER};
    cast(m, w, &c, BS_MAGIC_PLAYER, ev);
    return true;
}

void bs_magic_cast_on_player(bs_magic_state *m, const bs_magic_world *w, const bs_spell *s, bs_magic_events *ev)
{
    bs_spell_cast c = {.spell = *s, .owner = BS_MAGIC_PLAYER};
    apply(m, w, &c, BS_MAGIC_PLAYER, ev);
}

bool bs_magic_touch(bs_magic_state *m, const bs_magic_world *w, uint32_t target, bs_magic_events *ev)
{
    if (!m->ready || m->ready_cast.spell.delivery != BS_DELIVERY_TOUCH)
        return false;
    m->ready = false;
    apply(m, w, &m->ready_cast, (int32_t)target, ev);
    return true;
}

bool bs_magic_launch_ready(bs_magic_state *m, const bs_magic_world *w, bs_magic_events *ev)
{
    if (!m->ready || m->ready_cast.spell.delivery == BS_DELIVERY_TOUCH)
        return false;
    m->ready = false;
    launch(m, w, &m->ready_cast, NULL, ev);
    return true;
}

void bs_magic_cast_monster(bs_magic_state *m, const bs_magic_world *w, uint32_t who, int spell, int element,
                           int delivery, bs_magic_events *ev)
{
    if (spell < 1 || spell > BS_SPELL_COUNT || !w->spells || !monster(w, (int32_t)who))
        return;
    bs_spell_cast c = {.spell = w->spells[spell - 1], .owner = (int32_t)who};
    c.spell.element = (uint8_t)element;
    c.spell.delivery = (uint8_t)delivery;
    c.spell.detonation = BS_DETONATE_ANY;
    c.cost = bs_spell_cost(&c.spell, w->monsters->list[who].skill);
    cast(m, w, &c, BS_MAGIC_PLAYER, ev);
}

uint32_t bs_magic_anchor(bs_character *c, bs_player *body, bool drop)
{
    if (drop) {
        c->has_anchor = true;
        memcpy(c->anchor, body->pos, sizeof c->anchor);
        return MSG_ANCHOR_DROPPED;
    }
    if (!c->has_anchor)
        return MSG_NO_ANCHOR;
    memcpy(body->pos, c->anchor, sizeof body->pos);
    body->vy = 0;
    return 0;
}

/* ---- Every frame ---- */

/* The player's effects (0x28F11, 0x293EF): flags, pools and attribute changes made anew; the damage of
 * Continuous Damage and Poison; effects that ended go. */
static void upkeep(const bs_magic_world *w)
{
    bs_character *c = w->character;
    c->flags = 0;
    memset(c->pools, 0, sizeof c->pools);
    memset(c->bonus, 0, sizeof c->bonus);
    for (uint32_t i = 0; i < BS_EFFECTS; i++) {
        bs_effect *e = &c->effects[i];
        if (!e->spell)
            continue;
        if (w->now_ms >= e->until) {
            e->spell = 0;
            continue;
        }
        c->flags |= e->flags;
        for (int k = 0; k < 8; k++)
            c->bonus[k] += e->attributes[k];
        if (e->tick_damage && w->now_ms - e->tick_last > e->tick_ms) {
            e->tick_last = w->now_ms;
            c->health += e->tick_damage;
        }
        for (int k = 0; k < 3; k++)
            c->pools[k] += e->pools[k];
    }
    if (c->health > c->health_max)
        c->health = c->health_max;
    if (c->spell_points > c->spell_points_max)
        c->spell_points = c->spell_points_max;
}

void bs_magic_update(bs_magic_state *m, const bs_magic_world *w, uint32_t dt_ms, bs_magic_events *ev)
{
    upkeep(w);
    for (uint32_t i = 0; i < BS_MAGIC_MISSILES; i++) {
        bs_missile *mi = &m->missiles[i];
        if (mi->live && !fly(m, w, mi, BS_MAGIC_NOBODY, dt_ms, ev))
            mi->live = false;
    }
    /* Traps (0x5B702): someone other than the setter within 120 sets one off as Cause Damage. */
    for (uint32_t i = 0; i < BS_MAGIC_TRAPS; i++) {
        bs_trap *t = &m->traps[i];
        if (!t->live)
            continue;
        if (w->now_ms >= t->until) {
            t->live = false;
            continue;
        }
        int32_t who;
        if (!creature_near(w, t->pos, TRAP_RADIUS, t->cast.owner, &who))
            continue;
        bs_missile mi = {.cast = t->cast, .pos = {t->pos[0], t->pos[1], t->pos[2]}};
        mi.cast.spell.effect = BS_SPELL_CAUSE_DAMAGE;
        t->live = false;
        burst(m, w, &mi, BS_MAGIC_NOBODY, ev);
    }
    /* Summoned monsters leave when their time is up. */
    for (uint32_t i = 0; i < m->summon_count;) {
        bs_monster *s = w->monsters && m->summoned[i] < w->monsters->count ? &w->monsters->list[m->summoned[i]] : NULL;
        if (!s || s->gone || w->now_ms >= m->summoned_until[i]) {
            if (s)
                s->gone = true;
            m->summon_count--;
            m->summoned[i] = m->summoned[m->summon_count];
            m->summoned_until[i] = m->summoned_until[m->summon_count];
            continue;
        }
        i++;
    }
}
