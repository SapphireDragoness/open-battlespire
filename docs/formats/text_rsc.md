# TEXT.RSC

String table for spell descriptions, item descriptions, journal and book
excerpts, and UI text. Same layout as Daggerfall's `TEXT.RSC`.

Status: **layout verified** (all 300 records parse; `bstool rsc`). Control
codes are only partly understood.

## Layout

```
u16 table_size                 size of the entry table in bytes (1806 = 301 × 6)
{ u16 id; u32 offset; } × N    sorted by id; offsets ascending
string data
```

The last entry is a **sentinel**: id `0xFFFF`, offset = file size. A record
runs from its offset to the next entry's offset. 300 records (ids 0–1155,
not contiguous) plus the sentinel. Record 14 is empty.

## Text

Windows-1252, for example "Dragonne Papr**é**" (0xE9).

| Byte | Count | Meaning |
|---|---|---|
| `0xFE` | 299 | end of record; every non-empty record ends with it (verified) |
| `0xFD` | 91 | line break (inferred from context: verse lines in book excerpts) |
| `0xFC` | 165 | line break; in Daggerfall it means a centred line (unverified here) |
| `0xFA` + 1 byte | 3 | **unknown escape**. All 3 occurrences are `FA 00`, each exactly where an `&` belongs ("wizards `FA 00` guards dead", "a cloak `FA 00` amulet"). Probably a literal-ampersand escape, since `&` marks variables in the dialogue tables. |

`bs_rsc_to_utf8()` renders `0xFD`/`0xFC` as `\n` and other codes as `{XX}`
or `{XX:YY}`, so unknown codes stay visible.
