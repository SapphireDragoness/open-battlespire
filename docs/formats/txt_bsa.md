# TXT.BSA: dialogue tables and books

Status: **dialogue tables decoded** from the game's code (2026-10-16): parser
`bs_formats/talk.h`, conversations `bs_game/dialogue.h`. Every table opens,
and every reply's GoTo finds its line (`test_install_data`). The game's
rules are in [../re/README.md](../re/README.md), "Dialogue". Books are below.

254 records, all stored uncompressed: 253 `.TXT` files plus `TALKCHK.EXE`,
probably the developers' table checker. All text is Windows-1252,
tab-separated, exported from Excel (each file's header names its `.xls`
source).

## Record kinds (by name)

| Pattern | Example | Content |
|---|---|---|
| `<stem>T.TXT` | `SK1T.TXT` | **the script**: one speaker's lines and the player's replies |
| `<stem>M.TXT`, `<stem>F.TXT` | `SK1M.TXT` | **the opening**, for a male or a female player |
| `<stem>B.TXT` | `VM3B.TXT` | the opening for both |
| `BK<n>_<nnn>.TXT` | `BK2_002.TXT` | books and notes |
| `MG0_GEN.TXT`, `MG2_SPC.TXT` | | enchantments ([items.md](items.md)) |

A **stem** is three letters: two for the speaker (`SK` scamps, `CV`
Clarentavious, `JM` Jaciel …) and a digit. Speakers get theirs from BS6
`STRU` 108, or a monster from its level's default for its creature (`SK1` for
level 1's scamps). The talking heads in `FLC.BSA` take the speaker's two
letters (a monster's creature code, `SKTALK00.FLC`). The voices in
`WAVES.BSA` on the CD are named by say code (`SK1A21.WAV`).

The game reads a table as rows of cells: it turns the tabs into NULs and
0x92 into an apostrophe (`0x584C6`). A cell is column n of its row,
counting from the line's start (`0x58F86`). Lookups match a cell's start
against a code, ignoring case (`0x590C0`).

## The script (`*T.TXT`)

The first two rows are headers. Then:

| Column | Name | Use |
|---|---|---|
| 1 | Saycode | the line's code, e.g. `SK1A21` |
| 2 | NPC SAY | the line; `[[ … ]]` is narration, shown as a message when it opens a conversation |
| 3 | Replycode | the reply's code, e.g. `SK1A2A` |
| 4–6 | CHECK1–3 | conditions that must all hold for the reply to show |
| 7 | PC REPLY | the player's words |
| 8 | GoTo | the next line's code, `END`, or ranges of d100 `01-50: CF1A21 51-00: CF1A31` |
| 9 | DO THIS ACTION | actions, joined with `AND` (below) |
| 10 | SET1 | a variable set to true |
| 11, 12 | SET2, SET3 | not read by the game (one cell in all the tables uses SET2) |

- **A line's replies** are the rows around it whose reply codes share the
  say code's 4th and 5th letters (`A2` for `SK1A21`). So `SK1A21`, `SK1A22`
  and `SK1A23` offer the same replies, `SK1A2A` … `SK1A2D`.
- **Conditions** are words separated by spaces:
  - `&Name` is a variable, and `TRUE` holds.
  - `AND`, `OR` and `NOT` combine them from left to right.
  - Anything else is skipped, so a few rows' `PCFemale` and `PCMale`
    without their `&` hold nothing.
  - An empty condition holds.
- **Actions** (`0x59D62`) are a name, then a number after `_` or a space.
  Some take a second number after another `_`, and some a creature code
  first.

  | Action | Does |
  |---|---|
  | `DropReadyWeapon` | the player drops the weapon in hand: "You comply and drop your weapon on the ground." (508) |
  | `MonsterAttack`, `MonsterSeek` | the speaker attacks (AI state 8) |
  | `MonsterWander_n`, `MonsterFlee_n` | it wanders off (14) |
  | `MonsterWait_n` | it waits to talk (13) |
  | `MonsterRally_n` | 4 more of its kind come, whatever n |
  | `Command XX_n` | the speaker takes AI state n |
  | `CommandAll XX_n` | every monster of creature XX takes state n |
  | `Summon XX_n` | n monsters of creature XX come |
  | `PCTeam` | the speaker joins the player |
  | `HealthHalf` | the player loses half their health |
  | `Kill` | the speaker is gone when the conversation ends |
  | `Teleport_n` | the speaker vanishes, now (0) or after n seconds |
  | `PUT b_n`, `DROP b_n`, `TAKE b_n` | an item of base b with number n (a book's text, an enchantment, 10000 plain) is given to the player, dropped by the speaker, or taken |
  | `DialogLink_n` | the objects with `STRU` 104 = n are used |
  | `ENDFRAME_n` | the end: CHOP2 and text n (`FS1T`'s `ENDFRAME_0036` is a typo for 1036; the original quits on it) |

  - The game ignores `PcAttack`, `MonsterGuard`, `MonsterLead`, `PainSFX`,
    and names it doesn't know.
  - Ignored too are the tables' slips: `MonsteraAttack` (11 rows),
    `MonsterAtttack`, `SummonVM_2` (no space), `Teleport120`,
    `TanchelmTeleport0`.

## The opening (`*M.TXT`, `*F.TXT`, `*B.TXT`)

```
talkcheck                              rows: condition (column 1), say code (column 2)
Scamp   SKNoTalk     SK1A42   local    the first condition that holds picks the line
        ScampMad     SK1A31
talktable
        First        SK1A21   global   one row per conversation with this creature type so far
        Second       SK1A61
        Third        SK1A13
        Fourth +     GotoRandom        the fourth on: a d100 roll on the random rows
Random  Male PC
                     TalkGroup Notes
        01-10        SK1A11   Drop weapon
        ...
```

The words `local` and `global` are only notes. A variable a `talkcheck`
condition names becomes the speaker's own unless it's already global.

- **Gaps in the data:** some tables leave later visits empty, write `Random`
  for `GotoRandom` (FR9, TA1, TF1), or name a line their script doesn't have.
  The engine opens nothing there; the game would read past the table.
- **Test tables:** TA1 and TF1 ("Test Actions and Flags", "Test Flags").
  TF1 has notes in its condition columns.

## Books (`BK<n>_<nnn>.TXT`)

Verified against the 58 book files and the book screen's code
(`0x3C89B`, 2026-10-14); parser: `bs_formats/book.h`.

- **Which file:** a book or scroll item's number `nnn` finds
  `bk??%03d.txt` (`0x3C8BA`): the digit after `BK` (1–8, roughly the level)
  isn't used. All 48 books and scrolls placed in the levels find theirs.
- **Text:** Windows-1252 lines ending in CR LF. One line break is a space;
  two end a paragraph and leave an empty line; `^` or `$` end a line (`$`
  at the start of one gives an empty line). Control bytes aren't shown.
- **Codes** (`0x3CEDF`), four characters: `%PCF` the character's first name,
  `%PCN` the full name, `%APF` and `%APS` the companion's first and second
  name (Josian Kaid for a female character, Vatasha Trenelle for a male
  one). The books use `%APF` (10), `%APS` (1) and `%PCF` (7).
- **Pages** (`0x3CAFE`): `textbk.bsi`, text in `FONT0003` from x 80, lines
  wrapped before x 560 (480 pixels, counted without filling in codes) at the
  last space, one font height apart from y 53 while y < 410 − the font
  height. Buttons (`0xEFB17`): previous page (598, 79 – 620, 119), next
  page (598, 362 – 620, 401), close (582, 424 – 630, 474).
