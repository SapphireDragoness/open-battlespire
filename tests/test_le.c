#include <string.h>

#include "bs/formats/exe_text.h"
#include "bs/formats/le.h"
#include "bs_test.h"
#include "synth.h"

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint8_t exe[4096];
static size_t exe_size;

static void test_load(void)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_le_image img;
    BS_CHECK_EQ_INT(bs_le_load(exe, exe_size, &a, &img), BS_OK);
    BS_CHECK_EQ_INT(img.object_count, 2);
    BS_CHECK_EQ_INT(img.page_size, 512);
    BS_CHECK_EQ_INT(img.entry_va, 0x10008);
    if (img.object_count != 2) {
        bs_arena_free(&a);
        return;
    }

    const bs_le_object *o1 = &img.objects[0], *o2 = &img.objects[1];
    BS_CHECK(o1->base == 0x10000 && o1->mem_size == 1024 && (o1->flags & BS_LE_OBJ_EXECUTABLE));
    BS_CHECK(o2->base == 0x20000 && o2->file_size == 512 && o2->mem_size == 0x1000);

    BS_CHECK_EQ_INT(get32(o1->data + 0x10), 0x20020);
    BS_CHECK_EQ_INT(get32(o1->data + 0x20), 0x10100u - 0x10024u);
    BS_CHECK_EQ_INT(get32(o1->data + 0x30), 0x20005);
    BS_CHECK_EQ_INT(get32(o1->data + 0x40), 0x20005);
    BS_CHECK_EQ_INT(get32(o1->data + 0x1FE), 0x10044); /* spans the page boundary */
    BS_CHECK_EQ_INT(get32(o2->data + 0x00), 0x10001);

    /* Untouched bytes keep their page contents; the selector fixup isn't applied. */
    BS_CHECK(o1->data[0x14] == 0xA1 && o1->data[0x1FD] == 0xA1);
    BS_CHECK(o1->data[0x202] == 0xA2 && o1->data[0x210] == 0xA2 && o1->data[0x211] == 0xA2);
    /* Last page is short (0x100 bytes); the rest of the object is zero-filled. */
    BS_CHECK(o2->data[0xFF] == 0xB0 && o2->data[0x100] == 0 && o2->data[0xFFF] == 0);

    BS_CHECK_EQ_INT(img.stats.offset32, 6); /* 1 + 2 (list) + 2 (spanning, both pages) + 1 */
    BS_CHECK_EQ_INT(img.stats.relative32, 1);
    BS_CHECK_EQ_INT(img.stats.selector, 1);
    BS_CHECK_EQ_INT(img.stats.unsupported, 0);
    bs_arena_free(&a);
}

static void test_malformed(void)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_le_image img;
    uint8_t bad[4096];

    /* Truncated: data pages missing. */
    BS_CHECK_EQ_INT(bs_le_load(exe, exe_size - 300, &a, &img), BS_ERR_FORMAT);

    /* Not an executable. */
    BS_CHECK_EQ_INT(bs_le_load((const uint8_t *)"hello", 5, &a, &img), BS_ERR_FORMAT);

    /* Page size not a power of two (LE + 0x28). */
    memcpy(bad, exe, exe_size);
    bad[0x40 + 0x28] = 0x01;
    bad[0x40 + 0x29] = 0x03;
    BS_CHECK_EQ_INT(bs_le_load(bad, exe_size, &a, &img), BS_ERR_FORMAT);

    /* Object page range beyond the module's pages (object 2 page count = 9). */
    memcpy(bad, exe, exe_size);
    bad[0x40 + 0xB0 + 24 + 16] = 9;
    BS_CHECK_EQ_INT(bs_le_load(bad, exe_size, &a, &img), BS_ERR_FORMAT);

    /* e_lfanew pointing past the end. */
    memcpy(bad, exe, exe_size);
    bad[0x3C + 3] = 0x7F;
    BS_CHECK_EQ_INT(bs_le_load(bad, exe_size, &a, &img), BS_ERR_FORMAT);
    bs_arena_free(&a);
}

/* The message table: pointers into another object, found from "Exit the Automap.". */
static void test_exe_text(void)
{
    static uint8_t strings[64], table[32];
    static const char text[] = "x\0Exit the Automap.\0Second\0";
    memcpy(strings, text, sizeof text);
    const uint32_t ptrs[] = {0x2000 + 2, 0x2000 + 20, 0x2000 + 20, 0xDEADBEEF};
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 4; k++)
            table[3 + 4 * i + k] = (uint8_t)(ptrs[i] >> (8 * k));
    bs_le_object objs[2] = {
        {.base = 0x1000, .mem_size = sizeof table, .data = table},
        {.base = 0x2000, .mem_size = sizeof strings, .data = strings},
    };
    bs_le_image img = {.object_count = 2, .objects = objs};
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_exe_text t;
    BS_CHECK_EQ_INT(bs_exe_text_read(&img, &a, &t), BS_OK);
    BS_CHECK_EQ_INT(t.count, 3);
    BS_CHECK(strcmp(bs_exe_message(&t, 0), "Exit the Automap.") == 0);
    BS_CHECK(strcmp(bs_exe_message(&t, 2), "Second") == 0);
    BS_CHECK(strcmp(bs_exe_message(&t, 3), "") == 0);
    /* No table. */
    table[3] = 0;
    BS_CHECK_EQ_INT(bs_exe_text_read(&img, &a, &t), BS_ERR_NOT_FOUND);
    bs_arena_free(&a);
}

int main(void)
{
    exe_size = synth_le(exe, sizeof exe);
    BS_CHECK(exe_size > 0);
    BS_RUN(test_load);
    BS_RUN(test_malformed);
    BS_RUN(test_exe_text);
    return BS_TEST_RESULT();
}
