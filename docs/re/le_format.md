# LE executable format (as used by GAME.EXE)

The 32-bit "Linear Executable" format of DOS extenders and OS/2 2.x
(Watcom/CauseWay here). Implemented by `src/formats/src/le.c`, which
`bstool le2elf` uses to convert the EXE to ELF.

Status: **verified** on GAME.EXE (v1.5). All 32,001 fixup records parse, and
the parse ends exactly at the end of the fixup record table. Relocated
pointers in the data objects resolve to real strings and tables.

## GAME.EXE layout

| | |
|---|---|
| MZ stub | CauseWay loader; `e_lfanew` (offset 0x3C) = `0xB680` → LE header |
| Pages | 220 × 4096 bytes; last page 0x3DE bytes; data pages at file offset `0x50A00` (file-relative, not LE-relative) |
| Object 1 | code, base `0x10000`, virtual size `0xAD900`, 174 pages, flags `0x2045` (R X 32-bit) |
| Object 2 | data, base `0xC0000`, virtual size `0x18D1`, 2 pages, flags `0x2043` (R W 32-bit) |
| Object 3 | data, BSS and stack, base `0xD0000`, virtual size `0x72F990`, 44 pages, flags `0x2043` |
| Entry | object 1 + `0x601B0` = `0x701B0` |
| Stack | object 3 + `0x72F990` (top of object 3) |
| Imports, resources, debug info | none |
| Fixups | 31,999 32-bit offsets (28,546 with 32-bit and 3,453 with 16-bit target offsets), 2 selector fixups |

## Structures

All offsets are relative to the LE header unless noted otherwise.

| Offset | Field |
|---|---|
| `0x00` | `"LE"`, byte order 0, word order 0 |
| `0x14` | module page count |
| `0x18`, `0x1C` | EIP object (1-based), EIP offset |
| `0x20`, `0x24` | ESP object, ESP offset |
| `0x28`, `0x2C` | page size, bytes used on the last page |
| `0x40`, `0x44` | object table offset, object count |
| `0x48` | object page map offset |
| `0x68`, `0x6C` | fixup page table offset, fixup record table offset |
| `0x80` | data pages offset (**relative to the file**) |

**Object table entry (24 bytes):** virtual size, base address, flags
(`0x1` R, `0x2` W, `0x4` X, `0x2000` 32-bit), first page (1-based), page
count, reserved.

**Page map entry (4 bytes):** 24-bit **big-endian** data page number, then a
type byte (0 = in file, 3 = zero-filled). In GAME.EXE every page is type 0 and
the pages are in order.

**Fixup page table:** `page_count + 1` u32 offsets into the record table. The
records for module page *p* lie between entries *p−1* and *p*.

**Fixup record:**

```
u8 source      low nibble type: 0 byte, 2 selector, 3 16:16, 5 off16, 6 16:32, 7 off32, 8 rel32
               0x20 = source list (count follows instead of one offset)
u8 flags       low 2 bits target: 0 internal, 1 import ordinal, 2 import name, 3 entry table
               0x04 additive, 0x10 32-bit target offset, 0x20 32-bit additive,
               0x40 16-bit object/module number, 0x80 8-bit ordinal
[u8 count | i16 source offset]   source offset is signed: page-spanning fixups
                                 appear in both pages, the second with a negative offset
internal: object (u8/u16), then target offset (u16/u32) unless type is selector
[additive u16/u32]
[count × i16 source offsets]     if source list
```

Applied value: `target object base + target offset + additive`. Self-relative
fixups subtract the address after the 4-byte field. Selector fixups have no
meaning in a flat ELF image, so they're counted but not applied.
