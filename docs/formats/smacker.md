# Smacker videos (VIDEOS/*.SMK)

The CD's `VIDEOS` folder holds six RAD Game Tools Smacker videos. They are
not installed; the engine reads them from the CD image
([cd_image.md](cd_image.md)).

Status: **verified**. All six files decode to the end (6,180 frames). In
each, the decoded audio lasts as long as the video to within one frame.
Frames exported from every file look right: text, interlaced and doubled
lines, palette fades. `bs_formats/smacker.h` implements the decoder.

| File | Stored size | Display | Frames | Rate | Audio | Plays |
|---|---|---|---|---|---|---|
| `STORY.SMK` | 640×240 | doubled | 2371 | 15 fps | 11025 Hz | the intro (158 s), at start-up before the title menu |
| `MALE.SMK` | 640×240 | doubled | 1791 | 15 fps | 11025 Hz | the winning ending (119 s), male character |
| `FEMALE.SMK` | 640×240 | doubled | 1791 | 15 fps | 11025 Hz | the same, female character |
| `ANCHORS.SMK` | 640×480 | — | 101 | 30 fps | 32000 Hz | a joke ending: level 1 with all its generators turned off |
| `CHOP2.SMK` | 640×240 | interlaced | 80 | 15 fps | 32000 Hz | the "captivity" game over (a skeleton in a cell), followed by a text |
| `JUMP.SMK` | 640×480 | — | 46 | 15 fps | none | between levels (a green vortex) |

All audio is 8-bit mono and compressed. `MALE.SMK` and `FEMALE.SMK`
decode to byte-identical soundtracks but different frames. The exact
triggers are in the [RE notes](../re/README.md#videos-found-2026-10-13);
they come from static analysis and haven't been watched in the game.

## Format

This is standard Smacker version 2 (`SMK2`) with nothing
Battlespire-specific. Version 4 (`SMK4`) adds block modes that no shipped
file uses, and the parser rejects it as unsupported. Little-endian
throughout.

### Header (104 bytes)

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `SMK2` |
| 4 | 4 | width |
| 8 | 4 | height (stored lines) |
| 12 | 4 | frame count |
| 16 | 4 | frame rate: > 0 ms per frame, < 0 units of 10 µs, 0 = 10 fps |
| 20 | 4 | flags: bit 0 ring frame, bit 1 y-interlaced, bit 2 y-doubled |
| 24 | 7×4 | largest unpacked audio size per track |
| 52 | 4 | size of the tree data |
| 56 | 4×4 | unpacked sizes of the four video trees (`MMAP`, `MCLR`, `FULL`, `TYPE`) |
| 72 | 7×4 | per track: bits 0–23 rate, 31 present, 30 compressed, 29 16-bit, 28 stereo |
| 100 | 4 | reserved |

Then come the frame sizes (u32 each; the low two bits are flags, bit 0
marks a keyframe), one frame-type byte per frame, the tree data, and the
frames. With a ring frame, every table has one extra entry.

The display height is twice the stored height for doubled and interlaced
videos. A doubled video shows each line twice; an interlaced one follows each
line with a black line.

### Huffman trees

The bit reader takes bits LSB first.

- **Byte trees**: bit 1 means a branch (the 0-child comes first); bit 0
  means a leaf followed by an 8-bit value.
- **Big trees** have 16-bit leaves. First come two byte trees (low byte, high
  byte), each preceded by a present bit and followed by a terminating bit.
  Then three 16-bit escape values, then the tree, whose leaf values are
  decoded with the two byte trees. A leaf equal to an escape value becomes a
  cache slot. The three slots hold the three most recently decoded values:
  a value that differs from the newest pushes the others down. The cache
  resets to 0 at every frame.

The header's tree data holds the four video trees in order. Each is a
present bit, then a big tree, then a terminating bit. An absent tree
always decodes to 0.

### Frames

A frame holds the following parts in order, as flagged by its type byte:

1. **Palette** (bit 0). A length byte (× 4 is the part's total size, length
   byte included), then commands until 256 entries are filled:
   - `1nnnnnnn`: skip n+1 entries (keep them).
   - `01nnnnnn s`: copy n+1 entries from entry s of the previous palette.
   - `00rrrrrr g b`: a literal 6-bit colour, expanded as `(v << 2) | (v >> 4)`.
2. **Audio** for each track t whose bit (t+1) is set: a u32 length (counting
   itself), then the samples. A compressed track starts with a u32 unpacked
   size and a present bit; then come stereo and 16-bit bits (which must match
   the header), and one byte tree per channel and byte. The first samples
   are stored raw, as a big-endian 16-bit value for 16-bit audio. Each
   later sample adds a Huffman-coded delta to the previous one (DPCM). The
   first frame preloads about a second of audio.
3. **Video**: the rest of the frame, as a bit stream over 4×4 blocks in
   row order. A `TYPE` value gives the block type (bits 0–1), a run length
   (bits 2–7, through a 64-entry table: 1–59, then 128, 256, 512, 1024,
   2048) and, for solid blocks, a colour (bits 8–15):
   - **mono**: `MCLR` gives two colours and `MMAP` a 16-bit mask; a set bit
     selects the high colour.
   - **full**: per row, two `FULL` values give pixels 2–3, then 0–1.
   - **skip**: the block is unchanged.
   - **solid**: the whole block in one colour.

The palette is 8-bit RGB; the 15-bit screen conversion happens at display
time, as for FLIC.

## Tools

```sh
bstool smk                                  # decode all six, check audio length
bstool smk -o out/ --every 100 STORY.SMK    # export every 100th frame as PNG, audio as WAV
bstool smk -o out/ --cut 2 ANCHORS.SMK      # also write the first 2 frames as ANCHORS_cut2.smk
bsplay video STORY                          # play with sound
```
