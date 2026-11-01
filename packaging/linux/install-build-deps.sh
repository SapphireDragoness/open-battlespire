#!/usr/bin/env bash
# Installs what packaging/build-appimage.sh needs on Ubuntu 22.04 (run as root).
# The -dev packages are SDL3's optional back ends; without them SDL builds
# but the AppImage cannot open a window or play sound on those systems.
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
    build-essential ca-certificates curl file git ninja-build pkg-config python3-pip \
    libasound2-dev libpulse-dev libpipewire-0.3-dev \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
    libxss-dev libxtst-dev libxkbcommon-dev \
    libwayland-dev wayland-protocols libdecor-0-dev \
    libdrm-dev libgbm-dev libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev \
    libdbus-1-dev libudev-dev libibus-1.0-dev

# Ubuntu 22.04's CMake is 3.22; the project needs 3.25.
python3 -m pip install --no-cache-dir 'cmake>=3.25,<4'
