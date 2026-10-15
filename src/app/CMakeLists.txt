add_executable(openbattlespire main.c controls.c fight.c inventory.c spellbook.c statusbar.c)
target_link_libraries(openbattlespire PRIVATE bs_render bs_audio bs_game SDL3::SDL3)
bs_configure_target(openbattlespire)
