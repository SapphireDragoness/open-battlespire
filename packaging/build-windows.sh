#!/usr/bin/env bash
# Cross-compiles openbattlespire.exe for 64-bit Windows with MinGW-w64 and
# zips it with SDL3.dll.
#
# Needs: x86_64-w64-mingw32-gcc, cmake >= 3.25, ninja, curl, zip.
# Usage: packaging/build-windows.sh [version]
# Output: dist/OpenBattlespire-<version>-windows-x64.zip
set -euo pipefail

SDL_VERSION="${SDL_VERSION:-3.4.18}"
version="${1:-dev}"
root="$(cd "$(dirname "$0")/.." && pwd)"
work="$root/build/package-windows"
deps="$work/deps"
name="OpenBattlespire-$version-windows-x64"

mkdir -p "$deps" "$root/dist"

sdl="$deps/SDL3-$SDL_VERSION/x86_64-w64-mingw32"
if [ ! -d "$sdl" ]; then
    curl -fsSL "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL3-devel-$SDL_VERSION-mingw.tar.gz" |
        tar -xz -C "$deps"
fi

cmake -S "$root" -B "$work/build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$root/cmake/toolchains/mingw-w64-x86_64.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSDL3_DIR="$sdl/lib/cmake/SDL3" \
    -DBS_BUILD_TESTS=OFF \
    -DBS_BUILD_TOOLS=OFF
cmake --build "$work/build" --target openbattlespire

stage="$work/$name"
rm -rf "$stage"
mkdir -p "$stage"
cp "$work/build/src/app/openbattlespire.exe" "$sdl/bin/SDL3.dll" "$stage/"
x86_64-w64-mingw32-strip "$stage/openbattlespire.exe"
cp "$root/README.md" "$stage/"

# The .exe must need nothing beyond Windows itself and SDL3.dll.
imports="$(x86_64-w64-mingw32-objdump -p "$stage/openbattlespire.exe" | sed -n 's/^\s*DLL Name: //p')"
echo "openbattlespire.exe imports:" $imports
if echo "$imports" | grep -Eiq 'libgcc|libwinpthread|libstdc'; then
    echo "error: openbattlespire.exe depends on MinGW runtime DLLs" >&2
    exit 1
fi

rm -f "$root/dist/$name.zip"
(cd "$work" && zip -qr "$root/dist/$name.zip" "$name")
echo "Wrote dist/$name.zip"
