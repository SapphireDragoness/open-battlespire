# OpenBattlespire — Project Plan

A from-scratch, open-source reimplementation of the engine for *An Elder Scrolls
Legend: Battlespire* (Bethesda, 1997), in the spirit of Daggerfall Unity and
OpenMW. Written in C, with no Unity or other commercial engine.

## 1. Goals and ground rules

**Goals**
- Play the full Battlespire campaign (7 levels) on modern Linux, Windows and macOS
  using the data files from a legitimately owned copy.
- Match the original's gameplay closely by default. Optional improvements
  (higher resolution, widescreen, mouselook, true-colour rendering, fixes for
  original bugs) are opt-in settings.
- Keep the file-format code reusable on its own, as a library plus command-line
  tools, so modders and other projects can use it.
- Document every format and behaviour we work out in `docs/`.

**Non-goals (for now)**
- Network multiplayer (deathmatch / CTF). The design shouldn't rule it out (see §4.5),
  but it's not planned until single player is complete.
- A level editor.

**Legal ground rules**
- **Never commit game data.** The engine reads assets from the user's install at
  runtime. The repo must not contain any extracted asset: no textures, models,
  sounds, text or level files, and no test fixtures made from them. Tests that
  need real data look for the install through an environment variable and are
  skipped when it isn't set.
- Reverse engineering `GAME.EXE` is done for interoperability only. Notes go in
  `docs/re/` as prose and pseudocode describing behaviour, never as pasted
  disassembly or decompiler output.
- Third-party code must use a compatible licence and live in `third_party/`
  with its licence file. Projects such as ariscop/battlespire-tools are used as
  references for understanding formats; don't copy code from them unless their
  licence allows it.
- Proposed project licence: **GPL-3.0-or-later** (as OpenMW uses) or **MIT** (as Daggerfall Unity uses). *Decision needed.*

## 2. What we already know (verified 2026-10-10)

The source is the GOG release (DOS v1.5 running under DOSBox) at
`~/Games/Battlespire`.

| Item | Location | Notes |
|---|---|---|
| Executable | `GAME.EXE` | Watcom C/C++32, CauseWay DOS extender, LE format. Uses RAD Game Tools (Smacker) and Miles Sound System. |
| Archives | `GAMEDATA/*.BSA`, `3D.BS6` | Header is `u16 count, u16 type (0x0100 = named)`. The directory sits at the **end** of the file, with 18-byte entries: `char name[12]; u16 flags; u32 size`. Records are stored one after another starting at offset 4. `flags & 0x0100` = Okumura LZSS-compressed (ring buffer filled with spaces); verified. Type `0x0200` (numbered, 8-byte entries) is used by `SPIRE.SND`. See `docs/formats/bsa.md`. |
| 3D models | `3D.BSA` (2400 entries), `3D.BS6` (2115), loose `*.3d` | Version tag `v2.7`, the same one Daggerfall's `ARCH3D.BSA` uses. |
| Textures and sprites | `BSI.BSA` (2599) | Chunked format with big-endian lengths: `IFHD BHDR HICL HTBL CMAP DATA …`, close to Redguard's TEXBSI. Some records are compressed. |
| Levels | `BS6.BSA` (47) | Chunked format with little-endian lengths and a top-level `GNRL` container holding `TEXI`, `DIRN`, …. Includes unused or old maps (`L8OLD`, `L12`, `L4_OLD`, interiors `IN*.BS6`, `BACKUP`, `FIXIT`) plus a stray C source file named `C`. |
| Level table | `LEVELS.TXT` | Plain text: level file, ID, mode flags, sky, haze table, view distance, node file, name, and more. |
| AI navigation | `NODES.L*`, `NODES.M*` | Probably the pathfinding graphs, one per level. |
| Text and dialogue | `TXT.BSA` (254), `TEXT.RSC`, `*.txt` | **Windows-1252** text. The talk tables are dialogue scripts with conditions, `&variables` and actions (see `docs/formats/txt_bsa.md`). |
| Talking heads | `FLC.BSA` (164) | Autodesk FLIC animations. |
| Sounds | `SPIRE.SND`; `WAVES.BSA` (only inside the CD image `game.gog`, 104 MB) | Complete RIFF WAV files (8-bit mono 11025 Hz), not headerless PCM as UESP says. |
| Cutscenes | Inside the CD image: `videos/*.SMK` | Smacker video. |
| Music | `MUSIC/Track02–08.ogg` | Ripped CD audio. |
| Palettes and lookup tables | `PAL.RAW`, `LOWCOLOR.COL`, `TINT.*`, `HAZE.*`, `WATER.*`, `LAYER.DAT`, `FADE.DAT`, `SPIRE.H2L` | Colour-mapping tables for the 8-bit software renderer (lighting, fog, water, translucency). |
| Classes | `CLASS000–017.SCL` | Character class definitions. |
| Fonts | `FONT0001–0010.FNT` | |
| CD image | `game.gog` + `game.ins` (a CUE sheet) | Raw MODE2/2352 sectors. User data is at offset 24 in each sector, followed by a standard ISO 9660 (+UDF) filesystem. |

**Prior work to consult**
- [UESP: Battlespire file formats](https://en.uesp.net/wiki/Mod:Battlespire_File_Formats),
  [save format](https://en.uesp.net/wiki/Battlespire:Save_Game_Offset_List),
  [technical info](https://en.uesp.net/wiki/Battlespire:Technical_Information),
  [hacking notes](https://en.uesp.net/battle/bathack.shtml)
- [ariscop/battlespire-tools](https://github.com/ariscop/battlespire-tools): decoders for BSA, BSI, 3D and BS6
- Daggerfall Unity's `DaggerfallConnect` readers (MIT licence) for the shared XnGine formats
- [TCRF](https://tcrf.net/An_Elder_Scrolls_Legend:_Battlespire) for unused content and the known bug list

## 3. Technology choices

| Concern | Choice | Why |
|---|---|---|
| Language | **C17**. Compiles with GCC, Clang and MSVC. | User choice. Simple, close to the original code. |
| Build | **CMake ≥ 3.25** | Cross-platform, with CTest for tests. |
| Platform layer | **SDL3** (window, input, audio, timing, file dialogs) | Installed (3.4.x), with a C API. |
| Graphics | **OpenGL 3.3 core**, with our own small loader for the functions we use (`bs/render/gl.h`, loaded through `SDL_GL_GetProcAddress`) instead of glad | Simplest portable choice in C. The renderer is kept behind an interface (`bs_render`) so an SDL_GPU/Vulkan backend can be added later. |
| Music | **stb_vorbis** (vendored) | For the `.ogg` CD tracks. |
| FLIC and Smacker | **Our own decoders** | Both formats are documented and small. This avoids a dependency on ffmpeg/libav. |
| Image export (tools only) | stb_image_write | PNG output for verifying decoders. |
| Model export (tools only) | Our own minimal glTF writer | |
| Debug UI (optional) | Dear ImGui through cimgui, behind `-DBS_DEV_UI=ON` | Its core is C++, so it's an optional build flag and never required by the game itself. |
| Everything else | Our own code: math, collision, UI, scripting | Full control, as requested. |

**C coding conventions** (to be written up in `docs/CONTRIBUTING.md`)
- Prefix `bs_`. Modules are `bs_<module>_<verb>`. Types are `bs_<name>` (typedef'd structs).
- **All binary parsing goes through a bounds-checked reader** (`bs_reader`). Raw
  pointer casts on file data are not allowed. Parse explicitly with defined endianness.
- Memory comes from arenas: one per level, plus frame and persistent arenas.
  Avoid scattered `malloc`/`free` for asset data.
- Errors are returned as `bs_result` codes with a message buffer. Never `exit()` from library code.
- Run with `-Wall -Wextra -Werror` in CI, and with ASan/UBSan builds in CI.
- **Fuzz** every parser with libFuzzer/AFL (the parsers handle untrusted binary input).

## 4. Architecture

```
OpenBattlespire/
├── CMakeLists.txt
├── docs/
│   ├── PLAN.md            ← this file
│   ├── formats/           ← one .md per file format (spec, plus status: verified / partial / unknown)
│   └── re/                ← behaviour notes from reverse engineering GAME.EXE (prose and pseudocode)
├── src/
│   ├── core/      bs_core    — arenas, strings, logging, bounds-checked reader, hashing, VFS
│   ├── formats/   bs_formats — pure parsers, no SDL/GL. Depends only on bs_core.
│   ├── assets/    bs_assets  — assets by name (loose files, then archives), caching, level scene building
│   ├── tools/     bstool     — CLI: list/extract/convert/inspect (PNG, glTF, WAV, JSON)
│   │              bsview     — free-fly level viewer (SDL3 + bs_render)
│   │              bsplay     — plays a video, sound or music track
│   ├── render/    bs_render  — GL 3.3 renderer: levels, models, sprites, sky, fog, 2D overlay, full screens
│   ├── audio/     bs_audio   — mixer, 3D positional sound, music streaming, video playback
│   ├── game/      bs_game    — simulation: collision, player, level logic, items, monsters, combat,
│   │                           magic, character creation; saves to come
│   └── app/       openbattlespire — main(), the game loop and the screens: inventory, spell book,
│                                 the bar and its screens, character creation, fighting
├── tests/                    — unit tests (synthetic data) and data tests (need BS_DATA_DIR)
└── third_party/              — stb_vorbis (SDL3 comes from the system)
```

How it turned out (2026-10-15): SDL3 is used directly by the app, the tools
and `bs_audio`, so the planned `bs_platform` wrapper wasn't needed, and the
OpenGL functions are loaded by `bs_render` itself. The screens live in the
app rather than a `bs_ui` library. Each is a module given an environment
struct of what it needs, and the rules behind it (costs, items, spells) are
in `bs_game`, where the tests reach them.

### 4.1 Virtual file system (VFS)
- When the engine starts, it looks for the install: a path from the config file,
  then the `BS_DATA_DIR` environment variable, then a file dialog. It confirms
  the install by checking for `GAMEDATA/LEVELS.TXT` and `GAME.EXE`.
- It mounts, in priority order: a **mods/override folder**, then loose
  `GAMEDATA/` files, then the `.BSA` archives, then the **CD image**. The CD image
  is read directly through the `game.ins` cue sheet, with raw 2352-byte sectors
  converted to ISO 9660. This is how `WAVES.BSA` and the `.SMK` cutscenes are
  reached without the user extracting anything.
- File names are matched case-insensitively. Text data is Windows-1252 (verified in M1).

### 4.2 Formats layer (`bs_formats`)
Each parser is pure: it takes bytes, returns structs allocated from an arena,
and reports errors. Each format gets a spec page in `docs/formats/`.

### 4.3 Rendering
- **Classic mode:** reproduce the 8-bit look. Textures stay palette-indexed
  (`R8`), and the fragment shader applies the original light/fog/tint lookup
  tables (`TINT`, `HAZE`, `LAYER`, `FADE`). This gives the authentic colour
  banding. It runs at internal 320×200 / 640×480, with optional integer upscaling.
- **Enhanced mode:** true-colour expansion, any resolution, smooth fog, and
  optional texture filtering.
- Both modes share one scene graph:
  - static level geometry, batched per material
  - placed 3D objects
  - billboard sprites for enemies and effects (BSI)
  - sky (`*sky.bsi`)
  - water
  - FLIC talking-head overlay during dialogue

### 4.4 Simulation
- **Fixed timestep** (for example 35 Hz or 70 Hz, to be matched to the original
  tick rate found in RE). Rendering interpolates between ticks.
- A deterministic simulation, separate from rendering, keeps replays, saves and
  later networking straightforward.
- Collision uses a swept ellipsoid or capsule against level triangles (BSP or
  grid broadphase built at load time). It handles stairs, slopes, swimming,
  levitation and falling.
- Entities use plain C structs with type tags (no ECS framework at first).
- Level logic covers triggers, doors, elevators, sigils, NPC events and
  scripted encounters. **Where this logic lives is the biggest open question**:
  it could be data in `.BS6` records, the `TXT` talk tables, or hard-coded in
  `GAME.EXE`. Resolving this is an early RE task (see §6).

### 4.5 Multiplayer readiness (deferred)
- Treat the player as an entity driven by an input command stream. A remote
  player later becomes another command source.

## 5. Milestones

Each milestone has a concrete **exit criterion**. Format work and RE work run in
parallel throughout.

### M0 — Skeleton (small)
- CMake project, `bs_core` (arena, reader, log, VFS over loose files), CTest,
  and CI (Linux GCC/Clang, plus sanitizers).
- `.gitignore` that blocks game data (`*.BSA`, `*.BS6`, `*.BSI`, `*.3d`, `*.gog`, …).
- **Exit:** `bstool info <install>` detects the install and prints the version and file list.

### M1 — Archives and text ✅ (done 2026-10-10)
- BSA reader. Work out the **compression algorithm** for `flags=0x0100`
  records (probably an LZSS variant) and implement it.
- CD image reader (cue sheet, MODE2/2352 sectors, ISO 9660).
- Parsers for `LEVELS.TXT` and `TEXT.RSC`. `TXT.BSA` talk tables can be dumped as UTF-8;
  parsing their structure moved to M7, as did `.SCL` classes (binary, belongs with character creation;
  done 2026-10-15).
- **Exit:** `bstool extract` round-trips every record in every archive, including
  `WAVES.BSA` read from inside `game.gog`. Every text file can be dumped as
  readable UTF-8.

### M2 — Images ✅ (done 2026-10-10; colour tables partial, see below)
- Palettes and lookup tables: `PAL.RAW`, `.COL`, `TINT`, `HAZE`, `WATER`, `LAYER`, `FADE`.
- BSI images: single frames, animated frames, and the `HICL`/`HTBL` colour data
  (the part existing tools get wrong).
- `.FNT` fonts and FLIC animations.
- **Exit:** every BSI and FLC entry exports to PNG or an animated sequence with
  correct colours, checked by eye against screenshots of the game running in DOSBox.

### M3 — Models and levels; viewer 🟡 (formats and viewer done 2026-10-11; DOSBox comparison and lighting rules pending)
- 3D v2.7 format: vertices, planes, UVs and texture references (Daggerfall's
  readers are a reference).
- BS6 level format: geometry and placed objects (record types T2/6 and others,
  using the UESP save-format notes), lights, and entities.
- glTF export from `bstool`.
- **First executable:** a free-fly level viewer with textures, sky, fog from
  `LEVELS.TXT`, and classic and enhanced render modes.
- **Exit:** all 7 campaign levels and the 10 multiplayer maps load and look
  correct when compared side by side with DOSBox.

### M4 — Audio and video ✅ (done 2026-10-13)
- Sound effects from `SPIRE.SND` and `WAVES.BSA` (which index maps to which
  sound?), a mixer, and positional audio.
- Music: OGG tracks, using the level-to-track mapping found in RE.
- Smacker decoder for the intro and character-creation videos.
- **Exit:** the intro video plays with sound, and each level plays its music
  and ambient sounds.

### M5 — Walking around ✅ (done 2026-10-14; swimming, boats and the balloon 2026-10-17)
- Player controller (original controls plus modern WASD and mouselook),
  collision, gravity, jumping, swimming, and levitation.
- Doors, lifts, and other moving geometry.
- **Exit:** walk through L1 "The Weir Gate" from start to exit, with collision
  matching the original.

### M6 — Game simulation ✅ (started 2026-10-14; all five steps done 2026-10-15, exit check 2026-11-04)
- Entity system, item pickups, containers, keys and sigils, triggers, level transitions. ✅
- Actors: animation states, AI using the `NODES` graphs, perception, and pathing. ✅ (a shortest
  path stands in for the game's own node search)
- Combat: melee, ranged, and damage formulas (from RE). ✅ for the player; monsters with bows open.
- Magic: spellcasting and effects. ✅ but Dispel Magic's list, the Detect displays, Confusion,
  magic items' spells and Dispel Sigil.
- **Exit:** L1 is playable start to finish with enemies and items. **Working, checked manually on 2026-11-04**.

### M7 — UI and RPG systems 🟡 (started 2026-10-14; all items in 2026-10-20, exit check pending)
- Parsers: `.SCL` class files ✅ (2026-10-15), `TXT.BSA` talk and random-talk tables (moved from M1).
- Main menu ✅, HUD (the interface bar and its screens) ✅, character creation
  (classes, attributes, skills, spells, shops, advantages) ✅ (2026-10-15),
  inventory and equipment ✅, the spell book ✅ (Battlespire's stand-in for a
  spellmaker: icon, delivery, element and detonation per spell).
- Dialogue ✅ (2026-10-16): the talk tables, the talking heads, the voices,
  the replies, the variables and the actions.
- Levelling ✅ (2026-10-17): the level-up screen after each level, skills
  rising with use, regeneration.
- The advantages in play ✅ (2026-10-19): Acute Hearing, Athleticism and
  Adrenaline Rush join regeneration, Spell Absorption and Rapid Healing.
- The options bar's Screen Size and Controls ✅ (2026-10-19). Battlespire
  has no journal: no key, screen, picture or string for one (the item came
  from this plan's first draft).
- Cursor movement ✅ (2026-10-20): Controls' alternative to View, steering
  by the pointer's zone with the left button held.
- **Exit:** a new character can be created ✅, and every dialogue in L1 and L2 works (the engine runs
  them all; not yet played through in the game).

### M8 — Campaign completion and saves 🟡 (started 2026-10-15; saves done 2026-10-16; endings 2026-10-23)
- Our own save format (versioned), plus an optional importer for original
  `SAVE*` files (UESP has documented the save format). ✅ Our text format
  (`GAME.BSS`), the importer, Save and Load in the game, and `bssave` to edit
  original saves. Interiors in original saves ✅ (2026-10-30); levels left
  through a door kept as they were left ✅ (2026-10-31).
- All levels, the endings, and the remaining level scripting. The endings ✅
  (2026-10-23): the win, the captivity and `ENDFRAME` game overs with their
  texts, and the `ANCHORS.SMK` joke ending.
- **Exit:** the campaign can be completed from start to finish. Parity and bug
  tracking moves to an issue list.

### M9+ — Beyond parity
- Optional settings: widescreen, high resolution, rebinding controls, controller
  support, accessibility.
- Mod support through VFS overrides.
- Multiplayer (deathmatch and CTF maps are already in the data).
- Restoring unused content (unused interiors `IN*.BS6`, `L12`, `L8OLD`).

## 6. Reverse-engineering track (`GAME.EXE`)

Ghidra is installed. Set up a shared Ghidra project with the LE executable
loaded (it may need an LE loader extension, or the executable converted to a
flat image first).

Priority questions:
1. The BSA decompression routine, if it can't be identified from the data alone.
2. How BS6 records are interpreted. Where triggers and scripts live.
3. The simulation tick rate, movement constants (speed, gravity, jump), and
   collision radii.
4. Character formulas: attributes, skills, levelling, and damage/hit chance.
   (Damage and hit chance done in M6; character creation's build-point costs
   done 2026-10-15; levelling and skill increases done 2026-10-17.)
5. AI state machines and how `NODES` files are used.
6. How sound IDs map to `SPIRE.SND` and `WAVES.BSA` entries, and which music
   track plays in each level.
7. Dialogue table semantics (`talkcheck`, `talktable`, `GotoRandom`, …).

Findings go into `docs/re/<topic>.md` as behaviour descriptions.

**Verification:** run the original under DOSBox (the debugger build if needed)
to compare behaviour and capture reference screenshots and values.

## 7. Testing strategy
- **Unit tests** on synthetic data we build ourselves (always run, in CI).
- **Data tests** that run when `BS_DATA_DIR` is set. They parse every record in
  every archive and assert there are no errors and counts are stable. They're
  run locally, not in CI.
- **Fuzzing** for every parser.
- **Golden-image tests** for the renderer (optional, local-only, compared against
  stored hashes rather than stored images).

## 8. Risks

| Risk | Mitigation |
|---|---|
| Level logic is hard-coded in the EXE | Start RE question 2 during M1. Budget for porting scripted behaviour by hand, level by level. |
| BSA or BSI compression and colour details are unknown | Existing tools solved most of this already. Use the data tests and DOSBox comparisons. |
| Gameplay drifts from the original | Base numbers on RE. Write tests for each formula. Keep the DOSBox reference setup. |
| Scope creep (the project is large) | Each milestone stands on its own. The viewer at M3 is already useful to the community. |
| Data leaking into the repo | `.gitignore`, a CI check that rejects large binaries, and the rule in §1. |

## 9. Open decisions
1. Project licence: GPL-3.0-or-later or MIT.
2. Whether to use system SDL3 or vendor it (system for Linux development, vendored for releases?).
3. Public hosting (GitHub or Codeberg) and when to publish.
4. Minimum OpenGL version (3.3 core proposed).

## 10. Immediate next steps

**Where things stand (2026-10-23):**
- M0–M2, M4 and M5 are done. M3 is done but its DOSBox comparison.
- M6 has all five steps in; its exit check, a whole play-through of level 1,
  is pending.
- M7's items are all in: menus, the bar, inventory, the spell book,
  character creation, dialogue, levelling, the advantages, Screen Size,
  Controls and Cursor movement. Its exit check (the dialogues in L1 and L2
  played through) goes with M6's.
- M8 has started with saves: the original's load into the engine and can
  be edited with `bssave`; our own text saves are saved and loaded from the
  options bar. The endings are in (2026-10-23); playing the whole campaign
  through is still ahead.
- The plan is ordered under "Next" at the end of this section. The log below
  is kept in order as it was written.

M0, M1 and M2 are done (CI and Windows deferred by decision). M3's formats,
glTF export and viewer are done; its exit check needs DOSBox screenshots.

M2 findings that change the plan:
- The game renders in **15-bit colour (RGB555)**, not 8-bit. BSI pixels index
  a per-image table (`HICL`) of RGB555 values shifted left one bit, and fonts
  are RGB555. (M2 first read `HICL` as RGB565; DOSBox screenshots corrected
  this on 2026-10-11.) The plan's "classic mode" (§4.3) should emulate the
  original lighting through the `HTBL` shade tables, not 8-bit palette lookups.
- The colour lookup tables (`HAZE`, `TINT`, `LAYER`, `FADE`, `WATER`, `H2L`)
  are only partly understood. They move to the RE track.
- Fonts are code page 437, but text is Windows-1252: check the in-game
  rendering in DOSBox.

M3 findings:
- Models are Daggerfall's v2.7 format with 32-bit face textures. **Textures are
  named in the face itself, as six base-40 characters.** All 2,395 models parse
  and every texture resolves.
- Levels (BS6) are a tree of tagged chunks: placed models, flats, lights and
  viewpoints. The placement convention (axes, rotation order, 1/256 scale)
  was found by fitting and checked against the automap. Most flats are
  editor markers (monster and item spawns, starts); level logic lives in the
  undecoded `STRU`/`CTRL`/`LINK` chunks, which answers part of the §4.4
  question: **much of the level logic is data in the BS6 files**.
- Lighting is now measured, not guessed (2026-10-13): two DOSBox-X memory
  dumps with matching screenshots gave the game's camera, light list and
  shade tables. The viewer reproduces both views to within 1–2 of 31 shade
  levels (rule in `docs/re/README.md`). Sky mapping and the `C`-level fog are
  still approximations, labelled as such in the code and docs. "Classic" mode is
  640×480, nearest-neighbour, with 16-step light banding; the real `HTBL`/`HAZE`
  rules come from RE.

M4 progress (2026-10-13):
- Done: the WAV parser (every `SPIRE.SND` and `WAVES.BSA` record; three
  sample rates), a Smacker decoder (all six videos, audio length matches),
  Ogg music through vendored stb_vorbis, and an SDL3 mixer (`bs_audio`:
  64 voices with gain, pan and looping, music, and push streams for video
  audio). `bsplay` plays a video, a sound or a music track; `--audio-out`
  renders to a file for testing. Fuzz harnesses `wav`, `smk` and `nodes`; the
  first Smacker run found an unchecked allocation size (fixed, with a test).
- Done, from static analysis of `GAME.EXE` (`docs/re/README.md`, "Audio"):
  the level → music rule (track = level number + 1, looped), the sound
  placement rule (`bs/audio/place.h`: linear falloff, angle-only pan, fixed
  at start), and the ambient sounds (32 effects played at AI navigation nodes
  near the player). This needed the `NODES.*` format (`bs_formats/nodes.h`,
  all 13 files parse). `bsview` plays level music and ambience.
- Done: when each video plays (`docs/re/README.md`, "Videos"): STORY at
  start-up, JUMP between levels, MALE/FEMALE on winning, CHOP2 and ANCHORS
  on two game overs.
- Done: the game executable `openbattlespire` (`src/app`) with the start-up
  sequence: STORY (a key skips it), the title menu (`MULT01.BSI`, track 6, the
  original's button rectangles from `0xEDBC8`), and Single Player entering
  level 1 with its music and ambience. Shared pieces moved out of the tools:
  `bs_video` (audio-timed Smacker playback) and `bs_level_audio` in
  `bs_audio`; `bs_screen` (full-screen images) and `bs_level_view` (level
  loading and set-up) in `bs_render`. Tested headless (offscreen video, disk
  audio, sanitizers) and played on a real display: intro, title music and
  Single Player into level 1 with its music all work (2026-10-13).
- Open: which code plays which `SPIRE.SND` id (belongs with M6, as
  monsters, doors and spells come in) and the voice-clip player (`0x1CE6A`). The silent final level and
  the missing ambience are confirmed in the running game.

Level logic (2026-10-13): the `STRU`/`CTRL`/`LINK` chunks are surveyed
(`docs/formats/bs6.md`), and triggers, links, moves, rotations, timers and
teleports are decoded from `GAME.EXE` (`docs/re/README.md`, "Level logic").

Level logic in the engine (2026-10-13): `bs_bs6_entity_logic()` reads the
chunks; `bs_game`'s `level_logic` runs triggers, links, moves, rotations,
timers and teleports by the decoded rules (unit tests, and every level's
triggers run in the data test); the scene keeps interactive objects as
separate parts the renderer can move (pixel-identical to the static build);
`openbattlespire` sends Use from the crosshair and Step from a downward probe,
teleports and carries the camera. Guesses until M5's collision: use reach 192
units, floor probe 128 units, the camera standing in for the player.

M5 progress (2026-10-13): the movement and collision rules are decoded
(`docs/re/README.md`, "Player movement and collision": eye 72, body spheres,
floor probe, step and snap heights, walking speed from attributes, gravity,
jumps). `bs_game` has a collision world (static grid plus movable objects)
and a player controller using them; `openbattlespire` walks, with the step
and bump events coming from the real floor and side contacts, lifts carrying
the player, and teleports moving the feet. Tested with synthetic levels
(walls, steps, jumps, falls) and level 1 (landing exactly where the memory
dumps put the player, walking off into the pit, riding a lift).
Not yet: swimming and levitation, crouching's speed and the attributes'
names, the exact slide-plane rule, sprites and NPCs as obstacles, and the use
reach (192 is a guess).

M5 closed (2026-10-14): level 1 plays through with the original's movement
as checked by the user (charged jumps with the pointer, plank bridges, the
three walking speeds, both key layouts). Carried over: swimming and
levitation (levels 2, 5, 6), sprites and NPCs as obstacles.

M6 plan (started 2026-10-14):
1. What objects are: decode the remaining `STRU` properties (items,
   containers, texts, exits, monster spawns) and the item and monster tables
   in `GAME.EXE`.
2. Items: pick-up, containers, inventory, keys and sigils, messages.
3. Level transitions: exits, arrival points, the JUMP video.
4. Monsters: sprites and animation, AI on the NODES graphs, combat. (Done
   for melee, 2026-10-14.)
5. Magic. (Done, 2026-10-15.)

M6 step 1 (2026-10-14): `STRU` properties confirmed in `docs/formats/bs6.md`;
items in `docs/formats/items.md` (43 base items, magic enchantment files);
creatures in `docs/formats/creatures.md` (16 creatures, sprites, spawn rules).

M6 step 2 (2026-10-14): items in the engine. The base item, weapon and
material tables and the built-in messages are read from `GAME.EXE`, the
enchantments from `TXT.BSA`; every level's placed items and container
contents are made by the game's rules (672 items, all of them valid). Using
an item within 192 units takes it (and hides it), using a container takes its
contents, both while the weight stays within 4 × Strength; plaques show their
`TEXT.RSC` message. Messages show at the top of the screen as the game draws
them (FONT0001 over a FONT0003 shadow, 10 s each). F10 (or I) shows a plain
list of what is carried. Also found: the attribute names (+`0x20` Strength,
+`0x38` Speed), the use reach (192, from the game), the weapon stats and the
material roll.

Inventory screen (2026-10-14): `char12.bsi` as the game lays it out (its
22-button table decoded): the carried list and a container or floor list,
each with its open container on top and four slots of spinning item models
(the 40 item models and 26 sigil models, named by a table in `GAME.EXE`),
scroll strips with dimmed arrows, the six filters, selection with the item's
two info lines, dragging between lists and into containers (nested
containers open on double-click), the weight limit, arrow and potion stacks.
Using an item or a container in the world opens it, as F10 does with what
lies in the player's grid cell; dropped items lie on the floor as models and
can be picked up again. Items carried go on to the next level. New test
options: `--inventory`, `--screenshot FILE`.

Equipping (2026-10-14): the paper doll as the game composes it (body, face
and hair by sex and race, the worn items' pictures in their order, the
frame), items picking their worn picture when made, the class rules (none
yet: every character may equip everything), equipping by dropping on the
doll, taking off by dragging off it, the secondary weapon and arrow slots,
bows needing arrows, the six armour values and hand-to-hand damage. New
options until there is character creation: `--female`, `--race`.

Using items (2026-10-14): double-clicking reads books and scrolls on the book
screen (`textbk.bsi`, pages laid out as the game does, the name codes filled
in), drinks potions (Restoration, Water Breathing, and the others' spells
cast on the drinker: Cure Health heals, Cause Damage hurts, Slow Fall,
Jumping and Running last their time and change the movement), and teaches
spell scrolls' spells. The spell table is read from `GAME.EXE`; potions are
named after their spells. A first character record holds health, spell
points, known spells and effects (50 and 50 until character creation; new
option `--name`).

Keys, sigils and gems (2026-10-14): the levels' key scripts (level 1's Cogs
and level 2's Voidguides with their counter textures, level 3's control
rods, level 5's six and level 6's three Gatekeys, with the game's messages),
sigils (`sigil` + letter models: the matching Sigil Amulet of Entry makes one
vanish, without it it blocks and hurts) and gems (health and spell points,
gone for a minute after use).

Level transitions (2026-10-14): doors lead into the level 5 interiors and
back (the level left keeps its items), or to arrival points in the same
level; each level's exit (level 1's door panels and `1zoom`, level 2's
portal, level 3's `l2a46`, the locked exit objects of levels 4–6) plays
`JUMP.SMK` and goes on to the next campaign level by number with the carried
items. Not done: the `ANCHORS.SMK` joke ending (its condition isn't
understood), the experience for a level, the endings. (The joke ending and
the endings: done 2026-10-23.)

Not yet: magic items' spells (charges), Dispel Sigil, the level 5 bell rope, the message
box for long texts, monster loot (116 on monster flats).

Monsters (M6 step 4, 2026-10-14): the creature, image, per-level and sound
tables are read from `GAME.EXE` (`docs/formats/creatures.md`). Spawn points
are flats named `monster1`/`monster2`; they spawn by the game's rules (their
records' overrides, the level's random creatures by the `LEVELS.TXT`
percentage, always for dialogue codes and loot). Every frame the monsters
within the level's activity distance (the last unknown `LEVELS.TXT` field)
think by the decoded state machine. They chase the player straight or over
the navigation nodes, attack within 90 units, wander, flee and wait to talk
(with the game's greetings). They are drawn as animated sprites in five
mirrored views. Melee is the game's blow both ways: hit location, chance,
criticals and fumbles, armour, the skill and Strength scaling, pushes back.
The player swings by holding the right button and moving the mouse; the
direction picks the swing's factors as the game's table does. Dead monsters
drop their loot; the player's death leads to the captivity ending (CHOP2).
A first character record holds attributes and skills (all 50 until
character creation). New option: `--no-monsters`.

Not yet: spells (monsters' and the player's), dialogue, bows and missiles,
the weapon in hand, the hidden reserve monsters, a health display, the
game's own node search and path following (a shortest path stands in).

Magic (M6 step 5, 2026-10-15): the spells are decoded from `GAME.EXE`
(`docs/formats/spells.md`): casting and its costs, deliveries, elements and
detonations, missiles and bursts, resisting, absorbing and reflecting by the
spell pools, the elements' tolerances, every effect, the timed effects on
the player with their icons, summoning, Delayed Damage's traps, the
monsters' spell lists and their casting, and the spell book. In the engine:
`bs_game`'s `magic` runs all of these; the spell book (F12) is the game's
panel, where each spell's icon, delivery, element and detonation are chosen
and F1-F8 assigned; the hot keys cast in play; touch spells go with the next
use of a monster, ranged ones with the attack button; potions cast their
spells; missiles and bursts are sprites; the casting hand and the effects'
icons show over the view; the Teleport screen drops and returns to an
anchor. Monsters with spells stand and cast until their spell points run
out. New option: `--spells` (spells known at the start). The rendering
toggle moved from F2 (now a hot key) to G.

Not yet: Dispel Magic's list, the Detect spells' displays, Confusion's
controls, monsters fighting each other (summoned ones stand by), skills
rising with use, magic items' spells, Dispel Sigil.

The interface bar and the weapon in hand (2026-10-15): the bar across the
bottom of the screen (`panel.bsi`) with the character's face, the health and
spell point vials, the spell book and inventory buttons and the compass; Tab
shows the full view (the vials and the compass only). A weapon in hand is
its 3D model (`edagger.3d` ...), carried by the body and posed by the game's
keyframed states: raised when equipped, bobbing while walking, the swings by
the mouse's direction with the game's times (weapon, skill, Strength,
Speed), the blow half-way through. Bare hands punch with the `punch0.bsi`
sprite. Details in `docs/re/README.md`.

The bar's screens, bows and the secondary weapon (2026-10-15):
- **The bar's buttons all work.** Hold Enter for the cursor to click them.
  - The character sheet (F9): the doll, attributes, health, spell points,
    what is carried, the skills on a scroll.
  - The automap (F11): the level's map where the player has been, 16-pixel
    cells at a time, with the player's marker.
  - The options bar (Esc): the full view, the sound and music volumes, End
    Game and Drop to DOS with "Are you sure?", help.
  - The hot key list (messages) and the help pages, with the game shown
    small in their window.
- **Bows** have their own pose:
  - They follow the view's pitch with an arrow nocked.
  - A press shoots: the arrow flies straight at 1600 units/s from the arrows
    ready, hits monsters within 80 units of its path for the bow's damage,
    and half the time sticks in them (dropped with their loot).
  - The bow turns aside to nock the next one.
  - When the arrows run out the secondary weapon comes to hand.
- **The secondary weapon** swaps with the one in hand (A, or Q in the modern
  layout): the one in hand is lowered first, a bow shows 2 s later.
- **A wall right ahead** pulls the weapon back (the states 12–14 the first
  notes took for a bow).
- **The bar's face** is the paper doll cut out around its eyes, as the game
  does it (decoded, no longer measured), with a helmet when one is worn.
- New option: `--give` (items carried at the start).

Not yet: Controls and Screen Size in the options bar; the help
pages popping up by themselves; the level-up sheet; weapons wearing out in
melee; item hot keys.

Character creation (2026-10-15): Single Player makes the character as the
original does (`docs/re/README.md`, "Character creation"):
- **Select Scenario** (New Game), then the **name**, **male or female**, the
  **race** (the sliding frame, the description, double click or Done) and
  the **face** (hair, eyes and mouth stepped through or randomized, on the
  doll).
- **The class**: one of the 18 shipped (`CLASSnnn.SCL`, read by `bs_formats`'
  `scl`, `docs/formats/scl.md`) or a new one.
- **The CharacterMaker**, with the game's costs in `bs_game`'s `creation`:
  - the class name, wounds, attributes and the twelve skills (the skill list
    pops up; held buttons repeat, Shift steps ten);
  - spells to add and delete;
  - the equipment and magic item shops, one item at a time, turning in the
    window;
  - advantages and disadvantages, with what's chosen ticked and what's
    excluded greyed out;
  - Done's checks, and Cancel's "Are you sure?".
- The character gets the race's skill bonuses, spell points from
  Intelligence × magery, the class's rules for weapons, armour and material,
  its spells and items (carried, not worn), and the face on the bar and doll.
- The shipped classes check out against the costs: seven come to 4980 and
  three to exactly 5000, the single-player budget.
- Each screen's help page pops up the first time while help pops up by
  itself.
- The character sheet lists the class's skills first.
- `--level` still skips all this; `--name`, `--female`, `--race`, `--spells`
  and `--give` apply there.

Not yet (saving a class came 2026-11-01): the advantages' effects in play
(Acute Hearing, regeneration, Athleticism, Adrenaline Rush), the level-up
screen.

Dialogue (2026-10-16): conversations as the game has them
(`docs/re/README.md`, "Dialogue"; the tables in `docs/formats/txt_bsa.md`).
- **Who talks:**
  - monsters, when used within 384 units or half the time when they greet
    you, any of them with their level's default script;
  - the NPC flats (Clarentavious, Sumeer, Jaciel …) and level 2's talking
    gems, when used.
- **The screen:**
  - the full-screen talking head, `talk` animations while the voice from the
    CD's WAVES.BSA plays, `wait` animations after;
  - the line across the top, the replies on the right with the one under the
    mouse framed, what was said on the scroll (its arrows go back through it);
  - Exit once nothing is left to reply.
- **The engine** (`bs_game`'s `dialogue`, unit-tested and run over all 76
  conversations):
  - the openings (`talkcheck`, `talktable` by visits, `GotoRandom`);
  - narration in [[ ]] as messages;
  - replies by their codes and checks;
  - variables, the speaker's own and global, including those for what you
    carry and your sex;
  - random GoTos.
- **The actions:**
  - monster states, summons and rallies, joining the player, KILL and
    TELEPORT;
  - giving, dropping and taking items, dropping the weapon, half health;
  - dialogue links;
  - ENDFRAME and Dagon's captivity.
- **Saves:** SAVEVARS' visits, dialogue links and leaving speakers are
  decoded too.

Not yet:
- the text shown with CHOP2 (done 2026-10-23);
- the dialogue's help page;
- where summoned monsters appear (the game's placing isn't decoded).

Saved games, step 1 (2026-10-15): the original's save and load code is
decoded (`docs/formats/save.md`, `docs/re/README.md` "Saving and loading").
- **The four files:** the name, an 80 × 50 picture, SAVETREE.DAT (the
  object tree as records, then the level controllers' state) and
  SAVEVARS.DAT (fixed blocks: the clock, the log, the level's tables, the
  dialogue variables).
- **The variable name hash** reproduces all of UESP's 146.
- **Corrections to UESP's notes:** the variables' block sizes, the log block,
  and the female bit.
- `bs_formats`' `save` reads all four files, and class files now use the same
  record reader.
- `bstool save` prints a slot, writes its picture, and diffs two slots.
- **Checked against three saves made in level 1:** the files, the records'
  parents, and the controllers in our level logic's chain order.

Decided: the original saves are read first (an importer). How our own saves
are written is decided after that.

Saved games, step 2 (2026-10-16): the original's saves load into the engine
(`docs/formats/save.md`, "Loading into the engine").
- **`bs_game`'s `save_import`** turns a save into the engine's pieces:
  - the character, its spells and hot keys;
  - every item where it is: carried and worn, in containers, on the floor,
    in chests, or held by monsters;
  - the monsters left, the level's changed objects, the automap;
  - the dialogue variables and visits, speakers leaving, dispelled sigils,
    and the level script's progress.

  It's unit-tested, fuzzed, and run over the install's saves.
- **The game** loads one over a fresh campaign level:
  - the clock runs on from the saved one;
  - monsters drop the sack they carry when they die;
  - the controllers' state is restored.
- **The scenario screen's Load Saved Game:** the original's load screen
  (`load00.bsi`) with the ten slots' pictures and names and the sliding
  frame. `--load n` starts from slot n.
- **Checked:** all three samples load where they were saved, matching their
  pictures, with the right equipment, sheet and automap, clean under ASan.

Not yet: timed spell effects, items on hot keys, the options record and
volumes, an NPC that has already left, the `loading.bsi` picture while
loading.

Saved games, step 3 (2026-10-16): our own saves and an editor for the
original's.
- **Our format** (`docs/formats/game_save.md`): sectioned text, one
  `GAME.BSS` per slot with the original's 80 × 50 picture.
  - `bs_core`'s `ini` reads and writes the text;
  - `bs_game`'s `game_save` turns a `bs_saved_game` (what original saves
    import to) into it and back, with errors by line.
  - It keeps more than the original's: the level an interior was saved in
    and the items of levels left through doors, timed spell effects, every
    automap, every controller and moved object.
- **In the game:** the options bar's Save and Load open the original's
  screen (`loadsave.bsi`). Saving takes a slot and a typed name; loading
  shows our slots, and the original's on a second page (Page Down), as does
  the scenario screen's Load Saved Game. `--saves DIR` sets the folder.
- **`bssave`**, a new tool:
  - `show` (either kind), `fields`;
  - `edit` changes an original save's player fields, gives items (made as
    the game makes them), takes items or monsters, sets item fields and
    dialogue variables, renames it. It writes a new folder with every other
    byte as it was;
  - `convert` (original → ours), `check` (ours).
- **Checked:**
  - unit tests for the text, the format and the editor;
  - the install's saves round-trip through the editor byte for byte, and
    through our format unchanged;
  - fuzzing (the save fuzzer now imports; a new one for our format);
  - in the game, saved, reloaded and saved again (only the clock and settling
    positions differ), saved in a level 5 house and reloaded with level 5's
    items kept as when walking in;
  - an edited original save loaded in the engine;
  - ASan through saving and both kinds of loading.

Not yet:
- edited original saves tried in the original game (DOSBox);
- items on hot keys (neither format keeps them);
- spells and arrows in flight, the weapon's swing, monsters' paths;
- the monsters of levels left through doors (they're made again there).

Levelling (2026-10-17): decoded from `GAME.EXE` (`docs/re/README.md`,
"Levelling") and in the engine (`bs_game`'s `levelling`).
- **No experience:** finishing a level opens the character sheet as the
  level-up screen (`cscr01.bsi`) with the level's points (600, 800, 1100,
  1400, 1600, 1900 for levels 1–6) and any left from before, the
  CharacterMaker's at first. Health's maximum grows by 307 / 256, and health
  and spell points are filled.
- **Spending:** attributes cost about 40 + v / 2 a point up to 100; skills
  10 + v × factor / 100 (50 primary, 60, 70, 80 for the rest) while below
  their attribute. Both can go back down to where they were, an attribute
  not below a skill it governs. Shift does ten. The game's quirk of lowering
  an attribute on a raise without the points is kept.
- **Skills rise with use:** casting (potions too) counts for the school,
  blows for the weapon's skill, hits for Critical Strike, blows taken for
  Dodging, shots for Missile, jumps for Jumping, and moving while sneaking
  for Stealth every 5 s. A skill rises after v × rate × 2 / 5 + 1 uses,
  below 95 and its attribute. Backstabbing never counts, nor does Stealth
  do anything: both are the original's bugs. The installed saves' counts
  fit.
- **Regeneration:** the advantages give one health or spell point every
  5 s.
- **Saves:** the counts and the points left are kept in ours and read from
  the original's.
- **Checked:** unit tests for the costs, limits and rises; the screen
  driven by clicks in the game; a new option `--level-up N`.

Not yet: the level-up screen's help page popping up.

Swimming, boats and the balloon (2026-10-17): the carried-over part of M5,
decoded from `GAME.EXE` (`docs/re/README.md`, "Swimming, boats and the
balloon"). What the notes called levitation is riding level 5's balloon.
- **Water** (levels 3–6), for monsters too:
  - with the feet 50 below the surface the player swims: no falling, a
    splash on falling in, half speed plus the Swimming skill's bonus;
  - Page Up and Down rise and dive at 100 units/s, and carrying more than
    1.5 × Strength sinks;
  - swimming into a ledge up to 60 above the water climbs out.
- **Breath:** Endurance seconds under water, shown as the game's bar, then
  drowning. Water Breathing keeps it full. Under the surface the view has
  the game's blue haze. Swimming now counts for its skill.
- **Boats** (one each in levels 3 and 4, four in level 5):
  - using one gets in; the sideways keys turn it, and Forward and Back row
    it along its mirrored heading, so the rider faces the stern, as the
    code has it;
  - it stops where its spheres touch anything, and boats rock;
  - D (X in the modern layout) or using it gets out.
- **The balloon** (level 5):
  - Page Up and Down at 160 units/s, half the walking speed across;
  - its envelope and basket stop it;
  - left alone for 20 s it rises out of sight and comes back down where it
    began.
- **Saves:** the ridden object is kept (and read from the original's, where
  it's the player's child), and so are where boats and the balloon are.
- **Checked:**
  - unit tests (floating, diving, sinking, level 5's depth limit, swimming
    speed, climbing out, a boat stopping at a wall, the balloon);
  - in the game: swimming, a boat ridden and steered in level 3, the
    balloon flown and going home in level 5, drowning after 50 s.

Not yet:
- the Swimming bonus assumes 20 frames a second; the original's real frame
  rate in DOSBox would settle it;
- the underwater haze's distances use the "C" levels' fitted scale.

The advantages in play (2026-10-19), from every read of their bytes in
`GAME.EXE` (`docs/re/README.md`, "Character creation"):
- **Athleticism** (+0x199) is the flag the movement notes had guessed was
  the Running spell's: walking +282/256, jumps ×1.5 up and ×310/256 along.
  The engine took it from the spell's flag; it now comes from the class.
  The Running spell sets a flag nothing reads, so it does nothing, as in
  the original.
- **Adrenaline Rush** (+0x19E): +15 to the player's chance to hit in melee
  with health at most 25 / 256 of the maximum.
- **Acute Hearing** (+0x194): sounds carry range × 1024 / 127 instead of
  × 768 / 127 (1024 instead of 768 for nearly all). Near sounds still don't
  start beyond 768, so it makes them louder.
- **A correction to the audio notes:** the game places playing sounds again
  on every frame (`0x1C54E`), from where their sources are then, and stops a
  sound playing once when its source moves out of range. The notes had said
  placement was fixed at the start. `bs_level_audio` now follows playing
  sounds (monsters, level objects and ambience; the player's own stay
  centred), and the mixer's voice handles carry a serial so a reused voice
  isn't mistaken for an old one.
- **Checked:** unit tests (the walking speed, the chance to hit, the max
  distances and the per-frame rule); the game runs under ASan with sound.
  Not checked by ear.

Screen Size and Controls (2026-10-19), the options bar's last two screens
(`docs/re/README.md`, "Screen Size and Controls"). Battlespire has no
journal, so they finish M7's list but Cursor movement.
- **Screen Size:**
  - the view from 320 × 240 up to the whole screen in seven steps, centred,
    with `backscrn.bsi` and a black frame around it;
  - LOW draws it at half size and doubles it. The game starts LOW, so the
    GOG install, having no `screen.dat`, runs at 320 × 240. Classic
    rendering follows the setting; enhanced renders the view's rectangle
    at the window's resolution.
- **Controls:**
  - the game's 26 actions, in its table's order, can each take any key or
    mouse button. The names are read from `GAME.EXE`, keys used twice show
    red, and Exit is refused while any are;
  - Mouse Reverse, X and Y sensitivity, the double-click time and the turn
    rate;
  - Default puts the keys back;
  - Tab, F12, Enter and the new Lantern (the player's light) go through
    the bindings now, and the turning keys turn at the turn rate (768 angle
    units a second for 128, where the engine had guessed 3 radians);
  - Cursor movement can be chosen, but moves as View does.
- **Kept** in `settings.ini` in the user's data folder, with both screens'
  settings and the help pop-ups (the game's `screen.dat` and
  `controls.dat`). The turn rate and the movement go there too, where the
  game keeps them with the character. `--keys` on the command line keeps
  its layout's keys over the saved ones.
- **Found on the way:** the key table's numbers are the action numbers
  the movement notes use. The turn rate's slider can store 256 in a byte,
  as 0, which a level start makes 128; the engine copies this.
- **Checked:**
  - unit tests (`test_controls`: the defaults against the game's table,
    duplicates, Default, names, the rates, the settings round trip; the
    key names in the data test);
  - both screens driven by clicks and keys in the game, under ASan;
  - screenshots of every view size and resolution.

Cursor movement (2026-10-20), M7's last item (`docs/re/README.md`, "Cursor
movement"). The mouse stays free; holding the left button over the view
steers by where the pointer is.
- **The zones:** three rows (the top 35 %, the middle, the bottom 25 % down
  to the bar's top, or the screen's bottom in the full view) by three
  columns (x 250 and 390). The pointer is the zone's arrow (`direc0.bsi` ..
  `direc8.bsi`); over the bar it's the usual one.
- **What they do:**
  - the top row goes forward at Speed + 200 units a second, the corners
    turning too;
  - the middle row's sides turn, by up to 4 × the turn rate angle units a
    second, more the nearer the edge;
  - the bottom row steps left, back or right at Speed + 100.
  - Sneak Mode, swimming and the balloon halve the speeds; Athleticism and
    the Swimming skill don't add.
- **Selecting:** Select (the left button by default) uses only in the
  centre zone, the cross. The right button uses too when it isn't the
  attack's.
- **Found on the way:**
  - the game turns (keys and View's mouse) only when neither mouse button
    is held, and never in the air (`0x40096`). The engine still turns in
    both cases; a TODO below;
  - `0x41C09`, the address the notes gave, is inside the steering
    function, which starts at `0x41C04`.
- **Checked:**
  - unit tests (`test_player`: the zone edges, each zone's speeds and
    turns, the move with Sneak Mode);
  - in the game, with scripted pointer positions: forward, turning and the
    arrows drawn at the pointer, in level 1.

The endings (2026-10-23), M8 (`docs/re/README.md`, "Videos").
- **Winning:** Dagon's transport beam (`l8tbeam`, in `indagon.bs6`, his
  Hunting Lodge) carries the player to him; without the whole Savior's Hide
  they're captured 2 s later. Each conversation with him that doesn't end the
  game leaves 3 s before the capture, and a blow at him with the Moon Reiver
  in hand within them wins: `MALE.SMK` or `FEMALE.SMK`, then the title (the
  original quits to DOS).
- **The game overs:** CHOP2 with its `TEXT.RSC` text drawn over the video
  (1058 for the captivity, n for `ENDFRAME n`), the last frame kept until a
  key. Being captured now counts as dying, so CHOP2 follows 2 s later, as in
  the original; `ENDFRAME` ends the conversation, then CHOP2 plays at once.
- **The joke ending:** level 1 starts with one of its five generators
  running. That's BS6 property 120, now done: the object starts used, it and
  what it links to at their ends. Before, the engine started with none
  running, so the exit needed one more panel than the original's. Turning
  them all off plays `ANCHORS.SMK` (the level script, each frame on an
  object and each use).
- **Videos:** a key or a mouse button skips one only after its first 10
  frames, as in the original.
- **Saves:** the player record's +0x241 (came by the beam) and +0x2DA (the
  capture time) are imported; ours keep `beamed`.
- **Corrections to the notes:** type 0x21 objects are the flats, the NPCs,
  not plain models; the list the level 1 exit counts is the whole object
  tree; `FS1T`'s `ENDFRAME_0036` is a typo for 1036 that makes the original
  quit (the engine plays CHOP2 without text).
- **Not done:** a blow at another NPC pushes the player back in the original
  (the player isn't pushed by any blow yet); the game over text's font is
  assumed to be FONT0001.
- **Checked:**
  - unit tests (property 120 in `test_logic`, the joke ending's rule in
    `test_level_script`, level 1's generator and Dagon's lodge in the data
    test);
  - in the game, with a temporary hook since removed:
    - level 1 starts with one panel open;
    - turning it off plays ANCHORS at the next use, once the generator has
      stopped;
    - the beam without the armour captures after 2 s, and CHOP2 follows;
    - the blow at Dagon wins only with the Reiver and after the beam;
    - screenshots of CHOP2 with texts 1058 and 1057;
  - the ASan build in level 1 and Dagon's lodge.

Saved games, step 4 (2026-10-30): interiors, from a save in each level, M8
(`docs/formats/save.md`, "Interiors").
- **Samples:** a save from each of levels 2–7, one in a level 5 house (the
  shop), one in level 1 with a door moving and one with a summoned monster.
  All of them failed the data test: items in chests the level didn't have,
  models the controller table didn't place, the player off the automap.
- **What the original does:** it loads every interior into the campaign
  level's world when the level starts, each under its door, with its ids
  shifted by a base. A counter starts at the level's highest id + 100; each
  door adds 100, plus the file's highest id when it loads a new one (found
  from the saves, and it fits every interior in levels 5, 6 and 7, nested
  `inspire` too). The controller table has the level's chains, then each
  interior's. The player's +0x298 names the door of the space they're in,
  and each automap its space's at +0x21 (from the disassembly).
- **`bs_game`'s `level_spaces`** (new) finds a level's spaces, bases and
  chains. The importer takes them:
  - a save made in an interior starts there;
  - ids become the interior file's own;
  - items in the other spaces are kept for those levels (ours keep up to 32
    now; level 5 has 21 spaces);
  - only the player's space keeps its monsters and models.
- **The engine** reads the controller table with every space's chains, and
  fills the other spaces' automaps. `bssave convert` reads the spaces too.
- **Our format:** an item's container is looked for in its own level first,
  since ids repeat across levels.
- **Timed effects:** an effect is a child of the player (`0x295A0`), so it's
  saved while it lasts; the sample's Slow Fall (25 s at level 1) had run out.
- **Checked:**
  - unit tests (`test_level_spaces`: the rule, on synthetic levels; an
    interior save in `test_save_import`);
  - the data test over the nine saves: every parent is a door or a space's
    entity, every saved model at its place in the controller table, the
    controllers fill the tree exactly, the items in their levels, the round
    trip through ours;
  - the ASan and clang builds;
  - in the game: the shop save starts in the shop (its one monster, the
    shelves), level 5 outside, and level 1's door save load without
    controller warnings.

Saved games, step 5 (2026-10-31): timed effects and speakers that leave, M8
(`docs/formats/save.md`, "Timed effects", "Speakers that leave").
- **Samples:** level 1 with a Fire Shield running and a gem used, level 5
  just after the Dark Seducer left; SAVE6 already had Josian gone.
- **Timed effects:** type-66 children of the player, found by their spell's
  name. The layout is now known, from the sample and `0x293EF`: the icon + 1
  at +0x1A0, the end, the flags, then each attribute's change as the first 4
  of 12 bytes in 16.16. The importer fills the character's effects.
- **Speakers leaving:** TELEPORT 0 (every one in the campaign) hides the
  speaker at once: a monster goes into the hidden reserve, which loading
  already skips; a flat or object gets +0x14 bit 2 and is saved as such.
- **Flats** (type 33, now named) are imported: their positions (those with
  controllers) and whether they've gone.
- **The engine** keeps which speakers have gone: they aren't shown or talked
  to (a departed NPC flat could still be talked to before), and ours saves
  them (`[object]` `gone`).
- **Checked:** unit tests (an effect, an unknown one, a gone and a moving
  flat; `gone` in ours' round trip); the data test over the ten saves (every
  effect imported, every flat one of the level's); ASan and clang; fuzzing;
  in the game, the Fire Shield's icon after loading.
- **Not done:** a speaker gone in another space than the player's comes back
  there (Josian, when the save is outside Dagon's lodge), as the engine keeps
  only items for other levels; effects' spell point, health and skill
  changes.

Levels left through a door (2026-10-31), M8 (`docs/formats/game_save.md`,
`docs/formats/save.md` "Loading into the engine").
- **Before:** going back through a door brought a level's monsters back to
  life and its departed speakers back; only its items were kept.
- **Now:** a level left is kept as it was left: its items, the monsters
  alive (with their loot), the objects and controllers, the speakers gone,
  the sigils dispelled. Going back puts it all back once the level is made.
- **The clock** goes on through a door (it started again from 0 before, so
  timed effects and gems used before a door were off).
- **Original saves:** every space is kept that way: the importer lists each
  space's monsters, objects, controllers and sigils for its level.
  `level_spaces` now knows each controller chain's entity, so the importer
  turns the original's controller table into ours, and the engine has one
  way of loading controllers.
- **Ours:** version 2. `other_level = FILE monsters` marks a level whose
  monsters are listed; `[monster]`, `[object]`, `[controller]` and
  `[sigils]` take a `level`. A monster or container is looked for in the
  item's own level first.
- **Fixed on the way:**
  - loading a save while in the same interior applied the old session's
    state (a `loading` flag now keeps the level left out);
  - a running controller's start position is 3 floats in the original
    (`0x4BE40`); it was read as integers, which sent level 1's moving door
    (the first sample with one) far off. Checked in the game: it's back
    mid-way.
- **Checked:**
  - unit tests: the importer (every space's monsters, objects, controllers
    and sigils; a controller table that doesn't fit), ours' round trip with
    another level and its errors, the chains' entities;
  - the data test over the ten saves: every space kept, every object and
    controller one of its level's;
  - in the game, with a temporary hook since removed: two monsters killed
    in level 5 stay dead after the tavern and back; the tavern's monsters
    are the save's; the clock runs on; ours saved and loaded gives the same;
    Josian is gone in Dagon's lodge; out of the shop, level 5's 94 monsters;
    every save loads without warnings;
  - ASan (it found the state applied before the boats were set up: fixed),
    clang, fuzzing.

Playthrough notes (2026-10-31), from `docs/playthrough.md`:
- **Single and multiplayer entities:** BS6 property 122 says which kind of
  game an entity is in (0 single player, 1 multiplayer); the game deletes
  the other kind's as it loads the level (`0x246F5`). Notes come in pairs, a
  scroll to take beside a fixed one with the same text, so the battlemage
  at level 1's start had four scrolls instead of two. Multiplayer also has
  sigils, sacks of amulets and level 5's guards of them (creature 14).
  `bs_bs6_keep_game` drops them when the game loads a level; ids and the
  controller table still count them, as the saves show.
- **Floors that burn:** `lava00`, `lava01`, `mud00`, `gold00`, `fpit02`,
  `poop00` and `smth05` faces take 50 health a second as fire through the
  tolerances (`0x41817`); 40 a second in 50 ms steps here.
- **Gems on contact:** stepping onto or walking into a gem object uses it
  (`0x4072A`, `0x40760`), as level 1's `1hurt` energy column does.
- **The name screen's** Done and Cancel buttons (`0xEDFBC`).
- **The inventory's floor list** (corrected the same day, after a
  comparison in the original): of the player's cell of the object grid it
  shows only the items within 128 of the player, and the one clicked
  (`0x4EF5A`). Showing the whole cell had listed most of level 1.
- **Checked:** unit tests (122 on a synthetic level, the burning textures
  and steps), the data test (level 1: two scrolls by the battlemage, 58
  lava triangles), in the game (health 174 to 61 in 3 s on the lava, a gem
  stepped onto, the saves loading without warnings), ASan and clang.
- **Axes** were a misreport; weapons meet walls differently in the
  original, left as they are for now.
- **Feedback in fights:** a monster hurt and still alive bleeds
  (`blod00.bsi` at it, `0x43886`), and for 250 ms after the player is hurt
  the screen keeps only its red (`TINT.555`, `0x43957`). Checked in the game
  (screenshots of both by a Vermai), a unit test for the bleeding, ASan.
- **Weapon Swing** (an addition), on the Controls screen under Movement:
  Classic swings by the mouse's drag as the game does; Click swings on a
  press, in a random one of the 16 directions. Kept as `weapon_swing` in
  `settings.ini`. Its title is the picture's own "SWING WEAPON" letters,
  rearranged. Checked in the game (the screen, a click swinging, a drag
  not) and a unit test for the swings by direction.

Saving a class (2026-11-01), M8 (`docs/formats/scl.md`, "Writing"):
- **The CharacterMaker's Save** writes the class as the original does
  (`0x3B39C`): the whole player record, its spells, an automap and the items
  it carries and bought (`0x3BC97`), with "Saving out your level %d
  class..." and "Your level %d class has been saved.". A class of the same
  name and level asks `exist0.bsi` first and is replaced on Yes.
- **Where:** our saves folder's `classes`, never the install. The class list
  shows ours with the install's, ours in place of the install's of the same
  number; a new one takes the lowest number free (`CLASS018.SCL` first).
- **Enchantments as the game holds them** (`0x52EE1`): decoded, so saved
  magic items have the game's 0x233 bytes (`bs_magic_record`,
  `docs/formats/items.md`).
- **Checked:** unit tests (the writer read back, the records' headers, the
  enchantment's fields); the data test (each shipped class opened and saved
  again reads back the same, spells byte for byte but the caster, the 23
  enchanted items' bytes); the enchantments against all 1446 in the shipped
  classes and ten saves (a scratch program); in the game (a new class saved
  as `CLASS018.SCL`, saved again: the question, Yes; the Acrobat saved over
  the install's as our `CLASS000.SCL`, 9054 bytes like the shipped one; both
  in the class list).
- **Not checked:** loading an engine-saved class in the original (DOSBox).

Next:
1. **Saving and loading** (M8):
   - try `bssave edit`'s saves, and a class the engine saved, in the
     original game (DOSBox).
2. **M6's exit check:** play level 1 through with a made character and list
   what's off; it now ends on the level-up screen.
3. **A DOSBox check** of swimming speed and the underwater view (both rest
   on assumptions; see the 2026-10-17 entry).
4. **The campaign** (M8): go through levels 2–7 and Dagon's lodge for what
   blocks the way (keys, sigils, exits, the remaining level scripting).

## 11. TODO

Open items from M3's rendering work (2026-10-13). The lighting, fog-wall and
projection rules are measured; these are what's left. Details in
`docs/re/README.md`.

- [ ] **Sprite lighting:** how sprites and objects are shaded. `queue_sprite`
  takes a shade argument; one caller passes object field +0x19.
- [ ] **Light flicker:** the per-frame radius reduction (0–6 % below `RADI`
  in the dumps): which lights flicker, and the random rule.
- [ ] **Light selection:** which lights the game puts in its per-frame list
  (23 of level 1's 103 in each dump, plus the player's light).
- [ ] **Water:** colour treatment (`WATER.001`, `WATER.TBL`, `LAYER.DAT`),
  animation, and the texture scale on the water plane.
- [ ] **Fog in `C` (gradual haze) levels:** the decoded start of 8 units gives
  fog about 3× too dense against a level 5 screenshot. `bsview` uses a fitted
  3×; re-read `0x1122F`.
- [ ] **Shading curve:** apply `HTBL`'s non-linear brightness curve (or the
  per-texture tables themselves) in classic mode instead of a linear scale.
- [ ] **Menu-screen colours:** character-creation screens show a reduced colour
  set compared with the images' `HICL`.
- [x] **Remaining `LEVELS.TXT` field:** `unk_c` (`0x16C1A8`) is the distance within which monsters act (2026-10-14).
- [ ] **Sky mapping:** how the sky image is projected (`bsview` wraps it
  around the horizon, an approximation).

Open items from M4's audio work (2026-10-13), details in `docs/re/README.md`
("Audio"):

- [ ] **Final level music option:** the original asks for CD track 9 on the
  last level and the disc has none, so it's silent (confirmed in a longplay);
  `Track08.ogg` is never played. Add an option to play `Track08` there, off
  by default.
- [ ] **Frame rate:** ambient sounds are rolled once per main-loop frame, so
  their rate depends on the original's frame rate (`bsview` assumes 20 fps).
- [ ] **Ambient emitter bug:** the original indexes the whole node list with
  the number of nearby nodes, so ambient sounds rarely play (confirmed: none
  heard in level 1). `bsview` copies this; `--fixed-ambience` picks a nearby
  node. Decide whether the fixed behaviour becomes an engine option.
- [ ] **Sound ids:** which code plays which `SPIRE.SND` id (callers of
  `0x1CBD4`, `0x1CC68`, `0x1CCFC`); the voice-clip player `0x1CE6A`.
  (`+0x194` is Acute Hearing, 2026-10-19.)
- [ ] **Title menu:** Multi Player does nothing yet (Single Player's second
  menu, `MULT02.BSI`, is there since character creation, 2026-10-15). The
  game over videos (CHOP2, ANCHORS), JUMP between levels and the endings
  are in (2026-10-23).

Open items from the controls (2026-10-20):

- [ ] **Turning while a mouse button is held** (found 2026-10-20,
  `0x40096`): the game doesn't turn or look (keys or mouse) while either
  mouse button is held, nor in the air; the engine does both.
