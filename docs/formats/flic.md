# FLIC animations (FLC.BSA)

`FLC.BSA` holds 163 Autodesk FLC animations, plus `ADR.TXT`, which contains
the single number `435223`. They are the conversation screens: a 640×480
frame showing the speaking NPC on a stone background, above a parchment text
area. Names combine a speaker prefix with a state: `SMWAIT00` (Sumeer,
idle), `XMTALK01` (talking) and so on. The prefixes match the dialogue
tables in `TXT.BSA`.

Status: **verified**. All 163 files decode, 7,365 frame chunks in total. In
every file the final "ring" frame reproduces frame 1 byte for byte, which
checks the delta decoder end to end.

## Format

This is standard FLC (magic `0xAF12`) with nothing Battlespire-specific.
Every file is 640×480 at 8 bits, and frames are 71 ms apart (about 14 fps).

- 128-byte header. The first frame's offset is at byte 80. A `0xF100`
  prefix chunk comes before the first frame.
- Each frame chunk (`0xF1FA`) holds sub-chunks. These files use:
  - `4` COLOR_256 (8-bit palette)
  - `15` BYTE_RUN (first frame)
  - `7` DELTA_FLC (word-oriented delta)
  - `18` PSTAMP (thumbnail, skipped)
- The decoder also handles `11` COLOR_64, `12` DELTA_FLI, `13` BLACK and
  `16` COPY.
- After `frames` frames comes a ring frame: a delta back to frame 1. To loop,
  continue from frame 2.

The 8-bit palette is converted to the 15-bit (RGB555) screen format for display.

## Tools

```sh
bstool flc                     # decode everything, check ring frames
bstool flc -o out/ [--every N] [record...]   # export frame 0 (or every Nth)
```
