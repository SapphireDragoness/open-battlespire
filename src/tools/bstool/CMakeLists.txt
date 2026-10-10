add_executable(bstool
    main.c
    cmd_info.c)

target_link_libraries(bstool PRIVATE bs_core)
bs_configure_target(bstool)
