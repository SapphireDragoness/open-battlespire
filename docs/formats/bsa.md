# BSA archives

Used by `3D.BSA`, `3D.BS6`, `BSI.BSA`, `BS6.BSA`, `TXT.BSA`, `FLC.BSA`,
`SPIRE.SND` (installed) and `WAVES.BSA` (CD only). Same family as Daggerfall's
BSA files, with compression added.

Status: **verified**. All 8,724 records in the eight archives read and
decompress (`bstool verify`, `tests/test_install_data.c`).

## Layout

All integers little-endian.

```
offset 0        u16  record_count
offset 2        u16  type            0x0100 named, 0x0200 numbered
offset 4        record data, back to back, in directory order
size - dir_len  directory, record_count entries
```

Named entry (18 bytes):

```
char name[12]     NUL-padded; may fill all 12 bytes without a terminator
u16  flags        0x0100 = LZSS-compressed, 0x0000 = stored
u32  size         stored size in bytes
```

Numbered entry (8 bytes), used only by `SPIRE.SND`:

```
u16  id
u16  flags        always 0 in shipped data
u32  size
```

*Inferred:* reading the numbered entry as `u16 id, u16 flags` rather than
`u32 id` mirrors the named layout. Every upper half is 0 in the shipped data,
so the two readings can't be told apart.

Record offsets aren't stored. Each record starts where the previous one ends,
so the first is at offset 4. In all shipped archives the records end exactly
where the directory begins.

Names are matched case-insensitively. No archive contains duplicate names.

## Compression

Records with `flags & 0x0100` use **Okumura LZSS** (`LZSS.C`, 1989), with no header:

- 4096-byte ring buffer, **pre-filled with spaces (0x20)**
- first write position 0xFEE (`N - F`, where F = 18)
- a flag byte covers the next 8 items, read **LSB first**: 1 = literal byte, 0 = reference
- reference `b0 b1`: ring position `b0 | (b1 & 0xF0) << 4`, length `(b1 & 0x0F) + 3`
- no stored output size; decoding stops when input runs out

**Verification:**
- All 213 `.3d` files that ship both loose and archived decompress
  byte-identically (110 in `3D.BSA`, 103 in `3D.BS6`).
- The space fill is confirmed by 28 `BSI.BSA` records that reference ring
  bytes before writing them. One is `TMP.LST`, a DOS `dir` listing whose
  column padding only comes out right with spaces. (A zero fill gives NUL
  bytes instead.)

Compression usage: `3D.BSA` 2398 of 2400 records, `3D.BS6` all, `BSI.BSA` 577
of 2599, other archives none.

## Sound archives

`SPIRE.SND` (370 numbered records, ids 2–507) and `WAVES.BSA` (775 named
`*.WAV` records) hold **complete RIFF WAV files**, all PCM, 8-bit unsigned and
mono. (UESP describes `WAVES.BSA` as headerless PCM, but in this release it
isn't.) `bs_formats/wav.h` parses them, and the data test checks every record.

| Archive | Records | Sample rates |
|---|---|---|
| `SPIRE.SND` | 370 sound effects | 280 at 11025 Hz, 89 at 32000 Hz, 1 at 8114 Hz (id 6) |
| `WAVES.BSA` | 775 voice lines | all 11025 Hz |

The WAV header is the plain 44-byte form, but the parser walks the chunks
anyway. It skips unknown chunks and clips a `data` chunk that claims more
bytes than the file holds.

Which effect each `SPIRE.SND` id is, and which code plays it, is not yet
known. `WAVES.BSA` names follow the dialogue tables in `TXT.BSA`.

## Music

The GOG release ships the CD audio tracks as `MUSIC/Track02.ogg` to
`Track08.ogg` (track 1 is the data track). `game.ins` lists them as the CD's
audio tracks, so the DOS game played them as CD audio (presumably through
MSCDEX; not checked in the code yet). The engine decodes them with stb_vorbis (vendored in
`third_party/stb_vorbis`).

Each level plays CD track (level number + 1), looped: Lv1–Lv6 tracks 2–7,
every multiplayer level track 5, the title menu track 6. The final
level asks for track 9, which doesn't exist, so it has no music, and track 8
is never used. The rule is found by static analysis
([RE notes](../re/README.md#music)) and is implemented as
`bs_level_music_track()` in `bs_formats/levels.h`.

## Archive contents of note

Development leftovers that ended up in the archives:

| Archive | Record | What it is |
|---|---|---|
| `3D.BSA` | `3D.BSA` | an older copy of the archive itself |
| `3D.BSA`, `TXT.BSA` | `*.EXE` (e.g. `TALKCHK.EXE`) | developer tools |
| `BSI.BSA` | `TMP.LST`, `*.TXT`, `*.BAT`, `*.ERR`, `*.CEL` | build files; `TMP.LST` lists `n:\projects\batspire\bsa\bsi` |
| `BS6.BSA` | `C` | not C source: a BS6 level (an interior with `lust*` models from the `l5` art folder), followed by unrelated bytes |
| `BS6.BSA` | `L8OLD`, `L4_OLD`, `L4NEW`, `L12`, `BACKUP`, `FIXIT`, `IN*.BS6` | old or unused levels and interiors |

## Tools

```sh
bstool ls [-u] GAMEDATA/3D.BSA
bstool extract WAVES.BSA -o out/ [record...]   # --raw keeps compression
bstool cat GAMEDATA/TXT.BSA SM1T.TXT --text
bstool verify
```
