/* Filesystem and VFS tests, run against this source tree (BS_SOURCE_DIR). */

#include <string.h>

#include "bs/core/fs.h"
#include "bs/core/vfs.h"
#include "bs_test.h"

static void test_strcasecmp(void)
{
    BS_CHECK(bs_strcasecmp("GAMEDATA", "gamedata") == 0);
    BS_CHECK(bs_strcasecmp("3D.BSA", "3d.bsa") == 0);
    BS_CHECK(bs_strcasecmp("a", "b") < 0);
    BS_CHECK(bs_strncasecmp("SIGILF", "sigil", 5) == 0 && bs_strncasecmp("sig", "sigil", 5) != 0);
    BS_CHECK(bs_strncasecmp("abc", "abd", 2) == 0 && bs_strncasecmp("abc", "abd", 3) < 0);
    BS_CHECK(bs_strcasecmp("ab", "a") > 0);
}

static void test_path_join(void)
{
    char out[16];
    BS_CHECK(bs_path_join(out, sizeof out, "a", "b") && strcmp(out, "a/b") == 0);
    BS_CHECK(bs_path_join(out, sizeof out, "a/", "b") && strcmp(out, "a/b") == 0);
    BS_CHECK(!bs_path_join(out, 4, "abc", "def"));
}

static void test_resolve_case_insensitive(void)
{
    char out[BS_PATH_MAX];
    BS_CHECK_EQ_INT(bs_fs_resolve(BS_SOURCE_DIR, "DOCS\\plan.MD", out, sizeof out), BS_OK);
    BS_CHECK(strstr(out, "docs/PLAN.md") != NULL);
    BS_CHECK(bs_fs_is_file(out));
    BS_CHECK_EQ_INT(bs_fs_resolve(BS_SOURCE_DIR, "docs/no-such-file", out, sizeof out), BS_ERR_NOT_FOUND);
}

static void test_vfs_priority(void)
{
    char tests_dir[BS_PATH_MAX];
    BS_CHECK(bs_path_join(tests_dir, sizeof tests_dir, BS_SOURCE_DIR, "tests"));

    bs_vfs vfs;
    bs_vfs_init(&vfs);
    BS_CHECK_EQ_INT(bs_vfs_mount_dir(&vfs, BS_SOURCE_DIR), BS_OK);
    BS_CHECK_EQ_INT(bs_vfs_mount_dir(&vfs, tests_dir), BS_OK);
    BS_CHECK_EQ_INT(bs_vfs_mount_dir(&vfs, "/nonexistent/dir"), BS_ERR_NOT_FOUND);

    /* Present only in the lower-priority root mount. */
    BS_CHECK_EQ_INT(bs_vfs_exists(&vfs, "readme.md"), BS_OK);
    /* CMakeLists.txt exists in both; the tests/ one (mounted last) must win. */
    bs_arena a;
    bs_arena_init(&a, 0);
    uint8_t *data;
    size_t size;
    BS_CHECK_EQ_INT(bs_vfs_read(&vfs, "CMakeLists.txt", &a, &data, &size), BS_OK);
    BS_CHECK(data && strstr((const char *)data, "bs_add_test") != NULL);
    BS_CHECK(data && data[size] == 0);
    BS_CHECK_EQ_INT(bs_vfs_read(&vfs, "missing.txt", &a, &data, &size), BS_ERR_NOT_FOUND);
    bs_arena_free(&a);
}

int main(void)
{
    BS_RUN(test_strcasecmp);
    BS_RUN(test_path_join);
    BS_RUN(test_resolve_case_insensitive);
    BS_RUN(test_vfs_priority);
    return BS_TEST_RESULT();
}
