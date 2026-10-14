add_library(bs_formats STATIC
    src/bs6.c
    src/bsa.c
    src/bsi.c
    src/flic.c
    src/font.c
    src/le.c
    src/levels.c
    src/lzss.c
    src/model.c
    src/nodes.c
    src/palette.c
    src/rsc.c
    src/smacker.c
    src/wav.c)

target_include_directories(bs_formats PUBLIC include)
target_link_libraries(bs_formats PUBLIC bs_core)
if(NOT MSVC)
    target_link_libraries(bs_formats PUBLIC m)
endif()
bs_configure_target(bs_formats)
