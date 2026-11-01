# CD image (GOG `game.gog` + `game.ins`)

The GOG release ships the original CD as a raw image. Some data exists only
on it:

- `BATSPIRE/WAVES.BSA`: 775 voice and sound-effect WAVs, 104 MB
- `VIDEOS/*.SMK`: Smacker cutscenes (`STORY`, `MALE`, `FEMALE`, `ANCHORS`, `CHOP2`, `JUMP`)

The engine reads the image in place; nothing needs extracting.

Status: **verified** against the GOG image (`tests/test_install_data.c`) and
synthetic images (`tests/test_cdimage.c`).

## Cue sheet

`game.ins` is a standard cue sheet despite its extension:

```
FILE "game.gog" BINARY
  TRACK 01 MODE2/2352
    INDEX 01 00:00:00
FILE "music\Track02.ogg" MP3
  TRACK 02 AUDIO
...
```

Track 1 is data; tracks 2–8 are the CD music, which GOG supplies as the
`MUSIC/*.ogg` files. The reader uses the `FILE` of the first `MODE*` track,
resolved case-insensitively relative to the cue sheet.

## Sectors

Raw sectors are 2352 bytes: 12 sync bytes `00 FF×10 00`, a 4-byte header
(minute, second, frame, mode), then for **Mode 2 Form 1** an 8-byte subheader,
then 2048 bytes of user data. User data is at offset 24 for Mode 2 and offset
16 for Mode 1.

The format is detected from the sync pattern and mode byte of sector 0. A
file without the sync pattern is treated as a plain 2048-byte-sector ISO, so a
`.iso` file works too.

## Filesystem

ISO 9660 (volume id `BSPIRE`, burned with ImgBurn; a UDF bridge is also
present but not used). The primary volume descriptor is at sector 16, and the
root directory record is at offset 156 within it. Directory records don't
cross sector boundaries; a zero length byte means "skip to the next sector".
File identifiers have their `;1` version suffix and any trailing `.` removed.

## VFS layout

`bs_install_mount()` mounts, from lowest to highest priority:
1. the CD root
2. CD `BATSPIRE/`
3. the install directory

A loose file in the install always wins, and anything missing from the
install falls back to the CD's `BATSPIRE/` folder (which holds a complete
`GameData/`).
