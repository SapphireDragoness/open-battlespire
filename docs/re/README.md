# Reverse engineering GAME.EXE

Ground rules (from `docs/PLAN.md` §1): notes in this folder describe
**behaviour**, in prose or pseudocode. Never paste disassembly or decompiler
output. Every finding should say how it was confirmed: static analysis,
DOSBox debugger, or comparison with data.

## Getting GAME.EXE into Ghidra

`GAME.EXE` is a 32-bit DOS "LE" executable (Watcom C/C++, CauseWay DOS
extender) that Ghidra can't load directly. Convert it to ELF:

```sh
bstool le2elf -o build/re/GAME.elf        # uses GAME.EXE from BS_DATA_DIR
```

This loads every object at its preferred base address and applies all 32,001
fixups (see [le_format.md](le_format.md)). The output stays under `build/`,
which is gitignored. It's derived from the game binary, so never commit it.

**Ghidra import:**
1. *File → Import File* → `GAME.elf`. Format: ELF; language: `x86:LE:32:default`.
2. Accept the default analysis options. The entry point is the symbol
   `le_entry` (`0x701b0`).
3. The memory blocks are `obj1_text` (code, `0x10000`), `obj2_data`
   (`0xC0000`), `obj3_data` (`0xD0000`) and `obj3_bss` (BSS and stack, up to
   `le_stack_top` = `0x7FF990`).

**Calling convention.** The program uses Watcom's register convention, which
Ghidra 12.1 doesn't ship (no `__watcall` in the list):
- the first four integer arguments go in `EAX`, `EDX`, `EBX`, `ECX`, the rest on
  the stack;
- the callee pops its stack arguments (317 functions end in `ret N`), while
  variadic functions are cleaned up by the caller;
- functions save and restore `EBX`, `ECX` and `EDX` as well as `ESI`, `EDI`
  and `EBP`, so only `EAX` is clobbered.

`ghidra/` has the pieces to add it, all tested with the headless analyzer on
`GAME.elf`:

1. **Add the convention** to your program: *Edit → Options for 'GAME.elf' →
   Specification Extensions → Import* → `ghidra/watcall.xml`. Or run the script
   `ghidra/ImportWatcallSpec.java`; add `docs/re/ghidra` under *Window →
   Script Manager → Manage Script Directories* first.
2. **Apply it** to every function: run `ghidra/ApplyWatcall.java`. It sets
   `__watcall` on all functions still using the default convention (2,179 of
   2,188).
3. **Recover parameters:** *Analysis → One Shot → Decompiler Parameter ID*.
   Afterwards 1,445 functions have parameters, and calls show their register
   arguments (`FUN_00013d5c(s_levels_txt_000d7512, …)` in the `LEVELS.TXT`
   loader).

Caveat: `watcall.xml` doesn't mark `EBX`, `ECX` and `EDX` as preserved, even
though the code does preserve them. Marking them confuses Ghidra's parameter
recovery (they turn into `unaff_EDX`-style pseudo-parameters). The cost is
that a function which only passes a register through to a callee may show
too many parameters; trim its signature by hand. Variadic functions
(`printf`-style) should get `__cdecl`.

## Landmarks found so far

| Address | What | How found |
|---|---|---|
| `0x701b0` | C runtime start-up. It jumps over an embedded `WATCOM C/C++32 Run-Time system` copyright string, then sets up the stack and queries DOS and the extender | le2elf entry point; disassembly |
| `0xDA31C` | table of pointers to the C math library's error messages | fixup-resolved pointers in data |
| `0xED554` | hard-coded table of 12 pointers to level file names, indexed by level number − 1: `l1 l3 l2 l4 l5 l6 l8 l8 l9 l10 l11 l12`. Level numbers 7 and 8 both load `l8`; the campaign uses 8 (see [Audio](#audio-found-2026-10-13)). `l12` is unused | fixup-resolved pointers in data (strings at `0xD57BC`) |

## Leads (unconfirmed)

Found by searching `GAME.elf` for file-name strings and for the code that
references their addresses. Each address is an instruction **inside** the
function of interest; none of these functions has been analysed yet.

| Code address | References | Likely function |
|---|---|---|
| `0x1f203`–`0x1f292` | `bsi.bsa`, `3d.bsa`, `txt.bsa`, `flc.bsa`, `bs6.bsa`, `waves.bsa` | archive mounting |
| `0x146d3`, `0x146ee` | `.bs6`, `.3d` | loader dispatch by file extension |
| `0x23160`, `0x238de` | `%s.bs6` | level loader |
| `0x69327` | `levels.txt` | `LEVELS.TXT` parser |
| `0x15680`–`0x15a95`, `0x163fe` | chunk tags `HICL`, `CMAP`, `BHDR` as 32-bit immediates | BSI loader |
| `0x742a0`–`0x742b2` | tags `HTBL`, `BHDR` | second BSI reader, probably where `HTBL` is used |
| `0x14d5e` | `FONT%04d.FNT` | font loader |
| `0x3c050` | `text.rsc` | `TEXT.RSC` loader |
| `0x10612`, `0x10668` | `pal.raw`, `tint.555` | colour-table set-up |
| `0x10e95`, `0x10fcf`, `0x10fde` | `spire.h2l`, `water.001`, `tint.002` | colour-table set-up |
| `0x1101d`, `0x11104`, `0x111ae`, `0x11275` | `haze.%03d`, `haze.666`, `haze.444` | haze (fog) tables |
| `0x3637d` | `fade.dat` | |
| `0xefd6e`… | item model names (`dagger.3d`, `gold.3d`, …) | item → model table |
| `0xe34ad`… | weapon model names (`edagger.3d`, …) | equipped-weapon model table |

`layer.dat`, `water.tbl` and `lowcolor.col` don't appear as strings. They
may be built from a format string, or be unused.

## Open questions from M3

The viewer (`bsview`) approximates these; the real rules need the code that
draws levels (start from the level loader at `0x23160`):

- **Lighting.** How `LITD` lights (`RADI`, `BRIT` = 11..1023) light a face:
  falloff curve, brightness scale, per vertex or per face, and how the
  result indexes `HTBL`. What `AMBI` on viewpoints means (0..60000).
- **Fog.** How `HAZE.*` is indexed, and whether the `LEVELS.TXT` field
  `unk_b` (1024..31000) is the fog distance.
- **Sky.** How the sky image (`LEVELS.TXT` field 5) is mapped to the screen.
- **Sprites.** Confirm that BSI header field 11 is the world scale in 1/256,
  and how the game decides a sprite's anchor (flames centred, NPCs standing
  on their position; see `docs/formats/bs6.md`). Whether field 8 (71 for most
  images, 57 for `FLAM00`) is the animation frame time in ms.
- **Menu colours.** Why the character-creation screens show fewer colours
  than their images' `HICL` tables (in-game images match exactly).
- **Field of view and eye height.** The 3D view is 640×380 above the HUD.
- **Player start.** Which way the player faces at the `START` flat.
- **Model faces.** The face header byte at offset 1 (0, 6, 15, … up to 63).
  It might be a flag set or a shading value.
- **Texture names.** How the game resolves face texture names that don't
  match a file (`STRUT0` → `STRUT01.BSI`, `HAND00` → `HAND.BSI`).

## Projection (found 2026-10-12)

- `set_viewport(cx, cy, half_w, half_h)` at `0x6EC18` stores the screen
  centre in `0xF4E9C`/`0xF4EA0` and the half-size in `0xF4EA4`/`0xF4EA8`, as
  floats. The full-screen view passes (320, 240, 320, 240); a 128×128 item
  view passes 64s.
- `set_projection(focal_x, focal_y)` at `0x6EB78` stores the focal lengths in
  pixels (`0xF4EE8`, `0xF4EF0`; the default set-up at `0x6E900` uses 200, 180).
  `0x6EC74` derives `tan(half-FOV) = half-size / focal` (`0xF4EB8`, `0xF4EBC`)
  and the frustum side planes.
- The game view (`0x10F2D`–`0x10F79`) uses the screen size from `0xF22E4`
  (width) and `0xF22E8` (height): viewport centred, focal_x = width × 0.75,
  focal_y = height. At 640×480 that's a focal length of 480 on both axes, a
  **67.4° × 53.1° field of view**. The menu-screen set-up at `0x35CB6` uses
  the same numbers.
- The focal length doesn't explain the fog being 3× too dense when its
  distances are taken as level units: projection divides by Z, but the fog
  compares Z itself. The next place to look is how the per-vertex `1/z` that
  the fog routine reads (`[poly+0x64]`) is computed.

## Runtime addresses (DOSBox debugger)

Measured with DOSBox-X 2026.10.01 on the v1.5 `GAME.EXE` (2026-10-13). The
extender loads the program with flat segments (base 0, 4 GB limit), so
runtime addresses are linear:

- **code: runtime = ELF + `0x425000`** (found by locating a code dump in a
  full memory dump);
- **data: runtime = ELF + `0x415000`** (from relocated addresses in the code,
  and confirmed by following the level-info pointer to `l1.bs6`).

DOSBox-X set-up: `~/Games/DOS/battlespire-x.conf` (`core=normal`, which memory
breakpoints need). Start it from a terminal, because the debugger's interface
appears there; break in with Alt+Pause. Useful commands: `MEMDUMPBIN DS:0
4000000 full.bin` dumps all memory, `BPLM addr` breaks when a value changes
(there are no read breakpoints), and `BP sel:off` sets a code breakpoint.

## Lighting: what a memory dump showed (2026-10-13)

From a dump taken in level 1 at the start position (camera (−11, −72, −507),
right above the `START` marker), checked against a screenshot taken at the
same moment:

- **Renderer state:**
  - view distance 1336 and fog start 824, as decoded;
  - screen 640×480 in 16-bit colour; viewport centre (320, 240); focal
    length 480, 480;
  - the camera matrix's x and y rows include the projection scale (1.5 and
    2.0), but the depth row is unscaled, so depth is in level units.
- **Light objects** are type 7, with the fields: position (floats) at
  +0x07, `BRIT` at +0x15, id at +0x1D, `RADI` at +0x21, and the next sibling at
  +0x2D. Level 1's first light is at runtime `0x1C2E09C` in that dump.
- **The prepared light list** is an array of 49-byte (`0x31`) records, 22 of
  them in that frame. Each record holds:

  | Offset | Content |
  |---|---|
  | +0x00 | world position |
  | +0x0C | camera-space position |
  | +0x18 | intensity = 512 × `BRIT` (2 × `BRIT` levels in 8.8 fixed point) |
  | +0x1C | effective radius R |
  | +0x20 | R² |
  | +0x24 | intensity / R² |

  This implies a quadratic falloff: `intensity · (1 − d²/R²)`.
- **R is 0–6 % smaller than `RADI`**, irregularly from light to light, with no
  relation to camera distance. Probably flicker, a random per-frame shrink.
- **The rule.** Measured with three matched screenshot/dump pairs in
  level 1, about 7,000 surface blocks in all:

  ```
  level = min(31, face_byte / 2 + ambient / 512
                  + Σ over lights in front of the face  BRIT · max(0, 1 − d²/R²))
  ```

  - **`ambient`** is `LEVELS.TXT` field 7, clamped to 16128 (31.5 levels).
    It's 1024 (2 levels) in level 1, and 31000 (fully lit) in the clear
    outdoor levels. In code: the rasteriser adds it to `face_byte << 8`
    (`0x8D013`), in units where one level is 512.
  - **`face_byte`** is the model face's header byte, counted in half levels.
    Measured: the wall (45) gets about 21.6 levels, the battlemage (6) about
    2.8, and the waterfall (60) reaches full brightness.
  - **Lights** count only on the front side of a face, per pixel, with no
    cosine falloff. The sum includes the player's light.
  - **Fit:** byte weight 0.47; ambient = `unk_b` / 512 best explains the
    constant. Default `bsview` renders match the originals to within 1.5–3
    levels on average (correlation 0.77–0.94).
  - **How it was measured:** brightness is the original ÷ unlit render,
    converted to a level through the measured `HTBL` brightness curve
    (`HTBL` isn't quite linear: level 16 is 47 %, level 30 is 87 %).

- **The game renders its 3D view at 320×240** and doubles each pixel to fill
  640×480; the HUD is drawn at full resolution. This holds in every capture,
  from both DOSBox 0.74 and DOSBox-X. The viewport globals sometimes read
  (320, 240) with focal length 480, and sometimes (160, 120) with focal length
  240, depending on which projection set-up ran last.
- **The player's light:** brightness 40, radius 384, 12 units above the eye.
  It's in both dumps as the first prepared record.
- **The level becomes a colour through the image's `HTBL`:**
  `HTBL[level × 128 + pixel / 2]` (see `docs/formats/bsi.md`).
- **Fog in level 1 matches the decoded distances** (start 824, end 1336):
  scaling them by 2 or 3 fits the distant blocks worse. Level 5's screenshot
  needed about 3× instead, so the `C` (gradual haze) branch may be misread.
- **Still open:**
  - what selects the 23 level lights in the prepared list;
  - the flicker rule;
  - the meaning of the field before each record (1.0);
  - how sprites are lit (`queue_sprite`'s shade argument);
  - the fog rule for `C` levels.

## Audio (found 2026-10-13)

Static analysis. Two results have been confirmed in the running game
(2026-10-13): the final level is silent (seen in a longplay), and level 1
has no audible ambient sounds (DOSBox). The game
links **Miles Sound System 3.6C** statically. MSS's debug-log format strings
(`AIL_redbook_play(…)`, `AIL_set_sample_pan(…)`, …) are each referenced by
exactly one API wrapper, which names the wrappers:

| Wrapper | MSS function |
|---|---|
| `0x7C4D0` | `AIL_redbook_open` |
| `0x7BBB0` | `AIL_redbook_play(handle, start, end)` |
| `0x7BFD0` | `AIL_redbook_track_info(handle, track, &start, &end)` |
| `0x7C1C0` | `AIL_redbook_status` (1 playing, 3 stopped) |
| `0x7BDD0` | `AIL_redbook_position` |
| `0x7BAB0` | `AIL_redbook_stop` |
| `0x7B6A0` | `AIL_redbook_set_volume` |
| `0x81220`, `0x81100` | `AIL_init_sample`, `AIL_set_sample_file` |
| `0x80B20`, `0x80A60`, `0x809A0` | `AIL_set_sample_volume`, `_pan`, `_loop_count` |
| `0x80EB0`, `0x807D0`, `0x80CA0` | `AIL_start_sample`, `AIL_sample_status` (2 = done), `AIL_end_sample` |

### Music

Music is CD audio played through MSS's Redbook functions. The GOG release
emulates the disc's audio tracks with `MUSIC/Track02–08.ogg`.

- **`play_track(n)`** (`0x1BEFB`) remembers `n`, asks MSS for the track's
  start and end, and plays that range. MSS only knows tracks 1 to the disc's
  track count; for any other track it returns start = end = 0, and playing
  0..0 stops the CD and starts nothing.
- **Which track:** the level number (global `0xDA43A`) + 1. Level set-up
  (`0x26718`) starts it. While a level runs, the main loop (`0x13317`)
  restarts it whenever the remembered track differs.
- **Level numbers:** the campaign starts at 1 and the level advance
  (`0x26997`) adds 1, then skips 7: campaign levels are 1–6 and 8. The file
  table at `0xED554` maps both 7 and 8 to `l8.bs6`. The level picker
  (`0x344D5`) derives the same numbers from the file name (digit, with 2 and 3
  swapped, 7 raised to 8). It gives every deathmatch and capture-the-flag
  level number 4.
- So Lv1–Lv6 play tracks 2–7, multiplayer levels play track 5, and **the final
  level asks for track 9, which the disc doesn't have: no music.** Track 8 is
  only reachable as level number 7, which normal play never uses.
  **Confirmed:** a longplay of the final level has no music.
- **The title menu** (`0x31B50`, which logs "Playing sound track"; background
  `MULT01.BSI` with Select Scenario / Single Player / Multi Player) plays
  track 6.
- **Per-frame update** (`0x1BF8B`): if the CD has stopped, play the remembered
  track again; while playing, every 20 frames, if the position has passed the
  track's end, replay it. Music therefore loops.
- **Volume** (`0x1BE6E`): sets the Redbook volume; 10 or less stops the music
  altogether.

### Sound effects

- `SPIRE.SND` is opened once at start-up (`0x1045B`); records are looked up by
  their numeric id.
- **Cache** (`0x1C8BE`): 256 entries of 16 bytes at `0x10EBC4` (last-used frame,
  id, size, data). Once the total passes 384 KB, the least recently used
  entries are dropped until it is under 256 KB.
- **Play functions** take a sound id and an object (or none):
  - `0x1CBD4`: range 127; skipped up front beyond 768 units.
  - `0x1CC68`: range 1280; skipped beyond 8192.
  - `0x1CCFC`: range 127, with a loop count; the distance check applies only
    to sounds that play once.

  They pass the object's position (object + 7, three floats), or no position
  for the player's own sounds. The up-front distances are from the object to
  the player, whatever the hearing.
- **Voices** (`0x1C2C5`): six MSS samples; a voice is free when its sample is
  done. With six active, the sound is dropped.
- **Placement** (`0x1C33F`), when the sound starts:
  - max distance = range × 768 / 127: 768 for range 127, 7740 for 1280. With
    **Acute Hearing** (the class advantage, player + `0x194`) it is
    range × 1024 / 127: 1024 and 10320.
  - distance = the approximation below; a sound that plays once is not started
    at or beyond the max distance.
  - volume (0–127) = 127 within 25 units, else 127 − distance × 127 / max,
    clamped to 0.
  - pan (MSS: 0 left, 64 centre, 127 right): take the angle between the
    player's heading and the direction to the source, fold it so behind
    mirrors in front (0–90°), and pan = 64 ± 63 × angle / 90°. The sign is
    the side the source is on. A sound without a position is centred at full
    volume.
  - The voice (20 bytes at `0x3B46F0`) keeps the range (+4), the loop count
    (+0xC, 1 for once) and the pointer to the object's position (+0x10).
- **Following** (`0x1C54E`, corrected 2026-10-19; these notes first said the
  placement was never updated): the main loop calls it on every frame
  (`0x1330D`) with the player's position and heading. For each voice playing
  with a position it works the max distance out again, Acute Hearing
  included, and measures to the object's position as it is now:
  - within 25 units, volume 127 and pan 64;
  - beyond the max distance, a sound playing once is stopped (`0x1C28F`);
  - otherwise volume and pan by the rules above, volume 0 at the max
    distance or beyond.

  So sounds follow their sources and the player's turning, and Acute Hearing
  makes the near sounds started (within 768) louder and lets them carry to
  1024 as their sources move.
- **Distance approximation** (`0x1B900`, `0x1B91B`, used by `0x46022`):
  coordinates truncated to integers; in x/z, `long + short − short/2`; then
  with height, `long + short/4`.
- **Angles** (`0x1E711`): 2048 per turn, 0 = +z, increasing towards +x:
  `acos(dz / r) × 1024 / 3.14` (the game's own π), mirrored to `2047 − a` when
  dx ≤ 0. `0x1D14A` gives the difference between two such angles (0–1024)
  and which side it's on.
- `0x1CE6A` plays a sound from a named file outside `SPIRE.SND`, probably a
  voice clip from `WAVES.BSA`. Not analysed yet.

`bs_audio`'s `bs/audio/place.h` implements the placement rule.

### Ambient sounds

`0x1CDA5` runs once per main-loop iteration (from `0x132F5`):

1. Continue only if `rand()` ≤ 200 (out of 0–32767: about 0.6 % per frame).
2. Collect the AI navigation nodes ([nodes.md](../formats/nodes.md)) 64–600
   units from the player (`0x1A554`, into a buffer at `[0xFDC00]`); stop if
   there are none.
3. **Bug:** it uses the *number* of nodes found as an index into the whole
   node list, not into the collected ones. The sound comes from a fixed,
   low-numbered node, which is usually beyond the 768-unit limit and so never
   plays. The intent was presumably a random node in range. **Confirmed:** no
   ambient sounds are heard in level 1 in DOSBox.
4. Pick one of 32 sound ids (table at `0xED334`: 1–5, 28–31, 48, 50, 100,
   217, 329–344, 346–348) with `random(0, 31)`, and play it at that node's
   position with range 127. Id 1 isn't in `SPIRE.SND`.

`rand` (`0x71214`) is the Watcom C library's generator: state × 1103515245 +
12345, returning bits 16–30. `random(lo, hi)` (`0x1D8A3`) is
`lo + rand() % (hi − lo + 1)`.

**Open:** the frame rate of the main loop (this roll happens per frame; `bsview`
assumes 20 fps); which code plays which `SPIRE.SND` id.

## Videos (found 2026-10-13)

Static analysis. Two functions play a Smacker file from the CD's `VIDEOS`
folder (path `%svideos\%s`):

- `0x1EB53` (name): full screen; a key press skips it (confirmed in the game).
- `0x1ED0A` (name, text id): the same, then shows that `TEXT.RSC` string.

| Video | When it plays | Where |
|---|---|---|
| `STORY.SMK` | At start-up, before the title menu (`0x31B50` → `0x328FB`, which logs "Playing smacker file"). Skipped when the game was started for Mplayer (`0xF2197` = 2) or when `rep.dat` exists (see below) | `0x32913` |
| `JUMP.SMK` | Completing a level (`0x268A1`), before the level number advances | `0x26967` |
| `CHOP2.SMK` + text 1058 | The "captivity" game over: when the player is caught, as at death, 2 s later CHOP2 plays with text 1058 ("…you grow old and die in captivity…"). The capture time (player +`0x2DA`; when it passes, `0x13520` sets the time of death `0x16C258`) is set 2 s ahead by Dagon's transport beam without the armour (message 5, "You are not wearing the armor of protection!", `0x4AC78`), and 3 s ahead whenever a conversation whose file starts `ZZ` (Dagon's) ends (`0x581ED`) | `0x13637` |
| `CHOP2.SMK` + text n | The dialogue-script command `ENDFRAME n`: the conversation ends and CHOP2 plays at once | `0x5A395` |
| `ANCHORS.SMK` | Level 1's script (`0x3E578`) counts the `1doorpan` objects in the whole object tree (`0x3EBB9` from the root `0x16BFCC`) that are open (`0x4C5C0`: a move or rotation on it, or on an object it links to, has its "reversed" bit). It runs each frame the player stands on an object and on each use of one. With none open, this joke ending plays and the game is over. One generator runs from the start (`STRU` 120), so it takes turning them all off. With 5 or more open, standing on `1zoom` completes the level | `0x3E653` |
| `MALE.SMK` / `FEMALE.SMK` | Winning (`0x499DB`, in the player's blow `0x4975B`): a blow that reaches an NPC flat (type 0x21, within 90 units, in front, in sight) whose dialogue code is `ZZ1` (Dagon), while the player has come by his beam (+`0x241`), the capture is pending (+`0x2DA`, so within 3 s of his conversation) and the weapon in hand (+`0x260`) is base 3 with enchantment 8011, the **Moon Reiver** (a broadsword dropped by a Dark Seducer at (3407, −514, 811) on level 7). It sets the win flag (`0x49A07`) and leaves the game loop; the video plays as the game exits (`0x130B1`): `FEMALE` if player + `0x226` bit 3 is set, else `MALE`. The game then quits to DOS | `0x130E2` |

**Restarting after a game over.** On exit after a game over (`0x16BF60`, set
after CHOP2, ANCHORS and in two other places) in single player, the game
writes `rep.dat` containing `repeat`. `SPIRE.BAT` reruns `GAME.EXE` while
`rep.dat` exists. The new run deletes it (`0x128B4`) and goes straight to the
title menu without `STORY.SMK`.

**Playing.** Both players skip on a key or a mouse button, but only after the
first 10 frames (`0x1ECD1`, `0x1EE8C`). `0x1ED0A` draws its text over every
frame: the `TEXT.RSC` record wrapped to 600 pixels (`0x3C08A`, as long
messages are), each line centred, the last ending at y = 470 (`0x1EF10`), in
whichever font was chosen last. After a game over's video (CHOP2, ANCHORS) the
last frame stays until a key (`0x13641`, `0x3E65D`, `0x5A39F`); a key that
skipped the video probably ends that wait at once, since it isn't consumed.

**The win, in the game's terms.** Dagon's Hunting Lodge is reached through a
door on the final level. Walking into the transport beam carries the player to
Dagon (a teleport link) and marks the beam as used; without the full Savior's
Hide (the dialogue's `Armor`: items 5010–5015 carried, refreshed every 60
frames) the capture follows 2 s later. In Dagon's conversation (`ZZ1T`),
lacking the Moon Reiver or swearing to serve him ends the game at once
(`ENDFRAME_1057`); with the Reiver the player calls Jaciel Morgen
(`DialogLink_13`: the `jaciel` flat with `STRU` 104 = 13). Any ending that
isn't `ENDFRAME` leaves 3 s before the capture, and only a blow at Dagon with
the Moon Reiver in that time wins.

**Bug in the data:** Faydra Shardai's `FS1A41` ends with `ENDFRAME_0036`, a
typo for 1036. There is no text 36, so the original quits with "Bad DAGTEXT
number requested : 36." (`0x3C11F`). OpenBattlespire plays CHOP2 without text.

**Open:** the font of the game over texts. Item identities (8011 Moon Reiver,
5011 Savior's Hide) and the key skip come from the user's knowledge of the
game.

## Level logic: triggers and movements (found 2026-10-13)

Static analysis of how the BS6 `CTRL` and `LINK` chunks run
([bs6.md](../formats/bs6.md#survey-of-the-logic-chunks-2026-10-13) has the
data survey). Checked in the game (2026-10-13): using a closed door opens it
and using it again closes it (`r`); the wizard portals teleport; lifts carry
the player.

**Loading** (object reader `0x23F5B`; `CTRL` reader `0x25DF3`, `LINK` reader
`0x25C43`, `STRU` reader `0x2475B`). Each placed model becomes a type-6 game
object (`0x6F` bytes, entity id at +`0x1D`). Its `CTRL` entries become a chain
of controller records in file order (table `0x113764`, indexed by the
object's controller slot): a 64-byte header (next, active-list link, object,
type, start time, start position or angles, progress, flags at +`0x3C`, sound
at +`0x38`) followed by the `RAWD` bytes. Its `LINK` entries become a list of
target objects (table `0x112764`).

**Events.** An object is activated with an event number (`0x4ABA1`):

| Event | Sent when | Where |
|---|---|---|
| 16 | the player uses the object (and from scripts, dialogue and the level exits) | `0x3CED0` and others |
| 5 | the object the player stands on changes to this one | `0x40716` (collision sets it with flag bit 0; landing resets the fall) |
| 0 | the object the player touches from the side changes to this one | `0x4074F` (flag bit 1) |

The trigger controller types `CTRL` 16, 5 and 0 are these event numbers.

**Activation** (`0x4ABA1`, then the dispatcher `0x4AD6E`):
1. Nothing happens if the object has no controllers and no links, or if any
   of its controllers is still running.
2. If the object's chain contains a controller whose type equals the event,
   the **whole chain** starts.
3. If it started and the chain has no timer (type 149), every linked object
   is force-started (`0x4AD00`): its chain starts whatever its trigger types
   are. Links are not followed further from there, except through timers.
4. In multiplayer the event is sent to the other players.

**Starting a chain** (`0x4AF7C`). The chain joins the active list (`0x3F8694`)
and each controller notes the start time (the millisecond clock at
`0x421FBC`). If the object is a teleport target (flag `0x20` in byte +`0x13`;
the `user` flats that type-2 links point at), the controllers act on the
**player** instead: the player is put at the object's position, and its move
then carries the player on. In multiplayer this is refused when another
player stands within 100 units. A controller with a sound (from `STRU` 121,
below) plays it as it starts, through the near-sound function `0x1CBD4`.

**Every frame** (`0x4B0FD`, from the main loop at `0x12D03`) each active chain
is stepped; it leaves the list when all its controllers are done. Time spent
outside the level (menus) is added to the start times, so it doesn't count.

| Type | Data (i32) | Behaviour |
|---|---|---|
| 0, 5, 16 (triggers) | `[0, time, x, y, z, 0]` | done at once. If x, y, z aren't all 0 they become an **absolute target** for the moves stepped after it in the same frame. The time field isn't used here |
| 149 (`0x95`, timer) | `[0, period ms, …]` | done when the period has passed; then force-starts the linked objects. Timers linking to each other in a ring keep machinery going (the level 6 logs and gears) |
| 261 (`0x105`, move) | `[dx, dy, dz, time ms, options (text), …]` | moves the object from where it was when the chain started by (dx, dy, dz), linearly: start + offset × elapsed / time, in integer level units; at the end, start + offset. With an absolute target, it moves towards that point instead. Time 0 counts as 1 ms |
| 512 (`0x200`, rotate) | `[a0, a1, a2, time ms, options (text), …]` | the same for the object's three angles (2048 per turn) |

**Options** are a short text at byte 16 of the move and rotate data (the
game upper-cases it on start; the levels use `r`, `c`, `rc`, `cr`):
- **`r`, reversible.** After a run finishes, the direction flips, so the
  next activation runs the offset backwards (doors that close again).
  Without `r`, an object other than the player that has already moved
  ignores further activations: it moves once.
- **`c`, continuous.** At the end the clock restarts, so the movement repeats
  for ever (wheels, spikes). Together with `r`, a rotation also reverses on
  each cycle, so it swings back and forth.

State bits in the controller's byte +`0x3C`: bit 0 = running, bit 1 =
reversed (set after a forward run). Both are saved with the game (`0x4C3D8`).

**Teleports.** Type-2 links go to `user` flats whose move has time 0. The
player lands at the flat's position + the move's offset. In level 1, the five
wizard portals (`1wizpo1`–`5`) all lead to x 2458, z −2640, at five heights
(floors of one tower), and five other flats all lead back to (−3320, 248,
1384).

**Sounds.** `STRU` 121 is `[sound id, value]`: the reader keeps it, and after
the object is read it is attached to the object's last controller (+`0x38`;
the second value goes to +`0x3E`, use unknown). So the 72, 42, 351… in the
survey are `SPIRE.SND` ids.

**Special cases:** `l8tbeam` (Dagon's transport beam, in `indagon.bs6`, the
final level's Hunting Lodge) has its height forced to −1608 when its move
starts; activating it marks the player as having come by it and, without the
Savior's Hide, starts the captivity game over (see "Videos"). An object with
`STRU` 120 starts used: when the level is loaded (`0x23816` → `0x4C7BC`) its
moves and rotations, and those of the objects it links to, are put at their
ends (the start position or angles plus the offset, the "reversed" bit set).
The only one is a level 1 door panel, so one of its five generators runs from
the start; the original's saves show it so.

**Open:** what the trigger time field (`[1]`, e.g. 500 or 1000 on some step
triggers) does; the second value of `STRU` 121; how a moving floor carries
the player (seen in the game; the move helper returns each frame's change,
which is presumably what is applied to whoever stands on it); event 0 being a side contact is inferred
from the collision code, not confirmed.

## Player movement and collision (found 2026-10-13)

Static analysis of the movement function (`0x3FDA0`, every frame) and the
collision routine (`0x2C299`), with values read from the level 1 memory
dumps. Checked in the game (2026-10-14): jumps are charged by holding the
key, with a pointer showing the distance (decoded below after that report).

**Position and eye.** The player object (`0x16BFB8`) stores its position at
+7/+B/+F (floats, the **feet**; +y down) and pitch and yaw as words at +1 and
+3 (2048 per turn). The camera is at the feet minus the eye height, the
global `0xDA610`: **72** standing, **35** crouched (`0x3D0E4`). All five
dumps show the camera exactly 72 above the player. Standing up raises the eye
at 74 units/s (frame ms × 37 / 500); crouching lowers it to 35 and plays sound
402 (`0x41393`).

**Body.** Collision uses sets of spheres ("probes", data at `0xED6C0`…), each
a position, angles, a count and entries of (offset x, y, z, radius) relative
to the feet:

| Probe | Spheres (height above feet / radius) | Use |
|---|---|---|
| `0xED6EA` | 34/7, 41/11, 65/16 | the body, tested at the new position |
| `0xED6C0` | 0/16 | ground contact (finds the object stood on) |
| `0xED77E` | 10/35 at z −90, 10/20, 10/35 at z +90 (turned with the boat) | riding a boat (`0x16C064`); see [Swimming, boats and the balloon](#swimming-boats-and-the-balloon-2026-10-17) |
| `0xED7C8` | 700/700, 50/50 | riding the balloon (`0x16C214`) |
| `0xED802` | 60/60 | a first, coarse test |

The lowest body sphere reaches 27 above the feet: lower obstacles are steps.

**Collision** (`0x2C299`, from the old to the new position):
1. A ray 30 above the feet from the old to the new position finds walls in
   between.
2. The body spheres are tested at the new position. If they touch polygons,
   the touched planes are collected (normals at +`0x14` of 32-byte entries),
   the one most against the movement is picked, the movement is projected
   along it, and the test runs once more (sliding along walls); still
   blocked means no move.
3. A ray from 32 above the new feet (50, 90 or 110 in some states) down to 200
   below finds the floor. If the floor is less than 25 below the feet (or
   above them), the player stands on it: the height snaps to it and the
   ground probe names the object stood on (`0x16BF48`). During a jump
   (`0x16FFAD`) there is no snap: the jump lands when the feet reach the
   floor. Otherwise, if the ground probe (radius 16 at the feet) still
   touches something, the player doesn't descend (`0x2C770`): that is what
   holds the player on plank bridges with gaps and at ledge edges. Else the
   player is in the air.
4. A ray up to 200 above the feet finds the ceiling.

Objects the body touches from the side are `0x16BF4C`; a change of either
object sends the step (5) and bump (0) events (see "Level logic").

**Walking speed** (`0x3F4BD`, keys `0x3F654`), from the player record
(`0x16BF90`): base = attribute(+`0x38`) × 435 / 256; forward (action 12) =
base + 128 units/s, backward (13) = (base + 200) / 2, each sideways step
(14, 15) = base + 100; keys held together add up as vectors. (A further
speed, base + 80, is used elsewhere.) ×456/256 in deathmatch and
capture the flag; +282/256 more with Athleticism (+`0x199`);
halved with +`0x226` bit `0x40`, and when swimming or levitating (swimming
then adds half of skill +`0xA2`). No control in the air. With 50 it is 213
units/s forward.

**Gravity** (`0x3FA97`): vertical speed in 1/256 units/s at record +`0x206`
(+ down); each frame it gains `0x18800` × ms / 1000 (**392 units/s²**), up to
`0x3E800` (**1000 units/s**); the move is speed × ms / 1000 / 256. With flag
+`0x173` the fall is capped at 60 units/s (a slow-fall effect?). Landing
resets it.

**Jumping** (`0x40DF9`, `0x40860`): the upward speed is j = (attribute +`0x38`
+ attribute +`0x20`) / 2 + 120, plus j/2 × skill +`0x7E` / 100 (units/s;
×1.5 with Athleticism); with 50/50 and skill 0, 170 units/s, a jump of
about 37 units. A jump is **charged**: while the jump key (action 16) is held,
a timer runs up to 2000 ms (1000 ms if the player moved this frame, flag
`0x16FFAE`). The predicted distance is js × (j × 256 / 196) × held / max / 256
(speed × flight time; 196 is half the gravity), at least 24 (64 when moving),
where js = attribute +`0x38` + 300 + 2 × skill +`0x7E` (×1.29 with +`0x174`
bit `0x40`, ×1.21 with Athleticism). The **pointer** is the model `jumper.3d`,
placed that far ahead in the facing direction, 50 up, turning with the clock
(angle = ms / 2) (`0x4163D`). On release the jump starts: upward speed j,
forward speed js × held / 1000, halved when standing or ×282/256 when moving,
in the facing direction. If the distance stays at its minimum for over a
second, the charge is cancelled. Sound 400 or 401 by the player's sex.

**Water** (`0x16C23C` is the water height, 10000 = none): more than 50 below
the surface the player swims (`0x16FFAF`); falling into water stops the fall
50 below the surface with sound 86. Swimming up/down is 100 units/s,
the balloon's 160 units/s. The details are in
[Swimming, boats and the balloon](#swimming-boats-and-the-balloon-2026-10-17).

**Player record** fields seen: name at +0; eight attributes (i32) at
+`0x20`…+`0x3C` (the test character: 50, 55, 50, 50, 50, 50, 50, 50); skills
further on (+`0x7E`, +`0xA2`). The attributes are very likely in the order of
the game's names for them (messages 527–534 and their abbreviations 535–542,
see "Messages"): **Strength** (+`0x20`), Intelligence, Willpower, Agility,
Endurance, Personality, **Speed** (+`0x38`), Luck. That fits their uses:
Speed sets the walking speed, Strength the carrying limit (4 × Strength,
`0x5484D`) and with Speed the jump (inferred, 2026-10-14). The skills' names
are a list in the same place (`Medical`, `Jumping`, `Lockpicking`, … 14
`Short Blade` … 19 `Missile`, 20 `Critical Strike`); weapons name their skill
by this number (see [items.md](../formats/items.md)).

**Athleticism** (+`0x199`, the class advantage; corrected 2026-10-19, first
guessed to be the Running spell's) raises walking speed (+282/256), jump
speed (×1.21) and jump height (×1.5). The class files put the advantage at
that byte ([scl.md](../formats/scl.md)), and the movement (`0x3F527`) and the
jump (`0x408EA`, `0x4093D`) read it from the player record.

**Spell effects** (UESP, "Battlespire:Magic and Spells", 2026-10-14) match
two of these flags: Slow Fall (effect 8) is +`0x173` (falls capped at 60
units/s), Jumping (22, "jump at twice the normal rate") is +`0x174` bit
`0x40` (forward jump ×1.29). Running (23) sets flag `0x800000` (+`0x174`
bit `0x80`, `0x2874C`), which nothing reads: the spell does nothing. Sneak
Mode (the S key) halving the speeds matches +`0x226` bit `0x40`.

**Controls** (UESP, "Battlespire:Controls"): arrows move (actions 12–15), Alt
jumps (16), Delete/End turn, Insert/Home look, Page Up/Down move up and down
(21, 22), S sneaks, Space held with the mouse pulled back ducks (pushed:
shove), the left button uses, the right button swings the weapon.

**Open:** the exact rule for choosing the slide plane. (`0x16FFAE` is "moved this frame", set at
`0x407A6`.)

## Messages and using things (2026-10-14)

**Built-in messages:** 543 string pointers at `0xE82B9` (v1.5), from "Exit
the Automap." to "LCK": help lines, prompts, the attribute names. Code shows
one by index. `bs/formats/exe_text.h` finds the table by content.

**On-screen messages** (`0x1D211` queues, `0x1D3A8` draws): 16 slots of 80
characters with the time each was added. A message already showing isn't
added again; when all slots are in use it's dropped. Each lasts 10 s
(`0x2710` ms). They are drawn centred on x = 320 from y = 5, one line per
font height + 2: a dark box, the text in `FONT0003` (dark brown) two pixels
right and down as a shadow, then in `FONT0001` (white). Font n is
`FONTnnnn.FNT` (`0x14D48`). The text measure (`0x13A81`) advances a glyph's
width + 1, and 5 for a space or a byte past the font. Longer texts (a
`TEXT.RSC` record, `0x4DBD2`) are wrapped to 600 pixels, each line passed to
`0x1D33A` (probably the message log), and shown together in a box (`0x1097D`)
instead. OpenBattlespire shows them as top-of-screen messages for now.

**Using** (the left button, `0x4CF0A`): the object under the cursor is
dispatched by its type byte (+0): items (2), objects with properties (6),
monsters (0x12), flats (0x21: `0x4D5A4`, the NPCs) and type 0x43 ("You see
%s."). Reach: 192 units for items and objects with properties (the distance
in `0x3F86FC`). Details in [items.md](../formats/items.md#using-items-in-the-world).


## Keys, sigils and gems (2026-10-14)

**Keys** are scripted per level (`0x3E578`, run when an object is used and
from the frame code at `0x40511`), by level number and the object's model
name. Objects with BS6 property 103 are locked (+0x13 bit 8): their own use
event isn't sent (`0x4D1D2`); the script decides.

- **Level 1** (`l1.bs6`): using a locked object (the `1cpanel`) takes a Cog
  (base 36). Each moves a counter on (`0x3E4C7`): the panel's texture
  `1cnt01` shows `1cnt02` … `1cnt06` (`0x26482`, the textures found by name
  hash `0x75154` and swapped in place, `0x3E448`); the player record +0x25E
  counts. The fifth Cog sends the panel its use event.
- **Level 2** (`l3.bs6`): the same with Voidguides (base 30) and the altar's
  `alta01` … `alta05`: four.
- **Level 3** (`l2.bs6`): the gates `l2dr1` and `l2dr2` stay locked ("Gate
  has not yet been activated.") until the spool `l2spool` has taken three
  control rods (base 37), each only while both buttons `l2but4` and
  `l2but05` are pressed (`0x3EBB9` → `0x4C5C0`, a test through the logic
  data, approximated as "has moved"): "A control rod has been placed.",
  then "The gate is opened." and the spool's use event.
- **Level 5**: the temple door `tempd` takes all six Gatekeys (base 38) at
  once, or "You must have all six keys to open temple." (`0x4D42A`). The
  bell rope `belrope` moves another object (not done yet).
- **Level 6**: the `l6phone` takes Gatekeys (base 35), "A key has been
  placed."; the third: "The gate is opened." and its use event; without one,
  "You must place all three keys to open temple."
- Level exits are checked here too: see "Level transitions" below.

**Sigils** are the objects whose model is `sigil` + a letter (registered
when the level loads, `0x240B4` → `0x5B456`: object and height). Every
frame for each mover (`0x5B4B8`): a sigil within 300 units across and 160
up or down (measured, for the player, from the eye: inferred, the sigils
hang about 190 up) …
- … vanishes for the player carrying the Sigil Amulet of its letter: id −
  9001 = letter − 'a', so only the 26 "… Sigil of Entry" (9001–9026) open
  sigils (`0x5BCB5`); it sparkles (`twinkle.bsi`) and moves to height 10000;
- … otherwise, within 120 across, blocks the mover, and hurts the player:
  30 damage and a push away (`0x5BC4B`), not again in the next frame.
- The spell Dispel Sigil moves sigils within 500 away (`0x5BDEF`), once
  every 5460 ms ("You are still exhausted from your last sigil dispel.").

**Gems** are objects with property 110 (health) or 111 (spell points),
listed at `0xFE004` (9 bytes each: the object id, the value, a byte 0 for
health, 1 for spell points); loading one sets its +0x14 bit 4 and clears its
+0x21 (`0x24AED`). `0x4D64C` adds value % of the maximum, kept within 0 and
the maximum; a negative value also pushes the player 300 away from the
object (`0x439B8`). Then, as bit 4 is set, the object's +0x21 becomes the
clock + 60 s, and until then it isn't drawn (`0x4D7E9`, `0x2CDA9`). It goes
off when the gem is used (`0x4D278`), and also when the player steps onto it
or walks into it: the movement function calls it for a new floor object
(`0x16BF48`, after the step event, `0x4072A`) and a new side contact
(`0x16BF4C`, after the bump event, `0x40760`). The level 1 `1hurt`, an upright
column of `ENERGY` (y −1347 to −131), is one at −25 %, and so are
level 4's `l4a` … `l4z` and level 3's `61_3` (−50).

**Floors that burn** (2026-10-31): at the level's start `0x418C2` looks up
seven textures by name: `fpit02`, `poop00`, `lava00`, `gold00`, `mud00`,
`smth05` and `lava01` (`0x3F7C34`). At the end of the movement function,
every frame, `0x41817` checks the floor face (`0x3DADEC`) while there is a
floor object: on one of them, the player takes frame ms × 50 / 1000
(`0x16BF7C`) through `0x42E2F` with element 0, fire, so the tolerances
apply (`0x2AA5D`), and force 0: no push. Truncated per frame, that's 40 a
second at 20 frames a second. Level 1's lava is `LAVA00`.

OpenBattlespire does all of the above except the level 5 bell rope, the
sparkle, the pushes, and Dispel Sigil (with magic). Burning goes in steps
of 50 ms (2 each), the 20 frames a second assumed elsewhere.


## Level transitions (2026-10-14)

**Doors** are objects with BS6 property 100: a 14-byte name, the BS6 file
it leads to without `.bs6` (`l5`, `inkeep`, `mtemple`), then an arrival
number and two values not decoded. Using one (`0x4D287`) goes to that
arrival point. **Arrival points** are flats with property 101 (`0x248E6`):
0 or 666 marks the level's start, any other value registers arrival point n.
Level 5's houses, inn, tavern, temple and smithy are separate BS6 files
whose doors lead back to `l5` at the arrival point outside; levels 6 and 8
have doors whose arrival point is in the same level. The game loads every
interior a door names into the level's world when the level starts, each
under its door with its ids shifted (2026-10-30, from saves; see
[save.md](../formats/save.md#interiors)). So for the game every door's
arrival point is in the same world. OpenBattlespire loads interiors as
levels of their own instead: if the arrival point is in the current level,
it moves there, otherwise it loads the named file and arrives there. The
level left through a door is kept as it was left until a new campaign level
begins, as the original's world keeps it (2026-10-31): its items, the
monsters alive (the dead stay dead), its objects and controllers, the
speakers gone and the sigils dispelled. The game's clock goes on through a
door.

**Exits** are checked by the level script (`0x3E578`, called with no
object from the frame code at `0x40511`) while the player stands on an
object (`0x16BF48`, the object under the feet; `0x3DADEC`, the floor face,
whose texture is compared by name hash):

| Level (number, file) | Exit |
| --- | --- |
| 1, `l1.bs6` | standing on `1zoom` once all Cogs are in, with at least five `1doorpan` panels open |
| 2, `l3.bs6` | standing on a `port03` face once all Voidguides are in |
| 3, `l2.bs6` | standing on `l2a46` on a `black` face |
| 4, `l4.bs6`; 5, `l5.bs6` | standing on a locked object (property 103: `l4exit`, `exitgate`) |
| 6, `l6.bs6` | as 4 and 5, once the three Gatekeys are placed |

A door panel counts as open (`0x4C5C0`) when a move or rotation controller
on it, or on an object it links to, has run forward (its +0x3C value 2, the
"reversed" flag of `bs/formats/level_logic.h`). Level 1 has six: five turn
on its generators (each moves two pistons and a cover, all reversible) and one
opens a door for good. One generator runs from the start (`STRU` 120). With
none open, the script plays `ANCHORS.SMK` instead, a joke ending (see
"Videos"); the list it counts (`0x16BFCC`) is the root of the object tree.

**Completing a level** (`0x268A1`) plays `JUMP.SMK`, opens the level-up
screen with the level's points (a table at `0xED524`; see
[Levelling](#levelling-2026-10-17)), and goes to the campaign level whose
`LEVELS.TXT` number is one more, skipping 7: `l1`, `l3`, `l2`, `l4`, `l5`,
`l6`, then `l8` (number 8). Carried items go on; the counter at player
record +0x25E (Cogs, Voidguides, keys) starts again from 0. After level 8
OpenBattlespire goes back to the title; the endings aren't done.

## Monsters and combat (2026-10-14)

The details are in [creatures.md](../formats/creatures.md). The landmarks:

| Address | What |
|---|---|
| `0x25841` | flats named `monster1`/`monster2` become spawn points |
| `0x461BE`, `0x46A51` | the spawner and the monster maker |
| `0x4555E` → `0x450F8` | each frame: the movers near the player (within `LEVELS.TXT` field 9) |
| `0x44F7E`, `0x48476` | each mover's nearest node, and its target (the nearest mover of another team) |
| `0x44F0B` → `0x43ECF` | the AI state machine, per mover |
| `0x45843` | walking to a goal; `0x45EAA`/`0x456E1` turning |
| `0x47143` | the image, view and frame of a character |
| `0x47502` | a monster's melee attack |
| `0x41239` | the player's swing from mouse motion; `0x4975B` its blow |
| `0x4241F` | one blow: location, chance, criticals, damage |
| `0x42E2F` | damage taken, death, loot; `0x439B8` the push back |
| `0x48749` | creature sounds; `0x43E0C`/`0x43E6A` hit and miss sounds |
| `0x487B5` | a waiting NPC's greeting |
| `0x135D2` | 2 s after the player's death: `CHOP2.SMK` and text 1058 |

Also found on the way:
- Player record +0x132, which the inventory shows as hand-to-hand damage, is
  skill 16 (Hand to Hand). Skills are 21 records of 12 bytes from +0x72:
  skill 1 is jumping (+0x7E), 6 dodging (+0xBA), 20 critical strikes
  (+0x162).
- `LEVELS.TXT`'s last unknown field is the distance within which monsters
  act.
- The game's angle function (`0x1E711`) divides by 3.14 instead of π, and
  points straight along +z come out as 2047, not 0.

Open:
- The node search (`0x1A660`, `0x1A554`) and path following (`0x1B6F6`,
  `0x1B266`, `0x1BBC3`).
- Dialogue: done (2026-10-16), see "Dialogue".
- The weapon animations (`0x494C8`, `0x15EDB`; frame counter `0x16C24C`).
- The hidden reserve monsters (`0x16C2B0`).
- What `MA2`'s +0x242 = 1 does (`0x59A7F`).

## Magic (2026-10-15)

The details are in [spells.md](../formats/spells.md). The landmarks:

| Address | What |
|---|---|
| `0xDA614`, `0xDA60A` | the spell table, and the schools' skills before it |
| `0x27AEB` | casting: ethereal and cost checks, payment, by delivery |
| `0x29791` | the cost |
| `0x28A1E` | the duration and magnitude rolls |
| `0x296C4` | a spell taking effect straight from its caster (hand, sparkle) |
| `0x27F4C` | the effects, a switch on the spell's number |
| `0x295A0`, `0x293EF`, `0x28F11` | the player's timed effects: made, run each frame, upkeep |
| `0x274BE`, `0x28AD0`, `0x2786F` | a missile launched, flying, bursting |
| `0x27DAC` | resisting, absorbing and reflecting by the spell pools |
| `0x2AA5D` | an element's share of damage through the tolerances |
| `0x47645` | a monster casting; `0x456E1` turning to its target |
| `0x47B33` | summoning from the hidden reserve |
| `0x5B702` | Delayed Damage's traps going off |
| `0x2988B`, `0x29C92` | the spell book, its drawing; buttons at `0xED5B2` |
| `0x422F0` | the hot keys F1–F8 in play |
| `0x2A698` | the casting hand; `0x2B1FC`, `0x2B22F` the Teleport screen |

Also found on the way:
- The game's distances are approximations (`0x1B900`, `0x1B91B`): across,
  a + b − min(a, b) / 2; with height, max + min / 4.
- Message table entries past 543 exist (583 "You are confused...", 629
  "Spell fizzles...").
- Images named in code by base-40 handles, as model textures are: `SPEL00`
  + element for missiles, `EXPL00` + element for bursts.
- Fire Shield gives back half the damage, not double as UESP says.

Open:
- What the Detect spells show, Confusion's effect on the controls, Dispel
  Magic's list (`0x2AD30`).
- When Teleport fizzles ("Spell fizzles...", `0x27B80`: two conditions not
  traced).
- Monsters of different teams fighting each other (summoned ones).
- Skills rising with use: see [Levelling](#levelling-2026-10-17).

## The interface bar (2026-10-15)

The bar is a window of the game's UI system (`0x10E284`), set up at
`0x2E57C` with `panel.bsi` and seven buttons (`0xED838`: x0, y0, x1, y1, then
a key and a handler). Its draw callback (`0x2E659`) puts the vials and the
compass over the picture each frame.

- **The pictures:** `panel.bsi` holds the whole bar (image 1, 640 × 100 at
  y 380, transparent where the face goes), its left end (image 0), and the
  four buttons (images 2–5). The 3D view stays full-screen behind it, so the
  bar covers its bottom 100 lines.
- **The vials:** `engy01.bsi` is health (blue, at 194, 393) and `engy02.bsi`
  spell points (yellow, at 434, 393). Each has 32 frames from full to empty.
  The frame is 32 − 32 × current / maximum, cut to 0–31: player +`0x66` /
  +`0x6A` and +`0x60` / +`0x62`.
- **The compass:** frame = yaw × 10 / 568 of 36. It's `comp00.bsi` in the bar
  (at 525, 387) and `comp01.bsi`, in its own ring, in the full view (at 548,
  390).
- **The buttons:**

  | Rectangle | What it does |
  |---|---|
  | 33, 380 – 167, 479 (the face) | the character sheet (`0x558DC`) |
  | 229, 380 – 327, 429 | the spell book (`0x2988B`) |
  | 330, 380 – 417, 428 | the inventory (`0x4E4C1`) |
  | 229, 430 – 327, 479 | the automap (`0x57415`) |
  | 330, 430 – 417, 479 | the options (`0x2EA54`; another call when `0x16BFA4` is set) |
  | 500, 380 – 639, 479 (the compass) | the hot key list (`0x551C5`) |
  | 9, 455 – 32, 477 (the "?") | help (`0x2EA28`) |

  Their help texts ("Activate the character sheet menu." …) are pointers at
  `0xE8331`.
- **The full view** (Tab, `0x2ECA3`) toggles bit 0 of the options byte
  `[0x16BF58]`, which the window keeps as its own flag (`0x10E8FA`).
  - The buttons and the picture go.
  - The vials move to x 10 and 54.
  - The punching fist sits at the bottom of the screen (`0x2A8C5`).
- **The face** (`0x5678A`, while the paper doll is composed) is the doll cut
  out around its eyes: a 149 × 100 picture at (0, 380) is filled from the
  composed doll starting 70 left of and 35 above the eyes image's own
  placement (`%c%sey%03d.bsi`, kept at `0x3FBD84`), cut to 0. The frame
  `fram00.bsi` (149 × 100 at 0, 380) is drawn over it, then the bar.
  - The doll is the inventory's: body, eyes, mouth, hair (none under a
    helmet) and the worn items' pictures, so a helmet shows on the face.
  - Confirmed against two screenshots: a male Dark Elf's eyes at (103, 34)
    give the offset (−33, +381), measured (−33, +380); a female High Elf's at
    (157, 46) give (−87, +369), measured (−90, +368). The screenshots'
    characters had other eyes than the default ones, which accounts for the
    few pixels.
  - `fram00.bsi` belongs to this face, not to the inventory's doll. The engine
    drew it on the inventory screen by mistake until 2026-10-15.

## The bar's screens (2026-10-15)

The bar's other buttons, with their keys. Static analysis, with the screens'
pictures exported to check positions.

- **Help** (the "?", `0x2EA28`) runs the help pages: `0x20C5D` queues
  `0x20D33(first page, pages, forced)`, here pages 11–15.
  - The same function shows pages by themselves the first time a screen
    opens: the sheet 16, automap 18, options 20, inventory and others. Each
    is shown once per character (player +0x242, a bit per first page), and
    only while the "pop up" flag `0x16FFCD` is on. The engine doesn't show
    them yet.
  - A page is `help%02d.bsi` (640 × 480) with the game copied small into its
    window, (160, 121)–(479, 360) (`0x20A95`). The window grows from the
    centre over 12 ticks as it opens and shrinks as it closes.
  - `hyes00.bsi` or `hno000.bsi` marks the flag.
  - Clicks (`0x20F3A`, open intervals): Yes 274–295 × 450–463, No 300–319,
    Done 566–635 × 435–468, Next 493–561 × 437–468 (not on the last page),
    Previous 367–490 × 437–470 (not on the first). The pages carry their
    own buttons' pictures. Esc closes.
- **The character sheet** (the face, F9; `0x558DC`, drawn by `0x55B55`):
  - The picture is `cscr00.bsi`; `cscr01.bsi` is used when it opens for a
    new level.
  - 17 buttons (`0xF0EB4`): Automap, Inventory, the eight attributes,
    Exit, three bars that only have help texts, "?" (page 16), and the two
    level-up arrows.
  - The doll is drawn at (10, 30), 20 lower than the inventory's (`0x56C8B`).
    The name is at (256, 34).
  - The attributes are at (576, 50 + 50 n). Font 1 (`FONT0001`, white) if
    unchanged, 2 (red) if lower than the base (+0x40), 4 (green) if higher.
  - The next three use the last attribute's font: health "%d/%d" at
    (368, 354), spell points at (424, 322), and the load "%d.%d / %d" at
    (424, 386). The load is the carried weight in half units (`0x547C5`)
    against 2 × Strength.
  - The skills (`0x55E04`) are "%s [%s]: %d   " (skill names `0xE34E9`,
    attribute short names `0xE8B15` by `0xE8B39`). The class's twelve (+0x1A6)
    come first, then the rest except Medical, Lockpicking and Running
    (`0xEE5AC`).
  - Each skill line starts with a font code: 2 lower than its base (+0x7A),
    4 higher, 3 (`FONT0003`, dark) the same.
  - They scroll in a text box (272, 128)–(496, 272), with arrows at
    (522, 115) and (522, 247).
  - With an argument the sheet is the level-up screen (points to spend,
    `cscr01.bsi`); see [Levelling](#levelling-2026-10-17).
- **The automap** (F11; `0x57415`, drawn by `0x577B7`):
  - The picture is `map030.bsi`, a blank parchment with Exit.
  - The level's map comes from a table at `0xF1083`: 29 records of 0x42 bytes
    with the file name (levels and interiors) and the map picture
    `map000.bsi`… `map029.bsi`. Then ten numbers: x left, z top, x right,
    z bottom, the screen rectangle they map to, and a shift.
  - Level 3 has two maps: below y 700 it's the next record.
  - A level point maps in whole percent (`0x57D76`):
    fx = (x − left) × 100 / (right − left) and
    fy = 100 − (z − bottom) × 100 / (top − bottom).
    The screen point is the rectangle's corner + its size × f / 100 + the
    shift twice.
  - The map shows only in 16 × 16 cells the player has seen: a 40 × 30 grid
    kept in a level object (type 0x33). Each time the player has moved far
    enough (`0x41747`), the 3 × 3 cells around their map point are marked
    (`0x57AC2`).
  - The player's marker (`0x5752A`) is a cross 3 pixels across and 4 up and
    down, red (0x7C00) or green (0x3C1) every other 8 timer ticks, with a
    16-pixel line the way they face.
    - The line steps by the larger of the signed differences, so facing
      between north and west it isn't drawn.
    - It writes only the colour's low byte. The engine draws the line in the
      full colour.
- **The hot key list** (the compass; `0x551C5`) shows eight messages
  "F%d : %s": the spell's name, an item's, or "Nothing assigned." (514).
- **The options bar** (the gears, Esc; `0x2EA54`, drawn by `0x2EB70`):
  - The picture is `options.bsi`, in the bar's place; the world waits.
  - 11 buttons (`0xED8DC`, help texts `0xE82F9`): Fullscreen (the full view
    toggle, `0x2ECA3`; `optnon00.bsi` shows ON), Screen Size, Controls,
    Load, Save, End Game, Drop to DOS, the two volume sliders, Exit (Esc), "?"
    (page 20).
  - Screen Size and Controls open their own screens; see
    [Screen Size and Controls](#screen-size-and-controls-2026-10-19).
  - The sliders: volume = (x − 120) × 127 / 132, kept in 0..127.
    - Sound is `0xDA404` and player +0x24A (`0x1BEC5`); music is `0xDA400`
      and +0x236 (`0x1BE6E`).
    - Each is drawn as `optn07.bsi` or `optn08.bsi` cut to volume / 127 of
      its width, if over 5.
  - End Game and Drop to DOS ask "Are you sure?" (`0x1FDC1`): `sure00.bsi`
    at (162, 133), Yes 209–278 × 217–243, No 342–410, or the Y and N keys.

## Screen Size and Controls (2026-10-19)

The options bar's last two screens. Static analysis, with the pictures
exported to check positions and the engine's screens driven by clicks.

### Screen Size (`0x2ED23`, drawn by `0x2ED97`)

- **The box** is `screen00.bsi` (345 × 189 at 140, 156) over the options
  bar. Three buttons (`0xED9D9`):
  - Resolution (317, 179)–(430, 209) toggles `0x16C200` (`0x2EE4C`).
  - The view's size (323, 234)–(434, 248): size = (x − 323) / 16 + 1, so
    1 to 7 (`0x2EE93`).
  - Done (254, 287)–(389, 323), Esc.
- **The resolution:** `0x16C200` holds a pointer to a 640 × 480 buffer
  (`0x16C1B0`) or 0. Set, the view is drawn there at half size and copied
  to the screen with each pixel doubled (`0x1BBF9`): that is LOW. The box's
  picture says LOW; `screen01.bsi` (HIGH) is drawn over it when the flag is
  clear. The help text calls the button "Switch resolution to hires.".
- **The slider** is `screen02.bsi` (111 × 15 at 323, 234), shifted left so
  that size / 7 of its width shows.
- **The view's size** (`0x3EC12`, `0xF22E4` and `0xF22E8`):
  - size 7: the whole screen, 640 × 480, or 320 × 240 at low resolution;
  - size n below 7: (320 + 45n) × (240 + 34n), or (160 + 22n) × (120 + 17n)
    at low resolution, doubled on the screen.
  - The focal lengths stay 0.75 × width and height, so the field of view
    doesn't change.
- **A smaller view** is centred on the whole 640 × 480 screen, not on the
  part above the bar, and the bar covers its bottom at sizes 4 and up:
  - `backscrn.bsi`, copied into a buffer at start-up (`0x212ED`), fills the
    screen around it (`0x3EF4B`, `0x3F08A` at low resolution);
  - a black frame is drawn on its edge (`0x54D0F`, colour 0): the top and
    left lines on the view's first row and column, the bottom one row below
    the view, the right on its next-to-last column.
- **`screen.dat`** (written by Done, `0x2EEFD`; read at start-up, `0x10501`)
  is 9 bytes: the size (4), the resolution flag (4, non-zero for low) and
  whether help pops up (1). Without it the game starts at size 7, low
  resolution, help on (`0x10576`). The GOG install has none, so it runs at
  low resolution.

### Controls (`0x1F481`, drawn by `0x1F89A`)

- **The screen** is `controls.bsi`, the whole screen, with 13 buttons
  (`0xED3B8`). Opening it raises a turn rate below 10 to 10; help page 21
  pops up the first time.
- **The keys:** the game's 26 actions are a table of 32-bit codes at
  `0x16C2B8`, defaults at `0xE8229`. A code is a PC scan code (set 1), or
  minus the mouse buttons' mask: −1 left (M1), −2 right (M2), −3 or −4 M3.
  - In the screen's order, with their defaults: Inventory F10, Automap F11,
    Char Sheet F9, Chat Bar T, Chat Room C, Fullscreen Tab, Exit Vehicle D,
    Switch Weapon A, Special Moves Space, Swing Weapon M2, Sneak Mode S,
    Spellbook F12; Forwards, Backwards, Left, Right (the arrows), Jump Alt,
    Turn Right End, Turn Left Delete, Look Up Insert, Look Down Home, Move
    Up and Down Page Up and Down, Cursor Enter; Select M1, Lantern L.
  - These are the action numbers the movement notes use (12–15 move, 16
    jumps, 21 and 22 up and down). `0x1F3C0` asks whether an action is held,
    `0x1F33B` whether it was just pressed.
  - The boxes are 64 × 16, 32 apart from y 16: Inventory … Spellbook at
    x 176, Forwards … Cursor at 368, Select and Lantern at 560. Only Select's
    is a button there, so Lantern can't be changed.
  - A click on a box (`0x1F608`; not in the 16-pixel gaps) shows message 89
    centred at the top and queues message 91. The box is then framed in
    0x5000 (`0x1FC16`) until a key or mouse button comes. Esc and F1–F8
    (`0xE8291`) are refused with message 92.
  - The names are drawn centred in the boxes in font 1, or font 2 when
    another action has the same code (`0x200AE`). They come from a table of
    string pointers by scan code at `0xE80C5`, just before the messages
    ("ESC", "BCKSPC", "SPCBAR", "PG UP" …), and "M1"–"M3" (`0xED50E`).
- **Mouse Reverse** (592, 80)–(624, 96) toggles `0x16C328`; `mrev00.bsi`
  shows it on. It turns the mouse's vertical look around.
- **The sliders** are held buttons (flag 4); each sets its value from the
  click's place, p = (x − x0) × 100 / (x1 − x0) in whole percent:
  - Mouse sensitivity X (468, 132)–(622, 140) and Y (468, 148)–(622, 156):
    p × 3 / 100 + 0.01, floats at `0x16C320` and `0x16C324`, default 1;
  - Double Click Rate (452, 196)–(620, 204): p / 10 + 8 ticks (`0x16C329`,
    default 8). A second click within that many timer ticks and 4 pixels
    is a double click (`0x2164F`);
  - Turn Rate (452, 244)–(620, 252): (x − x0) × 256 / (x1 − x0), kept
    within 10..256, into the player's byte +0x2E0. 256 doesn't fit and
    becomes 0; a level start turns 0 into 128 (`0x12BDE`). The three
    installed saves have 128.
  - Each is drawn as a red (0x7C00) bar 9 high from the left: 155 wide for
    0.01–3.0, 169 for 8–18 and 10–256; none at the least.
- **What they do:**
  - The turn rate: the turning and looking keys move 6 × the rate angle
    units (2048 a turn) a second, a boat's turn half that (`0x3F19F`). 128
    is 768 a second, a turn in 2.7 s.
  - In View movement the mouse adds 2 × its motion × the sensitivity to the
    heading and the pitch, the pitch's sign flipped by Mouse Reverse; the
    pitch is kept within ±256 (45°) (`0x3F3FA`).
- **Movement** View (451, 311)–(525, 337) or Cursor (548, 311)–(621, 337)
  clears or sets the player's +0x226 bit 0x80; `view.bsi` or `cursor.bsi`
  is drawn on the button that's on. Cursor movement steers with the
  pointer and the left button: see
  [Cursor movement](#cursor-movement-2026-10-20).
- **Default** (256, 416)–(366, 453) puts back the table, both
  sensitivities, Mouse Reverse and the double-click time; not the turn rate
  or the movement (`0x1F703`).
- **Exit** (576, 416)–(639, 479), Esc, refuses with message 90 while two
  actions share a code (`0x20048`); else it writes `controls.dat`: the
  table, the sensitivities, Mouse Reverse and the double-click time,
  0x72 bytes from `0x16C2B8`. "?" (616, 2)–(637, 22) shows help page 21.
- **Lantern** (action 25, `0x41189`) toggles the player's +0x227 bit 4,
  which the CharacterMaker sets. While it's on the view has the player's
  light: radius 384, brightness 40 (`0x3D2D8`).
- **In the engine:** `controls.c` keeps the game's 26 actions in this
  order, then the engine's Duck and Fly, up to three keys each; the screen
  shows and sets the first. The settings go to `settings.ini` with Screen
  Size's (the turn rate and the movement too, which the game keeps with
  the character).

## Cursor movement (2026-10-20)

The Controls screen's alternative to View (player +0x226 bit 0x80). The
mouse isn't used to look around (`0x3F37D` skips View's mouse look); the
pointer stays on the screen, and holding the left button steers by where
it is.

- **The zones** (`0x41AA7`, every movement frame). The screen, in its
  640 × 480 coordinates, is cut into nine zones down to a bottom line H:
  the bar's top (the `panel.bsi` image's y, 380), or 479 in the full view.
  - The rows: y < H × 35 / 100 is the top row, y < H − H × 25 / 100 the
    middle one, the rest the bottom one. At H = 380 the rows change at 133
    and 285.
  - The columns: x ≤ 250 left, x ≤ 390 middle, the rest right. They don't
    follow the view's size from Screen Size.
  - The zone, 0–8 row by row from the top left, goes to `0x16FFC3`; a
    point below H is 100, "none".
  - On the left the turning amount is (250 − x) / 250 and on the right
    (x − 390) / 250 (`0x3F7BF0`). A vertical amount is worked out the same
    way (`0x3F7BEC`), but nothing reads it.
- **The pointer** (`0x124D0`). In a zone it's `direc<zone>.bsi`
  (loaded by `0x41A41`), drawn with its top left at the mouse:
  - forward and turning left, forward, forward and turning right;
  - turning left, a cross, turning right;
  - a step left, back, a step right.

  At 100 it's the usual pointer.
- **Steering** (`0x41C04`). This runs while the left button is held
  (`0x16FFCF` bit 1), in the same frames as the walking keys: on the
  ground, in water or riding, not during a jump (`0x400BD`). It runs just
  before the keys (`0x3F654`).
  - The speeds: ahead = Speed (+`0x38`) + 200, aside = Speed + 100, in
    units a second.
    - Sneak Mode halves them, and swimming or the balloon halves them once
      more.
    - Athleticism, the multiplayer bonus and the Swimming skill don't
      apply, unlike for the keys. At Speed 50 the result is 250 and 150.
    - A flag at `0xF1DDF` would put 1000 for Speed, but nothing sets it.
  - The turn: 4 × the turn rate (+`0x2E0`) × the amount angle units a
    second. At 128 and the screen's edge that's 512 a second, two thirds of
    the turning keys' speed.
  - Zones 0 and 2 turn, then go ahead along the new heading. Zone 1 goes
    ahead. Zones 3 and 5 only turn; 4 does nothing.
  - Zones 6 and 8 step aside at heading ∓ 512 (a quarter turn), and zone 7
    goes back.
  - On a boat, ahead and back follow the boat's heading (2047 − its yaw),
    but the side steps follow the player's own. The turns turn the player,
    not the boat.
  - The move is added straight to the player's position as whole units,
    sine and cosine from the tables at `0xF2B00` and `0xF2300`
    (`0x1B975`). The collision afterwards checks it as it does the keys'
    move.
- **Selecting** (`0x410AA`).
  - Select (action 24) uses only when the pointer is in the centre zone.
  - A press of the right button also uses, unless the right button is the
    attack's binding (the key table's entry 9 is −2). With the defaults,
    Attack is the right button.
  - In View movement Select works anywhere.
  - Either way the use itself is the usual one (`0x411F8` sets
    `0x3F8710`), aimed at the view's centre, not at the pointer.
- **Turning while a button is held** (found here, wider than Cursor
  movement): the movement function calls the turning (`0x3F19F`) only
  when neither mouse button is held (`0x40096`). That covers the turning
  and looking keys and View's mouse look, and the call is only made in the
  walking branch, so nothing turns in the air either. The engine still
  turns in both cases.
- **In the engine:**
  - `bs_cursor_zone` and `bs_cursor_steer_of` (`player.c`) give the zone
    and what it does.
  - `bs_player_input`'s `cursor_forward` and `cursor_strafe` move the
    player beside the keys, with the halvings above. The engine moves by
    fractions, not whole units.
  - The app turns the view, keeps the mouse free in the level, hides the
    system pointer over the view, and draws the arrow into the overlay.

## The weapon in hand (2026-10-15)

A weapon in hand is a 3D model drawn with the level. Bare hands are a sprite.
The game draws it over the level: up against a wall its blade shows in front
of the wall (seen in the DOS game, 2026-10-16). The engine draws the weapon
and its nocked arrow last, over a cleared depth buffer.

- **The models** are named by base item in a pointer table at `0xE34AD`:
  `edagger.3d`, `eloswrd.3d` (Short and Long Sword), `ebrdswrd.3d`,
  `eclaymo.3d`, `ebataxe.3d`, `ewaraxe.3d`, `emace.3d`, `ebow.3d` (both bows),
  `exbow.3d`, `ejav.3d` (Javelin and Sling), `estaff.3d`, `espear.3d`.
  - The weapon is shown when it's equipped (`0x48DF3`, `0x492C1`).
  - The blade points along +z from a pommel at z −32.
- **The placement** (`0x48B38`, `0x49303`) is in the body's frame:
  - The pose's offset (x, y, z), in tenths of a unit, is turned by the
    player's yaw only, added to the feet, and raised 5.
  - The model is turned by that yaw, then the pose's yaw, pitch and roll
    (`0x89568`, `0x89654`, `0x89740`, each applied after the last).
  - The weapon doesn't follow the view's pitch.
  - At rest it's 20 to the right, 50 above the feet and 10 back, pointing
    ahead. Below the eye, as it is, only the blade's tip shows, and the bar
    covers that.
- **The pose** runs through 15 states (`0xEF7D4`: pointers to records of
  0x4C bytes; driven by `0x48E88`). Each record holds:
  - a time in ms;
  - the next state: 100 removes the model; 101 goes to a pending state, or
    to idle or walking; 102 holds the state while a bow is drawn;
  - a there-and-back flag for the angles;
  - for x, y and z: an amplitude, a start and a step. The value is
    amplitude × sin(start + t × step), with t = 0…2048 over the state, plus
    200, −450 and −100;
  - for the three angles, a start and a step. A step of 0 gives 0.

  | State | What |
  |---|---|
  | 0, 1 | idle (110 ms); walking (1000 ms: 3 units ahead and back, tipping up 30 and back) |
  | 2, 3 | raised (1 s, from pointing down) and lowered (1 s) |
  | 4 | the wind-up before a swing (75 ms) |
  | 5, 7–11 | the swings by the mouse's direction (`0xDA53E`) |
  | 6 | back to rest after a swing (75 ms) |
  | 12, 14, 13 | pulled back (300 ms), held back while a wall is ahead (`0x16C028`), pushed out again (150 ms) |

- **A swing** (`0x494C8`) starts only from idle, walking or the recovery
  (`0x49521`). Walking is cut short.
  - Its state's time is set first (`0x4963F`): base[animation] (`0xDA58E`:
    5, or 4 for animation 11) × (1 + ½(w / 30)²) × (1 − ½(b / 140)³) ×
    62.5 ms, cut to whole ms.
    - w is weapon stats field 0.
    - b is the weapon's skill + Strength / 5 + Speed / 5.
  - Then the wind-up's next state is set to the swing (`0xEF498`).
  - The blow lands once more than half the swing's time has passed
    (`0x49053` → `0x4975B`). The game checks only while the state runs, so a
    frame longer than half a swing would lose the blow. The engine doesn't
    lose it.
  - The swing sounds by weapon are at `0xEF840`: 418 blades, 416 long blades,
    415 axes and mace, 419 javelin, staff and spear, none for bows.
- **Bare hands** (`0x494E7`, `0x2A837`): sound 0x1A2 and `punch0.bsi` (6
  frames of 85 ms at 429, 240). The blow lands once the frame passes 4
  (`0x2A892`).
- `epunch.3d` and `earrow.3d` are loaded too (`0x48A26`): the arrow is
  nocked and flies with that model; `epunch.3d` is probably for other players.
- **A wall ahead** (`0x405EB`): each frame with a weapon in hand, a segment
  from 15 right of and 45 above the feet, 90 units ahead along the yaw, is
  tested against the level (`0x2CC86`); the result is `0x16C028`. Correction
  (2026-10-15): it isn't the attack button, as the first notes had it.
  - A weapon at rest or walking is pulled back (state 12); in any other state
    state 12 is made pending (`0x48AE7`). That can drop a pending switch of
    weapons; the engine keeps the switch.
  - State 14 holds while the wall is there, then state 13 pushes it out.

### Bows (2026-10-15)

Bows and the crossbow (base items 8, 9, 10; `0x4A878` tests the weapon type
at player +0x25A) don't run the states. Their frame (`0x49A92`) does this:
- **A wait first.** While `0x3F81CC` is non-zero it counts down by the frame's
  ms and nothing shows. A wait over 10 s is dropped. Switching to a bow sets
  2000 (`0x48D85`, again at `0x48C64`).
- **The pose.** The bow sits at (100, −600, −100) tenths from the feet,
  turned by the yaw only, with no 5-unit lift (`0xEF810`).
  - Its angles are pitch = −view pitch and yaw = the turn aside, `0xEF820`.
  - The rotation (`0x49F7E`) is the body's yaw, then that pitch, then that
    yaw. Unlike a weapon, a bow follows the view's pitch.
  - The nocked arrow (`0x3F85AC`, `earrow.3d`) has the same place and angles
    but is turned as weapons are (`0x49303`). It isn't shown just after a
    shot (`0x3F81C8`).
- **Turning aside** (`0x49DD0`):
  - While a wall is ahead, or after a shot, the bow turns aside to 0x538.
    It goes down from 0x7FF by 512 per s × 1000 / (1000 − (Speed − 50) ×
    10) (player +0x38).
  - Otherwise it turns back up to 0x800 (= 0) at 512 per s.
  - At 0x538 after a shot the next arrow is nocked and it turns back. At
    Speed 50 a shot and the nock take about 2.8 s.
  - From rest the game subtracts from 0 too, so a bow at rest jumps aside at
    once.
- **Shooting** (`0x49C25`):
  - A press of the right button this tick (`0x16FFCF` now, `0x16FFCB` the
    last tick) with the bow at rest, the player not ethereal (+0x175 bit 0),
    shoots.
  - It plays sound 0x19E and uses skill 19 (Missile, +0x156).
  - The bow wears by 1 (`0x51230`).
  - The arrow is launched (`0x4A4E1`), the bow's turn set to 0x7FF.

### Switching weapons (2026-10-15)

The secondary weapon is a carried weapon marked at record +0x42 bit 8
(`0x55319` finds it). The A key's handler is `0x48BE6`:
- **The secondary is a bow:** with no arrows ready, "You have no arrows
  ready." (106). Otherwise the switch happens at once (`0x48C9F`) and the bow
  shows after 2 s.
- **No secondary, or nothing in hand:** at once.
- **Otherwise** the pending state is set to 3, lowering the weapon (1 s, after
  the current state ends). When state 3 ends the switch happens (`0x48F05`).
- **The switch** (`0x48C9F`):
  - The weapon in hand becomes the secondary (`0x55374`, which checks the
    class may use it) and stops being worn.
  - The secondary comes to hand, raised (state 2) or, for a bow, after the
    wait.
  - With no secondary the hand is empty: weapon type 15.
- **When the weapon in hand is destroyed** (`0x512B5`, condition 0: "%s is
  destroyed...", 256) or its arrows run out, the same handler runs.

### Arrows (2026-10-15)

- **Launch** (`0x4A4E1`):
  - One arrow comes off the arrows ready (`0x4A6E6`): the stack's count goes
    down. With the last one gone the stack is removed, "You have no more
    arrows ready." (108) shows, and the secondary weapon is called for. If a
    bow is still in hand, it's put away.
  - The arrow becomes an item object with its own copy of the nocked model,
    flag +0x14 bit 8 (flying), owner at +0x19.
  - Its heading (`0x1B9A0`, `0x1E7A2`) is (sin yaw, sin pitch, cos yaw) made
    unit, with pitch + looking down. Looking up or down, it climbs less
    steeply than the view.
- **Flight** (`0x4A127`, every frame):
  - Gone beyond 3072 units from its owner.
  - It moves frame ms × 1600 / 1000 units, a whole number (`0x1E847`), in a
    straight line with no drop.
  - The move is tested against the level 30 units higher (`0x2B7E3`); a wall
    takes the arrow.
  - Ten points along the move, from where it is, are tested for a creature
    within 80 units (`0x2B502`). The first one hit stops the arrow.
- **A hit** (`0x4A27A`):
  - Half the time (1..100 below 50) the arrow goes into the creature's
    carried items, joining a stack (`0x53EDD`), so it drops with its loot.
    Otherwise it's gone.
  - Sound 0x1B0 plays at the creature.
  - If the player shot it, and it isn't a level 5 Herne: "Arrow hits!" (107),
    then the damage below. There's no roll to hit.
  - Location: a roll through `0xEF1AC`.
  - Damage (`0x4A38A`): the roll of the weapon now in hand (`0xDB489` by
    type) × (100 + Missile) / 100, plus that weapon's material − 1, plus the
    arrow's material − 1.
    - A Short Bow adds (Strength − 60) / 5, a Long Bow (Strength − 70) / 5.
    - Less the creature's armour at the location.
  - The damage goes to `0x42E2F` as both damage and force, so arrows push
    hard.

Open:
- What hides the weapon besides a vehicle (`0x16C064`, `0x16C214` at
  `0x48A9C`).
- Weapons wearing out in melee: a hit wears the weapon by 1–4 one time in
  three (`0x42BCA`); "Your weapon is damaged." by a tenth of its condition
  (`0x42B23`); "Your weapon is knocked from your hand!" (`0x5134A`). The
  engine shows the messages only.
- Item hot keys (player +0x2E7, codes ≥ 100 in +0x1FE; equipping one adds
  2 s, `0x552DB`).
- Monsters with bows.

## Character creation (2026-10-15)

Single Player's screens, from `0x31B50`. Static analysis, with the screens'
pictures exported to check positions. Each screen is a full-screen picture
with a button table: 23-byte records of x0, y0, x1, y1 (inclusive), flags, a
key's scan code and the handler. Esc is Back on every screen, and each "?"
(616, 2)–(637, 22) shows the screen's help page. Entering a screen also
queues its page while help pops up by itself (`0x20C5D`): the name 0, the
class 1, the class list 2, the race 3, the face 4, the CharacterMaker 10
(5 for network play), its spells 9, equipment 8, magic items 6, advantages 7.

- **Select Scenario** (`mult02.bsi`, buttons `0xEDC0D`): Load Saved Game
  (195, 258)–(426, 286), L; New Game (195, 295)–(426, 323), N; Back
  (576, 416)–(639, 479).
- **The name** (`0x33502`, `name00.bsi`): typed at (170, 242), at most 31
  characters (`0x16BCA`), into the player record's +0x00. Buttons
  (`0xEDFBC`): Done (528, 416)–(575, 479), as Enter (`0x322F3`); Cancel
  (576, 416)–(639, 479), as Esc (`0x32355`); help.
- **Male or female** (`gender.bsi`, `0xEDDC2`): Male (224, 207)–(393, 234),
  M; Female (224, 247)–(394, 275), F, which sets +0x226 bit 3 (8).
- **The race** (`0x33F4B`, `char13.bsi`, `0xEDE07`):
  - The six pictures are buttons 192 × 160 from (16, 16), 208 apart across,
    176 down: Redguard, Breton, Nord, High Elf, Dark Elf, Wood Elf (keys R B
    N H D W). Done (9, 425)–(53, 472), Back.
  - A click picks the race (+0x25D). A double click also plays sound
    209 + race and goes on (`0x33FF7`).
  - The arrows move the choice (`0x34093`): right and Tab +1 below 5, left and
    Backspace −1 above 0, up −3 from the second row, down +3 from the first.
  - The animated frame `brdr00.bsi` (21 frames) sits at the picture's
    button + (12, −4) and slides from the old choice to the new one
    (`0x2093A`).
  - The description is TEXT.RSC 1076 + race in font 10, wrapped (`0x5A9C8`):
    - Each line is centred on x 357, from y 400.
    - A line breaks at the last space once the advances pass 383, or at a
      '$' (the record's line breaks).
    - The record's first version is used (versions are split by 0xFF).
- **The face** (`0x32B6C`, `face00.bsi`, `0xEDED6`):
  - Each part's pictures are the BSI names `<sex><race><part>*`, in the
    archive's order, with part `hr`, `ey` or `mt`. A picture's number is
    `atoi` of the name from its sixth character. The first is the start.
  - Buttons, each part's next above its previous:
    - hair (309, 133)–(331, 174) and (309, 214)–(330, 254), key H;
    - eyes (453, 133)–(476, 174) and (453, 214)–(476, 253), key E;
    - mouth (598, 133)–(620, 174) and (598, 214)–(620, 254), key M.
  - Randomize Face (280, 295)–(562, 332), R. Done (512, 416)–(575, 479), Back.
  - The parts are drawn centred on (258, 192), (401, 192) and (547, 192).
    The doll (body, eyes, mouth, hair) is the inventory's moved by (−5, 60).
  - The numbers go to +0x23E, +0x23F and +0x240.
- **The class** (`0x33AEC`, `clas00.bsi`, `0xEDD66`): Existing Class
  (224, 207)–(393, 234), E; New Class (224, 247)–(394, 275), N; Back.
  - New clears the player record but the name, sex, race and face.
  - Existing lists the classes (`0x3363F`, `clas01.bsi`, `0xEDFBC`):
    - The list is `class*.scl` in GAMEDATA, whose level (file +0x2E0) is
      within the level number, as "%s (%d)" (`0x337EE`).
    - Font 3 in a text box (219, 147)–(396, 329), with arrows
      (423, 159)–(444, 200) and (423, 268)–(444, 299). Done
      (528, 416)–(575, 479), Back.
    - The class is loaded over the player record, keeping the name, sex,
      race and face ([../formats/scl.md](../formats/scl.md)).
  - Both go on to the CharacterMaker.

### The CharacterMaker (`0x372DE`)

- **Start.** A new class has 5000 points, then `0x3B273` sets:
  - every attribute 50, every skill 5, 50 wounds;
  - any armour (3) and material (9);
  - Increase Magery × 1, for 300 points.

  An existing class keeps its own points left (+0x20A).
- **The shops** are made as for level 8 (`0x3B62A`).
  - Equipment: five Clothes (`0x520CC`), every weapon (`0x51A0A`; base 12 is
    rerolled as any other weapon), arrows, a random sack or chest
    (`0x5200F`) and the six armour pieces 24–29 (`0x51CA9`).
    - Weapons' and armour's condition is multiplied by material + 1.
  - Magic items: ten potions of the types at `0xE34A3` (4, 5, 15, 17, 8, 16,
    20, 21, 22, 26) and twelve enchanted items (`0x524F6`, ids at `0xEE592`:
    9242, 9265, 9248, 9259, 9204, 9245, 9251, 9254, 9214, 9221, 3014, 1012).
  - An existing class's items flagged 0x10 move into them as bought
    (`0x3BDC3`). Enchanted items and potions go to the magic items, the rest
    to the equipment.
- **The main page** (`char00.bsi`, 25 buttons at `0xEE5EB`; drawn by
  `0x3793E`, pages by their `char%02d.bsi`):
  - The class name (35, 11)–(286, 34): typed in, drawn at (36, 20).
  - Points at (81, 49); on the advantages page at (176, 65). Wounds (the
    base) at (241, 49), with up (315, 37)–(329, 54) and down
    (315, 60)–(329, 75).
  - The skill lists, primary (80, 120)–(329, 171), major (80, 216)–(329, 268)
    and minor (80, 354)–(329, 447):
    - Three, three and six lines in font 3, "%s [%s] : %d%%" from
      (110, 122), (110, 218) and (110, 356).
    - A click's line is (y − 120, 216 or 354) / line height.
    - Within 16 of the left edge it lowers the skill, within 16 of the right
      it raises it. Elsewhere it opens the skill list.
  - The attributes' buttons are 46 wide at x 30, from y 83, about 50 apart.
    - Their values are drawn in font 1 at (17, 105 + 50 n).
    - A click at or above y 100, 149, 198, 248, 298, 348, 397 or 446 raises
      it; at or below 107, 156, 205, 255, 305, 355, 404 or 453 lowers it
      (`0x38B8A`).
  - Spells (382, 185)–(592, 222), Equipment (382, 234)–(592, 269), Magic
    Items (382, 281)–(592, 317), Advantages/Disadvantages
    (382, 329)–(592, 366), Save (433, 432)–(550, 467), Cancel
    (590, 427)–(634, 473), Done (347, 427)–(391, 475).
  - A hidden button (−1, −1)–(1, 1) opens `char01.bsi`, a list of
    "miscellaneous" skills, unused.
- **Steps.** Raising or lowering attributes, wounds and skills happens at most
  every 300 ms (`0x387A1`). The guard suggests the buttons repeat while held,
  and the engine repeats them. With Shift held (`0x16FFCB` bit 2) a click
  does ten steps.
- **Costs**, with the game's fixed point (× 256 / d, × m, >> 8):
  - An attribute from v to v + 1 costs (v × 256 / 100 × 80 >> 8) + 40, within
    10..75 (`0x38B8A`). Not enough points: nothing happens, with no message.
  - It can't go below a skill it governs (`0x573A8`): "You cannot decrease
    this below the highest dependent skill." (565).
  - Wounds w to w + 1 cost (w × 256 / 200 × 50 >> 8) + 40, within 50..200
    (`0x37FDE`).
  - A skill v to v + 1 costs (m × (v × 256 / 100) >> 8) + 10, with m 50
    primary, 60 major, 70 minor. Skills go from 5 to 60 and not above their
    attribute (`0xE8B39`): "Skill cannot be increased higher than your
    agility." (525 and 527 + attribute).
  - Lowering gives back what raising to there cost.
  - "You do not have enough points." (431) otherwise.
- **The skill list** (`plst00.bsi` at (143, 117), buttons `0xEEC63`):
  - The skills not yet taken, but not Medical, Lockpicking or Running
    (`0x3B8BE`, `0xEE5AC`).
  - A text box (172, 143)–(406, 270) with arrows (436, 131)–(456, 170) and
    (436, 249)–(455, 287). Okay (172, 293)–(266, 331), Cancel
    (301, 293)–(396, 331).
  - Okay (`0x38618`) costs nothing:
    - A skill put in an empty slot starts at 30 (primary), 20 (major) or
      15 (minor).
    - One put in place of another takes that one's value, and the other
      goes back to 5.
- **Spells** (`char02.bsi`, `0xEE858`):
  - The known spells "%d - %s" (cost × 8, name) in (388, 121)–(583, 289),
    arrows (599, 108) and (599, 259).
  - Add (373, 343)–(469, 379) opens `char03.bsi` (`0xEE8B4`). Delete
    (499, 343)–(597, 378) gives back the cost (`0x395B5`).
  - The add page lists "%d - %s [%c]" (the school's letter from "ARDMTIP")
    in (382, 109)–(584, 200). These are the table's spells but Dispel Sigil
    and those known (`0x2A3AC`).
  - Its description is TEXT.RSC 200 + the spell's number, wrapped at 201
    and centred on x 484 from y 287.
  - Okay (373, 375)–(470, 415) adds the spell for 8 × its cost (`0x396AA`).
    Cancel (504, 375)–(596, 411) removes those added since the page opened
    and gives their points back (`0x39809`).
- **Equipment and magic items** (`char05.bsi`, `0xEE8F9`; `char07.bsi`,
  `0xEE983`): one item at a time.
  - Previous (612, 107)–(634, 147), Up; Next (612, 227)–(639, 267), Down.
  - Select (373, 386)–(470, 421), Enter, buys it: flag 0x10 (`0x39872`).
    Deselect (500, 386)–(598, 421), Delete, gives it back.
  - The item turns in (384, 128)–(592, 240), as the inventory shows items.
  - Its two inventory lines are in font 8 at (400, 130) and
    (400, 131 + line height), with "Cost: %d" (513) at (400, 150).
  - `slct00.bsi` shows over it when it isn't bought; `dlct00.bsi` and
    `brdr02.bsi` when it is.
  - The description is wrapped at 201 and centred on x 484 from y 307:
    - equipment: TEXT.RSC 100 + the base;
    - magic items (`0x3B9D5`): 300 + a potion's type, 401 + an enchanted
      item's place in the list at `0xEE592`, else 200.
  - Equipment costs (`0x398D9`):
    - Clothes nothing.
    - Sacks and chests (16–18) 10.
    - Anything else condition maximum / 5 (10 if that's 0) + 50 × material.
  - Magic items cost (`0x39AEF`):
    - Potions 60.
    - Bases above 14 that aren't armour (`0x54522`) 500.
    - Anything else 500 + 50 × material.
- **Advantages and disadvantages** (`char08.bsi`, 26 buttons at `0xEEA0D`):
  - Acute Hearing 200, Spell Absorption 2500, Rapid Healing 700, Regenerate
    Health 2000, Regenerate SPs 2000, Athleticism 400, Adrenaline Rush 200.
    - These are rows 16 high from (64, 128), 193 wide, toggled.
  - Increase Magery × 1, × 1.5, × 2, × 3 costs 300, 1000, 1800, 2500 in all
    (`0x3A022`, `0x3A0FA`).
    - Down (214, 240)–(270, 256), up (271, 240)–(325, 256).
    - Its label "1 x INT" … "3 x INT" is centred on (248, 242).
  - Resistance (64, 304)–(288, 336) costs 800 an element. Immunity
    (64, 384)–(288, 416) costs 1600.
  - Low Tolerance (352, 304)–(576, 336) gives 800 an element. Critical
    Weakness (352, 384)–(576, 416) gives 1600.
    - The element is (x − x0) / 48 if (x − x0) mod 48 < 33, its bit
      1 << that.
    - The game warns of too few points but takes them anyway (`0x39BCC`).
  - No regeneration of spell points (352, 80)–(560, 95) gives 1000. No
    Shield (352, 96)–(560, 111) costs 300; there are no shields.
  - Forbidden Armor (down (352, 127)–(368, 143), up (560, 127)–(576, 143))
    is worth −2000, −1500, −1000 or 0 for none … heavy (`0xEE5B3`).
    - Its name ("No armor allowed" … "Heavy Armor", 135–138) is centred on
      (464, 129).
  - Forbidden Material (down (352, 159), up (560, 159)) is worth −2000,
    −1800, … −400, 0 for Iron … Daedric (`0xEE5C3`).
    - The material's name is centred on (464, 161).
    - Lowering either gives the difference; raising it back costs it.
  - Forbidden Weapons, five boxes 32 square from (352, 224), 48 apart, give
    400 (short blades), 800 (long), 600 (blunt), 400 (axes) and
    1000 (missile).
  - What's chosen shows as `char09.bsi`'s pixels in its box (`0x3A930`).
  - What's excluded shows as `char10.bsi`'s (`0x3AEE1`):
    - Resistance, immunity, low tolerance and critical weakness to one element
      exclude each other.
    - Regenerate SPs and no regeneration exclude each other.
- **Done** (`0x3923A`) refuses with "You cannot exit while you have a
  negative number of build points." (131), "You need to name your class."
  (132) or "You must pick all twelve skills." (133). Then it finishes
  (`0x37508`):
  - wounds maximum and current = base;
  - spell points = Intelligence × magery >> 8;
  - the class fields copied to their base copies (+0x1B2);
  - points left (+0x20A) and level kept.
- **Cancel** asks "Are you sure?" (`sure00.bsi`) and goes back to the class.
- **Save** (`0x3B39C`) writes the class as the lowest free `class%03d.scl`:
  "Saving out your level %d class..." (515), "Your level %d class has been
  saved." (134). One with the same name and level is replaced if the player
  says so (`exist0.bsi`). The items bought go in it flagged 0x10
  (`0x3BC97`). Details in [../formats/scl.md](../formats/scl.md#writing-the-charactermakers-save).
- **The race's skill bonuses** (`0x3BA58`) are a permanent effect added
  once to the skill bases when the game starts:
  - Redguard: Long Blade, Short Blade and Missile +10; Axe, Blunt Weapon and
    Critical Strike +5.
  - Breton: Destruction, Mysticism and Illusion +10; Alteration, Thaumaturgy
    and Restoration +5.
  - Nord: Axe, Blunt Weapon and Critical Strike +10; Long Blade, Short Blade
    and Missile +5.
  - High Elf: Long and Short Blade +10; Mysticism, Alteration, Thaumaturgy,
    Restoration and Missile +5.
  - Dark Elf: Alteration, Thaumaturgy and Restoration +10; Destruction,
    Mysticism and Illusion +5.
  - Wood Elf: Missile, Jumping and Dodging +15.

  The descriptions (TEXT.RSC 1076–1081) list the same, except the High
  Elf's: it says Illusion +5, where the code adds Missile +5.

The shipped classes check out against these costs; see
[../formats/scl.md](../formats/scl.md#the-build-points).

In the engine: `bs_formats`' `scl` reads class files, `bs_game`'s `creation`
has the costs, choices, shops and the finished character, and the app's
`creation.c` has the screens, Load Saved Game's among them, and Save (2026-11-01). Not done: the help
window's growing.

**The advantages in play** (2026-10-19; static analysis, every read of the
bytes):
- **Acute Hearing** (+0x194): sounds carry 1024 / 768 as far (see
  [Sound effects](#sound-effects)).
- **Spell Absorption** (+0x195): see [spells.md](../formats/spells.md).
- **Rapid Healing** (+0x196): healing counts twice.
- **Regenerate Health and Spell Points** (+0x197, +0x198): a point every
  5 s (see [Levelling](#levelling-2026-10-17)).
- **Athleticism** (+0x199): faster walking and jumping (see
  [Player movement](#player-movement-and-collision-found-2026-10-13)).
- **Adrenaline Rush** (+0x19E): +15 to hit (see
  [Levelling](#levelling-2026-10-17), "Seen in passing").

The engine has all seven: `bs_level_audio` places and follows the sounds,
`bs_player_move` takes Athleticism, and `bs_melee_roll` Adrenaline Rush.

## Saving and loading (2026-10-15)

Static analysis; the files' layout is in
[../formats/save.md](../formats/save.md).

- **Saving** (`0x30307`, slot n):
  - `save%d\` is made.
  - The files are written in this order: the name (`0x301EE`), the view's
    80 × 50 picture (`0x30276`, 8000 bytes from `0x16C080`), SAVEVARS.DAT
    (`0x30A7D`) and SAVETREE.DAT.
  - SAVETREE.DAT is the version 0x100, the objects from the root's first
    child (`0x2FBC4`, each saved one with its subtree, `0x2FB80`), four zero
    bytes and the controllers (`0x4C20B`).
- **Loading** (`0x303FE`):
  - "LoadGame ****" goes to the debug log.
  - The version is read; below 0x100 is "Invalid save game format."
  - The world is cleared (`0x43CFD`): the Teleport anchor, the timed effects
    and the active controllers. A new player object is made (`0x312CA`, id
    50000).
  - SAVEVARS.DAT is read (`0x3078C`), which sets the level number, and the
    level is loaded (`0x22ECC`).
  - The spawn points' monsters are removed. Only the saved ones come back,
    with the hidden reserve in a new container object (type 22).
  - The records are read over the fresh level (`0x2FAE6`), then the
    controllers (`0x4C3D8`).
  - Then the rest is set up again:
    - an object whose record id is at player +0x298 (`0x16C1AC`; unknown);
    - a new type-6 object of 0x6F bytes (`0x16C1F8`) and `0x30139` run on
      every object (unknown);
    - while `0x16C008` is set, `0x492C1` with a name from the table at
      `0xE34AD`, picked by the weapon's base;
    - the weapon in hand, equipped again from +0x260 (`0x48DF3`).
- **Record ids** decide what a record replaces:
  - the BS6 entity id for placed models;
  - 50000 for the player;
  - `kind << 16 | n` for other new objects, with n from 10000 + 5000 × the
    network player's number when a level starts (`0x22E07`).
- **Variable names** are hashed by `0x1BAE9` (rotate left 4, add the
  uppercased character), as the dialogue tables use them. PCMale, PCFemale
  and SkelServe are set when a game starts (`0x5AE3D`).

Checked against three saves made in level 1. A level object's +0x27 is its
controller chain's number, the order `bs_level_logic` builds them in. The
hidden reserve monsters' parent ids are stale: loading ignores them.

More of loading (2026-10-16):
- **Which level:** the level loader (`0x22ECC`) takes the level by the saved
  number unless a door has named another (`0x16C27C`), which loading leaves
  clear. The level brings its interiors, so a save made in one comes back
  there: the player's +0x298 names the space's door, and loading makes it the
  current space (`0x16C1AC`, `0x3065C`). See
  [save.md](../formats/save.md#interiors).
- **Items** aren't removed before the records are read. Every item is saved,
  and the fresh level's placed items have the BS6 ids their records carry,
  so those are replaced. Container contents are new objects with counted ids
  (`kind << 16 | n`); loading relies on the fresh level counting the same.
- **Monsters carry their loot:** each saved monster has a Sack child with its
  items (the samples' also hold a Sigil Amulet, 9030).
- **The load screen** (`0x31104` reads the slots, `0x325B9` draws, buttons
  `0xEDC52`, `0x32868` picks a slot): the layout is in
  [../formats/save.md](../formats/save.md#loading-into-the-engine). Okay or
  a double click on a slot with SAVENAME.DAT (`0x31468`) shows
  `loading.bsi` and loads (`0x303FE`).

In the engine:
- `bs_formats`' `save` reads all four files. `bstool save` prints a slot's
  records, variables and log, writes its picture, and diffs two slots.
- `bs_game`'s `save_import` turns a save into the character, items, monsters,
  objects, automap and variables.
- The game loads one over a fresh level (`load_game` in `main.c`), from the
  load screen or `--load n`.
- `bs_game`'s `save_edit` changes an original save field by field
  (`bssave edit`), and `game_save` reads and writes our own text saves
  ([../formats/game_save.md](../formats/game_save.md)), which the options
  bar's Save and Load use (the original's screen, `0x31A62`: see
  [../formats/save.md](../formats/save.md#loading-into-the-engine)).

## Dialogue (2026-10-16)

Static analysis, checked against all 76 conversations in TXT.BSA. The tables
are in [../formats/txt_bsa.md](../formats/txt_bsa.md).

### Who talks

- **Monsters.** The greeting (`0x487B5`) needs a monster waiting to talk
  (state 13) with a dialogue code:
  - the player within 512 units, facing it within 200 angle units, in
    sight;
  - the player able to talk (`0x495C4`): no weapon, or one at rest,
    walking, recovering or held back by a wall.

  Then d100 < 50 starts the conversation, otherwise one of the seven
  greetings (6–12) shows. Either way it happens once (+0x13 bit 0x40).
- **Using a monster** (`0x4D005`): with a touch spell ready, the spell goes.
  Otherwise within 384 units it talks, and further off it's "You are too far
  away." (1). Any monster can talk: one without a code uses its level's
  default for its creature (`0x583E1`, table `0xF1CC6`, five per level). For
  example, level 1 has SK1, VM1 and DR1.
- **Using a level object** (`0x4D153`) or **a flat** (`0x4D5A4`) sends its
  use event, then talks if it has a code (`STRU` 108, in the conversation
  map `0xFE684`).
  - The flats are the NPCs: Clarentavious, Sumeer, Rishaal and the others,
    Jaciel, Imago, Dagon.
  - The objects are level 2's talking gems, Barak, l6war2.
  - The flat's distance test (a float against 100) isn't decoded. The engine
    uses 192.

### Starting (`0x584C6`)

1. **The files.** The stem finds the TXT.BSA names `<stem>*.txt`. The fourth
   letter T is the script; M or F (the player's sex) or B is the opening.
   The last match in the archive's order wins.
2. **The speaker's record.** It is found among 256 local records by id, or
   the last free one is taken (they fill from the end).
3. **The variables that follow the player** are set first (`0x59935`, also
   every frame of the screen):
   - from what is carried (table `0xF185C`, 26 rows: two items each, both
     needed; unnamed rows join the next): Tayem, Cess, Payem, Iya, WarAxe,
     Dagger, Reiver/NotReiver, Case, WearAmuletD/M/R/Z/W/H/O/U, Scourge,
     InvVoidguide1, Armor/NotArmor (the six pieces of the armour of
     protection), Pauldron;
   - PCMale, PCFemale and SkelServe, which is always false (`0x5AE3D`).
4. **`talkcheck`.** Every word its conditions name becomes a variable of the
   speaker unless it's global (`0x58B17`). After AND, OR or NOT the next word
   is taken whatever it is. Then the first condition that holds gives the
   line.
5. **Otherwise `talktable`.** The row after it, plus the number of times this
   creature type has been talked to (monsters only, `0x401D44`, at most 3).
   `GotoRandom` there rolls 1–100 against the rows from two after `random`.
6. **The visit count** of a monster's creature goes up, to at most 3.
7. **The line.** If it starts with `[` (after quotes), it's narration: shown
   as a message without its brackets, and no conversation.
8. **The screen opens** (`0x58043`), with help page 22 if help pops up by
   itself.

The variables' hash is `0x1BAE9` (see "Saving and loading"). A variable is
looked up among the speaker's eight, then the globals (`0x59457`). Setting
one (`0x595AC`) sets the speaker's if it has it, else a global, added if
new.

### The screen (`0x58043`, frame `0x58226`, draw `0x5922C`)

- **The picture** is the talking head, a 640 × 480 FLC that has the room,
  the scroll and the buttons too:
  - first `<XX>talk00.flc`;
  - then, whenever one ends, a random talk animation while the voice plays,
    else a random wait animation;
  - the counts are found by `<XX>talk0?.flc` and `<XX>wait0?.flc`
    (`0x5A6D6`).

  XX is a monster's creature code (`0xF1C86`), or the stem's two letters.
- **The voice** (`0x1CE6A`) is `<say code>.wav`, played when a line appears,
  except lines in `[[ ]]` and Jaciel's jm1a11 and jm1a21. Without sound, or
  if the file isn't there, the speaker talks for 4 s.
- **The line** is in font 9, wrapped at 639 and centred on x 320 from y 5.
- **The replies** are in font 8, wrapped at 300, centred on x 480, from y 100.
  Each starts two line heights after the last one's last line. The one under
  the mouse is framed in 0x6000 (`0x5AD2C`).
- **The scroll** (font 6, wrapped at 456 from (272, 398)) shows the history:
  the opening line, then each reply chosen (`0x5ABF0`). Its arrows
  (537, 386)–(558, 427) and (537, 432)–(558, 472) step back and forth.
- **Exit** (584, 425)–(631, 474), or Esc, leaves only when there are no
  replies left (`0x58324`). Until then `covr00.bsi` covers it.
- **When the conversation is over**, "[ END OF DIALOG ]" (123) is at
  (320, 15) and the message is "Conversation is done. Click exit to
  continue." (118).

### A reply (`0x58359`, `0x59BFD`)

1. Its text goes into the history.
2. SET1 is set.
3. Its actions run (the table in txt_bsa.md, `0x59D62`). The monster states
   are written to +0x26F:
   - SUMMON and MONSTERRALLY make monsters through `0x47B33` (not decoded:
     the engine puts them around the speaker);
   - TELEPORT n > 0 puts the speaker in an 8-slot table (`0x100B84`) to
     vanish later;
   - DIALOGLINK n uses every object in the table of `STRU` 104 values
     (`0x100BE4`) with n.
4. GoTo gives the next line. A d100 roll against its ranges if it has a ':'
   (`0x59168`); `end` ends it. If no range holds the roll, the game reads
   past the cell; the engine ends the conversation.

The conversation ends (`0x5817E`):
- KILL's speaker gets flag 2 (gone).
- A stem starting with "ZZ" (Dagon) sets the capture time, player +0x2DA,
  to now + 3 s: unless the winning blow comes first, the captivity ending
  (see "Videos").
- ENDFRAME n (`0x5A386`) ends the conversation, then plays CHOP2 with text n
  at once.

In the engine:
- `bs_formats`' `talk` reads the tables.
- `bs_game`'s `dialogue` keeps the variables and visits as SAVEVARS has them,
  and runs the conversation.
- The app's `talk.c` is the screen, and `main.c` applies the actions.

Not done:
- PCTEAMALL's exact effect (the tables don't use it);
- where summoned monsters appear;
- the flats' use distance;
- the help page (22) popping up;
- multiplayer's refusal (408).

## Levelling (2026-10-17)

Static analysis. Battlespire has no experience. Finishing a level gives
points to spend on the character sheet, and skills rise as they're used.
In the engine this is `bs_game`'s `levelling` (`bs/game/levelling.h`), with
the screen in the app's `menus.c`.

### The level-up screen

**Completing a level** (`0x268A1`), after `JUMP.SMK`, opens the character
sheet (`0x558DC`) with a number of points from a table at `0xED524`, by
the campaign level's number:

| Level | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| Points | 600 | 800 | 1100 | 1400 | 1600 | 1900 |

(Entry 0 is 10000 and 7 is 0; neither is used.)

**Opening the sheet with points** makes it the level-up screen (flag
`0x3FC1EE`):
- **The picture** is `cscr01.bsi`. It has arrows beside the attributes and
  the skills, and a Points bar where cscr00 has Automap and Inventory.
- **Points to spend** are the given ones plus the player's +0x20A, which
  holds what was left over last time; at first that's the CharacterMaker's
  points left (`0x37387`, from `0x3F5898`). Closing the sheet (`0x55ADC`)
  puts what's left back in +0x20A.
- **Health:** the base health (+0x6E) becomes +0x6E × 307 / 256, and
  current, maximum and base all take that value. Spell points are filled
  (+0x60 = +0x62).
- **Floors:** the base attributes (+0x40) are copied to `0x3FB2A4`, the
  floor for lowering them. The skill list (`0x55E04`) keeps each skill's
  value when it opened, its floor too, in a table at `0x3FBD90` (12 bytes:
  floor, skill, factor).
- **Spell points follow Intelligence:** every frame the draw (`0x55D5E`)
  sets current, maximum and base to base Intelligence × magery
  (+0x19A) / 256.
- **Points left** are drawn centred at (416, 416) in font 1.
- **Disabled:** Automap and Inventory do nothing (`0x56121`, `0x5614D`),
  and F9 doesn't close it; Esc and Exit do.

**An attribute's button** (`0x562D0`; 560, 31 + 50 n – 617, 70 + 50 n)
takes one step, or ten with Shift (`0x16FFCB` bit 2):
- **Cost:** raising from v costs ((v × 256 / 100) × 50 >> 8) + 40, about
  40 + v / 2 (`0x56453`). Lowering to v gives the same back.
- **Upper or lower half:** a click above the button's middle raises the
  attribute while it is below 100 and the points are there. Any other
  click lowers it, provided it's above its floor.
- **A quirk:** a click on the upper half without enough points, or at 100,
  also lowers the attribute (when it has been raised), since the game falls
  through to the lowering branch. The engine copies this.
- **Dependent skills:** an attribute can't go down while a skill it governs
  is as high (`0x573A8`, the skills' attributes at `0xE8B39`): "You cannot
  decrease this below the highest dependent skill." (message 565).
- **Effects:** the attribute with effects (+0x20) is set to the new base.

**A skill's arrows:**
- **Where:** the strips left (239–254) and right (503–518) of the skill
  list, y 126–269. The row is (y − 126) / 16 plus the scroll.
- **Raising** (right, `0x57074` → `0x57238`), one step or ten with Shift:
  - while the skill is below 100 and below its attribute's base;
  - otherwise "Skill cannot be increased higher than your" and the
    attribute's name ("%s %s.", messages 525 and 527 + attribute);
  - it costs ((v × 256 / 100) × factor >> 8) + 10;
  - without the points, "You do not have enough points." (431).
- **Lowering** (left, `0x56F5A` → `0x57184`) goes down to the floor and
  gives back the cost of the step.
- **The factor** comes from the skill's place in the class's list
  (+0x1A6): 50 for places 0–2, 60 for 3–5, 70 for 6–11, and 80 for skills
  the class doesn't list. So the fourth major skill costs as a minor one
  does.
- **Colour:** a raised skill's line is drawn green (font 4), the others in
  font 3.

### Skills rising with use

**Counting** (`0x1E4FB`): each skill's 12-byte record (+0x72 + 12 n) holds
the value with effects, a use count (+0x76) and the base (+0x7A). A use
adds to the count, kept within 0..20000. The callers, all adding 1:

| Where | Skill | When |
|---|---|---|
| `0x2752B`, `0x29713` | the spell's school (`0xDA60A`: Alteration 11, Restoration 9, Destruction 8, Mysticism 13, Thaumaturgy 12, Illusion 10) | the player launches a missile, or a spell takes effect from the player: on themselves, by touch, or a potion drunk (`0x5042C` passes the player as caster) |
| `0x40B96` | Jumping | a jump |
| `0x424CB` | the weapon's skill (the weapon table's +0x14) | each of the player's blows at a monster, hit or miss |
| `0x4284B` | Dodging | each blow at the player |
| `0x429A3` | Critical Strike | each of the player's blows that hits |
| `0x42A38` | Backstabbing | blows with the blow function's stack argument set: every caller passes 0, so never |
| `0x49C6F` | Missile | each arrow shot |
| `0x40CD8` | Stealth | the 5 s tick, while sneaking and having moved |
| `0x40D00` | Swimming | the 5 s tick, while in water and having moved |

**Rising** (`0x1E3DF`):
- **When:** the main loop calls it on every frame but each 32nd
  (`0x13272`).
- **Base to current:** it first copies every base skill (+0x7A) to +0x72,
  so effects don't change skills.
- **The threshold:** a skill's count must reach
  trunc(1 + v × rate × 2.0 / 5.0) (`0x1E576`, truncating). The rate is a
  16-bit table at `0xED4E3`:

  | Skill | Rate |
  |---|---|
  | Medical | 12 |
  | Running | 50 |
  | Critical Strike | 4 |
  | Swimming, Backstabbing, Destruction, Illusion, Alteration, Mysticism, Missile | 1 |
  | the rest | 2 |

  For example, Long Blade at 50 rises after 41 blows.
- **The rise:** the count starts again from 0. The base goes up by one only
  if the value is below 95 and below its attribute's base: "%s skill has
  increased..." (message 88).

**The installed saves agree:**
- SAVE2's player has counts 11 for Long Blade and 10 for Critical Strike
  (blows, and hits).
- SAVE1's has Dodging 3, Restoration 4, Long Blade 4 and Critical Strike 3.

### Every 5 s

The loop calls `0x40C9A` when the clock (`0x421FBC`) enters the first
second of each 5 (now mod 5000 < 1000, the last frame's ≥ 1000; at
`0x13213`).
- **Stealth and Swimming:** it counts the two as above. "Having moved" is
  `0x3F7C28`, which `0x41759` raises when the player is 128 units from
  the point last marked (`0x3F7BD0`, also what the automap is revealed
  from); the tick clears it.
- **Regeneration:** a character with Regenerate Health (+0x197) gains one
  health below the maximum, and with Regenerate Spell Points (+0x198) one
  spell point.
- **Sneaking:** `0x40D87` sets the "hidden" flag `0x16FFD2` while sneaking
  (+0x226 bit 0x40). Moving, it is meant to be hidden only on a d100
  below Stealth, but the code compares the roll with the address of the
  skill (player + 0x96), so it is always hidden. Stealth therefore does
  nothing.
- **Also there:** a 1-in-13 call, `0x5555B`, that only works in
  multiplayer.

### Seen in passing

- **Adrenaline Rush:** the blow (`0x4285B`) adds 15 to the player's chance
  to hit while their health (+0x66) is at most maximum (+0x6A) × 25 / 256
  and they have Adrenaline Rush (+0x19E). It comes after the dodge and
  before the 5–95 limit; arrows and spells don't get it.
- **Another halving:** a blow at the player has its chance halved while
  `0xDA610` is 0x23 (not traced).

### In the engine

- **The screen:** after `JUMP.SMK` the next level begins with the level-up
  screen. `--level-up N` opens it at the start with level N's points.
- **Counts and points:** the counts and +0x20A are kept in our saves
  (`skill_uses`, `points`) and imported from the original's. The skills
  imported are the base values.
- **Approximations:**
  - "having moved" is measured on the ground plane;
  - the hidden flag isn't used.

## Swimming, boats and the balloon (2026-10-17)

Static analysis of the movement step (`0x3FDA0`), its speeds (`0x3F4BD`,
`0x3F654`), the vertical step (`0x3FB10`), the collision (`0x2C299`) and
the objects that can be ridden. What the M5 notes called levitation
(`0x16C214`) is riding the balloon; `0x16C064` is riding a boat. There is
no levitation spell. In the engine: `bs_game`'s `player` (water and
riding), the app's `main.c` (boats, the balloon, breath).

### Water

**Where water is:**
- A level has water when its `LEVELS.TXT` mode[1] is `Y`. Its surface is
  the BS6 `WATR` height (`0x262AC`): campaign levels 3, 4, 5 and 6.
- Otherwise `0x16C23C` is 10000, and level 1 never has water.

**In the water** (`0x3FB10`, for monsters too):
- **When:** a mover is in the water while its feet are 50 or more below
  the surface (y ≥ surface + 50, +y down).
- **Falling in:** nothing falls in water. A mover that wasn't in the water
  is put exactly 50 below the surface, with sound 86 (a splash).
- **Too much to carry:** carrying more than 3 × Strength in half units
  (1.5 × Strength in units, `0x5493B`) shows "You carry too much stuff to
  stay afloat!" (message 14) at the splash.
- **Slow Fall:** "Slowfalling active." (13) is the message when Slow Fall
  first holds a fall.

**Up and down** (`0x3FE57`):
- **Rising:** Page Up (action 21) rises at 100 units/s while the feet are
  more than 50 below the surface. Above that the swimmer leaves the water,
  falls back and splashes again.
- **Sinking:** Page Down (22), or carrying too much, sinks at 100 units/s
  unless something is stood on. In level 5 sinking stops 256 below the
  surface.

**Speeds** (`0x3F4BD`):
- **Halved:** swimming halves the walking speeds after Sneak Mode's
  halving.
- **The Swimming skill:** half the skill (+0xA2) is then added to the
  forward step of each frame, in units, not units per second.
- **Frame steps:** the movement splits a frame into steps of about 15
  units (`0x3FCF0`: the frame time × 15 / the step's length). Each step
  gets the bonus again, so the swimming speed depends on the frame rate.
- **The engine's assumption:** it assumes the game's 20 frames a second.
  With Speed 50 that's 140 units/s with skill 5 (the shipped classes have
  about that), and 1080 with skill 50.

**Climbing out** (`0x2C299`):
- **The floor probe:** for a swimmer at the surface (feet exactly 50
  below), the floor is looked for from 110 above the feet, not 32
  (`0x2C64C`).
- **Which floors:** a swimmer, a jumper, or one over deep water (feet at or
  above 50 below the surface, the floor more than 50 below it) stands only
  on floors the feet reach, never snapping down (`0x2C67C`, `0x2C739`).
- **Climbing:** when the body is blocked while swimming, the move is tried
  once more before sliding along the wall. It goes to a point 35 units
  further on (`0x40215`, kept at `0x16C1E0`), with the floor found first and
  the body tested there. A ledge up to 60 above the water is climbed that
  way.
- **No jumping:** a swimmer can't jump, since jumps need a floor (and
  `0x4036B` clears the jump).

**Breath** (`0x1E5E8`, every frame):
- **Above the water:** while the view is above the surface, the player's
  +0x2C0 is kept at the clock.
- **Under it**, or while +0x2C0 is ahead of the clock, the breath left in
  half-seconds is (Endurance × 1000 − (now − +0x2C0)) / 500.
- **The bar:** while that is at most 500 it is drawn as a bar 7 pixels wide
  at x 610, from y 220 up. It is green (`0x03E0`), red (`0x7C00`) under 10.
- **Drowning:** at 0 the player dies (`0x16C258`, as at 0 health).
- **Water Breathing** (its potion sets +0x2C0 = now + 5 min) keeps the
  breath full, and the bar shows while it lasts, above the water too.

**The view under water** (`0x110A8`):
- **When:** with the camera below the surface.
- **The haze:** the game loads `haze.666` and sets the view distance to
  1024, the fog's start to 8 (as "C" levels) and its colour to RGB555
  `0x109A`. Above the surface again, the level's are restored.
- **In the engine:** it uses `haze.666`'s full-fog colour, with the
  distances scaled as for "C" levels (see "Lighting").

### Boats

- **Which:** in single player every placed `boat.3d` and `boat2.3d` can be
  ridden (`0x23DD5` marks their models, object flag +0x13 bit 0x10). There
  is one in each of levels 3 and 4, and four in level 5.
- **Boarding:** using one (`0x4D1F9` → `0x4D84D`) puts the player where it
  is, facing its yaw. It becomes the player's child, so it saves as such.
- **Leaving:** using it again, or action 6 (D, `0x41178` → `0x4D907`),
  leaves it in the level where it is.
- **Steering** (`0x3F19F`): the sideways keys turn the boat and the view
  together at half the turning keys' rate. There is no sideways step.
- **Travel:** Forward and Back move along 0x7FF − the boat's yaw, the
  mirror of its yaw (`0x3F6A6`), at the walking speeds.
- **Facing the stern:** objects are drawn turned by −yaw, so that is the way
  the boat's model points. The view, which took the boat's yaw, faces the
  other way unless the boat lies north–south. The player rows facing the
  stern.
- **No falling:** nothing falls while riding (`0x3FC92`).
- **Collision:** the boat's spheres (`0xED77E`: 10 above the feet, 90
  ahead and behind with radius 35, 20 in the middle) are tested at the new
  place, turned with the boat.
- **Blocked:** touching anything, nothing moves, and the turn is undone
  (`0x2C4C5`, `0x40397`).
- **Rocking:** every boat rocks (pitch) 10 either way over 2 s (`0x4DAA1`,
  `0x4DB75`). The one ridden follows the player.

### The balloon

- **Which:** level 5's `l8balon1.3d` (`0x240CC`, `0x4D22D`). Using it boards
  it (`0x4D984`) without changing the view; using it again, or D, leaves it
  (`0x4DA13`).
- **Moving:** Page Up rises at 160 units/s. Page Down sinks at 160 while
  nothing is stood on and the feet are above the water. Moving goes at half
  the walking speeds.
- **No falling:** nothing falls while riding.
- **Collision:** its spheres (`0xED7C8`: the envelope, radius 700 centred
  700 above the feet, and the basket, 50 at 50) stop all movement when they
  touch anything.
- **Going home** (`0x21A1x`; its start is kept at `0x16C290`):
  - leaving it starts a 20 s wait (+0x2BC), and standing on it starts it
    again;
  - after that, away from where it began, it rises at 150 units/s until
    3000 above the player;
  - then it jumps to 3000 above its start and sinks back there.

### Saves

- **The original's:** the object being ridden is the player's child.
  OpenBattlespire's importer reads it that way.
- **Ours:** the object's id is `ride` in `[player]`. Boats and the balloon
  that have moved are `[object]`s.

### Not done

- What `0x16FFB0` is: it halves the speeds like swimming, but nothing sets
  it.
- Multiplayer's boat messages (517, 518).
