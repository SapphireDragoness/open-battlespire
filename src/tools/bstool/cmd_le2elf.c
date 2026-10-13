/*
 * bstool le2elf: converts an LE executable (GAME.EXE) into a static i386 ELF
 * file for disassemblers. Each LE object becomes a PT_LOAD segment at its
 * preferred base address with fixups applied; uninitialized tails become
 * NOBITS sections. Symbols mark the entry point and initial stack top.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/formats/le.h"
#include "commands.h"

#define ELF_PAGE 0x1000u

/* Growable output buffer. */
typedef struct obuf {
    uint8_t *data;
    size_t len, cap;
    bool failed;
} obuf;

static void ob_reserve(obuf *b, size_t extra)
{
    if (b->failed || b->len + extra <= b->cap)
        return;
    size_t cap = b->cap ? b->cap : 4096;
    while (cap < b->len + extra)
        cap *= 2;
    uint8_t *p = realloc(b->data, cap);
    if (!p) {
        b->failed = true;
        return;
    }
    b->data = p;
    b->cap = cap;
}

static void ob_put(obuf *b, const void *p, size_t n)
{
    ob_reserve(b, n);
    if (b->failed)
        return;
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

static void ob_pad_to(obuf *b, size_t off)
{
    ob_reserve(b, off > b->len ? off - b->len : 0);
    if (b->failed)
        return;
    while (b->len < off)
        b->data[b->len++] = 0;
}

static void ob_u8(obuf *b, uint8_t v) { ob_put(b, &v, 1); }
static void ob_u16(obuf *b, uint16_t v) { uint8_t x[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; ob_put(b, x, 2); }
static void ob_u32(obuf *b, uint32_t v)
{
    uint8_t x[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    ob_put(b, x, 4);
}

/* Section header string table, built up front. */
typedef struct strtab {
    char data[1024];
    uint32_t len;
} strtab;

static uint32_t st_add(strtab *t, const char *s)
{
    uint32_t off = t->len;
    size_t n = strlen(s) + 1;
    if (t->len + n > sizeof t->data)
        return 0;
    memcpy(t->data + t->len, s, n);
    t->len += (uint32_t)n;
    return off;
}

enum { SHT_NULL = 0, SHT_PROGBITS = 1, SHT_SYMTAB = 2, SHT_STRTAB = 3, SHT_NOBITS = 8 };
enum { SHF_WRITE = 1, SHF_ALLOC = 2, SHF_EXECINSTR = 4 };
enum { PF_X = 1, PF_W = 2, PF_R = 4 };

typedef struct section {
    uint32_t name, type, flags, addr, offset, size, link, info, align, entsize;
} section;

/* Initialized bytes of an object: its file pages, trimmed to the memory size. */
static uint32_t init_size(const bs_le_object *o)
{
    return o->file_size < o->mem_size ? o->file_size : o->mem_size;
}

static bool write_elf(const bs_le_image *img, const char *path)
{
    obuf b = { 0 };
    strtab shstr = { .len = 1 };
    strtab sym = { .len = 1 };
    section secs[2 + 2 * 256 + 3];
    uint32_t nsec = 1;
    memset(secs, 0, sizeof secs);

    /* Section index holding the entry point, for the symbol. */
    uint32_t entry_shndx = 0, stack_shndx = 0;

    uint32_t phoff = 52;
    uint32_t data_start = phoff + 32 * img->object_count;
    uint32_t file_off = (data_start + ELF_PAGE - 1) & ~(ELF_PAGE - 1);
    uint32_t seg_offset[256];

    for (uint32_t i = 0; i < img->object_count; i++) {
        const bs_le_object *o = &img->objects[i];
        /* Keep p_offset congruent to p_vaddr modulo the page size. */
        uint32_t want = o->base % ELF_PAGE;
        while (file_off % ELF_PAGE != want)
            file_off++;
        seg_offset[i] = file_off;

        char name[32];
        bool code = (o->flags & BS_LE_OBJ_EXECUTABLE) != 0;
        uint32_t sflags = SHF_ALLOC | (code ? SHF_EXECINSTR : 0) | ((o->flags & BS_LE_OBJ_WRITABLE) ? SHF_WRITE : 0);
        uint32_t isz = init_size(o);
        snprintf(name, sizeof name, "obj%" PRIu32 "_%s", i + 1, code ? "text" : "data");
        secs[nsec] = (section){ st_add(&shstr, name), SHT_PROGBITS, sflags, o->base, file_off, isz, 0, 0, 16, 0 };
        uint32_t this_sec = nsec++;
        if (o->mem_size > isz) {
            snprintf(name, sizeof name, "obj%" PRIu32 "_bss", i + 1);
            secs[nsec++] = (section){ st_add(&shstr, name), SHT_NOBITS, sflags, o->base + isz, file_off + isz,
                                      o->mem_size - isz, 0, 0, 16, 0 };
        }
        if (img->entry_object == i + 1)
            entry_shndx = this_sec;
        if (img->stack_object == i + 1)
            stack_shndx = nsec - 1;
        file_off += isz;
    }

    /* Symbol table: null, entry, stack top. */
    uint32_t sym_entry = st_add(&sym, "le_entry");
    uint32_t sym_stack = st_add(&sym, "le_stack_top");
    uint32_t symtab_off = (file_off + 3) & ~3u;
    uint32_t symtab_size = 3 * 16;
    uint32_t strtab_off = symtab_off + symtab_size;
    uint32_t shstr_name_sym = st_add(&shstr, ".symtab");
    uint32_t shstr_name_str = st_add(&shstr, ".strtab");
    uint32_t shstr_name_shstr = st_add(&shstr, ".shstrtab");
    uint32_t shstr_off = strtab_off + sym.len;
    uint32_t shoff = (shstr_off + shstr.len + 3) & ~3u;

    uint32_t symtab_idx = nsec;
    secs[nsec++] = (section){ shstr_name_sym, SHT_SYMTAB, 0, 0, symtab_off, symtab_size, symtab_idx + 1, 1, 4, 16 };
    secs[nsec++] = (section){ shstr_name_str, SHT_STRTAB, 0, 0, strtab_off, sym.len, 0, 0, 1, 0 };
    uint32_t shstrndx = nsec;
    secs[nsec++] = (section){ shstr_name_shstr, SHT_STRTAB, 0, 0, shstr_off, shstr.len, 0, 0, 1, 0 };

    /* ELF header. */
    static const uint8_t ident[16] = { 0x7F, 'E', 'L', 'F', 1 /* 32-bit */, 1 /* LE */, 1 /* version */ };
    ob_put(&b, ident, sizeof ident);
    ob_u16(&b, 2);      /* ET_EXEC */
    ob_u16(&b, 3);      /* EM_386 */
    ob_u32(&b, 1);
    ob_u32(&b, img->entry_va);
    ob_u32(&b, phoff);
    ob_u32(&b, shoff);
    ob_u32(&b, 0);      /* flags */
    ob_u16(&b, 52);
    ob_u16(&b, 32);
    ob_u16(&b, (uint16_t)img->object_count);
    ob_u16(&b, 40);
    ob_u16(&b, (uint16_t)nsec);
    ob_u16(&b, (uint16_t)shstrndx);

    /* Program headers. */
    for (uint32_t i = 0; i < img->object_count; i++) {
        const bs_le_object *o = &img->objects[i];
        uint32_t pf = ((o->flags & BS_LE_OBJ_READABLE) ? PF_R : 0) | ((o->flags & BS_LE_OBJ_WRITABLE) ? PF_W : 0) |
                      ((o->flags & BS_LE_OBJ_EXECUTABLE) ? PF_X : 0);
        ob_u32(&b, 1);  /* PT_LOAD */
        ob_u32(&b, seg_offset[i]);
        ob_u32(&b, o->base);
        ob_u32(&b, o->base);
        ob_u32(&b, init_size(o));
        ob_u32(&b, o->mem_size);
        ob_u32(&b, pf);
        ob_u32(&b, ELF_PAGE);
    }

    /* Segment data. */
    for (uint32_t i = 0; i < img->object_count; i++) {
        ob_pad_to(&b, seg_offset[i]);
        ob_put(&b, img->objects[i].data, init_size(&img->objects[i]));
    }

    /* Symbols: st_name, st_value, st_size, st_info, st_other, st_shndx. */
    ob_pad_to(&b, symtab_off);
    for (int k = 0; k < 16; k++)
        ob_u8(&b, 0);
    ob_u32(&b, sym_entry);
    ob_u32(&b, img->entry_va);
    ob_u32(&b, 0);
    ob_u8(&b, 0x12);  /* STB_GLOBAL, STT_FUNC */
    ob_u8(&b, 0);
    ob_u16(&b, (uint16_t)entry_shndx);
    uint32_t stack_va = img->stack_object >= 1 && img->stack_object <= img->object_count
                            ? img->objects[img->stack_object - 1].base + img->stack_offset
                            : 0;
    ob_u32(&b, sym_stack);
    ob_u32(&b, stack_va);
    ob_u32(&b, 0);
    ob_u8(&b, 0x11);  /* STB_GLOBAL, STT_OBJECT */
    ob_u8(&b, 0);
    ob_u16(&b, (uint16_t)stack_shndx);

    ob_put(&b, sym.data, sym.len);
    ob_put(&b, shstr.data, shstr.len);

    /* Section headers. */
    ob_pad_to(&b, shoff);
    for (uint32_t i = 0; i < nsec; i++) {
        const section *s = &secs[i];
        ob_u32(&b, s->name);
        ob_u32(&b, s->type);
        ob_u32(&b, s->flags);
        ob_u32(&b, s->addr);
        ob_u32(&b, s->offset);
        ob_u32(&b, s->size);
        ob_u32(&b, s->link);
        ob_u32(&b, s->info);
        ob_u32(&b, s->align);
        ob_u32(&b, s->entsize);
    }

    bool ok = !b.failed && bs_fs_write_file(path, b.data, b.len) == BS_OK;
    free(b.data);
    return ok;
}

int cmd_le2elf(int argc, char **argv)
{
    const char *in = NULL, *out = NULL;
    for (int i = 1; i < argc; i++) {
        if (!out && i + 1 < argc && strcmp(argv[i], "-o") == 0)
            out = argv[++i];
        else if (!in)
            in = argv[i];
    }
    if (!out) {
        fprintf(stderr, "usage: bstool le2elf [exe] -o <out.elf>\n"
                        "  exe defaults to GAME.EXE of the install\n");
        return 2;
    }

    bstool_ctx ctx;
    bool opened = false;
    bs_arena arena;
    bs_arena_init(&arena, 0);
    uint8_t *exe;
    size_t size;
    bs_result r;
    if (in) {
        r = bs_fs_read_file(in, &arena, &exe, &size);
    } else {
        if (!bstool_open(&ctx)) {
            bs_arena_free(&arena);
            return 1;
        }
        opened = true;
        in = "GAME.EXE";
        r = bs_vfs_read(&ctx.vfs, in, &arena, &exe, &size);
    }

    int status = 1;
    bs_le_image img;
    if (r == BS_OK)
        r = bs_le_load(exe, size, &arena, &img);
    if (r != BS_OK) {
        fprintf(stderr, "le2elf: %s: %s\n", in, bs_result_str(r));
        goto done;
    }
    if (img.object_count > 256) {
        fprintf(stderr, "le2elf: too many objects\n");
        goto done;
    }

    printf("%s: LE header at 0x%" PRIx32 ", %" PRIu32 " pages of %" PRIu32 " bytes\n",
           in, img.le_offset, img.page_count, img.page_size);
    for (uint32_t i = 0; i < img.object_count; i++) {
        const bs_le_object *o = &img.objects[i];
        printf("  object %" PRIu32 ": 0x%08" PRIx32 "-0x%08" PRIx32 "  %c%c%c  %" PRIu32 " pages\n", i + 1,
               o->base, o->base + o->mem_size, (o->flags & BS_LE_OBJ_READABLE) ? 'r' : '-',
               (o->flags & BS_LE_OBJ_WRITABLE) ? 'w' : '-', (o->flags & BS_LE_OBJ_EXECUTABLE) ? 'x' : '-',
               o->page_count);
    }
    const bs_le_fixup_stats *s = &img.stats;
    printf("  entry 0x%08" PRIx32 "\n", img.entry_va);
    printf("  fixups: %" PRIu32 " offset32, %" PRIu32 " relative32, %" PRIu32 " offset16, %" PRIu32
           " ptr16:32, %" PRIu32 " ptr16:16, %" PRIu32 " byte, %" PRIu32 " selector (not applied), %" PRIu32
           " unsupported\n",
           s->offset32, s->relative32, s->offset16, s->pointer1632, s->pointer1616, s->byte, s->selector,
           s->unsupported);

    if (!write_elf(&img, out)) {
        fprintf(stderr, "le2elf: cannot write %s\n", out);
        goto done;
    }
    printf("Wrote %s\n", out);
    status = 0;
done:
    bs_arena_free(&arena);
    if (opened)
        bstool_close(&ctx);
    return status;
}
