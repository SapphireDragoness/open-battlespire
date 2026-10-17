add_executable(bssave main.c)
target_link_libraries(bssave PRIVATE bs_game)
bs_configure_target(bssave)
