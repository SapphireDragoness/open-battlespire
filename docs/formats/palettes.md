# Palettes and colour tables

Battlespire draws on a **15-bit RGB555 screen** (verified against DOSBox
screenshots):
- BSI images use per-image colour tables of RGB555 shifted left one bit
  ([bsi.md](bsi.md)).
- Fonts use RGB555 ([fonts.md](fonts.md)).
- FLIC animations are 8-bit with their own palettes ([flic.md](flic.md)).

The 256-colour VGA palettes and the colour lookup tables below play a
smaller role, and most of them still need reverse engineering to explain how
they're used.

| File | Size | What is known |
|---|---|---|
| `PAL.RAW` | 768 | VGA palette, 6 bits per channel (values ≤ 63). Not used by BSI images (their pixels index `HICL`). |
| `LOWCOLOR.COL` | 776 | Daggerfall-style `.COL`: u32 file size (776), u32 `0xB123`, then 768 bytes of 8-bit RGB. Probably for a low-colour fallback mode. |
| `INSTALL/SPIRE.RGB` | 768 | 8-bit RGB palette for the installer screen `SPIRE.RAW` (640×480, 8-bit). |
| `SPIRE.H2L` | 32768 | One byte per 15-bit colour ("hi to low"): maps RGB555 to a palette index (16–255). It doesn't fit `PAL.RAW` well; which palette it targets is unknown. |
| `TINT.555` | 65536 | 32,768 u16: RGB555 → RGB555, **keeping only the red channel** (white → red, green → black; every entry is exactly (r, 0, 0)). **Confirmed:** the screen for 250 ms after the player is hurt (`0x43957`; see [creatures.md](creatures.md)). |
| `LAYER.DAT` | 65536 | 32,768 u16: RGB555 → RGB555, darkening and shifting towards blue (`0x7FFF` → `0x1138`). Purpose unknown; maybe underwater or a translucent layer. |
| `WATER.TBL` | 256 | Bytes 0–222; the first 32 are 0. Probably a per-colour water mapping. |
| `FADE.DAT` | 38400 | Values 0–255, mostly small. Unknown (38,400 = 150 × 256 = 160 × 240). |
| `HAZE.444`, `.555`, `.666`, `TINT.002` | 12288 | **Fog tables** (verified, see below). `555` fades to black, `444` to grey (16,16,16), `666` to blue (6,6,24), `TINT.002` to grey-blue (9,11,13). `LEVELS.TXT` picks the haze per level; `666` and `TINT.002` aren't used by any level there |
| `WATER.001` | 12288 | Same layout, but not a plain fade; probably the underwater effect |

## Fog tables (`HAZE.*`)

Reverse engineered from `GAME.EXE` on 2026-10-12, and checked against all
four files (each matches a linear blend within 2–3 steps of rounding).

```
3 channel tables in the order red, green, blue
  32 fog levels (level 31 = no fog, level 0 = full fog)
    32 entries, one per 5-bit input value: u32, the output already shifted
    into its RGB555 position (high 16 bits unused)
```

To fog a pixel, look up each of its three 5-bit channels in that channel's
table at the fog level, and OR the three results. Every file is a blend:
`out = (in·level + fog·(31 − level)) / 31`, where `fog` is the file's
colour.

In the game (`0x6DC90`), fog is a pass over spans of already-drawn pixels.
The fog level comes from a depth value interpolated across the span
(`0x6BAD0`), set up by `set_fog(start)` (`0x6B778`): it rises linearly from
the start distance to the level's view distance (`LEVELS.TXT` field 3). In
16-bit colour mode the internal fog value runs 0..16128 (63 × 256, 8.8 fixed
point); in 8-bit mode 0..3840 (15 × 256).

**Open:** taken as level units, these distances give fog about 3× too dense
compared with a screenshot of the original (level 5, UESP "Point E"; the
viewer reproduces it with `--camera 14384,-400,32696,-335.5,-14.3`). The
viewer multiplies the fog distances by 3 (`--fog-scale`), fitted by eye. The
likely cause is the unit of the `1/z` values the fog routine works on: the
projection set-up at `0x8BB14` scales by `0xF4E9C`/`0xF4EA0`, which are set
at run time.

**Next step:** describe `WATER.001`, `LAYER.DAT`, `FADE.DAT` and `TINT.555`
from the code that uses them.
