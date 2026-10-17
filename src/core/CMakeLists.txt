add_library(bs_core STATIC
    src/arena.c
    src/cdimage.c
    src/fs.c
    src/ini.c
    src/install.c
    src/log.c
    src/png.c
    src/reader.c
    src/result.c
    src/stream.c
    src/strmap.c
    src/text.c
    src/vfs.c)

target_include_directories(bs_core PUBLIC include)
bs_configure_target(bs_core)
