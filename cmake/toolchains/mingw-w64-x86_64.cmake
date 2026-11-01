# Cross-compiling for 64-bit Windows with MinGW-w64 (e.g. Debian/Ubuntu's
# gcc-mingw-w64-x86-64 or Arch's mingw-w64-gcc).
#
#   cmake -B build/win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake \
#         -DSDL3_DIR=<SDL3-devel-mingw>/x86_64-w64-mingw32/lib/cmake/SDL3

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(BS_MINGW_PREFIX x86_64-w64-mingw32)
set(CMAKE_C_COMPILER ${BS_MINGW_PREFIX}-gcc)
set(CMAKE_RC_COMPILER ${BS_MINGW_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${BS_MINGW_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# Link libgcc and winpthread statically so the .exe needs no MinGW DLLs.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
