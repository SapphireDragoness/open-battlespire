add_executable(openbattlespire main.c controls.c fight.c inventory.c spellbook.c statusbar.c menus.c creation.c text.c talk.c loadsave.c launcher.c platform.c)
target_link_libraries(openbattlespire PRIVATE bs_render bs_audio bs_game SDL3::SDL3)
# On Windows a GUI program: no console window opens with it (platform.c attaches to one it was started from).
set_target_properties(openbattlespire PROPERTIES WIN32_EXECUTABLE TRUE)
bs_configure_target(openbattlespire)
