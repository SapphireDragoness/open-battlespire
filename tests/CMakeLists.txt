# bs_add_test(<name>) builds tests/<name>.c and registers it with CTest.
# Exit code 77 marks a test as skipped (used by data tests without BS_DATA_DIR).
function(bs_add_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE bs_core)
    target_compile_definitions(${name} PRIVATE BS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    bs_configure_target(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
endfunction()

bs_add_test(test_arena)
bs_add_test(test_reader)
bs_add_test(test_fs)
bs_add_test(test_install_data)
