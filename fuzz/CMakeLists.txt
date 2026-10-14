# libFuzzer harnesses; built with -DBS_FUZZ=ON (Clang only). See fuzz/README.md.

add_executable(make_seeds make_seeds.c)
target_link_libraries(make_seeds PRIVATE bs_core bs_synth)
bs_configure_target(make_seeds)

foreach(target lzss bsa bsi flic font rsc levels cdimage le model bs6 wav smk nodes)
    add_executable(fuzz_${target} fuzz_${target}.c)
    target_link_libraries(fuzz_${target} PRIVATE bs_formats)
    target_link_options(fuzz_${target} PRIVATE -fsanitize=fuzzer)
    bs_configure_target(fuzz_${target})
endforeach()
