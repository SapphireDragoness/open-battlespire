# Spells (in GAME.EXE)

Status: **decoded** (static analysis of `GAME.EXE` v1.5, 2026-10-14/15):
the spell table, casting and its costs, missiles, resistances, effects, the
monsters' casting and the spell book. OpenBattlespire reads the table
(`bs/formats/spells.h`) and plays spells by these rules (`bs/game/magic.h`,
`src/app/spellbook.c`). Addresses below are v1.5's.

## The spell table (`0xDA614`)

31 records of 119 (`0x77`) bytes, one per spell effect, in the order UESP
lists them (1 Monster Summoning … 31 Dispel Sigil). A potion of type n
casts spell n; a spell scroll numbered 10000 + n teaches it. The same
layout is the game's **spell object** (type 9): a cast spell, a missile, a
trap, and each spell the player knows (kept as objects carried by the
player, `0x2A144`, which the spell book changes).

| Offset | Size | Field |
|---|---|---|
| 0x00 | 32 | name |
| 0x20 | 1 | number (1–31) |
| 0x21 | 1 | target: 0 on the caster only, 1 others only, 2 either |
| 0x22 | 1 | the icon (`symbNN.bsi`, 0–26) shown while it lasts; 200+ on the four unused spells |
| 0x23 | 4 | duration base (0: none) |
| 0x27 | 4 | duration increase |
| 0x2B | 4 | duration step |
| 0x2F | 4 | magnitude minimum (0: none) |
| 0x33 | 4 | magnitude maximum |
| 0x37 | 4 | increase minimum |
| 0x3B | 4 | increase maximum |
| 0x3F | 4 | magnitude step |
| 0x43 | 4 | in an object: the rolled duration, ms |
| 0x47 | 4 | in an object: the rolled magnitude |
| 0x4B | 1 | number − 1 |
| 0x4D | 1 | element: 0 fire, 1 cold, 2 poison, 3 shock, 4 magic |
| 0x4E | 1 | delivery: 0 caster, 1 touch, 2 one creature at range, 3 area around the caster, 4 area at range (the table has touch for spells on others) |
| 0x4F | 4 | in an object: its caster (owner) |
| 0x53 | 12 | in a missile: its direction (3 floats) |
| 0x5F | 4 | the base cost |
| 0x63 | 4 | in an object: the cost paid |
| 0x67 | 4 | the school, an index into the six bytes 10 before the table (`0xDA60A`): the skills 11 Alteration, 9 Restoration, 8 Destruction, 13 Mysticism, 12 Thaumaturgy, 10 Illusion |
| 0x6B | 4 | detonation: 0 Any, 1 Organic, 2 Organic Drop |
| 0x6F | 4 | in a missile: Organic Drop's falling speed |
| 0x73 | 4 | in a missile: its speed |

Messages 58–60 name the detonations and 70–79 the deliveries and elements;
425–429 name the elements again ("Magic", "Fire", …) in another order.
Elements have bits in the tolerances below: fire 2, cold 4, poison 16,
shock 8, magic 1 (`0xED681`).

**Rolls** (`0x28A1E`):
- duration = (base + caster level / step × increase) × 5 s;
- magnitude = min..max + (inc min..inc max) × (level number / step). This
  is the dungeon's level number, not the caster's.

The rolls are made again for every effect: when a spell takes effect on its
caster, and when a missile bursts.

**Cost** (`0x29791`):
- base × (110 − the school's skill) / 100;
- then × 80, 90, 100, 110 or 120 % for the delivery (`0xED6A5`);
- at least 5.

A monster's skill is its one skill value.

## Casting (`0x27AEB`)

The player can't cast:
- while ethereal ("You cannot cast spells while ethereal.", message 507);
- without the spell points ("Not enough spell points to cast!", 80).

The cost is paid at once. Then by delivery:

- **Caster**: the spell takes effect on its caster (`0x296C4`). For the
  player the casting hand moves (below); a monster sparkles (`twinkle.bsi`).
  Then the rolls and the effect.
- **Touch**: the player's spell is made **ready** (`0x16BFA4`). The next use
  of a creature (the left button, `0x4CF0A` → `0x27CE5`) puts it on that
  creature; using anything else spends it. A monster's takes effect on its
  target at once.
- **Range and areas**: the player's is made ready, and the attack button
  (`0x411F8`) or the spell's hot key again sends it where the player looks.
  A monster's flies at once at its target's feet (`0x274BE`).

While a spell is ready, using objects does nothing and no other spell can
be cast from a hot key (`0x2A960`). The spell book's Cast button casts
anyway, and the ready spell is lost.

**The casting hand** (`0x2A698`): `cast00`–`cast04.bsi` by element, 9
frames, at their own place on the screen (bottom right). While a spell is
ready its first frame shows. When a spell goes (`0x2A808`) the whole
animation plays; until it ends no hot key casts.

## Missiles (`0x274BE`, `0x28AD0`)

A missile starts 50 above its caster's feet and flies 900 units/s (1000 on
level 5). It heads at its target's feet or, for the player, along the
view; with the cursor showing, towards the cursor. It plays sound `0x194` +
(0, 1, 3, 2, 4 by element) as it goes. Every frame:

- It is gone after 10 s, 3072 units from its caster, or under 120 units/s.
- Organic Drop slows by 150 units/s every second and falls as the player
  does. It bounces up off floors (faces whose normal points up more than
  half).
- It meets the level 30 units lower than it flies. Any bursts there (except
  a touch spell); Organic and Organic Drop bounce off the face.
- After moving, the first creature within 150 units is hit, the player
  first unless ethereal. The caster is ignored while within 100.
  OpenBattlespire also ignores the caster until the missile has once been
  150 away. With the game's frame time a missile can pass through the
  50-unit band where the caster is neither ignored nor out of reach.
- A creature within 65 of the hand when the spell is cast is hit at once.

**Bursting** (`0x2786F`): sound `0x199` + the element's, the rolls again,
then:

- A spell for one creature that hit one: on that creature.
- Otherwise (an area spell, or a single one that met the level): on every
  creature within 192 units, except the caster of a spell on the area
  around it. Each creature hit shows `explNN.bsi` (7 frames).

Before an effect the target may stop the spell (`0x27DAC`), against its
spell **pools**:

1. With Spell Resistance (flag `0x10000`) and a cost no more than the
   resistance pool (+0x2A4): the pool pays, and the spell stops.
2. With Spell Absorption (`0x20000`) and not its own spell: the absorption
   pool (+0x2A8) pays, and the cost goes to the spell points. If they would
   go over the maximum, the spell hits after all. The class advantage +0x195
   absorbs on a roll of 1–100 within Willpower, without paying from the
   pool.
3. With Spell Reflection (`0x8000`), a cost within the reflection pool
   (+0x2AC), and not its own spell: the pool pays, and the missile turns
   back and becomes the target's.

The player's pools are made anew every frame from the effects, magnitude ×
10 each. A monster's start at 150 and don't come back. Level 5's Hernes
reflect everything (pool 1,000,000). Some creature flags already hold these
bits: Seducers resist, Dark Seducers resist and reflect, and the Daedra
Lord reflects.

## Effects (`0x27F4C`)

Most timed effects only work on the player; on a monster they do nothing.

| # | Spell | On the player | On a monster |
|---|---|---|---|
| 1 | Monster Summoning | see below | |
| 2–8 | Detect Spell, Detect Enemy, Detect Invisibility, Invisibility, Shadow, Chameleon, Slow Fall | flags `0x4`, `0x8`, `0x10`, `0x20` (monsters without Detect Invisibility don't see the player), `0x40`, `0x80`, `0x100` | nothing |
| 9, 10 | Continuous Damage, Poison | flag `0x200` / `0x400`: health − magnitude / 5 every second | the magnitude at once, as damage |
| 11 | Confusion | +0x30B until the end; "You are confused..." | wanders for 3 s (state 14) |
| 12 | Vampiric Drain | the player gains the magnitude, whoever cast it; then the target is damaged | the same |
| 13 | Delayed Damage | a trap 40 above the target's feet: anyone else coming within 120 sets it off as Cause Damage on the area around (`0x5B702`) | the same |
| 14 | Dispel Magic | a list of spells on and near the player, one to end (`0x2AD30`) | |
| 15–17 | Spell Reflection, Resistance, Absorption | flags `0x8000`, `0x10000`, `0x20000`; pools magnitude × 10 | nothing |
| 18 | Cause Damage | damage | damage |
| 19 | Fire Shield | flag `0x80000`: a monster's blow that hurts gets half the damage back, if that is at least 2 (`0x4384E`) | nothing |
| 20 | Cure Poison | ends effects with flag `0x400` | nothing |
| 21 | Cure Health | heals the magnitude, to the maximum: "You healed %d. You now have %d out of %d." | heals it |
| 22, 23 | Jumping, Running | flags `0x400000` (a jump goes 330/256 as far, [the RE notes](../re/README.md)) and `0x800000`, which nothing reads: Running does nothing | nothing |
| 24 | Etherealness | flag `0x1000000`: no casting; blows and missiles pass through; monsters walk through | Teleport's screen comes up (a fall-through) |
| 25 | Teleport | the anchor screen (`teleport.bsi`): Anchor drops an anchor where the player stands ("Anchor dropped."), Teleport goes there ("You have not dropped an anchor.") | the same screen |
| 26 | Shield | the **player's** shield points (+0x16E) + magnitude, whoever it was cast on; blows take them first | |
| 27 | Resistance | flag bit 27 + element: that element does nothing | nothing |
| 28–30 | Slow, Haste, Strength | Speed −, Speed +, Strength + magnitude while it lasts | nothing |
| 31 | Dispel Sigil | multiplayer: sigils within 500 move away, once in 5460 ms | |

The player's double healing (+0x196) doubles Cure Health on whoever it heals.

**Timed effects** (`0x295A0`) are objects carried by the player, one per
spell (by name); casting it again lengthens the one there. Each holds
([save.md](save.md#timed-effects) has the layout):
- its icon + 1, its end and its flags;
- attribute changes (12 bytes each from +0x20, the first 4 in 16.16), then
  spell points and health maximum, then the skills;
- the tick of Continuous Damage and Poison (+0x194 health every +0x19C ms);
- pool values (+0x1B4, +0x1B8, +0x1BC).

Every frame (`0x28F11`, `0x293EF`) the player's attributes start again from
their base values (+0x40), and the flags and pools from 0. Each effect then
adds its own. Attributes stay at least 10.

**Icons** (`0x29080`): each effect's from the top left (5, 5), side by side,
on a new row past x 635. Then the Shield's (`symb25`) while there are shield
points, blinking below 15.

**Damage of an element** (`0x42E2F` → `0x2AA5D`): the share that gets
through comes from a roll against a chance: 40 + (Willpower − 50) / 2 for
the player, 30 for monsters. The target's tolerances change it (creature
+0x96, monster +0x17A: +0x18 resists, +0x19 immune, +0x25 low tolerance,
+0x26 critical weakness, each a byte of element bits):

1. Resistance to the element (flag bit 27 + element) or immune: nothing.
2. Critical weakness: all of it.
3. Low tolerance halves the chance; resisting adds half of it. The chance
   stays within 5–95.
4. A roll of 1–100 over the chance: all of it. More than 20 under it:
   nothing. Otherwise (chance − roll) × 5 %.

The creatures' tolerances:
- Fire Daedra are immune to fire and never resist cold; Frost Daedra the
  other way round.
- Spider Daedra are immune to poison, Clannfear to poison and magic.
- The Daedra Count and Lord resist all five; the Count is immune to cold
  and shock.
- Spells never hurt level 5's Hernes.

## Monster Summoning (`0x2804A` → `0x47B33`)

1. One of the level number's four creatures (`0xE80A1`, as the spawner's)
   is taken from the hidden reserve the spawner made.
2. It is placed at a navigation node 128–512 units from the player where no
   one stands within 42.
3. It is on the caster's team, stands idle, and leaves when the spell's
   duration ends (+0x2A0).

With the reserve used up: "No monster hears your summons...". A monster
summoned by the player isn't the player's enemy. In OpenBattlespire it
doesn't fight anyone yet.

## Monsters casting (`0x47645`)

Creature records +0x74 and +0x78 point to two 0-terminated lists of spell
numbers, kept in a block right before the creature table:

| Creature | First list | Second list |
|---|---|---|
| Dremora | Cause Damage | |
| Spider Daedra | Poison | |
| Ghost | Vampiric Drain | |
| Morphoid Daedra, Fire Daedra, Frost Daedra, Seducer | Cause Damage | |
| Dark Seducer | Cause Damage | Monster Summoning |
| Daedra Count, Daedra Lord | Continuous Damage, Vampiric Drain | Cure Health, Spell Reflection, Resistance, Absorption |

A spawn point's record (BS6 `STRU` 106, fields 8–12) replaces them.

A monster **may cast** when it:
- is aware of its target;
- has spell points and spells;
- hasn't given up casting (+0x227 bit 1).

Then, instead of coming nearer (states 0, 8 and 9 of [the state
machine](creatures.md#thinking)), it turns to its target and, facing it:

1. If its target reflects spells and it has spent spell points, a roll of
   1–100 under its Intelligence makes it give up casting for good.
2. Once (100 − Intelligence) × 10 ms have passed since it last tried, it
   tries again. A quarter of the time (a roll of 0–100 under 25) it casts.
3. The spell: one of the spawn record's; else from the second list one time
   in five (a roll of 1–100 under 20); else from the first list.
4. If its spell points are under the spell's cost (by the table's delivery),
   they become 0 and it stops casting.
5. Fire Daedra cast fire, Frost Daedra cold, others a random element, all
   with detonation Any. Spells on others go by touch within 90 units and at
   range further away; spells on oneself go on itself.
6. The casting pays, and then the monster pays the cost again. This is a
   quirk: every spell costs a monster twice.

When its spell points run out it comes to fight in melee. Spells for either
target cast by a monster at range land on the player: a Daedra Lord's Spell
Resistance protects the player.

## The spell book (F12, `0x2988B`)

`spmenu.bsi` (11 images: the background last, then the frames and buttons
over it) lies across the bottom of the screen, from y 380 to 480; the game
goes on above. With no spells known: "You dont have any spells." The
buttons (`0xED5B2`):

| Rectangle | Button |
|---|---|
| 20, 396 – 197, 468 | the spell list ("cost - name", FONT0003), scroll arrows at 218, 381 and 218, 460 |
| 256, 396 – 287, 425 | the icon: the next of 27 |
| 311, 395 – 344, 427 | the delivery: the next one, never off the caster for self-only spells and skipping the caster for spells on others (`0x29A8C`) |
| 367, 397 – 396, 423 | the element: the next of 5 |
| 412, 389 – 554, 411 | the hot key: "No need to click here, simply press F1 through F8 for key." |
| 411, 415 – 576, 438 | the detonation: the next of 3 |
| 563, 389 – 631, 414 | Cast |
| 590, 427 – 634, 473 | Exit |
| 9, 455 – 32, 477 | help |

The selected spell's parts are drawn at:

| Part | Position |
|---|---|
| icon | 252, 391 |
| delivery (`dlvrNN.bsi`) | 307, 391 |
| element (`elmtNN.bsi`) | 363, 391 |
| cost | 318, 450 |
| duration in 5 s units, or "N/A" | 375, 450 |
| magnitude, as "min-max + inc-inc" | 463, 450 |
| detonation | 452, 420 |
| hot key ("F1"…, or "NONE") | 498, 395 |

Pressing F1–F8 in the book gives the selected spell that key (player record
+0x1FE, one byte per key) and takes the spell off any other key. In play
the key casts it (`0x422F0`).

## Not yet in OpenBattlespire

- Dispel Magic's list.
- What the Detect spells show.
- Confusion's effect on the controls.
- Summoned monsters fighting for the player: monsters don't fight each
  other yet.
- The help button.
- Dispel Sigil.
- Magic items' spells.
- The spell book's order: the game lists spells in the order learnt.
- The light that flies with a missile.
- The "You set up a delayed damage spell trap here." message.
