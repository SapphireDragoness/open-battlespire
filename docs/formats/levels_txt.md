# LEVELS.TXT

The level table: 7 campaign levels, 7 deathmatch and 3 capture-the-flag
entries. Plain text, one level per line, with fields separated by tabs and
spaces. The display name takes the rest of the line.

Status: **layout verified** (17 entries parse), and the game's reader has been
reverse engineered (below). Every field's meaning is known.

```
l1.bs6  Lv1  1336  NNF  xxxx  555  1024  nodes.l1  1500  4096  80  The Weir Gate
```

| # | Field | Values | Record offset | Meaning |
|---|---|---|---|---|
| 1 | file | `l1.bs6` … | +0x00 string | level file |
| 2 | id | `Lv1`–`Lv7`, `Dm1`–`Dm7`, `Cf1`–`Cf2` | +0x0D string | level id. `Cf1` appears twice (foggy and night variants of `dmkk.bs6`). Never read by the game |
| 3 | view distance (unk_a) | 1024–3500 | +0x40 int | **the view distance, where fog reaches full strength**. Copied to the float global `0xF4E98` (38 uses); `set_fog` (`0x6B778`) fades from the fog start to this distance. Multiplayer games override it with 1500 |
| 4 | mode | 3 letters | +0x11..0x13 | [0] **game type**: `N` campaign, `D` deathmatch, `C` capture the flag (checked in 28 places). [1] **water**: `Y` loads the water texture `water2.bsi`, `N` turns water off. [2] **fog shape**: `F` starts the fog 512 units before the view distance (a fog wall at the edge of view), `C` starts it at 8 units (haze over the whole range) |
| 5 | sky | `sky1.bsi`, … or `xxxx` | +0x14 string | sky image; a value starting with `x` means no sky |
| 6 | haze | `444`, `555`, `666` | +0x21 u16 | **fog table**: loads `haze.%03d`. Also picks the **fog colour** (`0xF22EC`, RGB555): `444` gives grey (120,120,120), anything else near-black (8,8,8) |
| 7 | ambient (unk_b) | 1024–31000 | +0x23 int | **ambient light in 1/512 shade levels.** Copied to `0x5E7DE0`, clamped by the renderer to 0..16128 (31.5 levels: fully lit), then added to each polygon's `face_byte << 8` (`0x8D013`). Level 1 (1024) gets 2 levels everywhere. The clear outdoor levels (31000) are fully lit without light sources. Measured against the original (see `docs/re/README.md`) |
| 8 | nodes | `nodes.l1` … or `xxxx` | +0x27 string | AI navigation graph ([nodes.md](nodes.md)); `x…` means none. `l2.bs6` uses `nodes.l2` but is level Lv3: files are numbered by build order, not play order |
| 9 | active distance (unk_c) | 1000–2500 | +0x34 int | **how near the player monsters act.** Copied to global `0x16C1A8`; every frame the monsters within this distance of the player think and move (`0x450F8`), the rest wait. See [creatures.md](creatures.md) |
| 10 | grid cell size (unk_d) | 2048–6000 | +0x38 int | **the size of the level's spatial grid cells.** Copied to `0x16C198`. `place_in_grid` (`0x25B21`) files every object (lights included) into a 64 × 64 grid at `0x114764`: cell = position / this + 32, for x and z. Objects outside the grid are counted and not placed. The big outdoor levels use the largest cells |
| 11 | monsters (unk_e) | 55–100 | +0x3C int | **monster density: the percentage of monster spawn points that get a monster** (the spawner at `0x462CB`; the developer statistics report prints it as `"MONSTER COUNTS (%d%%)"`). Copied to `0x16C204`, forced to 100 in multiplayer. A global override at `0xDA40E` decides: 0 = no monsters, 1 = this value, 2+ = that percentage instead |
| 12 | name | rest of line | +0x44 string | display name. Never read through the current-level pointer; probably used by the level-select menu |

## How the game reads it

Found by static analysis of `GAME.EXE` (v1.5) on 2026-10-12.

- **Loader:** `0x69311`. It opens the file through the general data-file
  opener (`0x13D5C`: archive first, then a loose file) and reads tokens with
  `0x694DF`.
- **Tokens are separated by tab, CR or LF only, not spaces.** That's why the
  name, which contains spaces, is a single token. Every shipped line has
  exactly 12 tab-separated fields.
- **Records** are 0xC8 (200) bytes, in an array at `0x168C5C`. The number
  fields go through the C library's `atoi`. The record's 1-based index is
  stored at +0xC4. The count goes to `0x16C2B4`: the loop increments it once
  more after the last line fails to read, so it's one more than the number of
  levels.
- **Current level:** `0x16C278` points at `levels[level_number − 1]`, where
  `level_number` is the global at `0xDA43A`. Level number 8 also uses record
  index 6, which matches the duplicate `l8 l8` entry in the hard-coded file
  table at `0xED554`.
- **Level set-up** (`0x26294`) reads water, sky, haze, `unk_b`, nodes and
  `unk_c`–`unk_e`. The fog set-up (`0x11150`–`0x112C7`) reads `unk_a`,
  `unk_b`, mode[2] and haze.

Campaign play order differs from file numbering: Lv2 = `l3.bs6`,
Lv3 = `l2.bs6`, Lv7 = `l8.bs6`.

**Level numbers** (`0xDA43A`) are 1–6 and 8 for the campaign: the level
advance skips 7. Every multiplayer level is 4. The level number picks the
music (CD track = number + 1; see the [RE notes](../re/README.md#music)).
`bs_level_number()` derives it from an entry as the game's level picker
does: the file name's digit, with 2 and 3 swapped and 7 raised to 8.

Still open: the users of `0xF22EC` (the fog colour).
