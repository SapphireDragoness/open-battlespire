#include <string.h>

#include "bs/formats/bsa.h"
#include "bs_test.h"
#include "synth.h"

typedef struct buf {
    uint8_t data[1024];
    size_t len;
} buf;

static void build_named(buf *b, uint16_t count, uint16_t type)
{
    b->len = synth_bsa(b->data, sizeof b->data);
    b->data[0] = (uint8_t)count;
    b->data[1] = (uint8_t)(count >> 8);
    b->data[2] = (uint8_t)type;
    b->data[3] = (uint8_t)(type >> 8);
}

static bs_result open_buf(const buf *b, bs_bsa **out)
{
    bs_stream *s;
    bs_result r = bs_stream_open_memory(b->data, b->len, &s);
    return r == BS_OK ? bs_bsa_open(s, out) : r;
}

static void test_named(void)
{
    buf b;
    build_named(&b, 2, BS_BSA_TYPE_NAMED);
    bs_bsa *bsa;
    BS_CHECK_EQ_INT(open_buf(&b, &bsa), BS_OK);
    if (!bsa)
        return;
    BS_CHECK_EQ_INT(bsa->count, 2);

    const bs_bsa_record *hello = bs_bsa_find(bsa, "hello.txt");
    const bs_bsa_record *comp = bs_bsa_find(bsa, "COMP.3D");
    BS_CHECK(hello && comp && !bs_bsa_find(bsa, "missing"));
    BS_CHECK(hello && hello->offset == 4 && !bs_bsa_record_compressed(hello));
    BS_CHECK(comp && comp->offset == 9 && bs_bsa_record_compressed(comp));

    bs_arena a;
    bs_arena_init(&a, 0);
    uint8_t *data;
    size_t n;
    BS_CHECK_EQ_INT(bs_bsa_read(bsa, hello, &a, &data, &n), BS_OK);
    BS_CHECK(n == 5 && memcmp(data, "hello", 5) == 0);
    BS_CHECK_EQ_INT(bs_bsa_read(bsa, comp, &a, &data, &n), BS_OK);
    BS_CHECK(n == 6 && memcmp(data, "a     ", 6) == 0);
    BS_CHECK_EQ_INT(bs_bsa_read_raw(bsa, comp, &a, &data, &n), BS_OK);
    BS_CHECK(n == sizeof synth_comp_data && memcmp(data, synth_comp_data, n) == 0);
    bs_arena_free(&a);
    bs_bsa_close(bsa);
}

static void test_numbered(void)
{
    buf b;
    b.len = synth_bsa_numbered(b.data, sizeof b.data);

    bs_bsa *bsa;
    BS_CHECK_EQ_INT(open_buf(&b, &bsa), BS_OK);
    if (!bsa)
        return;
    const bs_bsa_record *rec = bs_bsa_find_id(bsa, 507);
    BS_CHECK(rec && rec->offset == 8 && rec->size == 3 && rec->name[0] == '\0');
    BS_CHECK(bs_bsa_find_id(bsa, 1) == NULL);
    bs_bsa_close(bsa);
}

static void test_malformed(void)
{
    buf b;
    bs_bsa *bsa;

    build_named(&b, 2, 0x0300); /* unknown type */
    BS_CHECK_EQ_INT(open_buf(&b, &bsa), BS_ERR_UNSUPPORTED);
    BS_CHECK(bsa == NULL);

    build_named(&b, 500, BS_BSA_TYPE_NAMED); /* directory larger than file */
    BS_CHECK_EQ_INT(open_buf(&b, &bsa), BS_ERR_FORMAT);

    build_named(&b, 2, BS_BSA_TYPE_NAMED); /* record size overruns the directory */
    b.data[b.len - 4] = 0xFF;
    BS_CHECK_EQ_INT(open_buf(&b, &bsa), BS_ERR_FORMAT);

    b.len = 3; /* truncated header */
    BS_CHECK_EQ_INT(open_buf(&b, &bsa), BS_ERR_FORMAT);
}

int main(void)
{
    BS_RUN(test_named);
    BS_RUN(test_numbered);
    BS_RUN(test_malformed);
    return BS_TEST_RESULT();
}
