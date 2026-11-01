# Creatures

Status: **decoded for melee** (static analysis of `GAME.EXE` v1.5,
2026-10-14). The creature, image, per-level and sound tables are read
(`bs/formats/creatures.h`). Spawning, the AI's states, walking, the melee
blow and damage are traced and implemented (`bs/game/monsters.h`,
`bs/game/combat.h`). The node search and path following are approximated.
Their spells are in [spells.md](spells.md); dialogue and missiles (bows)
aren't decoded yet.

Addresses are in the data object. A creature's fields are given at their
offset in the character record (`+0x3D` into the object), which is laid out
the same as the player's.

## Creature table (`0xE3559`)

16 records of 206 (`0xCE`) bytes. In the code a creature number is 0-based
(the table index); spawn records count from 1. The spawner's maker
(`0x46A51`) creates a character object (type `0x12`, `0x317` bytes) and copies
these fields in:

| # | Creature | Images | | # | Creature | Images |
|---|---|---|---|---|---|---|
| 0 | Scamp | `smp` | | 8 | Fire Daedra | `fda` |
| 1 | Vermai | `ver` | | 9 | Frost Daedra | `icd` |
| 2 | Dremora | `dre` | | 10 | Herne | `mas` |
| 3 | Spider Daedra | `spd` | | 11 | Clannfear | `cln` |
| 4 | Skeleton | `skl` | | 12 | Seducer | `sdc` |
| 5 | Ghost | `ght` | | 13 | Dark Seducer | `dsr` |
| 6 | Wraith | `wth` | | 14 | Daedra Count | `mld` |
| 7 | Morphoid Daedra | `mrd` | | 15 | Daedra Lord | `dld` |

| Offset | Copied to | Values | Meaning |
|---|---|---|---|
| 0x00 | +0x00 | "Scamp" | name |
| 0x20 | +0x20 and +0x40 | 8 × i32: Scamp 20 40 10 60 20 10 65 30 | the eight attributes, Strength .. Luck (+0x40: their base values) |
| 0x40 | +0x60, +0x62, +0x64 (u16) | Scamp 0, Dremora 300, Daedra Lord 500 | spell points |
| 0x44 | +0x66, +0x6A, +0x6E | Scamp 30, Skeleton 60, Wraith 2000 | health (current, maximum) |
| 0x48 | +0x72 + 12 n, n = 0..20 | Scamp 20, Skeleton 50, Daedra Count 100 | every one of the 21 skills |
| 0x4C | +0x172 | 0, Seducer `0x10000`, Dark Seducer `0x18000`, Daedra Lord `0x8000` | flags |
| 0x6C | +0x176 | Scamp 1, Skeleton 3, Daedra Count 7 | level |
| 0x70 | +0x268 + 4 n, n = 0..6 | Scamp 2, Skeleton 8, Daedra Lord 10 | armour, the same at every hit location |
| 0x74, 0x78 | | pointers, casters only | two 0-terminated lists of the spells it casts, in a block right before the table ([spells.md](spells.md#monsters-casting-0x47645)) |
| 0x7C | | u16: Scamp 20, Vermai 120, Morphoid 200; Ghost, Wraith 0 | mass: a blow's push is weighed against a quarter of it (`0x43A1D`). A mass of 0 is never pushed |
| 0x7E | | 24 bytes | a weapon record like the player's weapons (`0xDB47D`): +0xC and +0x10 the damage roll (Scamp 1–6, Skeleton 12–18, Daedra Lord 15–30), +0x14 the skill used (16, hand to hand) |
| 0x96 | +0x17A | 56 bytes, mostly 0 | +0x18, +0x19, +0x25, +0x26: the elements it resists, is immune to, has a low tolerance or a critical weakness for (bits by element, [spells.md](spells.md#effects-0x27f4c)); the rest not decoded |

The maker also sets these fields:
- +0x2A4, +0x2A8, +0x2AC = 150: the spell pools for resisting, absorbing
  and reflecting spells (`0x27DAC`).
- +0x232 = 9: the AI state, idle.
- +0x22A = 8: the team. All monsters are on one team.
- +0x25C = `0xFF`: no animation yet.
- +0x24E = the creature number.

## Images (`0xE4239`)

This table follows the creature table directly: 20 records of 799 (`0x31F`)
bytes, the 16 creatures then four multiplayer figures (`rma`, `rfe`, `bma`,
`bfe`). Each record holds 47 image names of 13 bytes, then room for their 47
name hashes (+0x263, filled in when the images load, `0x46CD2`). An animation
has five views, from 0 (seen from the front) to 4 (from behind):

| Slots | Image | Frames (Scamp) |
|---|---|---|
| 0–4 | `smpw00`–`04`: walking | 8 × 142 ms |
| 5–9 | `smpa00`–`04`: attacking | 8 × 114 ms |
| 10–39 | empty for all 16 | |
| 40–44 | `smps00`–`04`: standing | 4 × 128 ms, played back and forth |
| 45 | `smpd00`: dying | 16 × 57 ms |

The frame count is BHDR field 7 and the frame time is field 8. Field 11 is
the image's scale; it differs by view (Scamp: 58, 74, 79, 99, 112).

**Which image** (`0x47143`): the view is ((facing + 128) − angle from the
monster to the player) mod 2048, divided by 256, giving 0–7. Views 5, 6 and 7
show views 3, 2 and 1 mirrored (table `0xEF257`; object +0x14 bit 1). The
animation numbers:

| Anim | Image | |
|---|---|---|
| 0 | walk | |
| 1 | attack | |
| 2 | the dying image, frame 0 held | while pushed back by a blow |
| 3, 4, 6, 7 | slots 15, 20, 30, 35 | empty in the data: fall back to slot 0 |
| 8 | stand | back and forth: frames 0 .. n − 1 .. 1 |
| 9 | dying | |
| 10 | slot 46 | empty |

The frame is the time since the animation began divided by the frame time.
Past the last frame it wraps, and +0x14 bit 0x80 ("played through") is set;
the bit clears when the animation changes. A missing image falls back to
slot 0.

## Per-level creatures (`0xE80A1`)

For each level number 0–8 there are four creature numbers (0-based). Spawn
points without a creature of their own get one of these at random:

| Level | Creatures |
|---|---|
| 1 | Scamp, Vermai |
| 2 | Scamp, Vermai, Dremora, Spider Daedra |
| 3 | Skeleton, Ghost, Wraith, Morphoid Daedra |
| 4 | Dremora, Fire Daedra, Frost Daedra, Seducer |
| 5 | Fire Daedra, Frost Daedra |
| 6 | Clannfear, Herne, Daedra Count, Seducer |
| 7, 8 | Wraith, Dark Seducer, Daedra Count, Daedra Lord |

## Sounds (`0xEF25F`)

**Creature sounds** (`0x48749`): four `SPIRE.SND` ids per creature, for idle,
attack, hurt and death (Scamp: 445–448). A forced sound always plays. An
unforced one plays only when `rand()` ≤ 2000, about one call in 16.

**Blows** (`0x43E0C`): the sound depends on the player's weapon (table
`0xEF1FE`, bare hands 439) or on the attacking creature (`0xEF21C`).

**Misses** (`0x43E6A`): the defending creature (`0xEF23E`) picks one of two
sets, 91, 373, 374 or 291–299.

**The player's pain**: `0x1A5` or `0x1A6`, or `0x1A7`/`0x1A8` for a female
character, when `rand()` < 30000 (`0x437FD`).

## Spawn points

Spawn points are flats whose `FILN` starts with `monster1` or `monster2`
(`0x25841`). They become hidden markers (type `0x22`, flags `0x202`) in a list
at `0x120F6C`. Their properties:

**`STRU` 106**, the creature record: 13 i32 (7 in a few old files), kept per
flat in a 56-byte table (`0xFED84`):

| Field | Values | Meaning (`0x46421`, `0x4672E`) |
|---|---|---|
| 0 | 1–16 | the creature, 1-based. Anything else: "Invalid monster override at %f, %f, %f", and no monster |
| 1 | | Speed, if not 0 |
| 2 | | Strength, if not 0 |
| 3 | | spell points, if not 0 |
| 4 | | health, if not 0 |
| 5 | | every skill, if not 0 |
| 6 | 13 on 87 records | the AI state to start in, if not 0. 13 waits to talk |
| 7 | 0, `0x8000`, `0x80000` | flags OR'ed into +0x172 |
| 8–12 | 0, or 9, 10, 18, 23 | spells it casts instead of the creature's lists (one picked at random, `0x47797`). A non-zero first one sets +0x227 bit 2 |

**`STRU` 108**: a dialogue code of three letters (`0xFE684`).

**`STRU` 116**: loot. The items are made as container contents, carried by
the monster and dropped where it dies.

## Spawning (`0x461BE`)

1. **Whether there are monsters.** The global `0xDA40E` decides:
   - 0: no monsters. Their loot lies at the spawn points (`0x46867`).
   - 1 (the default): the share of spawn points used is the level's
     `LEVELS.TXT` monster percentage (100 in multiplayer).
   - Above 1: that value is the percentage instead.
2. **A hidden reserve.** Up to 20 hidden monsters (255 minus the number of
   spawn points, at most 20) are made from the level's four creatures, at the
   first points' places. They are kept in a list (`0x16C2B0`), probably for
   summoning.
3. **Each spawn point.**
   - With a creature record, it gets that creature.
   - Without one, it gets one of the level's four creatures if a roll of
     0–100 is within the percentage, or always if it has a dialogue code or
     loot.
   - The monster stands 4 units above the floor found under the point.
4. **The point's other properties.** Then come the record's overrides and
   the loot, moved onto the monster. Two dialogue codes do more:
   - `MA2` sets +0x242 = 1. Its blows kill outright (story-related, not
     traced).
   - On level 5, `DS2` sets +0x242 = 2, and the game remembers the monster
     (`0x16C238`).
5. **Level 5's Hernes** all get +0x173 bit `0x80` with a pool of 1,000,000:
   they reflect every spell.
6. **The spawn point object is removed.**

Every campaign level has 54–125 spawn points. At 100 % all of them get a
monster.

## Thinking

**Who acts** (`0x4555E` → `0x450F8`, every frame). A character joins the list
of movers if it is within the level's `LEVELS.TXT` field 9 (1000–2500 units,
`0x16C1A8`) of the player; on level 2 it must also be within 400 up or down.
Its target is cleared each frame. With two or more movers, each picks a
target (`0x48476`): the nearest mover of another team, which for a monster is
the player. A monster is **aware** of its target (+0x226 bit 4) within 1024
units if it can see it (`0x2B703`).

**The state machine** (`0x43ECF`, per mover; the state is at +0x232):

| State | | |
|---|---|---|
| 0 | attack | Within reach (90 units of the player, 64 of others): the attack below. Out of reach: cast a spell if it can, else 8 |
| 1 | find a way | If aware and not sent here by a failed approach: 8. Otherwise it finds the node nearest itself (`0x1A660`) and the target's node. In another node group: give up (9). The same node: 8. Otherwise it walks to its node (2) |
| 2 | to a node | Arrived: compute the path to the target's node (`0x1B6F6`), then 3. Stuck: 14 or 9 |
| 3 | follow the path | In reach and aware: 0. The target's node changed: 1. Otherwise walk the path (`0x1B266`) |
| 4–7 | flee | To a node 1000–10,000 units away (or 500–), over the nodes. At the end: 13 if it should talk, else 9 |
| 8 | approach | Cast if it can. Otherwise walk straight at the target. Stuck: 14 if in another node group, else 1. In reach and aware: 0 |
| 9 | idle | Stands, turns to its goal, makes idle sounds. A positive +0x25F waits that many frames. With a target: in reach and aware, 0; aware, 8 (or cast); otherwise 1 |
| 13 | waits to talk | Turns to the player and stands; greets once (below). If hurt, or hit at, it fights |
| 14 | wander | Picks a random facing, then 15 |
| 15 | wandering | Walks that way for 3 s (picking a new facing when stuck), then 9 |

Three other rules change the state:
- A Seducer (12) at a fifth of its health runs once (14).
- A blow pushes a monster back (anim 2); while pushed it doesn't think.
- A monster much weaker than the player that hits it (health under a fifth of
  the player's) runs (4) if a roll of 0–100 is under its Intelligence
  (`0x433B0`).

**Walking** (`0x45843`):
- It walks straight at its goal, not along its facing, at Speed × 435 / 256
  + 160 units/s, at most 20 units a frame.
- Its facing turns 128 angle units a frame, or straight to the goal when
  within 64.
- It has arrived when within 12 units across and 60 up or down.
- After hitting a wall it walks 90° off until it bumps into something again
  (+0x226 bit 0x20).
- Other movers within 40 units, ledges and sigils stop it.

**The attack** (`0x47502`): the monster turns to its target. During the
attack animation, the blow is rolled once, at frame 4 (`0x4241F`). Between
attacks it stands. Once (100 − Speed) × 5 ms have passed since the last
attack began, it attacks again 85 % of the time, unless stunned (+0x284: it
skips two).

**Greeting** (`0x487B5`): a monster that has a dialogue code and waits to
talk greets the player once. The player must be within 512 units, facing it
(within 200 angle units) and in sight. Half the time the dialogue starts;
otherwise one of seven messages ("The %s invites you to speak.", messages
6–12) shows.

## Combat

A blow (`0x4241F`) works the same for the player and for monsters, attacker
against defender.

**Location**: a roll of 0–99 is walked down the chances 10, 20, 40, 15, 10,
then the rest (`0xEF1AC`): head, shoulders, chest, legs, hands, feet. These
are the player's six armour places (+0x268).

**Damage**: the weapon's roll.
- A monster's roll is doubled.
- The player's roll is multiplied by the swing's damage factor / 4. With
  bare hands, the hand-to-hand skill / 7 is added instead.
- A shield (+0x16E) takes what it can first, then the armour at the location
  comes off.
- A Wraith's blows scale with its health.

**Chance to hit**:
- attack = the weapon's skill + (Luck − 50) / 5 + Agility / 5 × the swing's
  skill factor / 4.
- defence = the defender's skill with its own weapon + Luck / 10 + Agility /
  5.
- chance = (attack − defence) / 2 + 50, less the defender's dodging skill
  (skill 6) / 5 %, kept within 5–95. The roll is 0–100.
- Two modifiers aren't implemented: +15 for the player near death with flag
  +0x19E, and halving against a player under spell 35.

**Criticals and fumbles** happen only on the player's blows.
- A critical comes on a roll up to skill 20 / 7 + chance / 20 (plus (Luck −
  50) / 5 of a defending player):
  - 60 %: "You land a telling blow." (the weapon's maximum × 3).
  - 30 %: a stun (the monster skips two attacks).
  - 10 %: "You send your opponent reeling." (× 3, pushed twice as far).
- A fumble comes on a roll at or above 100 − (100 − chance) / 20, with a
  weapon: off balance for 2 s, the weapon damaged, or the weapon knocked from
  the hand.

**A hit** scales the damage by (skill / 100)² × 2.5 and adds (Strength − 50)
/ 10, with a result of at least 1. The player's weapon then adds its material
− 1. Level 5's Hernes take no damage ("You dont seem to be doing any
damage."), except from magic item 5016 on a spear (1,000,000).

**The player's swing** (`0x41239`). While the attack key (the right mouse
button) is held, the game sums the last eight frames' mouse motion
(`0x21476`). When the sum reaches 100, its direction picks one of 16 swings
(the game's angle of it / 128):

| Direction | Mouse | Skill factor | Damage factor | Animation |
|---|---|---|---|---|
| 0, 15 | down | 4 | 8 | 7 |
| 1, 2, 13, 14 | down and across | 3 | 6 | 8, 11 |
| 3, 4, 11, 12 | across | 4 | 4 | 5, 10 |
| 5–10 | up | 5 | 2 | 9 |

With bare hands, pressing the key punches (`0x494C8`, sound `0x1A2`). The
blow lands when the weapon animation passes frame 4 (`0x2A892`). It hits every
mover that is within 90 units, within 200 angle units of the facing (or
closer than 10 units), and in sight (`0x4975B`).

**Damage taken** (`0x42E2F`): health goes down.
- A monster at 0 health shows "%s dies...", drops its loot, plays its death
  sound and its dying image (as an effect, `0x1D8DF`), and is removed.
- A monster still alive plays its hurt sound, and bleeds (`0x43886`):
  `blod00.bsi` (5 frames of 71 ms) is made an effect object by `0x1D8DF`, at
  the monster's position 40 up and moved 10 towards the player, and plays
  once. With a file `C:\SPIRE.GRD` (`0x112EF` sets `0x16FFCE`) it's
  `blod90.bsi`, sparkles, instead: a gore lock. The engine stands the image
  on that point (inferred).
- The player, hurt, has the time noted (`0x43996`, into `0xEF1B2`); so do
  negative gems (`0x4D839`), sigils (`0x5BCA7`), the dialogue's half health
  (`0x5A11F`) and a push at `0x49A7E`. For 250 ms after it, every frame,
  `0x43957` passes the whole 640 × 480 screen through `TINT.555`, which keeps
  only red. The engine notes any drop in health and clears green and blue.
- The defender is pushed back (`0x439B8`):
  - force = 30 × damage and distance = 3 × damage, both doubled for a good
    blow. Force is capped at 400.
  - The mass m is the creature's mass / 4 (for the player, 150 / 4).
  - speed = force / m × (distance − distance × (force − m) / (force + m)),
    at most 64. Below 15 there is no push.
  - The push lasts 5 ms per unit of speed and fades out.

**The player's death**: the global `0x16C258` holds the time of death. 2 s
later (`0x135DD`), `CHOP2.SMK` plays with text 1058 (the captivity ending)
and the game is over. Being captured by Dagon sets the same time (see the RE
notes, "Videos").

## OpenBattlespire

**Done**:
- The tables and spawning (without the hidden reserve).
- The active set, and the states above. A shortest path over the node links
  replaces the game's node search and path following.
- Walking with the player's collision, the animations and views.
- Melee both ways by the decoded rules, pushes, loot and greetings.
- Casting by the decoded rules ([spells.md](spells.md)); the reserve's
  size limits summoning.
- The player's death and the captivity ending.

**Additions** (not in the game):
- A monster that gets no nearer its goal for 1.5 s counts as stuck.
- After a bump, a monster walks sideways for at most 1 s.

**Not yet**:
- Dialogue, missiles (bows), monsters fighting each other.
- The weapon in hand and its animations: a swing takes 568 ms here, with its
  blow at 355 ms.
- The reserve monsters themselves (summoned ones are made new), monsters kept
  across visits to a level, NPCs 29/30.
