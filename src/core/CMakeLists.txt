add_library(bs_core STATIC
    src/arena.c
    src/fs.c
    src/install.c
    src/log.c
    src/reader.c
    src/result.c
    src/vfs.c)

target_include_directories(bs_core PUBLIC include)
bs_configure_target(bs_core)
