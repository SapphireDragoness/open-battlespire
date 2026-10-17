# OpenBattlespire

An open-source engine reimplementation of *An Elder Scrolls Legend: Battlespire* (1997), written in C, with the help of Claude Opus 5.5.
This is a hobby project made both to revamp an old, poorly dated game, and to test LLM's capabilities.

DISCLAIMER: **No game data is included.** You need your own copy of Battlespire (e.g. the GOG release); the engine reads the original files at runtime.

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

`openbattlespire` runs the start-up sequence: the
intro video (any key skips it), the title menu with its music, and Single
Player (or Enter). Load Saved Game shows your saves, and on a second page
(Page Down) the original game's ten slots (`SAVE0`–`SAVE9` in the install),
with their pictures and names: pick one and Okay (or double-click it) to carry
on where that game was saved: the level, your character and everything
carried, the items, monsters and doors as they were left, the automap and the
conversations so far. In play, the options bar (Esc) has Save (pick a slot,
type a name, Enter) and Load. Our saves are text files you can edit
(`docs/formats/game_save.md`), in your data folder or `--saves DIR`; `--load`
starts from one, or from an original slot (`--load 0`). New Game makes your character as the original does: a
name, male or female, one of the six races, a face (hair, eyes and mouth, or
Randomize), and a class, either one of the 18 that come with the game or a
new one. Either way the CharacterMaker lets you spend build points on
attributes, wounds and twelve class skills, spells, equipment, magic items,
advantages and disadvantages. Done puts you in the first level with its
music and ambient sounds. You walk with the original's collision, speeds,
gravity and jumping: W A S D (hold Space to charge a jump, release to jump;
C ducks, Ctrl sneaks), and click (once the mouse is captured) or press E to
use what's under the crosshair. `--keys original` uses the game's own layout
(arrows, Alt to jump, S to sneak, Space with the mouse pulled back to duck).
Doors, lifts and portals work. Using an item, chest or sack opens the
inventory screen with it (F10 or I opens it with what lies around): drag items
between your inventory and the other list, double-click containers to open
them, drop weapons, armour and clothes on your character to equip them and
drag them off again, double-click books to read them and potions to drink
them.
Plaques show their text. Cogs, rods and keys open what they should;
sigils bar the way unless you carry the right Sigil Amulet; gems restore
health and spell points. Doors lead into level 5's houses and back, and each
level's exit takes you on to the next. Monsters hunt you: hold the right mouse
button and move the mouse to swing (pull down to chop, push up to thrust,
sideways to slash); the weapon you wield is in your hand, and bare hands
punch. With a bow and arrows ready, a click of the right button shoots. Q (A in
the original layout) swaps your weapon for the secondary one. Some wait to talk and greet you, and half the time
start talking; use a monster (within reach) or an NPC to talk to them: their
talking head speaks its lines (voiced from the CD), click a reply on the right,
and Exit when there's nothing left to say. What you say and carry changes what
they do: some attack, some join you, some give you things. Dead monsters drop their loot. If you die, the captivity ending
plays. F12 opens the spell book across the bottom of the screen: pick a
spell, click its icon, delivery, element and detonation to change them, and
press F1-F8 to give it a hot key; Cast or the hot key casts it. A touch spell
goes on the next monster you use, a ranged one flies with the right button.
Potions cast their spells, and monsters with spells cast theirs at you.
`--level` skips the title and the character: then `--name`, `--female` and
`--race br` choose one, `--spells all` (or a list such as `--spells 18,21`)
the spells known, and `--give` hands you items, e.g. `--give 9,22,2,24` for a
long bow, arrows, a long sword and a helmet. The bar at the bottom shows your face (with what you wear),
health (blue) and spell points (yellow) and the compass. Hold Enter for the
cursor to click its buttons: the face opens the character sheet (F9), the
books the spell book and the automap (F11), the chest the inventory, the
gears the options bar (Esc), the compass lists your hot keys and "?" shows
the help pages. Tab hides the bar for the full view. F toggles flying, G
classic/enhanced rendering. End Game in the options bar goes back to the
title.

There is no experience: as in the original, finishing a level opens the
level-up screen with points to spend (600 after level 1, up to 1900 after
level 6). Click an attribute's upper half to raise it and its lower half to
lower it, or the arrows beside a skill; Shift does ten, and points left over
keep for next time. Skills also rise as you use them ("Long Blade skill has
increased..."). `--level-up N` opens the screen at the start with level N's
points.

Levels 3 to 6 have water. Fall or walk in and you swim, half as fast (faster
with the Swimming skill); Page Up and Page Down rise and dive, and carrying
too much drags you under. Swim into a low ledge to climb out. Under water the
bar at the right is your breath: when it runs out (Endurance seconds) you
drown. Use a boat (levels 3, 4 and 5) to get in: Forward and Back row it, the
sideways keys turn it, and, as in the original, you sit facing the stern. Level
5's balloon rises and sinks with Page Up and Page Down; left alone for 20
seconds it goes back to where it started. D (X in the modern layout), or
using it again, gets you out.

```sh
./build/debug/src/app/openbattlespire            # --no-intro, --level l3.bs6, --load 2, --mute, --no-monsters, --spells all, --give 9,22
```

## Tools

`bstool` inspects and converts the game data (`bstool help` lists every command):

```sh
bstool ls GAMEDATA/3D.BSA             # list an archive
bstool bsi -o out/ WALL96.BSI         # images to PNG
bstool model -o out/ table            # 3D models to glTF
bstool level -o out/ l1               # whole levels to glTF (opens in Blender)
bstool save 0 --vars -o save0.png     # an original saved game: records, variables, picture
bstool save 0 --diff 1                # what changed between two saves
```

`bssave` shows and edits saved games. The original game's are changed field
by field, keeping every byte it doesn't touch, and written to a new folder;
ours are text, so it converts and checks them:

```sh
bssave show 2                                  # an original slot: the player, what's carried, variables
bssave fields                                  # the player's fields set takes
bssave edit 2 -o ~/SAVE5 set health=70 strength=80 long_blade=60 \
    give "Long Sword" material=9 worn give Arrow quantity=40 \
    take 81594 item 76930 condition=100 var ScampOath=1 rename "Edited"
bssave convert 2 -o game.bss                   # an original save as one of ours
bssave check game.bss                          # what's wrong in an edited one, by line
```

Point the original game at the edited folder by copying it over one of its
`SAVE0`–`SAVE9` (keep a copy of what was there).

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
