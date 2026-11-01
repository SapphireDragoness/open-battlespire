# Items

Status: **decoded for placement, naming, carrying, the inventory screen,
equipping and using** (static analysis of `GAME.EXE` v1.5, 2026-10-14). Parsers:
`bs/formats/items.h` (the tables and model names in `GAME.EXE`, the `MG*.TXT`
enchantments); making items, names, weight and where items are (floor,
containers, carried) are in `bs/game/items.h`. Not decoded yet: using,
equipping and the effects of enchantments, potion names.

An item in the game is a **base item** (one of 43 kinds) with a **material**
(for weapons and armour), optionally a **magic enchantment** (a name suffix
with spells and properties), and a few numbers of its own (a potion's type,
a book's text, an arrow count). The levels place items and fill containers
with pairs of these numbers (see
[bs6.md](bs6.md#survey-of-the-logic-chunks-2026-10-13)).

## Base items (in GAME.EXE)

A table of 43 records of 754 (`0x2F2`) bytes at `0xDB5FD` in the data object
(v1.5). An item in the world is an object (type 2) with a copy of its base
record at +`0x3D`; the rest of the record is filled in for each item.

| Offset | Size | Field |
|---|---|---|
| 0x00 | 32 | name |
| 0x20 | 2 | index (0–42) |
| 0x22 | 9 | inventory icon image (`mwpns600`, `hlmt`); empty for some |
| 0x2B | 4 | condition (dagger 100, broad sword 500, key 200) |
| 0x2F | 4 | maximum condition (the same); the inventory shows +0x2B × 100 / +0x2F as a percentage |
| 0x33 | 2 | in the table: dagger 5, broad sword 2, potion 15, helmet 32 (an image number?); in an item: the **material**, or a sigil's id |
| 0x35 | 2 | **weight** in half units: shown as "Weight: 0.5" for 1 (`0x54678`; dagger 2, broad sword 16, helmet 4, book 1) |
| 0x39 | 1 | 0xFF; a potion's type in a potion |
| 0x3E | 4 | an arrow bundle's count |
| 0x42 | 1 | bit 0: a **container** (Sack, Large Chest, Small Chest, Coffer of Restoration) |
| 0xB3 | | the enchantment record (`0x233` bytes) is copied here; its name is the suffix |

| Index | Item | Index | Item | Index | Item |
|---|---|---|---|---|---|
| 0 | Dagger | 15 | Potion | 30 | Voidguide |
| 1 | Short Sword | 16 | Sack | 31 | Sigil Amulet |
| 2 | Long Sword | 17 | Large Chest | 32 | Book |
| 3 | Broad Sword | 18 | Small Chest | 33 | Gold Pieces |
| 4 | Claymore | 19 | Clothes | 34 | Key |
| 5 | Battle Axe | 20 | Gem | 35 | Gatekey |
| 6 | War Axe | 21 | Scroll | 36 | Cog |
| 7 | Mace | 22 | Arrow | 37 | Rod |
| 8 | Short Bow | 23 | Lantern | 38 | Gatekey |
| 9 | Long Bow | 24 | Helmet | 39 | Parchment |
| 10 | Crossbow | 25 | Cuirass | 40 | Spear Case |
| 11 | Javelin | 26 | Pauldrons | 41 | Gatekey |
| 12 | Sling | 27 | Greaves | 42 | Coffer of Restoration |
| 13 | Staff | 28 | Boots | | |
| 14 | Spear | 29 | Gauntlets | | |

### Weapon stats (`0xDB47D`)

Right before the base table: 16 records of 24 bytes (six `i32`), one per
weapon (base items 0–14) and a last one for bare hands.

| Field | Values | Meaning |
|---|---|---|
| 0 | 2, 8, 12, 20, 30 (dagger … claymore) | ? grows with the weapon |
| 1 | 1 or 2 | hands |
| 2 | 0–8 | ? |
| 3, 4 | dagger 2–6, broad sword 4–12, claymore 8–16, hands 2–4 | the damage roll; the inventory shows material + min − 1 to material + max − 1 |
| 5 | 14–19 | the skill used: 14 Short Blade, 15 Long Blade, 16 Hand to Hand, 17 Axe, 18 Blunt Weapon, 19 Missile (the skill list's order) |

### Materials

Ten names (`Iron Steel Silver Elven Dwarven Mithril Adamantium Ebony Orcish
Daedric`) through a pointer table at `0xDA412`. Each new item gets one
(`0x51634`): a roll of 1–100, plus 8 per level number above 4, plus 20 on
multiplayer levels (or 100 in a debug mode), against the thresholds 40, 65,
75, 85, 90, 95, 97, 98, 99: Iron up to 40, Steel up to 65, … Daedric above
99. A global can cap the material.

## Magic enchantments (TXT.BSA)

Loaded at start-up (`0x10851`) by `0x53577`, one text record at a time, into a
table of 563-byte (`0x233`) records (id at +`0x22F`, Item at +`0x21`); a
missing id stops the game with "Unable to find magic item specified as %d_%d".

- `MG0_GEN.TXT`: 212 records, ids 9001–9419 (general; 9001–9052 are the
  sigils).
- `MG2_SPC.TXT`: 232 records, ids 1010–8015 (special: the thousands digit
  seems to follow the kind, 5xxx armour, 8xxx weapons).

Records are tab-separated lines, separated by blank lines:

```
Name	"of the Moon Reiver"
ID#	8011
Item	0
Spell	-1, 28, -1, 8, 4
App	15, 30, 100
Adv	36, 37
Uses	10
Level	0
```

- `Name` is the suffix added to the item's name (the Moon Reiver is a Broad
  Sword "of the Moon Reiver"). Quotes are plain or Windows-1252 curly ones.
- `Item` (0–3) picks the base when a placement asks for a random one
  (`0x524F6`): 0 Clothes, 1 a weapon (any but the Sling), 2 either at even
  odds, 3 a random enchantment instead (then again by its `Item`). MG0 has
  160 records of kind 2 and 52 of kind 3 (the sigils); MG2 has 29 of kind 0,
  14 of kind 1, 188 of kind 2, 1 of kind 3.
- `Spell` is always five numbers; `App` and `Adv` lists of 1–17 numbers
  that often end in 100 or −1; `Uses` charges; `Level` a level.

**The record** (`0x52EE1`, 2026-11-01). Each line is matched by its first
letters, any case, in any order; a record ends at the next `Name`. An item
copies the record to +0xB3 (`0x524F6`), which puts +0x2C3 at +0x210 (the
item maker's 0xFF over it). `bs_magic_record` makes it; it matches all 1446
enchanted items in the shipped classes and ten saves, but for the charges
used.

| Offset | Size | From |
|---|---|---|
| 0x00 | ≤ 32 | `Name`: the text less its first and last characters (the quotes; `""SCOURGE, …""` keeps one pair) |
| 0x20 | 1 | `Level` |
| 0x21 | 1 | `Item` |
| 0xBA + 12 n | 4 | `App`: pairs "n, value", to a 100 |
| 0x1E2 | 0x38 | `Adv`: flags laid out as the player record's from +0x17A (below), to a −1 |
| 0x21A | 5 × 1 | `Spell` |
| 0x21F | 4 | `Uses`: the charges; the item's +0x2D2 |
| 0x223 | 4 | the same: the maximum, the item's +0x2D6 |
| 0x22F | 4 | `ID#`; the item's +0x2E2 |

`Adv`'s numbers (`0x53299`), into +0x1E2 + the player record's offset −
0x17A ([scl.md](scl.md#the-player-type-3)): 9–13 resistance and 14–18
immunity (magic, fire, cold, shock, poison bits), 19–24 Acute Hearing,
Spell Absorption, Rapid Healing, Regenerate Health, Regenerate SPs,
Athleticism; 25–28 Increase Magery × 1, 1.5, 2, 3; 29 Adrenaline Rush; 30 no
spell point regeneration; 31–35 forbidden weapons; 36–38 heaviest armour
none, light, medium; 39 no shield; 40 best material Orcish; 41–45 low
tolerance; 46–50 critical weakness. Only the first number's sign is read: the
others skip the '−', so a later −1 is 1 and doesn't end the list. When an
item is worn, its flags are ORed into the player's (`0x53842`).

`ITEML2.TXT` (7 records), `SITEML2.TXT` (6) and `MAGIC.TXT` (a different
layout with `CLASS class000.scl`) are similar lists; the code paths seen so far
don't load them.

## Items in the levels

The BS6 property reader (`0x2475B`) calls the item maker (`0x26C73`) for each
item property of an object, in the order they come:

- **Placed item** (`STRU` 105, 16 bytes `base ? ? number`): e.g. `22 0 1
  10000`, a bundle of arrows. The item takes the object's place in the world
  (it copies the object's header and becomes a type 2 object).
- **Book** (107, `n`): as 105 with base 32 and number n.
- **Potion** (115, `n`): as 105 with base 15 and number n.
- **Container contents** (116, 16 bytes `a b ? ?`): an item inside the object,
  which gets flag +0x13 bit 0 and keeps its place. Monster flats carry the
  same records as loot.

The maker's rules, with base and number taken from `base`/`number` (105) or
`b`/`a` (116):

1. number 9001–9052: a Sigil Amulet (its own copy of base 31, at `0xE114B`)
   with that enchantment (`0x52384`).
2. 116 only, base 23: a Lantern (`0x51701`).
3. base 15, or number 40: a potion of type number (`0x51EAA`; type 14 is made
   as 21).
4. 105 only, base 21 or 32: a scroll or book with text number (`0x522D7`).
5. number 10000: a plain item of the base (`0x521AB`).
6. base 10000: enchantment number on a random base (`0x524F6`, see `Item`).
7. otherwise enchantment number on the base (`0x5261A`): `8011 3` is the
   Moon Reiver broadsword, `5011 …` the Savior's Hide.

A plain item (`0x521AB`) copies the base record, rolls the material, and for
arrows rolls a count: 4–8 × (level number / 2 + 1).

All 672 item placements on objects in the 45 listed levels make an item (537
objects; 115 of them containers).

## Using items in the world

The click handler (`0x4CF0A`) dispatches on the clicked object's type:

- **Items** (type 2, `0x4D0C6`): within 192 units (else "You are too far
  away."). A container opens the inventory screen (`0x4E4C1`) with its
  contents as the second list; any other item opens it with the item's
  cell of the level's 64 × 64 object grid, shown near the player (see below) (`0x3E16D`: x / cell +
  32 by z / cell + 32, truncating; the cell size is the level's
  `LEVELS.TXT` grid cell; everything outside shares one list).
- **Objects with properties** (type 6, `0x4D153`): within 192 units: the
  level logic's use event, dialogue, a `TEXT.RSC` message (`STRU` 114, shown
  wrapped to 600 pixels), then if the object holds items, the inventory screen
  with them.
- **Flats** (type 0x21, `0x4D5A4`): the use event, then the NPCs talk (see
  the RE notes, "Dialogue"; the distance test isn't fully decoded).
- Type 0x43: "You see %s." with the object's name.

Moving an item into the carried list checks the weight (`0x5484D`): the
total, with an arrow bundle counting count / 2 + 1 and a container its
contents, may not pass 4 × Strength ("You cannot carry any more stuff.");
over 3 × Strength the player is encumbered (`0x5493B`).

### Item models

Items are shown by 3D models, in the inventory and when they lie in the
world: a table of 40 model names (`0xEFD89`, loaded at `0x5152F`; `bataxe.3d`
… `arrow.3d`, some shared) indexed by the base record's +0x33 image number.
A Sigil Amulet has its own model instead: `sigila.3d` … `sigilz.3d` by its
id − 9001 (less 26 past 25), with `x` and `y` made `z` (`0x515A1`,
`0x52430`). Model points are in 1/256 units, the radius in the header too.

## The inventory screen

Opened by `0x4E4C1` with a list: a container's contents, or (no container)
the player's cell of the object grid (a type-0x2F object whose children are
the cell's objects). From a cell the list shows only the items within 128 of
the player by the game's distance (`0x4EF5A` → `0x45F99`: across = |dx| +
|dz| − the smaller / 2, `0x1B900`; then the larger of that and |dy| + the
smaller / 4, `0x1B91B`), and the item clicked to open it however far
(`0x3F86F4`, set at `0x4D105`). Level 1's cells are 4096 wide, and with the
truncation the middle one spans −4095 to 4095, so without the distance the
list would hold most of the level. The screen is
`char12.bsi`; a table of 22 buttons (`0xEFB5C`, 23 bytes each: x0, y0, x1,
y1, flags, handler) with help texts from message 216 on:

| Buttons | Rectangle | |
|---|---|---|
| 0, 5 | x 320–416 / 464–560, y 32–96 | the open container of each list (the red boxes) |
| 1–4, 6–9 | same x, y 112–176, 192–256, 272–336, 352–416 | four item slots per list: carried on the left, the other list on the right |
| 10, 11 | x 430–451 / 575–596, y 102–431 | scroll: a click above y 240 scrolls up one, below down one (`0x4FC5A`) |
| 12 | 0, 0 – 300, 479 | "Items currently worn" (the paper doll) |
| 13 | 582, 424 – 630, 474 | Exit |
| 14 | 247, 406 – 314, 476 | armour class |
| 15–20 | x 272, 329, 385, 441, 497, 553 (55 wide), y 8–26 | filters: Weapons, Magic, Misc, Clothing, Potions, Amulets |
| 21 | 616, 2 – 637, 22 | help |

- **Slots** draw the item's model spinning (`0x51030`): it sits 4 × its
  radius ahead of the camera and a quarter radius down, projected through a
  200 × 180 view plane (`0x6EB78`) where the 3D view uses 240 × 180, so items
  look 1.2 times wider (inferred). Every item turns about y at 1/2 unit per
  ms (2048 a turn); weapons, armour except helmets, and the Voidguide also
  about x; weapons about z at 1 per ms.
- **Selecting and dragging** (`0x4FE25`): pressing on an item selects it
  (its two info lines show in `FONT0008` at 325, 450); moving more than 8
  pixels drags it, drawn at the mouse in a 96 × 64 box kept on the screen.
  Releasing over a list moves it into that list's container, or into the
  container item it is dropped on (`0x50763`); into the inventory the weight
  is checked ("You cannot carry any more stuff."). Dropped on the floor list
  it lies at the player's feet and the list shows that cell. Arrows and
  potions join stacks of the same kind (`0x53EDD`, `0x54E77`).
- **Double-click** (`0x50036`): on the box, back out to the container
  holding it; on a container item, open it in that list (each list keeps a
  stack, so containers nest); on anything else, use it (`0x501B4`: learn a
  scroll's spell, read a book, drink a potion — Restoration restores spell
  points, Water Breathing lasts 300 s — or use a magic item's charges).
- **Filters** (`0x54BC9`, state `0xEF8DC`, all on at first): they apply
  only to carried items, and containers always show. Each item has one
  category: potions Potions, sigil amulets Amulets, anything enchanted Magic,
  weapons and arrows Weapons, Clothes Clothing, everything else (armour too)
  Misc. A click toggles a filter, with a modifier only it shows; a filter
  that is on is drawn with its `filt0n.bsi`.
- The scroll arrows are covered by `point0.bsi` / `point1.bsi` (dimmed) when
  the list can't scroll that way (`0x4FB7E`).
- **Info lines** (`0x4F37D`): weapons "Material Name suffix" and "Damage:
  material + min − 1 - material + max − 1  Condition: n%  Weight: w"; arrows
  "Number: n …"; armour "AC: n …"; potions "Potion of Restoration (n)",
  "Potion of Water Breathing (n)" or "%s of %s (n)" with the spell's name;
  others "Condition: n%  Weight: w".
- Drops onto the bottom-left slots: secondary weapon (16–112, 400–464) for a
  weapon, arrows (128–224, 400–464) for arrows; onto the paper doll, equip.
- Keys: Page Up/Down scroll the carried list by four, the arrow keys by one.

OpenBattlespire implements the lists, slots, scrolling, filters, dragging,
nesting, the info lines and equipping (`src/app/inventory.c`). The world
waits while the screen is open, which isn't checked against the game.

## Equipping and the paper doll

**Pictures.** Each item with an icon picks, when it is made (`0x52CB4`),
the image it shows worn: weapons their icon with the character's sex letter
(`mwpns603` → `FWPNS603` for a female character); Clothes (icon `x`) first
one of `armb`, `cape`, `pnts`, `shrt` at random (`0xEF860`); then every
other item one of the BSI images whose names start with the sex letter and
the icon (`MHLMT*`: `MHLMT500` … `MHLMT509`), at random. The name is stored
in place of the icon (+0x22). All 268 items with icons in the levels find
one; the sling has none.

**Armour values** (`0x544A4`): the picture's last digit mod 4, plus 1, plus
the material less 1 (halved above 1); at least 1. The screen shows six
(`0x543F5`, `0x542B8`, player record +0x268): helmet, pauldrons, cuirass,
greaves, gauntlets, boots (bases 24, 26, 25, 27, 29, 28), in `FONT0008` at
x 285 from y 405, 11 apart; and hand-to-hand damage as n/7 + 2 – n/7 + 4
from player record +0x132, the Hand to Hand skill (skill 16; see
[creatures.md](creatures.md#combat)), at 283, 388.

**Who may equip what** (`0x54984`), from the character's class (player
record): weapons not of a forbidden group (+0x1A2: a bit per skill, Short
Blade 0, Long Blade 1, Hand to Hand 0, Axe 3, Blunt 2, Missile 4; table
`0xEFE91`), armour if the class wears any (+0x1A3) and its type (value / 2,
at most 3) is within it, and either only up to a material (+0x1A5). Thirteen
items anyone may equip (`0xEF874`): base and enchantment 7/2017, 37/3013,
37/3012, 37/3011, 0/4012, 3/8011 (the Moon Reiver), 24–29/5010–5015 (the
Savior's Hide), 14/5016. Refused: "You are not allowed to equip this."

**Equipping** (dropping on the doll, `0x4E802` → `0x50EAE` → `0x52A00`):
- An item not carried yet is weighed first; arrows join a carried stack.
- Bows and the crossbow (8, 9, 10) need arrows ready: "You have no arrows
  ready." Without arrows ready a wielded bow is put away (`0x4E7A4`).
- Only weapons, Clothes, arrows and armour (`0xEFD56`) can be equipped.
- What it replaces comes off (`0x527F9`): the weapon for a weapon; otherwise
  a worn item whose picture's first five letters are the same (`MHLMT`, so
  the same part; arrows, which have no picture, replace arrows). The game
  then compares for clothes against `brst`, `pldn`, `pnts`, `shrt`, `skrt`
  without the sex letter, which never matches.
- It is marked worn (object +0x14 bit 0x20) and moved into the inventory; a
  constant enchantment effect starts; a weapon becomes the one in hand
  (player record +0x260).
- Dropping a worn item anywhere takes it off first (`0x507D0`).
- The secondary weapon (`0x55374`; the A key swaps it with the one in hand,
  see [Switching weapons](../re/README.md#switching-weapons-2026-10-15)) is a
  carried weapon marked in its record
  (+0x42 bit 8), one at a time.
- Worn items and the secondary weapon don't show in the carried list
  (`0x4ED43`).

**The doll** (`0x56510`) is a stack of images drawn at their own
placements in the 300 × 480 area: the body `%c%sbd001.bsi` (sex, race:
`rd`, `br`, `no`, `hi`, `dk`, `wd`, table `0xF103B`; `bd901`, a clothed
female body, when the file `c:\spire.grd` exists), the eyes
`%c%sey%03d.bsi`, mouth `%c%smt%03d.bsi` and hair `%c%shr%03d.bsi` (player
record +0x23F, +0x240, +0x23E; no hair under a helmet), the worn items'
pictures sorted by their number (pants 1xx, shirts 2xx, cuirasses 26x, arm
bands 3xx, pauldrons 33x, gauntlets 34x, greaves 45x, boots 46x, helmets and
capes 5xx, weapons 6xx). The same composition makes the bar's face, cut out
around the eyes with the frame `fram00.bsi` over it (`0x5678A`; see
[the RE notes](../re/README.md#the-interface-bar-2026-10-15)); the frame isn't
part of the doll on the inventory screen. A click on the doll picks
the topmost worn item whose image isn't transparent there (`0x56CFA`), to
drag off; the secondary weapon and the arrows ready show as models in their
slots (`0x4F8FA`).

## Using items

Double-clicking an item that isn't a container uses it (`0x501B4`):

- **A spell scroll** (base 21, number 10000 + n) teaches spell n: "Spell has
  been added to your spell list." and the scroll is gone, or "You already
  have that spell."
- **A book or other scroll** opens the book screen with its text (`0x54078`;
  see [txt_bsa.md](txt_bsa.md#books-bkn_nnntxt)).
- **A potion**, one of a stack:
  - Restoration (40) restores the spell points: "Your spell points have been
    restored.", unless they're full ("You are already at full spell point
    strength.") or the class can't ("You are not able to regenerate spell
    points this way.", player flag +0x1A1);
  - Water Breathing (39) lasts 5 minutes (+0x2C0 = now + 300000 ms): "You
    now have water breathing ability.";
  - any other: "You drink the potion." and its spell is cast on the drinker
    ([spells.md](spells.md)). A potion is used up even when it did nothing.
- **A magic item with a spell to use** (enchantment +0x21B): Restoration,
  Water Breathing or the spell is readied ("You already have a spell
  ready."), using a charge. Not implemented yet.
- Anything else: nothing.

Potions are named "%s of %s (%d)" (message 242): "Potion of Cure Health
(2)" with the spell's name and how many there are.

**Open:** magic items' spells, enchantments' effects, the overall armour class
(button 14), the other player record fields, potion and scroll names, gems
(110, 111), keys and sigils in use, what the 105 fields 1 and 2 and the 116
fields c and d mean.
