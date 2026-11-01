#!/usr/bin/env bash
# Builds SDL3 from source, then openbattlespire, runs the tests, and packs the
# game and libSDL3 into an AppImage with linuxdeploy. Build on the oldest
# distribution you want to support: the AppImage needs that glibc or newer.
# packaging/linux/install-build-deps.sh installs what Ubuntu 22.04 needs.
#
# Needs: a C compiler, cmake >= 3.25, ninja, curl, and SDL3's build dependencies.
# Usage: packaging/build-appimage.sh [version]
# Output: dist/OpenBattlespire-<version>-x86_64.AppImage
set -euo pipefail

SDL_VERSION="${SDL_VERSION:-3.4.18}"
version="${1:-dev}"
root="$(cd "$(dirname "$0")/.." && pwd)"
work="$root/build/package-linux"
deps="$work/deps"
sdl_prefix="$deps/sdl3-$SDL_VERSION"
name="OpenBattlespire-$version-x86_64.AppImage"

mkdir -p "$deps" "$root/dist"

if [ ! -f "$sdl_prefix/lib/libSDL3.so" ]; then
    rm -rf "$deps/SDL3-$SDL_VERSION"
    curl -fsSL "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL3-$SDL_VERSION.tar.gz" |
        tar -xz -C "$deps"
    # SDL loads the audio and video back ends (X11, Wayland, PipeWire, ...)
    # at run time, so libSDL3 itself only links against libc.
    cmake -S "$deps/SDL3-$SDL_VERSION" -B "$deps/sdl-build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$sdl_prefix" \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DSDL_TESTS=OFF \
        -DSDL_EXAMPLES=OFF \
        -DSDL_STATIC=OFF
    cmake --build "$deps/sdl-build"
    cmake --install "$deps/sdl-build"
fi

cmake -S "$root" -B "$work/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$sdl_prefix" \
    -DBS_BUILD_TOOLS=OFF
cmake --build "$work/build"
ctest --test-dir "$work/build" --output-on-failure

linuxdeploy="$deps/linuxdeploy-x86_64.AppImage"
if [ ! -x "$linuxdeploy" ]; then
    curl -fsSL -o "$linuxdeploy" \
        https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    chmod +x "$linuxdeploy"
fi

appdir="$work/AppDir"
rm -rf "$appdir"
rm -f "$root/dist/$name"
# APPIMAGE_EXTRACT_AND_RUN: linuxdeploy runs without FUSE (e.g. in containers).
APPIMAGE_EXTRACT_AND_RUN=1 \
LD_LIBRARY_PATH="$sdl_prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
LINUXDEPLOY_OUTPUT_VERSION="$version" \
LDAI_OUTPUT="$root/dist/$name" \
    "$linuxdeploy" \
    --appdir "$appdir" \
    --executable "$work/build/src/app/openbattlespire" \
    --desktop-file "$root/packaging/linux/openbattlespire.desktop" \
    --icon-file "$root/packaging/linux/openbattlespire.png" \
    --output appimage
echo "Wrote dist/$name"
