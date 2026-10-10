/* Data test: needs a real installation in BS_DATA_DIR; skipped otherwise. */

#include <stdlib.h>

#include "bs/core/install.h"
#include "bs_test.h"

static const char *data_dir;

static void test_detect(void)
{
    bs_install inst;
    BS_CHECK_EQ_INT(bs_install_detect(data_dir, &inst), BS_OK);
    BS_CHECK(inst.version != BS_VERSION_UNKNOWN);

    for (const char *const *f = bs_install_required_files; *f; f++) {
        char path[BS_PATH_MAX];
        BS_CHECK_EQ_INT(bs_fs_resolve(inst.root, *f, path, sizeof path), BS_OK);
    }
}

static void test_rejects_non_install(void)
{
    bs_install inst;
    BS_CHECK_EQ_INT(bs_install_detect(BS_SOURCE_DIR, &inst), BS_ERR_NOT_FOUND);
}

int main(void)
{
    data_dir = getenv("BS_DATA_DIR");
    if (!data_dir || !*data_dir)
        return BS_TEST_SKIP;
    BS_RUN(test_detect);
    BS_RUN(test_rejects_non_install);
    return BS_TEST_RESULT();
}
