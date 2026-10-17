add_library(bs_formats STATIC
    src/automap.c
    src/book.c
    src/bs6.c
    src/bsa.c
    src/bsi.c
    src/creatures.c
    src/exe_text.c
    src/flic.c
    src/font.c
    src/items.c
    src/le.c
    src/levels.c
    src/lzss.c
    src/model.c
    src/nodes.c
    src/palette.c
    src/rsc.c
    src/scl.c
    src/save.c
    src/talk.c
    src/smacker.c
    src/spells.c
    src/wav.c)

target_include_directories(bs_formats PUBLIC include)
target_link_libraries(bs_formats PUBLIC bs_core)
if(NOT MSVC)
    target_link_libraries(bs_formats PUBLIC m)
endif()
bs_configure_target(bs_formats)
