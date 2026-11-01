# Character classes (`GAMEDATA/CLASSnnn.SCL`)

The 18 classes the game comes with (Acrobat … Warrior) are `CLASS000.SCL` to
`CLASS017.SCL`. The CharacterMaker's Save writes the next free number
(`0x3B39C`). A class file is the game's own object records as a save writes
them (`0x2FB80`; [save.md](save.md)): the player record first, then the
objects it holds.

**Status:** verified. `bs_class_parse` (`bs_formats/scl.h`) reads all 18
shipped classes, and `test_install_data` checks them. Rebuilding each class
with the CharacterMaker's costs (`bs_game/creation.h`) comes to about 5000
points for all of them (below). `bs_class_write` writes one as the game's
Save does ([Writing](#writing-the-charactermakers-save)); each shipped class
saved again reads back the same.

## Records

Each record is:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | length of the rest of the record (not counting these 4 bytes) |
| 4 | 1 | object type: 3 player, 9 spell, 2 item, 51 (0x33) automap |
| 5 | 60 | the rest of the object header |
| 24 | 1 | the object's flags (+0x14): 0x10 bought when the class was made, 0x20 worn |
| 65 | … | the body |

Offsets in the bodies below are the game's offsets into its in-memory
objects. A save editor's offsets (UESP's save notes) are these plus 65.

The order is: the player, its known spells, its carried items, and an automap
record, which the parser skips.

## The player (type 3)

The body is the player record at `0x16BF90`, 0x317 bytes in memory. Most
files store 852 bytes. `CLASS002.SCL` stores 743, which still covers every
field a class uses; the parser needs 0x2E2.

| Offset | Size | Field |
|---|---|---|
| +0x00 | 32 | character name (empty in classes) |
| +0x20 | 8 × 4 | attributes with effects (Strength, Intelligence, Willpower, Agility, Endurance, Personality, Speed, Luck) |
| +0x40 | 8 × 4 | attributes, base |
| +0x60, +0x62, +0x64 | 2 each | spell points: current, maximum, base |
| +0x66, +0x6A, +0x6E | 4 each | wounds (health): current, maximum, base |
| +0x72 + 12 n | 4 | skill n with effects (21 skills, Medical … Critical Strike) |
| +0x76 + 12 n | 4 | skill n's use count |
| +0x7A + 12 n | 4 | skill n, base |
| +0x17A | 24 | class name |
| +0x192 | 1 | resistances, a bit per element (1 magic, 2 fire, 4 cold, 8 shock, 16 poison) |
| +0x193 | 1 | immunities, the same bits |
| +0x194 | 1 | Acute Hearing |
| +0x195 | 1 | Spell Absorption |
| +0x196 | 1 | Rapid Healing |
| +0x197 | 1 | Regenerate Health |
| +0x198 | 1 | Regenerate Spell Points |
| +0x199 | 1 | Athleticism |
| +0x19A | 4 | Increase Magery: spell points per point of Intelligence × 256 (0x100, 0x180, 0x200, 0x300) |
| +0x19E | 1 | Adrenaline Rush |
| +0x19F | 1 | low tolerances, element bits |
| +0x1A0 | 1 | critical weaknesses, element bits |
| +0x1A1 | 1 | bit 0: no spell point regeneration |
| +0x1A2 | 1 | forbidden weapons: 1 short blades, 2 long blades, 4 blunt, 8 axes, 16 missile |
| +0x1A3 | 1 | heaviest armour allowed: 0 none, 1 light, 2 medium, 3 heavy |
| +0x1A4 | 1 | no shield |
| +0x1A5 | 1 | best material allowed: 0 Iron … 9 Daedric |
| +0x1A6 | 12 | class skills: three primary, three major, six minor (0xFF none) |
| +0x1B2 | … | base copies of the fields from +0x192, made when the class is finished (`0x37508`) |
| +0x20A | 4 | build points left |
| +0x226 | 4 | flags: 8 female (`0x324E6`) |
| +0x23E, +0x23F, +0x240 | 1 each | the face: hair, eyes and mouth picture numbers |
| +0x25D | 1 | race (0 Redguard, Breton, Nord, High Elf, Dark Elf, 5 Wood Elf) |
| +0x29F | 1 | the level the class is for (all 1) |
| +0x2E1 | 1 | the build points / 100 it was made with for network play; 0 in all |

## Known spells (type 9)

The body is a 0x77-byte spell record like the table's in `GAME.EXE`
([spells.md](spells.md)), with the class's choices of icon, delivery,
element and detonation.

## Items (type 2)

The body is an item record, 0x2F2 bytes and more:

| Offset | Size | Field |
|---|---|---|
| +0x00 | 32 | name |
| +0x20 | 2 | base item |
| +0x22 | 9 | worn picture, e.g. `MWPNS603` (starting with the maker's sex letter) |
| +0x2B | 4 | condition |
| +0x2F | 4 | condition maximum |
| +0x33 | 2 | material |
| +0x39 | 1 | a potion's type (0xFF none) |
| +0x3E | 4 | quantity (arrows) |
| +0x2E2 | 4 | the enchantment (its id in `MG*.TXT`), 0 if none |

When a class is loaded (`0x54219`), its weapons', clothes' and armour's
(`0x54522`) pictures get the new character's sex letter. The shipped classes'
armour pictures all start with `f` or `F`. Its items are carried,
not worn. In the CharacterMaker, the items flagged 0x10 go to the shops as
bought (`0x3BDC3`): enchanted items and potions to the magic items, the rest
to the equipment.

## The build points

Rebuilding the shipped classes from a new one, with the CharacterMaker's
costs ([../re/README.md](../re/README.md), "Character creation"), gives
these totals of points spent plus `+0x20A` left. Spent counts each skill from its free start
(30 primary, 20 major, 15 minor) and the items and spells at their prices:

- 4980 for Acrobat, Archer, Barbarian, Burglar, Mage, Ranger and Thief.
- 5000 for Assassin, Bard and Warrior.
- 5010 for Spellsword.
- 5030 for Battlemage.
- 5080 for Monk.
- 5230 for Healer and Rogue.
- 5240 for Knight.
- 5280 for Nightblade.
- 5480 for Sorcerer.

Seven classes come to 4980 and three to exactly 5000, so the costs are the
game's and the budget was 5000 points (single player's). The rest come out
higher, by multiples of 10 up to 480. The spells and items of those classes
probably cost more when they were made than the table and items price them
now, but this isn't confirmed.

## Writing (the CharacterMaker's Save)

The Save button (`0x391A0`) copies the items bought in the two shops to the
player as new objects flagged 0x10, equipment first (`0x3BC97`), saves
(`0x3B39C`), then deletes the player's 0x10 items again (`0x3BD6F`). The
save:

1. Shows "Saving out your level %d class..." (message 515).
2. Lists the classes as the class list does (`0x337EE`). If one has the same
   "Name (level)", it asks `exist0.bsi` ("A class with that name already
   exists. Do you wish to overwrite it?", the same box and buttons as
   `sure00.bsi`). No stops the save; Yes deletes that file.
3. Sets the player's points left (+0x20A), the level (+0x29F), +0x307 (0
   outside network play) and the header's +0x29 to the clock.
4. Takes the lowest number from 0 that no `class*.scl` in GAMEDATA has
   (`atoi` of the name from its sixth character, at most 127 files).
5. Writes the player object and everything under it (`0x2FB80`) to
   `class%03d.scl`: the records as a save writes them, with no version and no
   end record. Then "Your level %d class has been saved." (134).

So a file holds the whole player record: the character's name, sex, face and
race too, which loading a class doesn't take. The shipped files have the
attributes and skills with effects (+0x20, +0x72) at a new class's 50 and 5
(`0x3B273` sets both; the CharacterMaker changes only the bases), wounds and
spell points 0 but the base wounds, and the base copies (+0x1B2) as they were
when the CharacterMaker opened. Spells' header +0x14 is 2 and the player's
+0x13 is 1; children have ids `1 << 16 | n` and the player (50000) as parent.

**In the engine** (`bs_class_write`; `bs_creation_write` makes the records
from the CharacterMaker's state):
- The player record as above, the base copies equal to the fields.
- The spells: the spell table's record in `GAME.EXE` with the class's spell
  over it. A shipped class's spells come out the same but +0x4F, the
  caster, set when it's cast.
- An automap with nothing seen.
- The items: each one's base record (`0x521AB`) with its material,
  condition, quantity, potion type and picture, and its enchantment as the
  game holds it ([items.md](items.md#magic-enchantments-txtbsa)). The rest
  the item maker fills in (+0x44 to +0xA5, the model's position and turn;
  the weight; the clothes' names) is left as the base record has it.

Our saved classes go in a `classes` folder in our saves folder, not in the
install. The class list shows ours with the install's, ours in place of the
install's of the same number. A new class takes the lowest number neither
has (so the first is `CLASS018.SCL`); replacing one keeps its number. The
files are the original's format and can be copied into its GAMEDATA.

