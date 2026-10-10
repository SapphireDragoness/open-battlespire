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

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(BSCompilerFlags)

add_subdirectory(src/core)

if(BS_BUILD_TOOLS)
    add_subdirectory(src/tools/bstool)
endif()

if(BS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
