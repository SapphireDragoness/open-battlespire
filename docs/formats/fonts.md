# Fonts (FONT0001–0010.FNT)

Ten bitmap fonts in two sizes:
- `FONT0001`–`0005`: glyphs up to 15×16, colour variants (white, red, …)
- `FONT0006`–`0010`: small, up to 8×9

`INSTALL/SPIRE.FNT` is the installer's font, in a different format. It's
not documented here.

Status: **verified**. All 10 fonts parse, each with 134 glyphs. The glyph
sheets render correctly (`bstool fnt -o out/`).

## Layout

```
u32 count                              241
count x { u16 width; u16 height; }
count x u32 offset                     absolute file offset of each glyph's pixels
pixels                                 width x height little-endian u16, RGB555; 0 = transparent
```

- **Glyph i is character 33 + i**, starting with `!`. There's no space glyph;
  the space width must come from the game code.
- Glyphs whose offset equals the file size are **absent** (stored as 1×1).
  Glyphs 134–240 are all absent, so the fonts cover characters 33–166.
- Glyphs may share pixel data.
- **Colour is RGB555.** In this format `FONT0001` is white and `FONT0002` is
  red-brown; read as RGB565 they come out cyan and green. The screen is
  RGB555 too, so fonts can be copied as is; BSI colour tables are RGB555
  shifted left one bit (see [bsi.md](bsi.md)).

## Character set mismatch

The glyphs above 127 follow **DOS code page 437**: 128 = `Ç`, 129 = `ü`,
130 = `é`, … 165 = `Ñ`, 166 = `ª`. But the game's text files are
**Windows-1252** (see [README.md](README.md)). If the game renders text
bytes directly:
- the right single quote `’` (0x92) would draw as `Æ`;
- `“ ”` (0x93/0x94) would draw as `ô ö`;
- `…` (0x85) would draw as `à`;
- `é` in "Papré" (0xE9) would have no glyph at all.

**To check in DOSBox:** what does dialogue containing "you’ve" or "…"
actually look like in game? That decides whether we reproduce the original
or show the intended characters.
