#!/usr/bin/env bash
# Runs one libFuzzer harness.
#
#   fuzz/run.sh <target> [seconds] [extra libFuzzer args...]
#   targets: lzss bsa bsi flic font rsc levels cdimage le model bs6
#
# Build first:  cmake --preset fuzz && cmake --build --preset fuzz
#
# Everything is written under build/fuzz/ (never committed):
#   seeds/<target>/     synthetic seeds from make_seeds
#   realseeds/<target>/ seeds copied from the game install if BS_DATA_DIR is set
#   corpus/<target>/    the growing corpus
#   crashes/<target>/   crashing inputs (reproduce: build/fuzz/fuzz/fuzz_<target> <file>)
set -euo pipefail

target=${1:?usage: fuzz/run.sh <target> [seconds] [libFuzzer args...]}
secs=${2:-60}
shift
[ $# -gt 0 ] && shift

root=$(cd "$(dirname "$0")/.." && pwd)
build=$root/build/fuzz
bin=$build/fuzz/fuzz_$target
[ -x "$bin" ] || { echo "missing $bin; build the 'fuzz' preset first" >&2; exit 1; }

[ -d "$build/seeds/$target" ] || "$build/fuzz/make_seeds" "$build/seeds"

real=$build/realseeds/$target
if [ -n "${BS_DATA_DIR:-}" ] && [ ! -d "$real" ]; then
    mkdir -p "$real"
    bstool=$build/src/tools/bstool/bstool
    case $target in
        rsc)    cp "$BS_DATA_DIR/GAMEDATA/TEXT.RSC" "$real/" ;;
        levels) cp "$BS_DATA_DIR/GAMEDATA/LEVELS.TXT" "$real/" ;;
        le)     cp "$BS_DATA_DIR/GAME.EXE" "$real/" ;;
        bsi)
            # A few small real images of each encoding.
            "$bstool" extract GAMEDATA/BSI.BSA -o "$real" TERR02.BSI DRES90.BSI SCRL00.BSI SKEL00W0.BSI >/dev/null ;;
        font)   cp "$BS_DATA_DIR/GAMEDATA/FONT0001.FNT" "$BS_DATA_DIR/GAMEDATA/FONT0006.FNT" "$real/" ;;
        flic)
            # The smallest talking-head animation.
            name=$("$bstool" ls GAMEDATA/FLC.BSA | awk '$2 ~ /FLC$/ { print $3, $2 }' | sort -n | awk 'NR == 1 { print $2 }')
            "$bstool" extract GAMEDATA/FLC.BSA -o "$real" "$name" >/dev/null ;;
        lzss)
            # A sample of compressed records, stored bytes as-is.
            # (awk limits the count: `head` would SIGPIPE bstool and trip pipefail)
            names=$("$bstool" ls GAMEDATA/3D.BSA | awk '$3 == "lzss" && n++ < 40 { print $2 }')
            # shellcheck disable=SC2086
            "$bstool" extract GAMEDATA/3D.BSA --raw -o "$real" $names >/dev/null ;;
        model)
            # Small models of both versions, plus loose patch models.
            "$bstool" extract GAMEDATA/3D.BSA -o "$real" CLERRUG2.3D TABLE.3D STEP2.3D HUTVANE.3D L2WHEL1.3D >/dev/null ;;
        bs6)    "$bstool" extract GAMEDATA/BS6.BSA -o "$real" 1ROOM.BS6 BELLIN.BS6 L10.BS6 >/dev/null ;;
        *) ;;
    esac
fi

case $target in
    le)      max_len=1500000 ;;
    flic)    max_len=600000 ;;
    cdimage) max_len=120000 ;;
    *)       max_len=65536 ;;
esac

mkdir -p "$build/corpus/$target" "$build/crashes/$target"
dirs=("$build/corpus/$target")
[ -d "$build/seeds/$target" ] && dirs+=("$build/seeds/$target")
[ -d "$real" ] && dirs+=("$real")

exec "$bin" "${dirs[@]}" \
    -max_total_time="$secs" -max_len="$max_len" -rss_limit_mb=2048 \
    -artifact_prefix="$build/crashes/$target/" -print_final_stats=1 "$@"
