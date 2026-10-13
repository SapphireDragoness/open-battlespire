add_executable(bsview main.c)
target_link_libraries(bsview PRIVATE bs_render SDL3::SDL3)
bs_configure_target(bsview)
