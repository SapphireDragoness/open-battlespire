# OpenBattlespire

An open-source engine reimplementation of *An Elder Scrolls Legend: Battlespire* (1997), written in C.

**No game data is included.** You need your own copy of Battlespire (e.g. the GOG release); the engine reads the original files at runtime.

Status: early. The data formats for archives, text, images, animations, fonts, models and levels are decoded, and a free-fly level viewer works. See [docs/PLAN.md](docs/PLAN.md).

## Building

Requires CMake ≥ 3.25 and a C17 compiler (GCC, Clang or MSVC). The viewer
also needs SDL3 (3.2 or newer) and OpenGL 3.3; without SDL3 it is skipped and
everything else still builds.

```sh
cmake --preset debug          # or: asan (AddressSanitizer + UBSan), release
cmake --build --preset debug
ctest --preset debug
```

Point the tools and the data tests at your install with `BS_DATA_DIR`:

```sh
export BS_DATA_DIR=~/Games/Battlespire
./build/debug/src/tools/bstool/bstool info
ctest --preset debug          # data tests are skipped when BS_DATA_DIR is unset
```

## The game

There is no gameplay yet. `openbattlespire` runs the start-up sequence: the
intro video (any key skips it), the title menu with its music, and Single
Player (or Enter), which puts you in the first level to fly around with its
music and ambient sounds. You walk with the original's collision, speeds,
gravity and jumping: W A S D (hold Space to charge a jump, release to jump;
C ducks, Ctrl sneaks), and click (once the mouse is captured) or press E to
use what's under the crosshair. `--keys original` uses the game's own layout
(arrows, Alt to jump, S to sneak, Space with the mouse pulled back to duck).
Doors, lifts and portals work. F toggles flying. Esc goes back to the title.

```sh
./build/debug/src/app/openbattlespire            # --no-intro, --level l3.bs6, --mute
```

## Tools

`bstool` inspects and converts the game data (`bstool help` lists every command):

```sh
bstool ls GAMEDATA/3D.BSA             # list an archive
bstool bsi -o out/ WALL96.BSI         # images to PNG
bstool model -o out/ table            # 3D models to glTF
bstool level -o out/ l1               # whole levels to glTF (opens in Blender)
```

`bsview` flies through a level:

```sh
./build/debug/src/tools/bsview/bsview --level l1.bs6
```

Click to capture the mouse, then WASD to move, Space/C up and down, Shift
faster; PgUp/PgDn switch levels, F2 toggles classic/enhanced rendering and
F12 saves a screenshot. Each level plays its music and ambient sounds; M
toggles sound and `--mute` turns it off. `bsview --help` lists everything.

`bsplay` plays the videos, sounds and music:

```sh
./build/debug/src/tools/bsplay/bsplay video STORY     # a video from the CD, with sound
./build/debug/src/tools/bsplay/bsplay sound 102       # a SPIRE.SND effect by id
./build/debug/src/tools/bsplay/bsplay music 2         # MUSIC/Track02.ogg
```
