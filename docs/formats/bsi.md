# BSI images

Textures, sprites (monsters, items, effects) and UI screens, stored in
`BSI.BSA` (2,592 `.BSI` records). The format is related to Redguard's
TEXBSI.

Status: **verified**, including the shade table. All 2,592 records parse into 2,621 images and decode
to exactly the expected pixel counts (`tests/test_install_data.c`). Colours
were checked by eye against known in-game screens. They should still be
compared with DOSBox screenshots.

## Chunks

A file is a sequence of chunks: a 4-character tag plus a **big-endian** u32
length.

| Chunk | Size | Content |
|---|---|---|
| `BSIF` | — | Optional container header for multi-image files: the tag plus 4 bytes that aren't a usable length. Skip 8 bytes. |
| `IFHD` | 44 | File header. Inside `BSIF` containers the first u32 is the image count; otherwise always `01 00 …`. |
| `NAME` | var | Optional NUL-terminated name (228 images). |
| `BHDR` | 26 | Image header, see below |
| `HICL` | 256 | **The image's colours: 128 values, RGB555 shifted left one bit** (missing in 1 image) |
| `HTBL` | 8192 | Shade table: 32 levels × 128 RGB555 colours (missing in 98 images), see below |
| `CMAP` | 768 | A 6-bit VGA palette that doesn't match the pixels; see "Colour" |
| `DATA` | var | Pixel data, encoded per `BHDR` |
| `END ` | — | End of file. **Its length field may be garbage** (`TEST0.BSI`, `PANEL1.BSI`), so stop at the tag. |

An image runs from `IFHD` (or from `BHDR`, in the old-style `SKEL00W0.BSI`)
to its `DATA` chunk. A `BSIF` container holds several images back to back,
for example the 11 pieces of the spell menu in `SPMENU.BSI`. `BIP.BSI` holds
only `END ` (no images).

## BHDR (13 × i16, little-endian)

| # | Meaning | Values |
|---|---|---|
| 0, 1 | x, y placement. For UI pieces this is the screen position (e.g. the spell menu bar at 0,380) | |
| 2, 3 | width, height | |
| 4 | **3 = pixel 0 is transparent** (sprites, cut-outs), 2 = opaque, 1 = one test image | inferred from content |
| 5, 6 | always 0 | |
| 7 | frame count | 1–21+ |
| 8 | unknown; 71 for most images, 142/171/100/114 for monster sprites. Maybe animation timing | |
| 9 | unknown; 0, 1 or 2 | |
| 10 | always 0 | |
| 11 | unknown; 256 for most, 56–184 for monster sprites. Maybe a colour count | |
| 12 | **encoding**: 0, 4 or 6 | |

## Pixel encodings

- **0, raw:** `width × height × frames` bytes, frame after frame (2,046 images).
- **4 and 6, row table:** `DATA` starts with one u32 per row of every frame
  (`height × frames` entries), each an offset from the start of `DATA`.
  - **Bit 31 clear:** a raw row of `width` bytes at that offset. Identical rows
    share their bytes; that sharing is all encoding 4 does (15 images).
  - **Bit 31 set (encoding 6 only):** an RLE row. Control byte `c`: if
    `c < 0x80`, copy `c` literal bytes; if `c ≥ 0x80`, repeat the next byte
    `c & 0x7F` times. Each row decodes to exactly `width` pixels.

  In encoding 6, 458 images use only RLE rows and 63 mix RLE and raw rows.

## Colour

**Every pixel value in every image is even** (a data test checks this), and
the colour is `HICL[pixel / 2]`. Each `HICL` entry is **RGB555 in bits 15–1**
(bit 0 unused): the screen colour is `HICL[pixel / 2] >> 1` as RGB555. The
game draws on a 15-bit RGB555 screen.

Verified **pixel for pixel** against DOSBox screenshots (2026-10-11): the HUD
frame `FRAM00`, the help button `QUES00` and the inventory filter buttons
`FILT00`–`FILT05` match exactly at their `BHDR` positions.

Reading `HICL` as RGB565 instead (as M2 first did) gives nearly the right
red and green but roughly doubles blue. That looks plausible by eye, which is
why it went unnoticed until there were real screenshots to compare against.

The character-creation screens (race selection and its photos) match only
approximately (60–70% of pixels within one step). Those screens seem to be
reduced to a smaller colour set when drawn. How isn't known yet.

`CMAP` gives believable colours for some textures but clearly wrong ones for
UI screens. It's probably a leftover from the 8-bit art pipeline. Neither
`CMAP` nor the global `PAL.RAW` should be used for display.

`HTBL` is the **shade table**: 32 brightness levels × 128 colours, each a
plain RGB555 value (no shift, unlike `HICL`). A texel with pixel value `p`
drawn at shade level `L` (0–31) is `HTBL[L × 128 + p / 2]`.
- **Level 31 is identical to `HICL`**, verified for every entry of four
  textures.
- **Level 0 is black.** The levels in between scale almost linearly (level 15
  ≈ 47 %).
- Images without `HTBL` (98) presumably aren't drawn with lighting.

How the game picks the level is in `docs/re/README.md` (lighting).

## Tools

```sh
bstool bsi [record...]                 # print headers (default: all records)
bstool bsi -o out/ [record...]         # export indexed PNGs, one per frame
bstool bsi -o out/ --palette cmap      # compare: CMAP or PAL.RAW ("global")
```
