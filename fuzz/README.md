# Fuzzing

libFuzzer harnesses for every parser that reads untrusted data. They build
with Clang and AddressSanitizer + UBSan.

| Target | Covers |
|---|---|
| `lzss` | LZSS decompression |
| `bsa` | BSA directory parsing, lookup, read and decompress of every record |
| `bsi` | BSI chunk parsing and pixel decoding (raw, row-table and RLE encodings) |
| `flic` | FLIC header and frame decoding (palette, byte-run, FLI/FLC deltas) |
| `font` | font parsing and glyph access |
| `rsc` | TEXT.RSC parsing, lookup, UTF-8 rendering; Windows-1252 conversion |
| `levels` | LEVELS.TXT parsing |
| `cdimage` | CD sector detection, volume descriptor, directory walking, file reads; cue sheet parsing |
| `le` | LE executable loading: object table, page map, fixups |
| `save` | saved games' record trees, controllers and SAVEVARS.DAT; class files; importing a tree |
| `talk` | dialogue tables, and a conversation run on them |
| `game_save` | our saved games' text (and the sectioned text reader under it): what reads must write and read again |

```sh
cmake --preset fuzz && cmake --build --preset fuzz
fuzz/run.sh bsa 600             # fuzz for 10 minutes
BS_DATA_DIR=~/Games/Battlespire fuzz/run.sh le 600   # also seed from real game files
```

Seeds come from `make_seeds`, which builds synthetic inputs with the same
builders the unit tests use (`tests/synth.c`). With `BS_DATA_DIR` set, the
script also copies a few real files from your install as seeds (TEXT.RSC,
LEVELS.TXT, GAME.EXE and some compressed 3D records).

All corpora, real-data seeds and crash files stay under `build/fuzz/`, which
is gitignored. They may contain game data, so never commit them.

To reproduce a crash, run the harness on the file:
`build/fuzz/fuzz/fuzz_bsa build/fuzz/crashes/bsa/crash-…`. Then turn the
input into a unit test with synthetic data before fixing.
