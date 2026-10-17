add_executable(bstool
    main.c
    common.c
    cmd_3d.c
    cmd_archive.c
    cmd_bsi.c
    cmd_flc.c
    cmd_fnt.c
    cmd_info.c
    cmd_le2elf.c
    cmd_save.c
    cmd_smk.c
    cmd_text.c
    gltf.c)

target_link_libraries(bstool PRIVATE bs_assets)
bs_configure_target(bstool)
