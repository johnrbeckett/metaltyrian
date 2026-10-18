#!/bin/bash
# Headless regression run of the game code on the SDL shim, for Linux (no
# window, no Mac). Builds the engine and shim with the same defines as
# Package.swift, plays the attract demo for N seconds and writes every 50th
# frame to OUT_DIR, for comparing against an earlier run or an SDL build.
#
# Usage: Tests/linux/run.sh /path/to/tyrian2000-data OUT_DIR [seconds]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DATA="$1"; OUT="$2"; SECS="${3:-100}"
B="$(mktemp -d)"
CC="${CC:-clang}"
FLAGS=(-std=gnu99 -O2 -Wall -Wextra -Wno-missing-field-initializers -DNDEBUG -DMETALTYRIAN -DTARGET_UNIX
       '-DTYRIAN_DIR="."' '-DOPENTYRIAN_VERSION="metaltyrian"' -D_GNU_SOURCE -I"$ROOT/Sources/SDLShim/include")
for f in "$ROOT"/Engine/src/*.c; do "$CC" "${FLAGS[@]}" -Dmain=tyrian_main -c "$f" -o "$B/$(basename "$f" .c).o"; done
"$CC" "${FLAGS[@]}" -c "$ROOT/Sources/SDLShim/sdl_shim.c" -o "$B/sdl_shim.o"
"$CC" "${FLAGS[@]}" -c "$ROOT/Tests/linux/linux_host.c" -o "$B/linux_host.o"
"$CC" "$B"/*.o -o "$B/mt_linux" -lm -lpthread
mkdir -p "$OUT"
export HOME="$B/home" XDG_CONFIG_HOME="$B/home"; mkdir -p "$HOME"
MT_DUMP_DIR="${MT_DUMP_DIR-$OUT}" MT_DUMP_EVERY=50 "$B/mt_linux" "$DATA" "$SECS"
