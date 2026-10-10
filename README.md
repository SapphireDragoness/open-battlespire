# OpenBattlespire

An open-source engine reimplementation of *An Elder Scrolls Legend: Battlespire* (1997), written in C.

**No game data is included.** You need your own copy of Battlespire (e.g. the GOG release); the engine reads the original files at runtime.

Status: planning. See [docs/PLAN.md](docs/PLAN.md).

## Building

Requires CMake ≥ 3.25 and a C17 compiler (GCC, Clang or MSVC).

```sh
cmake --preset debug          # or: asan (AddressSanitizer + UBSan), release
cmake --build --preset debug
ctest --preset debug
```

Point the tools and the data tests at your install with `BS_DATA_DIR`:

```sh
export BS_DATA_DIR=~/Games/Battlespire
./build/debug/src/tools/bstool/bstool info
ctest --preset debug          # data tests are skipped when BS_DATA_DIR is unset
```
