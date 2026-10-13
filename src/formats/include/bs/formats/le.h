#ifndef BS_FORMATS_LE_H
#define BS_FORMATS_LE_H

#include <stddef.h>
#include <stdint.h>

#include "bs/core/arena.h"
#include "bs/core/result.h"

/*
 * Loader for 32-bit DOS "LE" (linear executable) programs such as GAME.EXE
 * (Watcom C, CauseWay DOS extender). Used by `bstool le2elf` to produce an
 * ELF file that Ghidra and objdump can read; the engine itself never runs it.
 *
 * Each object is loaded at its preferred base address, with pages copied
 * from the file and 32-bit fixups applied, as the DOS extender would.
 * Selector fixups have no flat equivalent and are left as stored (counted in
 * stats.selector). See docs/re/le_format.md.
 */

#define BS_LE_OBJ_READABLE   0x0001u
#define BS_LE_OBJ_WRITABLE   0x0002u
#define BS_LE_OBJ_EXECUTABLE 0x0004u
#define BS_LE_OBJ_BIG        0x2000u  /* 32-bit code/data */

typedef struct bs_le_object {
    uint32_t virtual_size;
    uint32_t base;          /* preferred load address */
    uint32_t flags;         /* BS_LE_OBJ_* */
    uint32_t page_index;    /* first module page (1-based) */
    uint32_t page_count;
    uint32_t file_size;     /* bytes backed by file pages: page_count * page_size */
    uint32_t mem_size;      /* max(virtual_size, file_size) */
    uint8_t *data;          /* mem_size bytes, fixups applied, zero-filled past file data */
} bs_le_object;

typedef struct bs_le_fixup_stats {
    uint32_t offset32;      /* 32-bit offsets (type 7) */
    uint32_t relative32;    /* 32-bit self-relative (type 8) */
    uint32_t offset16;      /* 16-bit offsets (type 5) */
    uint32_t pointer1632;   /* 16:32 far pointers (type 6); offset part applied */
    uint32_t pointer1616;   /* 16:16 far pointers (type 3); offset part applied */
    uint32_t byte;          /* 8-bit (type 0) */
    uint32_t selector;      /* 16-bit selectors (type 2); not applied */
    uint32_t unsupported;   /* imports, entry-table targets, unknown types; not applied */
} bs_le_fixup_stats;

typedef struct bs_le_image {
    uint32_t le_offset;     /* file offset of the LE header */
    uint32_t page_size;
    uint32_t page_count;
    uint32_t entry_object;  /* 1-based */
    uint32_t entry_offset;
    uint32_t entry_va;      /* objects[entry_object-1].base + entry_offset */
    uint32_t stack_object;
    uint32_t stack_offset;
    uint32_t object_count;
    bs_le_object *objects;
    bs_le_fixup_stats stats;
} bs_le_image;

/* Loads an LE executable (MZ stub + LE, or bare LE) from memory. */
bs_result bs_le_load(const uint8_t *exe, size_t size, bs_arena *arena, bs_le_image *out);

#endif
