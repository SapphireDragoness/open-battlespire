/* CD image tests on the synthetic ISO from synth.c, as a plain ISO and as a raw Mode 2 image with a cue sheet. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/core/cdimage.h"
#include "bs/core/vfs.h"
#include "bs_test.h"
#include "synth.h"

static uint8_t iso[SYNTH_ISO_SIZE];

static void write_file(const char *path, const void *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    fwrite(data, 1, n, f);
    fclose(f);
}

static void write_raw_mode2(const char *path)
{
    static uint8_t raw[SYNTH_ISO_SECTORS * 2352];
    size_t n = synth_raw_mode2(iso, sizeof iso, raw, sizeof raw);
    write_file(path, raw, n);
}

static void check_image(const char *path)
{
    bs_cdimage *cd;
    BS_CHECK_EQ_INT(bs_cdimage_open(path, &cd), BS_OK);
    if (!cd)
        return;
    BS_CHECK(strcmp(bs_cdimage_volume_id(cd), "TESTVOL") == 0);

    bool is_dir;
    uint64_t size;
    BS_CHECK_EQ_INT(bs_cdimage_stat(cd, "hello.txt", &is_dir, &size), BS_OK);
    BS_CHECK(!is_dir && size == 11);
    BS_CHECK_EQ_INT(bs_cdimage_stat(cd, "subdir", &is_dir, NULL), BS_OK);
    BS_CHECK(is_dir);
    BS_CHECK_EQ_INT(bs_cdimage_stat(cd, "nope", NULL, NULL), BS_ERR_NOT_FOUND);
    BS_CHECK_EQ_INT(bs_cdimage_stat(cd, "hello.txt/x", NULL, NULL), BS_ERR_NOT_FOUND);

    /* Read across the sector boundary at offset 2048. */
    bs_stream *s;
    BS_CHECK_EQ_INT(bs_cdimage_open_file(cd, "SubDir\\big.bin", &s), BS_OK);
    if (s) {
        uint8_t buf[100];
        BS_CHECK_EQ_INT(bs_stream_read_at(s, 2000, buf, sizeof buf), BS_OK);
        bool ok = true;
        for (int i = 0; i < 100; i++)
            ok = ok && buf[i] == (uint8_t)((2000 + i) * 7);
        BS_CHECK(ok);
        BS_CHECK_EQ_INT(bs_stream_read_at(s, SYNTH_ISO_BIG_SIZE - 10, buf, 11), BS_ERR_FORMAT);
        bs_stream_close(s);
    }

    /* Mounted into a VFS, by subdirectory. */
    bs_vfs vfs;
    bs_vfs_init(&vfs);
    BS_CHECK_EQ_INT(bs_vfs_mount_cd(&vfs, cd, ""), BS_OK);
    BS_CHECK_EQ_INT(bs_vfs_mount_cd(&vfs, cd, "SUBDIR"), BS_OK);
    BS_CHECK_EQ_INT(bs_vfs_mount_cd(&vfs, cd, "HELLO.TXT"), BS_ERR_NOT_FOUND);
    bs_arena a;
    bs_arena_init(&a, 0);
    uint8_t *data;
    size_t n;
    BS_CHECK_EQ_INT(bs_vfs_read(&vfs, "HELLO.TXT", &a, &data, &n), BS_OK);
    BS_CHECK(n == 11 && memcmp(data, "hello world", 11) == 0);
    BS_CHECK_EQ_INT(bs_vfs_exists(&vfs, "BIG.BIN"), BS_OK);
    bs_arena_free(&a);

    bs_cdimage_close(cd);
}

static void test_plain_iso(void)
{
    write_file("test_cd_plain.iso", iso, sizeof iso);
    check_image("test_cd_plain.iso");
}

static void test_raw_with_cue(void)
{
    write_raw_mode2("test_cd_raw.gog");
    static const char cue[] =
        "FILE \"TEST_CD_RAW.GOG\" BINARY\r\n"
        "  TRACK 01 MODE2/2352\r\n"
        "    INDEX 01 00:00:00\r\n"
        "FILE \"music\\Track02.ogg\" MP3\r\n"
        "  TRACK 02 AUDIO\r\n";
    write_file("test_cd_raw.ins", cue, sizeof cue - 1);
    check_image("test_cd_raw.ins"); /* cue names the data file in different case */
    check_image("test_cd_raw.gog"); /* the raw data file directly */
}

static void test_rejects_non_iso(void)
{
    static const char junk[] = "FILE \"missing.bin\" BINARY\n  TRACK 01 MODE1/2352\n";
    write_file("test_cd_bad.cue", junk, sizeof junk - 1);
    bs_cdimage *cd;
    BS_CHECK_EQ_INT(bs_cdimage_open("test_cd_bad.cue", &cd), BS_ERR_NOT_FOUND);

    uint8_t zeros[18 * 2048] = { 0 };
    write_file("test_cd_zero.iso", zeros, sizeof zeros);
    BS_CHECK_EQ_INT(bs_cdimage_open("test_cd_zero.iso", &cd), BS_ERR_FORMAT);
    BS_CHECK(cd == NULL);
}

int main(void)
{
    synth_iso(iso, sizeof iso);
    BS_RUN(test_plain_iso);
    BS_RUN(test_raw_with_cue);
    BS_RUN(test_rejects_non_iso);
    remove("test_cd_plain.iso");
    remove("test_cd_raw.gog");
    remove("test_cd_raw.ins");
    remove("test_cd_bad.cue");
    remove("test_cd_zero.iso");
    return BS_TEST_RESULT();
}
