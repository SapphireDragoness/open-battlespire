# File format documentation

One page per format. Each page states what is **verified** (checked against
all shipped data, usually by a data test), what is **inferred** (fits the
data but has no independent confirmation), and what is **unknown**.

| Format | Files | Page | Status | Parser |
|---|---|---|---|---|
| BSA archive | `*.BSA`, `3D.BS6`, `SPIRE.SND` | [bsa.md](bsa.md) | Verified | `bs_formats/bsa.h` |
| LZSS compression | compressed BSA records | [bsa.md](bsa.md#compression) | Verified | `bs_formats/lzss.h` |
| CD image | `game.gog` + `game.ins` | [cd_image.md](cd_image.md) | Verified | `bs_core/cdimage.h` |
| TEXT.RSC | `GAMEDATA/TEXT.RSC` | [text_rsc.md](text_rsc.md) | Layout verified, control codes partial | `bs_formats/rsc.h` |
| LEVELS.TXT | `GAMEDATA/LEVELS.TXT` | [levels_txt.md](levels_txt.md) | Layout verified, several fields unknown | `bs_formats/levels.h` |
| Dialogue tables and books | `TXT.BSA` | [txt_bsa.md](txt_bsa.md) | Verified: books, dialogue scripts and openings (all 76 conversations) | `bs_formats/book.h`, `bs_formats/talk.h` |
| Sounds | `SPIRE.SND`, `WAVES.BSA` records | [bsa.md](bsa.md#sound-archives) | Verified: standard RIFF WAV; id → effect unknown | `bs_formats/wav.h` |
| Music | `MUSIC/Track02–08.ogg` | [bsa.md](bsa.md#music) | Ogg Vorbis (CD audio); level → track unknown | stb_vorbis |
| AI navigation graphs | `NODES.*` | [nodes.md](nodes.md) | Layout verified; some node fields unknown | `bs_formats/nodes.h` |
| Creatures | `GAME.EXE` tables, BS6 `STRU` 106 | [creatures.md](creatures.md) | Tables, spawning, AI states and melee decoded; spells and dialogue not yet | `bs_formats/creatures.h` |
| Items | `GAME.EXE` tables, `TXT.BSA` `MG0_GEN.TXT`, `MG2_SPC.TXT` | [items.md](items.md) | Tables verified against the data; placement, names and weight decoded; use and effects not | `bs_formats/items.h` |
| Spells | `GAME.EXE` table | [spells.md](spells.md) | Decoded: table, casting, missiles, effects, monsters' spells, spell book | `bs_formats/spells.h` |
| Character classes | `GAMEDATA/CLASSnnn.SCL` | [scl.md](scl.md) | Verified (all 18; build point costs check out) | `bs_formats/scl.h` |
| Saved games | `SAVE0–9/SAVETREE.DAT`, `SAVEVARS.DAT`, `SAVENAME.DAT`, `IMAGE.RAW` | [save.md](save.md) | Decoded from the save code; checked against three saves (read, imported, edited and written back byte for byte) | `bs_formats/save.h`, `bs_game/save_import.h`, `bs_game/save_edit.h` |
| OpenBattlespire's saves | `saves/SAVE0–9/GAME.BSS`, `IMAGE.RAW` | [game_save.md](game_save.md) | Ours: a text format | `bs_game/game_save.h` |
| Built-in messages | `GAME.EXE` string table | [../re/README.md](../re/README.md#messages-and-using-things-2026-10-14) | Verified (543 messages) | `bs_formats/exe_text.h` |
| Smacker videos | CD `VIDEOS/*.SMK` | [smacker.md](smacker.md) | Verified | `bs_formats/smacker.h` |
| BSI images | `BSI.BSA` | [bsi.md](bsi.md) | Verified; `HTBL` lighting layout open | `bs_formats/bsi.h` |
| FLIC animations | `FLC.BSA` | [flic.md](flic.md) | Verified | `bs_formats/flic.h` |
| Fonts | `FONT0001–0010.FNT` | [fonts.md](fonts.md) | Verified | `bs_formats/font.h` |
| Palettes and colour tables | `PAL.RAW`, `*.COL`, `TINT.*`, `HAZE.*`, … | [palettes.md](palettes.md) | Partial | `bs_formats/palette.h` |
| 3D models | `3D.BSA`, loose `*.3d` | [3d.md](3d.md) | Verified; face byte and object data unknown | `bs_formats/model.h` |
| BS6 levels | `BS6.BSA`, loose `*.bs6` | [bs6.md](bs6.md) | Structure verified; entity logic (`STRU`/`CTRL`/`LINK`) not decoded | `bs_formats/bs6.h` |


**Text encoding.** All text data seen so far is **Windows-1252**, not DOS
code page 437. The dialogue was authored in Excel (`file SM1M.xls` headers),
and contains `0x85` (…), `0x92` (’), `0x93`/`0x94` (“ ”) and `0xE9` (é, in
"Dragonne Papré"). The game's fonts, however, are in code page 437 order;
see [fonts.md](fonts.md#character-set-mismatch).
