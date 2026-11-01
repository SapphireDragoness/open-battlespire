# OpenBattlespire's saved games (`GAME.BSS`)

Our own saves are plain text, made to be read and changed by hand. The game
writes them from the options bar's Save; `bssave convert` makes one from an
original save ([save.md](save.md)). Any text editor changes them, and
`bssave check` says what's wrong with an edited one (by line).

**Status:**
- The reader and writer are `bs_game_save_read` and `bs_game_save_write`
  (`bs_game/game_save.h`), on the sectioned text reader `bs_core/ini.h`.
- Unit tests cover a round trip of a game with some of everything, and the
  errors.
- `test_install_data` converts the install's original saves to this format
  and reads them back unchanged.
- A fuzz harness reads arbitrary text, writes what it read, and checks that
  the result reads again.

## Where they are

Ten slots, `SAVE0` … `SAVE9`, in the saves folder:
- by default the user's data folder (SDL's preference path,
  `~/.local/share/OpenBattlespire/OpenBattlespire/saves` on Linux);
- or `--saves DIR`.

Each slot holds two files:
- `GAME.BSS`: the game, as below;
- `IMAGE.RAW`: the view when saved, 80 × 50 RGB555. This is the original's
  picture format, so the load screens show both kinds of save alike.

The original game's slots stay where they are, in its install. The load
screens show them on a second page (Page Down).

## Syntax

```
# a comment
[section]
key = value            # a comment after a value
name = "quoted, # and \" allowed"
```

- **Sections:** a section may repeat (an `[item]` for each item), and so may
  keys.
- **Blanks:** blanks around keys and values don't count.
- **Comments:** a `#` after a blank starts a comment. A value that starts
  with `"` runs to the closing quote, with `\"` and `\\` inside.
- **Numbers:** decimal; flags may be hex (`0x80`). Lists are numbers
  separated by blanks. Yes/no values take `yes`, `no`, `true`, `false`, `1`
  or `0`.
- **Errors:** an unknown section or key, or a value out of range, is an
  error with its line, so a typo doesn't go unnoticed.

## Sections

### `[save]`

| Key | Meaning |
|---|---|
| `version` | 2 (levels left keep their monsters and objects since 2). A newer version than the game knows is refused. |
| `name` | the name on the load screens |
| `level` | the level file the player is in (`l1.bs6`, or an interior such as `mtemple.bs6`) |
| `campaign` | the campaign level, when `level` is an interior |
| `other_level` | a level left through a door, kept as it was left (repeats, at most 32). `FILE monsters`: its monsters are listed, and those not are dead; just `FILE`: they're made again when the player goes back (version 1 saves) |
| `level_number` | the campaign level's number (required) |
| `clock` | the game's clock, ms: the times below are on it |
| `beamed` | `yes` once the player has come by Dagon's transport beam (the win needs it) |
| `captive_at` | when the captivity begins, if it's pending (Dagon's beam without the armour, or after his conversation) |

### `[player]`

Most keys are what they say:
- `name`, `female`, `race` (`rd br no hi dk wd`), `hair`, `eyes`, `mouth`;
- `level`, `health`, `health_max`, `spell_points`, `spell_points_max`;
- the attributes `strength` … `luck`, and the 21 skills `medical` …
  `critical_strike` (as `bssave fields` lists them), their base values;
- `class`, `magery` (spell points per Intelligence × 256), `shield`.

The rest:

| Key | Meaning |
|---|---|
| `pos` | the feet, x y z (+y down) |
| `yaw` | 2048 a turn, 0 facing +z, 512 +x |
| `pitch` | + looks down |
| `class_skills` | 12 skill numbers: primary 3, major 4, minor 5; −1 none |
| `skill_uses` | 21 numbers, 0–20000, in the skills' order: uses counted towards each skill's next rise |
| `points` | level-up points not spent, added at the next level-up |
| `ride` | the boat or balloon being ridden, by its object's BS6 id; missing if none |
| `resists`, `immune`, `low_tolerance`, `critical_weakness` | element bits: 1 magic, 2 fire, 4 cold, 8 shock, 16 poison |
| `advantages` | words: `acute_hearing regenerate_health regenerate_sp athleticism adrenaline_rush double_healing absorbs_spells no_spell_point_potions` |
| `forbidden_weapons` | 1 short blades, 2 long, 4 blunt, 8 axes, 16 missile |
| `armour`, `material` | the heaviest armour (0–3) and best material (0 Iron … 9 Daedric) allowed |
| `confused_until`, `water_breathing_until` | clock times |
| `anchor` | the Teleport spell's anchor, x y z; missing if there's none |
| `hotkeys` | F1–F8: a spell's number, 0 none |
| `script_progress`, `script_done` | the level's keys placed (cogs, Voidguides …), and whether the way on is open |

### `[spell]`, `[effect]`

`[spell]` repeats for each spell known:
- `number` (1–31) names the spell, which otherwise comes from the game's
  spell table;
- `icon`, `element`, `delivery` and `detonation` are the spell book's
  choices.

`[effect]` repeats for each timed spell on the player:
- `spell`, `icon`;
- `flags` (OR'ed into the player's while it lasts);
- `until`, `tick_damage`, `tick_ms`, `tick_last`;
- `attributes` (8 numbers added) and `pools` (resist, absorb, reflect).

### `[item]`

One for each item in the game, wherever it is.

| Key | Meaning |
|---|---|
| `id` | the item's id, for `where = item ID`. A placed item that hasn't moved keeps its object's BS6 id, so the object shows it. Ids are a level's own, so two levels may share one: `item ID` looks in the item's own level first. |
| `base` | the base item, by number (a comment names it) or by name |
| `material`, `magic`, `number`, `quantity`, `condition`, `condition_max` | as the item: a potion's type, a scroll's or book's text, a Sigil Amulet's id are the `number`; `quantity` is an arrow count |
| `picture` | what it shows worn (`MHLMT503`; the first letter is the sex's) |
| `where` | `carried`, `floor` (at `pos`), `item ID` (in a container item), `object ID` (in a level object, a chest, by BS6 id), `monster ID` (held by a monster: its loot sack), `nowhere` |
| `pos` | where it lies |
| `worn`, `secondary` | worn or wielded; the secondary weapon |
| `level` | one of `[save]`'s `other_level`: the item is kept in that level |

### `[monster]`

One for each monster alive, in the level the player is in and in each
`other_level` marked `monsters`. Those not listed are dead.

| Key | Meaning |
|---|---|
| `id` | a spawned monster's is its spawn point's flat's BS6 id, which gives it its dialogue code and spells; one made in play has 100000 + n |
| `creature` | 0–15 (a comment names it) |
| `pos`, `angle` | where it stands, its facing |
| `health`, `health_max`, `spell_points`, `spell_points_max`, `skill`, `attributes` (8) | |
| `flags` | the creature flags (+0x172) |
| `state` | the AI state: 0 attack, 9 idle, 13 waits to talk … |
| `greeted`, `summoned`, `ally` | |
| `level` | one of `[save]`'s `other_level`: the monster is in that level (none: the player's) |

### `[object]`, `[controller]`

`[object]` covers a level object, flat or light that has moved or whose
controllers run, every object that can be locked or is a used gem, the
boats and balloon that have moved, and the speakers that have left.

| Key | Meaning |
|---|---|
| `id` | its BS6 id |
| `kind` | `object` (the default), `flat` or `light` |
| `pos`, `angles` | where it is now |
| `locked` | yes or no (a key opens it) |
| `gem_until` | when a used gem comes back |
| `active` | its controllers run |
| `gone` | a speaker (an NPC flat, or an object that talks) that has left, by TELEPORT or KILL: it isn't shown and doesn't talk |
| `level` | as `[monster]`'s |

`[controller]` covers each controller not as it began:
- `object` and `kind` name its object, and `index` its place in the
  object's chain;
- `reversed` makes the next run go backwards;
- `running`, `start_ms` and `start` (the position or angles when it
  started) describe one that is running;
- `level` as `[monster]`'s.

### `[sigils]`, `[automap]`, `[dialogue]`, `[variables]`, `[locals]`

- `[sigils]` `gone`: the BS6 ids of the sigils dispelled. One section for
  each level with any; `level` names another level, as `[monster]`'s.
- `[automap]`: one for each automap with cells seen.
  - `map` is its number in the game's automap table, or −1 for "the level's"
    (from an original save).
  - Then 30 `row`s of 40 cells, `x` seen and `.` not.
- `[dialogue]`:
  - `visits`: 16 numbers, the conversations had with each creature type;
  - `leaving`: a speaker's id and when it goes (repeats).
- `[variables]`: the dialogue's global variables, `Name = value`. A name
  that isn't known is written `@hash`. Any name may be added: the game
  hashes it as the dialogue tables do.
- `[locals]`: one for each speaker with variables of its own: `id`, then up
  to 8 `Name = value`.

## What it doesn't keep

- Spells and arrows in flight, the weapon's swing, monsters' paths.
- Messages on the screen and the conversation open, if any.
- Speakers about to leave (`leaving`) in levels left through a door: they
  stay.
- Items on hot keys, as with original saves.
