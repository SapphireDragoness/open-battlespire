#include <string.h>

#include "bs/core/reader.h"
#include "bs_test.h"

static const uint8_t sample[] = {
    0x60, 0x09, 0x00, 0x01,             /* u16le 0x0960, u16le 0x0100 */
    'I', 'F', 'H', 'D', 0x00, 0x00, 0x00, 0x2C, /* tag + u32be 44 */
    0xFF, 0xFF, 0xFE, 0xFF,             /* i16le -1, i16le -2 */
    'A', 'B', 'C', 0, 'x', 'x',         /* 6-byte NUL-padded name */
};

static void test_integers(void)
{
    bs_reader r = bs_reader_make(sample, sizeof sample);
    BS_CHECK_EQ_INT(bs_read_u16le(&r), 0x0960);
    BS_CHECK_EQ_INT(bs_read_u16le(&r), 0x0100);
    char tag[4];
    BS_CHECK(bs_read_bytes(&r, tag, 4) && memcmp(tag, "IFHD", 4) == 0);
    BS_CHECK_EQ_INT(bs_read_u32be(&r), 44);
    BS_CHECK_EQ_INT(bs_read_i16le(&r), -1);
    BS_CHECK_EQ_INT(bs_read_i16le(&r), -2);
    BS_CHECK(bs_reader_ok(&r));
}

static void test_fixed_str(void)
{
    bs_reader r = bs_reader_make(sample, sizeof sample);
    bs_reader_seek(&r, 16);
    char name[16];
    BS_CHECK(bs_read_fixed_str(&r, 6, name, sizeof name));
    BS_CHECK(strcmp(name, "ABC") == 0);
    BS_CHECK(bs_reader_eof(&r));

    bs_reader_seek(&r, 16);
    char small[3];
    BS_CHECK(bs_read_fixed_str(&r, 6, small, sizeof small));
    BS_CHECK(strcmp(small, "AB") == 0); /* truncated, still terminated */
}

static void test_overrun_is_sticky(void)
{
    bs_reader r = bs_reader_make(sample, 3);
    BS_CHECK_EQ_INT(bs_read_u16le(&r), 0x0960);
    BS_CHECK_EQ_INT(bs_read_u32le(&r), 0); /* only 1 byte left */
    BS_CHECK(!bs_reader_ok(&r));
    BS_CHECK(bs_reader_eof(&r));
    BS_CHECK(bs_read_span(&r, 0) == NULL); /* even zero-length reads fail after error */
    BS_CHECK(!bs_reader_seek(&r, 0));
}

static void test_sub_reader(void)
{
    bs_reader r = bs_reader_make(sample, sizeof sample);
    bs_reader_skip(&r, 4);
    bs_reader chunk = bs_reader_sub(&r, 8);
    BS_CHECK_EQ_INT(r.pos, 12);
    BS_CHECK_EQ_INT(bs_reader_remaining(&chunk), 8);
    bs_read_u32le(&chunk);
    bs_read_u32le(&chunk);
    bs_read_u8(&chunk);
    BS_CHECK(!bs_reader_ok(&chunk)); /* cannot read past the sub-range */
    BS_CHECK(bs_reader_ok(&r));      /* parent unaffected */

    bs_reader too_big = bs_reader_sub(&r, 1000);
    BS_CHECK(!bs_reader_ok(&too_big));
    BS_CHECK(!bs_reader_ok(&r));
}

static void test_null_buffer(void)
{
    bs_reader r = bs_reader_make(NULL, 100);
    BS_CHECK_EQ_INT(r.size, 0);
    BS_CHECK(bs_read_span(&r, 0) != NULL);
    BS_CHECK_EQ_INT(bs_read_u8(&r), 0);
    BS_CHECK(!bs_reader_ok(&r));
}

int main(void)
{
    BS_RUN(test_integers);
    BS_RUN(test_fixed_str);
    BS_RUN(test_overrun_is_sticky);
    BS_RUN(test_sub_reader);
    BS_RUN(test_null_buffer);
    return BS_TEST_RESULT();
}
