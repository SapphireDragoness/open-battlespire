cmake_minimum_required(VERSION 3.25)

project(OpenBattlespire
    VERSION 0.0.1
    DESCRIPTION "Open-source reimplementation of the Battlespire engine"
    LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(BS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(BS_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(BS_BUILD_TESTS "Build the test suite" ON)
option(BS_BUILD_TOOLS "Build command-line tools (bstool)" ON)
option(BS_BUILD_VIEWER "Build the level viewer (bsview; needs SDL3)" ON)
option(BS_FUZZ "Build libFuzzer harnesses (Clang only; implies sanitizers)" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(BSCompilerFlags)

add_subdirectory(src/core)
add_subdirectory(src/formats)
add_subdirectory(src/assets)

if(BS_BUILD_VIEWER AND NOT BS_FUZZ)
    find_package(SDL3 3.2 CONFIG)
    if(SDL3_FOUND)
        add_subdirectory(src/render)
        add_subdirectory(src/tools/bsview)
    else()
        message(WARNING "SDL3 not found; the viewer (bsview) will not be built")
    endif()
endif()

if(BS_BUILD_TOOLS)
    add_subdirectory(src/tools/bstool)
endif()

if(BS_BUILD_TESTS OR BS_FUZZ)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BS_FUZZ)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "BS_FUZZ requires Clang (libFuzzer)")
    endif()
    add_subdirectory(fuzz)
endif()
