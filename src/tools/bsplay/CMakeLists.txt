add_executable(bsplay main.c)
target_link_libraries(bsplay PRIVATE bs_audio bs_formats SDL3::SDL3)
bs_configure_target(bsplay)
