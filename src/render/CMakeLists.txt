add_library(bs_render STATIC
    src/gl.c
    src/level_renderer.c)

target_include_directories(bs_render PUBLIC include)
target_link_libraries(bs_render PUBLIC bs_assets)
bs_configure_target(bs_render)
