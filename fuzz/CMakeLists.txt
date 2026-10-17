# libFuzzer harnesses; built with -DBS_FUZZ=ON (Clang only). See fuzz/README.md.

add_executable(make_seeds make_seeds.c)
target_link_libraries(make_seeds PRIVATE bs_core bs_synth)
bs_configure_target(make_seeds)

foreach(target lzss bsa bsi flic font rsc levels cdimage le model bs6 wav smk nodes magic book)
    add_executable(fuzz_${target} fuzz_${target}.c)
    target_link_libraries(fuzz_${target} PRIVATE bs_formats)
    target_link_options(fuzz_${target} PRIVATE -fsanitize=fuzzer)
    bs_configure_target(fuzz_${target})
endforeach()

add_executable(fuzz_talk fuzz_talk.c)
target_link_libraries(fuzz_talk PRIVATE bs_game)
target_link_options(fuzz_talk PRIVATE -fsanitize=fuzzer)
bs_configure_target(fuzz_talk)

add_executable(fuzz_save fuzz_save.c)
target_link_libraries(fuzz_save PRIVATE bs_game)
target_link_options(fuzz_save PRIVATE -fsanitize=fuzzer)
bs_configure_target(fuzz_save)

add_executable(fuzz_game_save fuzz_game_save.c)
target_link_libraries(fuzz_game_save PRIVATE bs_game)
target_link_options(fuzz_game_save PRIVATE -fsanitize=fuzzer)
bs_configure_target(fuzz_game_save)
