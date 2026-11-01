# BS6 levels (*.BS6)

Level layouts: placed 3D models, flats (sprites and markers), lights and
viewpoints. The game's levels are listed in `LEVELS.TXT`
([levels_txt.md](levels_txt.md)).
- `GAMEDATA/BS6.BSA` holds 45 `.BS6` records: the campaign levels, the
  deathmatch and capture-the-flag maps, interiors (`INKEEP`, `TAVERN`, …),
  and old or unused levels (`L4_OLD`, `L8OLD`, `L12`, `FIXIT`, `BACKUP`).
- The v1.5 patch adds `dmog.bs6`, `dmka.bs6` and `dmzr.bs6` as loose files
  in `GAMEDATA/`.

Status: **structure verified**. All 45 archived levels and the 3 loose
ones parse and build complete scenes. The levels in `LEVELS.TXT` place
4,085 objects, and exactly one model is missing: `dmsc.bs6` places
`scgrate.3d`, which isn't in the data. The entity logic chunks (`STRU`,
`CTRL`, `LINK`) have been surveyed but not decoded.

## Chunks

A tree of chunks, each a 4-character tag and a **little-endian** u32 body
length (unlike BSI's big-endian lengths). Container chunks hold other
chunks; the rest hold fixed-size values. Name fields (`DIRN`, `FILN`, `LFIL`
entries) are 260 bytes, NUL-padded: Windows `MAX_PATH`, so the level editor
was a Windows tool.

```
GNRL                              the whole file
  TEXI { DIRN }                   editor texture folder ("e:\projects\batspire\art\tex_cels\hicolor")
  BITS  i32                       15 in every level
  BBOX  6 x i32                   bounding box; the minimum y is often garbage
  OFST  3 x i32                   0
  RADI  i32
  CENT  3 x i32                   centre (y often garbage, like BBOX)
  SNAP { IDTY POSI ANGS }  x2     editor snap grid settings
  WATR  i32                       water height; non-zero in l2 (303), l5 (91), l6 (354)
  VIEW { IDNB IDTY POSI ANGS AMBI }  x4   viewpoints
  STRU { DIRN FILN }              "structs": the editor's entity definitions file
  OBJS                            placed models
    DIRN                          editor model folder
    LFIL                          model file list: n x 260-byte names without extension ("1blok1")
    OBJD { IDNB IDFI POSI ANGS SELE [STRU] [CTRL] [LINK] ... }   one per placed model
  LITS
    LITD { IDNB IDTY POSI RADI BRIT SELE [CTRL] }                 one per point light
  FLAS
    FLAD { IDNB FILN POSI SCAL AMBI SELE [STRU] [CTRL] [LINK] }   one per flat
```

| Chunk | Body | Meaning |
|---|---|---|
| `IDNB` | i32 | entity id, unique within the level; `LINK` chunks refer to it |
| `IDTY` | i32 | type: lights always 0; views 0–6; links 0 or 2 |
| `IDFI` | i32 | index into `LFIL` |
| `POSI` | 3 × i32 | position in level units, **+y down** |
| `ANGS` | 3 × i32 | rotation, **2048 units per turn**; see below |
| `SELE` | i32 | editor selection state (almost always 0) |
| `FILN` | 260 bytes | flat image name, a BSI without extension (`start`, `monster1`, `flmw00`) |
| `SCAL` | i32 | flat scale? 0 in 2,374 of 2,383 flats; the rest 23–219 |
| `AMBI` | i32 | ambient value? On flats 0 (a few 30, 31); on views 0–60000, mostly 16128. Meaning unknown |
| `RADI` | i32 | light radius in level units (e.g. 850) |
| `BRIT` | i32 | light brightness, 11–1023 (mostly 32–128). Scale unknown |
| `STRU` | `{ IDNB RAWD }` | entity property: `IDNB` is the property id, `RAWD` raw bytes. See the survey below |
| `CTRL` | `{ IDNB RAWD }` | controller: triggers and movements. See the survey below |
| `LINK` | `{ IDTY IDNB }` | link to another entity by id |

`STRU`, `CTRL` and `LINK` carry the level logic: doors, lifts, monster and
item spawns, triggers. The top-level `STRU` names the editor's definitions
file, `e:\projects\batspire\structs`, which isn't shipped.

### Survey of the logic chunks (2026-10-13)

Counted over the 45 levels in `BS6.BSA` (the loose `dm*.bs6` files replacing
their archived copies). Every property and controller id has a fixed
`RAWD` size, and all 486 links point at an existing entity. **The meanings
are guesses** from which models and flats carry each id and what the values
look like; the RE step will confirm them. The game's reader compares these
tags at `0x23F5B`–`0x23FD3`, `0x247DA`–`0x247EC` (id plus `RAWD`),
`0x24FC0`–`0x2501E` and `0x2528F`–`0x25339`.

**`STRU` properties** (`IDNB` = property id):

| Id | Bytes | On | Values | Guess |
|---|---|---|---|---|
| 100 | 30 | 55 doors and ladders (`inbrdr1`, `l6kepdr`, `catlader`) | a 14-byte name (`l5`, `inkeep`), then three i32 (e.g. 463, 4, 10000) | **confirmed:** a door: the name is the BS6 file it leads to (without `.bs6`), the first i32 the arrival point (property 101) there; doors with the same name are paired (`0x24817`); the last two not decoded. See the RE notes, "Level transitions" |
| 101 | 4 | 59 `start`/`user` flats | 0 or numbers like 221–448 | **confirmed:** 0 or 666 makes the flat the level's start; other values register arrival point n (`0x248E6`), where doors (100) lead |
| 102 | 4 | 2 monster flats, 4 objects (`scroll`, `72_3`) | 25, 10 | ? |
| 103 | 4 | 10 exits and panels (`l4exit`, `exitgate`, `1cpanel`) | 1 | **confirmed:** sets object flag `0x08` (`0x24974`): the object's own use event isn't sent; the level's script (`0x3E578`) handles it: keys (level 1's Cogs, level 2's Voidguides) and level exits. See the RE notes, "Keys, sigils and gems" |
| 104 | 4 | 1 NPC (`jaciel`, in `indagon.bs6`) | 13 | **confirmed:** the number the dialogue's `DIALOGLINK n` uses (table `0x100BE4`): Dagon's `DialogLink_13` calls Jaciel |
| 105 | 16 | 354 items (`arrow`, `helm`, `dagger`, `coffer`) | e.g. 22 0 0 10000 | **confirmed:** a placed item (base item first); see [items.md](items.md) |
| 106 | 52 (28 in older files) | 698 monster flats, a few NPCs | first i32 2–16, rest mostly 0 | **confirmed:** a monster spawn's creature record on `monster1`/`monster2` flats: the creature (1–16), overrides of Speed, Strength, spell points, health and skills, the AI state to start in, flags, spells; see [creatures.md](creatures.md) |
| 107 | 4 | 92 books, scrolls, sheets | 15–42 | **confirmed:** a book (base item 32) with this text number |
| 108 | 4 | 464 monster and NPC flats, gems | three ASCII letters (`sr5`, `DS5`, `cf3`) | **confirmed:** a three-letter code kept per object (`0x24A9F`); the win check compares it with `ZZ1`; probably the dialogue table code. On a spawn point it always makes a monster, which greets the player ([creatures.md](creatures.md)) |
| 110 | 4 | 102 gems, traps | 100, −25, −75, … | **confirmed:** a gem: using it adds this % of the maximum health (`0x4D64C`; see the RE notes, "Keys, sigils and gems") |
| 111 | 4 | 41 gems | 100, 80 | **confirmed:** a gem for spell points, as 110 |
| 112 | 4 | 27 monster flats, scrolls | 26, 31, 51 | ? |
| 113 | 8 | 29 doors, coffins, feathers | pairs like 1032 1033 | **confirmed:** a pair of values per object in a table (`0x24BE6`); probably two `TEXT.RSC` messages |
| 114 | 4 | 184 plaques, scrolls, graves | 1019, 1150, … | **confirmed:** stored in the object as value + 2,000,000,000 (`0x24C26`): the `TEXT.RSC` record shown when the object is used (`0x4D254`; 1150 is the briefing on Lord Dagon) |
| 115 | 4 | 469 potions and small chests | 10–40 | **confirmed:** a potion (base item 15) of this type |
| 116 | 16 | 333 containers, 296 monster flats | e.g. 5010 24 0 0, 10000 19 0 0 | **confirmed:** container contents, or on a spawn point the monster's loot, dropped where it dies: magic item a on base item b, or random, see [items.md](items.md) |
| 119 | 32 | 7 books | a short string (`&cfa…`) | **confirmed:** a text name; with a leading `&` it is looked up by name (`0x24D7E`) |
| 120 | 4 | 1 `1doorpan` (level 1) | 1 | **confirmed:** the object starts used: when the level is loaded, its moves and rotations and those of the objects it links to are put at their ends (`0x23816` → `0x4C7BC`), so one of level 1's generators runs from the start (see the RE notes on `ANCHORS.SMK`) |
| 121 | 8 | 729 triggers and moving parts | e.g. 72 1, 351 1 | **confirmed:** sound played when the object's (last) controller starts: `SPIRE.SND` id, then a value of unknown use |
| 122 | 4 | 171 sacks, scrolls, sigils | 0 or 1 | **confirmed:** the kind of game the entity is in: 0 single player only, 1 multiplayer only (without it, both). The level loaders delete the other kind's entities (`0x246F5`, `0x2548C`, `0x25A90`; multiplayer is `0xF219F`, set once the network driver is up). Notes come in pairs: a scroll to take with a book text (107) for single player beside a fixed one with the same text from `TEXT.RSC` (114) for multiplayer (level 1: 999/1099, 995/1095, 993/1093, 992/1092). Multiplayer also has sigils, sacks of amulets and the monsters guarding them (level 5's creature 14). The deleted entities' controllers keep their places in a save's controller table |
| 123 | 4 | 8 levers | 1 | lever |

**`CTRL` controllers** (`IDNB` = controller id; 24 or 32 bytes of i32):

| Id | Bytes | On | Values | Guess |
|---|---|---|---|---|
| 16 | 24 | 458 buttons, gems, chests, wheels | mostly all 0 | **confirmed:** trigger when used |
| 5 | 24 | 308 lifts, floor pieces, portals | mostly 0; a few with a time (500, 1000) | **confirmed:** trigger when stepped onto |
| 149 | 24 | 103 logs, gears, pit parts (mostly level 6) | second value 900–60000 | **confirmed:** timer; after the period (ms) it starts the linked objects |
| 0 | 24 | 4 (`biggem`, `l8tbeam`, `catrck2`) | 0 | trigger on event 0: touching the object from the side (inferred from the collision code) |
| 261 | 32 | 439 objects, 57 flats, 2 lights | x y z, time in ms, options text, … (e.g. 0 −500 0 4000 "r": up 500 in 4 s, reversible) | **confirmed:** move by an offset, linearly |
| 512 | 32 | 214 doors, wheels, spikes | three angles (2048 per turn), time in ms, options text | **confirmed:** rotate (doors −512 = 90°, wheels 2047 = one turn) |

The "114" in the survey is the letter `r`: bytes 16–27 of moves and rotations
are an option text, `r` (reversible), `c` (continuous) or both. The trigger
values 2–4, when set, are an absolute destination for the moves started with
them. Full rules: [RE notes](../re/README.md#level-logic-triggers-and-movements-found-2026-10-13).

**`LINK`** (`IDTY` = link type, `IDNB` = target entity). **Confirmed:** when
an object's own trigger fires (and its chain has no timer), every linked
object is started too. Triggers link to the parts they move: in level 1 each `1panel` (16) links to a block, door or
platform with a move (261); each stalactite (5) to a plank that moves.
Type 2 (57 links) goes from a trigger to a `user` flat whose 261 holds
coordinates (the wizard portals `1wizpo1`–`5`): a teleport, **confirmed** (the
player lands at the flat's position + its move offset). Type 0 (429) is
everything else.

Survey scripts: `build/re/scripts/` (not committed; they read the levels
extracted by `bstool extract GAMEDATA/BS6.BSA`).

## Placing models

A model point `p` (model units, see [3d.md](3d.md)) goes to level
coordinates as

```
level = R(ANGS) · (p / 256) + POSI
R = Rz(a2) · Rx(a0) · Ry(−a1)       angles in radians = a · 2π / 2048
```

`Rx`, `Ry` and `Rz` are the usual right-handed rotation matrices: rotate
about y by −a1 first, then about x, then about z. This was found by trying
every axis order and sign and counting shared vertices between neighbouring
models. The winner is the same in every campaign level, and it lines up
corridors, rooms and doorways seamlessly. Most models use a single angle;
`ANGS = (512, 0, 0)` stands models authored lying flat upright.

The coordinate system is right-handed with +y down. Seen from above, +x is
east and **+z is north**: level 1 rendered this way matches its automap
image `MAP000.BSI` without mirroring. To convert to y-up axes (glTF,
OpenGL), map `(x, y, z) → (x, −y, −z)`. That's a 180° turn, not a mirror.

## Flats

Most flats are **markers** for the level logic, not visible sprites. Their
images are 64×64 editor icons, with BSI header field 8 = 0:

| Image | Count (all `L*.BS6`) | Probably |
|---|---|---|
| `MONSTER1`, `MONSTER2` | 851 | monster spawn points (the monster type is in `STRU`/`CTRL`) |
| `STUFF` | 243 | item placements |
| `RESTART` | 212 | respawn or checkpoint points |
| `BSTART`, `RSTART` | 208 | blue and red team starts (CTF) |
| `START` | 31 | player start |
| `USER`, `TRIGGER` | 69 | logic markers |

The rest are drawn in game: flames (`FLAM00`, `FLMW00`, animated) and the
NPCs (`SUMEER`, `XIIVILAI`, … : full-figure images about 80×230).

Sprite size and anchor (inferred from where flats sit, and from a DOSBox
screenshot of level 1's start):
- **Scale:** BSI header field 11 looks like a scale in 1/256: 256 for flames,
  87–102 for NPC figures, 56–184 across monster sprites. At 256, one pixel is
  one level unit.
- **Flames** are centred on `POSI`. At that scale, the four torch flames in
  level 1 rest exactly on their pillars (centre 91–115 units above the pillar
  top, image 197 pixels tall), as they do in the game.
- **NPC figures** stand on `POSI`: their flats sit 1–3 units above the floor.
  At their scale (about 0.4), an NPC is about 94 units tall, against doors of
  154.

## Tools

```sh
bstool level l1                 # summary: models, flats, lights, geometry, bounds
bstool level --tree l1          # dump the chunk tree
bstool level -o out/ l1 l3      # export as glTF (+ PNG textures); --markers adds flats as nodes
bsview --level l1.bs6           # fly through it
```
