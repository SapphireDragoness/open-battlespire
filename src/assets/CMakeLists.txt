add_library(bs_assets STATIC
    src/assets.c
    src/scene.c)

target_include_directories(bs_assets PUBLIC include)
target_link_libraries(bs_assets PUBLIC bs_formats)
bs_configure_target(bs_assets)
