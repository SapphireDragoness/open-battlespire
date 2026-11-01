# Saved games (`SAVE0` … `SAVE9`)

The original game keeps ten save slots, folders `SAVE0` to `SAVE9` in its
install. Each holds four files. This page works from `GAME.EXE`'s save code
(`0x30307`) and load code (`0x303FE`), checked against UESP's notes on the
format (*Mod:Battlespire Save Game Format*). Where the two differ, the code
wins; the differences are listed at the end.

**Status:**
- **Container layout:** decoded from the code.
- **Checked against three saves made in the game in level 1** (2026-10-15):
  - every file reads;
  - the records' parents resolve as loading does;
  - the controllers take up the rest of SAVETREE.DAT exactly, in the
    engine's chain order (`test_install_data` checks any saves in the
    install);
  - the variables and the pictures come out right.
- **The parser:** `bs_save_*` (`bs_formats/save.h`), with unit tests on
  synthetic files and a fuzz harness.
- **Loading into the engine** (2026-10-16): `bs_save_import` (`bs_game/save_import.h`) turns a save
  into the engine's pieces, and the game starts from one (the scenario screen's Load Saved Game, or
  `--load n`). All three samples load where they were saved, matching their pictures. See
  [Loading into the engine](#loading-into-the-engine).
- **Checked against a save from each campaign level** (2026-10-30): levels 2–7, one made in a level 5
  house, one with a door moving in level 1, and one with a summoned monster. They led to
  [Interiors](#interiors): a level's interiors are in its save, and the controller table has theirs.

| File | Size | Contents |
|---|---|---|
| `SAVENAME.DAT` | 32 | the save's name (`0x301EE`); the load screen reads its first 24 bytes (`0x31104`) |
| `IMAGE.RAW` | 8000 | the view when saved, 80 × 50 pixels, RGB555 (`0x30276`) |
| `SAVETREE.DAT` | varies | the game's objects, then the level's controllers |
| `SAVEVARS.DAT` | 34805 | the player again, the clock, the log, the level's tables, the dialogue variables |

## `SAVETREE.DAT`

```
u32 version              0x100 (0xEDBBE); loading refuses anything below 0x100
record ...               the objects (below)
u32 0                    a record of length 0 ends them
controllers              the level's controller state (below)
```

### Records

The game keeps every object in a tree: a root, the level, the player and so
on, each object linked to its parent, its first child and its siblings. A
record is a `u32` length and then the object exactly as held in memory: a
`0x3D`-byte header, then the body. (The object's allocation size is the
length, so a record's size is fixed by its type.) Class files
([scl.md](scl.md)) are the same records with no version.

The header (offsets into the object; UESP's offsets are 4 more):

| Offset | Size | Field |
|---|---|---|
| +0x00 | 1 | type (below) |
| +0x01 | 3 × 2 | pitch, yaw, roll, 2048 a turn. The automap keeps its grid's width and height in the first two. |
| +0x07 | 3 × 4 | position, floats, the game's axes (+y down) |
| +0x13 | 2 | flags. Byte +0x14: 0x10 bought with the class, 0x20 worn, 2 a hidden reserve monster. |
| +0x15 | 4 | file id: a level object's model |
| +0x19 | 4 | |
| +0x1D | 4 | **record id** |
| +0x21 | 4 | a time on the game's clock (when a used gem comes back) |
| +0x25 | 2 | a level object's link list (−1 none) |
| +0x27 | 2 | a level object's controller chain: its index in the chain table (−1 none) |
| +0x29 | 4 | |
| +0x2D | 4 | in memory: the next sibling |
| +0x31 | 4 | in memory: the previous sibling |
| +0x35 | 4 | in memory: the first child |
| +0x39 | 4 | the parent. In memory a pointer; in the file the parent's record id, 0 for the root, 1 for the level. |

Types, and the body sizes in memory:

| Type | Body | What |
|---|---|---|
| 2 | 0x2F3 | an item ([items.md](items.md)) |
| 3 | 0x317 | the player |
| 6 | 0x6F | a level's placed model (door, lever, lift …) |
| 7 | 1 | a missile; its spell is its child |
| 9 | 0x77 | a spell, as the spell table's records ([spells.md](spells.md)) |
| 18 | 0x317 | a monster: the player's layout |
| 23 | 5 | options: the full view flag |
| 33 | 1 | a level's flat (an NPC, a marker): saved when it has controllers, or flag 2 in +0x14 (a speaker that has left) |
| 51 | 0x4B5 | the automap: 40 × 30 cells, 1 where seen ([../re/README.md](../re/README.md#the-bars-screens-2026-10-15)) |
| 66 | 0x1C1 | a timed spell effect, a child of the player (below) |

**Record ids** (`+0x1D`):
- The player's is always 50000 (`0x312CA`).
- A level's placed models keep their BS6 entity id.
- Objects made with `object_creator` (`0x22BCF`) take the root's counter.
- Other new objects (`0x21DE4`, `0x22E07`) take `kind << 16 | n`:
  - n counts from 10000 + 5000 × the network player's number when a level
    starts (`0x22ECC`), skips ids in use and wraps at 63000;
  - `0x21DE4` always asks for kind 1, so these ids are 75536 and up, as in
    class files.

**What is saved** (`0x2FBC4`, `0x2FF85`). The tree is walked from the root.
An object is written with its whole subtree when it is one of these:
- an item or the player;
- a spell or a monster;
- options, the automap, or types 42 and 44;
- a placed model with a link (+0x25 or +0x27) or flags `0x210`;
- type 33 with children, a link or flag 2 in +0x14, unless flag 2 is in +0x13.

Otherwise only its children are looked at.

When writing (`0x2F9B9`), the parent pointer becomes the parent's id (0 for
the root, 1 for the level). The pointers inside bodies become ids too:
- a spell's last target (+0x8C of the object);
- the player's and monsters' weapon and secondary weapon (`0x30F05`);
- type 42's +0x41.

**Loading** (`0x2FAE6`, `0x2FC18`). The level is loaded from its BS6 file
first, then each record in turn. The parent is found like this:
- a monster with flag 2: the hidden reserve, whatever its parent says (in
  the samples their parent is the reserve's old id, or even their own);
- the automap: the player;
- parent 0: the root;
- parent 1: the level cell at the record's position ("WARNING. Object
  dropped from load!" if there's none);
- otherwise the object with that id. That may be a saved record, or one of the
  level's own objects that wasn't saved, such as a chest holding items (by
  its BS6 id).

If an object with the record's id exists already (a placed model from the
BS6, say), the record is copied over it: the header's first 0x2D bytes and
the body. The object keeps its place in the tree (+0x2D on), and a type-68
object keeps its +0x29. Otherwise a new object is made. A player record
shorter than 0x317 is padded with zeros. Afterwards, items get their base
item's table pointer back, and monsters their sprite set.

### Controllers

After the records comes the state of the level logic's controllers
(`0x4C20B`, read by `0x4C3D8`). See [../re/README.md](../re/README.md),
"Level logic". For each controller chain in the table at `0x113764` (the BS6
objects' `CTRL` entries, in file order), and for each controller in the chain:

```
u8   flags               bit 0: running; bit 1 seen on some idle ones, probably "reversed" (to run backwards next)
-- if running:
u32  object              its object's record id
u8   values[36]          controller +0x14, +0x20, +0x2C: 3 floats each (the start position or angles, and the
                         two after; 0x4BE40, 0x4BEBC)
u32  start               controller +0x10: when it started, on the game's clock
```

Then the running chains:

```
u32  count
u8   chain[count]        indices into the chain table
```

The file doesn't say how many chains there are or how long each is: those
come from the level. `bs_save_controllers_read` takes them as arguments.
The chains are the objects with `CTRL` entries in the order `bs_level_logic`
builds them. In the level 1 samples all 130 saved objects' chain numbers
(+0x27) agree, and level 1's 179 controllers fill the 183 bytes.

## `SAVEVARS.DAT`

Written by `0x30A7D` and `0x5AA46`, read by `0x3078C` and `0x5AB76`. Fixed
blocks, one after the other. Before writing, the tables that hold object
pointers turn them into record ids (`0x30D6A`), and back after reading
(`0x30E2D`).

| Offset | Size | Contents |
|---|---|---|
| 0 | 0x317 | the player record's body (the same as the tree's) |
| 791 | 0x104 | `0x16C088`, unknown |
| 1051 | 4 | the campaign level's number |
| 1055 | 4 | the game's millisecond clock (`0x421FBC`) |
| 1059 | 4 × 4 | `0x16C024`, `0x16C008`, `0x16BFC8`, `0x16BFD0` |
| 1075 | 0x500 | the message log: NUL-terminated messages, each missing its first letter (the samples hold only "Enter name of save game.") |
| 2355 | 0x40 | `0x112724`, unknown |
| 2419 | 4 | `0x16BF9C` |
| 2423 | 4 | music volume (0..127) |
| 2427 | 4 | sound volume |
| 2431 | 1 | `0x16FFAD` |
| 2432 | 4 | `0x16C1B8` |
| 2436 | 0x400 + 4 | table `0x100FE4` and its count `0x16BF0C` (UESP: the log's times) |
| 3464 | 0x400 + 4 | dialogue links: 128 × (record id, STRU 104's number), count (`0x100BE4`; the dialogue's DIALOGLINK n uses those with n) |
| 4492 | 0x400 + 4 | conversations: 128 × (record id, 4-letter file stem), count |
| 5520 | 0x1C00 + 4 | the level's placed monsters' overrides: 128 × 56 (below), count |
| 12692 | 0x480 + 4 | health and spell point gems: 128 × (record id, amount, u8 spell points), count |
| 13848 | 0x200 + 4 | 128 record ids, count (`0x100984`; unknown) |
| 14364 | 0x300 + 4 | sigils: 64 × (record id, s32 y, u32), count |
| 15136 | 0x60 | speakers leaving: 8 × (record id, when, when told), from the dialogue's TELEPORT n (`0x100B84`, `0x5A41C`) |
| 15232 | 1 | `0x16FFB8` |
| 15233 | 4 | the number of global variables |
| 15237 | 0x800 | 256 global variables: (hash, value) |
| 17285 | 0x4400 | 256 monsters' local variables: (record id, 8 × (hash, value)) |
| 34693 | 16 | conversations so far per creature type, at most 3: they pick the `talktable` row (`0x401D44`) |
| 34709 | 0x60 | the 8 × 12 bytes of `0x100B84` again |

A placed monster's override (from BS6 `STRU` 106), 14 × 32-bit values:
- the record id;
- 1 + the creature (0 random);
- speed, strength, spell points, health, skill;
- a goal (13 waits to talk);
- the innate effects as spell bits;
- five spells.

**The variables.** A dialogue variable is stored by the hash of its name
(`0x1BAE9`):
- Leading spaces and one `&` are skipped.
- The name ends at the first space or control character, uppercased.
- For each character: h = (h rotated left by 4) + c.

This reproduces all 146 hashes UESP lists. One of them is `SkelServe`, not
`SkelServ`.

The globals start with PCMale and PCFemale, set by the character's sex (from
+0x226 bit 3), and SkelServe, false (`0x5AE3D`).

## The player record (type 3 and 18)

The class file fields are in [scl.md](scl.md). A save also uses these:

| Offset | Field |
|---|---|
| +0x72 + 12 n, +0x76, +0x7A | skill n: with effects, uses, base |
| +0x16E | the Shield spell's points |
| +0x172 | the effects on, a bit per spell |
| +0x176 | a monster's level |
| +0x1FE | hot keys F1–F8: a spell's number, or 64 + n for the item in `+0x2E7`'s slot n |
| +0x20A | level-up points left over (at first the CharacterMaker's; see [Levelling](../re/README.md#levelling-2026-10-17)) |
| +0x226 | bits: 3 female, 0x40 sneaking, 0x80 Cursor movement (Controls) |
| +0x227 | bits: 4 the lantern on (set by the CharacterMaker, toggled by the Lantern key) |
| +0x236, +0x24A | music and sound volumes |
| +0x241 | 1 once the player has come by Dagon's transport beam (`0x4AC68`); the win needs it |
| +0x24E | a monster's creature |
| +0x260, +0x264 | the weapon and secondary weapon (record ids in the file) |
| +0x268 | armour values: helmet, pauldrons, cuirass, greaves, gauntlets, boots, shield |
| +0x2B0 | Teleport's anchor |
| +0x2C0 | when the breath runs out |
| +0x2DA | when the player is captured (Dagon's beam without the armour, or 3 s after his conversation), 0 none; see the RE notes, "Videos" |
| +0x2E0 | turn rate (Controls): the turning keys go 6 × it angle units a second; 0 becomes 128 when a level starts |
| +0x2E7 | 8 hot key items' record ids |
| +0x30B | when Confusion ends |

## Editing with `bssave`

`bssave edit` changes an original save and writes it to a new folder (or the
same one with `--in-place`). Everything it doesn't touch stays byte for byte
as it was; the install's three saves come back identical when nothing is
changed (`test_install_data`). It's built on `bs_save_edit`
(`bs_game/save_edit.h`):

- **The player's fields by name** (`bssave fields`):
  - an attribute or skill sets both its value and its base;
  - `health_max` sets +0x6A and +0x6E;
  - `level` sets +0x176 and the class's byte +0x29F;
  - `female` also switches the sex letter of the pictures of the weapons,
    clothes and armour carried, as making a character does (`0x54219`).
- **SAVEVARS.DAT's copy of the player** is made the same as the record,
  except the weapon and secondary weapon (+0x260, +0x264). In SAVEVARS
  those are the pointers the game had in memory: it turns them into ids only
  for the tree (`0x30F05`), and loading replaces them from the record anyway.
- **A new item** is made as the item maker makes one (`0x521AB`):
  - the base record copied from GAME.EXE (0x2F2 bytes; the record's last byte
    is left 0);
  - the material, or a scroll's, book's or Sigil Amulet's number (+0x33),
    and a potion's type at +0x39 (0xFF otherwise);
  - the condition, an arrow count (+0x3E), the picture, and +0x2C3 = 0xFF;
  - an enchantment is copied from an item in the save that has it (its
    0x233 bytes at +0xB3);
  - the record goes right after its parent, at the parent's place, with an
    id past every other.
- **Taking an item or a monster** takes what's under it, and clears what
  pointed at it: the weapons, a hot key (and the F key on it), a speaker
  leaving.
- **Variables** are set by name (hashed) or hash, and added if they aren't
  there.

The fields that the save's runtime data overwrites on loading (the
renderer's floats at +0x44 … +0xA5 of an item) are left as they are; a new
item has them zero, as a freshly made one does.

## Loading into the engine

The game loads a save over a fresh copy of its campaign level (`0x303FE`), and the engine does the same:

- **The level** is the campaign level with the saved number (`0x22ECC` picks it by number), with its
  interiors in its world. The engine loads interiors as levels of their own: a save made in one (the
  player's +0x298) starts in that interior. Every other space (the campaign level, the other interiors)
  is kept as a level left through a door, with its items, monsters, objects, controllers and sigils.
- **Ids** of an interior's records are taken back to its own file's (less its space's base; see
  [Interiors](#interiors)).
- **The clock** runs on from the saved one, so the times in the save (a used gem's return, controllers'
  start times, speakers leaving) mean what they meant.
- **Riding:** a boat or the balloon being ridden is a child of the player's record; it's imported as
  ridden (`bs_saved_game.ride`).
- **The player:** the record's position is the feet, its angles the view (yaw, and pitch with + down).
  The character comes from the record: base attributes (+0x40), base skills (+0x7A) and their use
  counts (+0x76), the level-up points left (+0x20A), health and spell points, the level (+0x176), the class's rules and advantages, the face, race and sex. The
  spells known are its spell children, as the spell book left them. Hot keys keep their spells; an
  item on a hot key (64 + n) isn't kept.
- **Items:** every item is saved, so the level's own items aren't made: the save's are put where they
  are, which is one of:
  - carried (the player's child), worn if +0x14 has 0x20, the secondary weapon by +0x264;
  - in a container item;
  - lying in the level (parent 1). A placed item keeps its object's BS6 id: while it lies where the
    object was, the object shows it, as when the level made it;
  - in a level object (a chest), saved or not, by its BS6 id;
  - held by a monster: monsters carry their loot in a sack (the level's `STRU` 116 items and a Sigil
    Amulet). The sack falls where the monster dies.

  Items in the level's other spaces are kept for those levels.

  Where the game keeps a scroll's or book's text number and a Sigil Amulet's number in the material
  field (+0x33, `0x522D7`, `0x52384`), the engine has them as the item's number; a potion's type is
  at +0x39 (`0x51EAA`).
- **Monsters:** only those saved are made, in each space; the rest are dead. A spawned monster's record id is its spawn
  flat's BS6 id, which gives it its dialogue code and spells. The record gives its creature, position,
  facing, attributes, health, spell points, skill, flags, AI state (+0x232), whether it greeted
  (+0x13 bit 0x40), and whether it was summoned (+0x226 bit 0x10) for the player's team (+0x22A). The
  hidden reserve (+0x14 bit 2, kept in a type-22 container) isn't kept: in the samples it holds
  pre-made monsters and the two killed.
- **The level's placed models** that were saved (those with links or controllers) take their position
  and angles, their lock (+0x13 bit 8, cleared when a key opens it) and a used gem's return (+0x21).
  Type-6 records in the root (ids 75538 and up, at the origin, +0x14 bit 2) are the engine's own and
  are skipped.
- **Controllers:** the table is read with every space's chains (`bs_level_spaces` knows each chain's
  entity), and each controller not as it began becomes one of ours, by its object: its reversed bit, and
  for a running one its start time and start position or angles (+0x14, floats, rounded). A running
  chain's object is active. +0x20 and +0x2C aren't used. A table that doesn't fit leaves the controllers
  as the levels begin.
- **The rest:** the automap's 1200 cells go row by row into the level's maps (checked: the cell under the
  player is seen); the grids of the other spaces go into theirs; the dialogue variables and visits;
  speakers leaving; dispelled sigils (y 10000 in the sigil table); the level script's progress (+0x25E, with its counter picture) and whether it's
  done (byte 15232).
- **Timed effects** (type 66) by their spell's name: the icon, the end, the flags, the attributes'
  changes, the tick and the pools. The spell points', health's and skills' changes aren't kept (the
  engine's effects don't make them).
- **Flats** that were saved: those with controllers take their position, and a speaker that has left
  (flat or object, +0x14 bit 2) is hidden and doesn't talk.

Not loaded: the options record (the full view flag), the volumes, and the log.

**The load screen** (Select Scenario's Load Saved Game, `0x31C1E`; drawn by `0x325B9`, buttons
`0xEDC52`):
- `load00.bsi`, with each slot's picture at x 48 + 112 (n mod 5), y 192 (slots 0–4) or 304 (5–9), and
  its name in font 8 64 pixels below.
- `brdr01.bsi` frames the chosen slot from 4 pixels up and left, and slides to a new one (`0x2093A`).
  It starts on slot 0.
- Right or Tab, Left or Backspace, Up and Down move it.
- A click chooses a slot, a double click loads it. Okay (199, 416)–(294, 455) or Enter loads the one
  chosen, Cancel (327, 418)–(422, 455) or Esc goes back.
- A slot loads only if it has SAVENAME.DAT (`0x31468`). The game shows `loading.bsi` while it loads.

**The options bar's Load and Save** (`0x2EF60`, `0x2EF9A`, "Not during
network play."; the screen `0x31A62`, drawn by `0x31776`, buttons `0xEDA93`):
- `loadsave.bsi`, with `load.bsi` or `save.bsi` over it (the title and OK
  button).
- The same ten slots, buttons 84 × 53 from (48, 192), and `brdr01.bsi`
  sliding to the one chosen. Only the chosen slot's name shows, centred at
  y 397.
- OK (252, 434)–(375, 467) or Enter; Exit (590, 427)–(634, 473) or Esc. A
  double click on a slot is OK.
- Loading loads the slot (`0x303FE`) if it has a save.
- Saving (`0x31714`) says "Enter name of save game." and takes the name
  typed at (141, 397) in font 1, at most 23 characters, into the slot's
  name. Enter saves (`0x30307`).

The engine has this screen for its own saves ([game_save.md](game_save.md)):
Load also shows the original game's slots on a second page.

### Timed effects

A timed spell effect (`0x295A0`) is a type-66 child of the player, one per spell, so it's saved while
it lasts. Its body:

| Offset | Size | Field |
|---|---|---|
| +0x00 | 32 | the spell's name: effects are found by it (the spell's number isn't kept) |
| +0x20 | 8 × 12 | each attribute's change: the first 4 bytes, 16.16, are added each frame (`0x294A0`) |
| +0x80, +0x8C | 12 each | spell points and health maximum changes, the same way |
| +0x98 | 21 × 12 | the skills' changes, added whole |
| +0x194, +0x198, +0x19C | 4 each | health added each tick, the last tick, the ms between (Continuous Damage, Poison) |
| +0x1A0 | 4 | the icon + 1 (`symbNN.bsi`; the spell record's +0x22) |
| +0x1A4 | 4 | when it ends, on the game's clock |
| +0x1A8 | 4 | the flags it ORs into the player's +0x172 |
| +0x1B4 | 3 × 4 | the spell pools (resist, absorb, reflect) |

Checked with a Fire Shield (icon 18, flag `0x80000`, 32 s left when saved).

### Speakers that leave

A speaker that leaves (a dialogue's TELEPORT 0, or KILL, `0x5817E`) isn't removed:
- **a monster** goes into the hidden reserve (+0x14 bit 2), where it stood. Loading skips the reserve,
  so it stays gone (checked: level 5's Dark Seducer);
- **a flat or a talking object** gets flag 2 in +0x14 and is saved with it (checked: Josian, flat 93 of
  Dagon's lodge).

TELEPORT n > 0 waits n seconds in the speakers-leaving table first (`SAVEVARS.DAT` 15136); no line in
the campaign's dialogue uses one.

## Interiors

The game loads a campaign level's interiors (level 5's houses, level 6's keep, Dagon's lodge) into the
level's world when the level starts (`0x22ECC`). Each sits at its own BS6 coordinates (level 5's shop
at y 18439–18803, far below the town), and a door moves the player within that one world. So a save
holds every interior, whether the player went in or not. The engine finds them with
`bs_level_spaces_build` (`bs_game/level_spaces.h`).

- **Which interiors, in what order:** the level's doors (objects with BS6 property 100) in object
  order, then the doors of each interior loaded, in the order they were loaded. A nested interior (level
  5's `inspire`, behind `inmntn`) comes after all of the level's own.
- **Ids:** an interior's entities get its BS6 ids plus a base. A counter starts at the level's highest
  entity id (objects, flats and lights) + 100. Then, for each door:
  - if it leads to a file not loaded yet, the file gets the counter as its base, and the counter goes up
    by the file's highest id + 100;
  - any other door adds 100. That covers a door back to the level, a second door to an interior
    already loaded (level 5's 316, to `barckin` again), and a name that doesn't load (level 5's 314,
    with no name).
- **Level 5's bases:** `mtemple` 797, `tavrn2` 956, …, `inmntn` 3536, then `inspire` 5966. Level 6's
  `inkeep` is 1288 and level 8's `indagon` 459. All match the saves' records by position.
- **Where the records hang:** an interior's top-level entities (its placed models, monsters and the
  items lying in it) have the door that loaded it as their parent; a nested interior's door has the id
  its interior gives it (`inspire`'s door is 3536 + 3). Items in an interior's chests have the chest's
  id with the base added.
- **The controller table** has the level's chains, then each interior's, in load order. An interior's
  saved models have their place in it at +0x27, as the level's do.
- **The space the player is in:** the player's +0x298 holds the id of its door, 0 in the level
  (`0x3065C` sets the current space, `0x16C1AC`, from it on loading). Each automap has its space's door id
  at +0x21; each frame the game marks the grid whose +0x21 matches the current space (`0x454C4`).

## Differences from UESP's notes

- **The global variables** are 256 records of 8 bytes, and the local
  records 256 of 68. UESP read 1344 globals and 128 local records, because
  the local records fill from the end.
- **The block from 2355** is 0x40 bytes, five values (two of them the
  volumes) and two 0x400-byte tables with their counts. UESP took all of it
  for the log's times.
- **The health and spell point table** is 0x480 bytes, 128 records of 9.
- The file's last 0x60 bytes repeat the table at 15136.
- **Female** is bit 3 (8) of +0x226; UESP lists bits 3 and 11.
- **The 16 bytes at 34693** count the conversations had with each creature type (up to 3), not local
  records. The 1024 bytes at 3464 are the dialogue links, and the 96 at 15136 the speakers leaving
  (see [../re/README.md](../re/README.md), "Dialogue").

## Open

- **The unknown blocks:** `0x16C088`, `0x112724`, the table at `0x100FE4`, and
  `0x100984`.
- Speakers about to leave (TELEPORT n > 0) outside the space the player is in are dropped when
  loading; none of the campaign's dialogue uses one.
