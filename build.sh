#!/usr/bin/env bash
# Builds plugin/race07bridge.sbp (32-bit Windows DLL) with llvm-mingw.
# CXX defaults to the first i686-w64-mingw32-clang++ on PATH or in ~/.local/opt/llvm-mingw-*.
set -euo pipefail
cd "$(dirname "$0")"

CXX=${CXX:-$(command -v i686-w64-mingw32-clang++ || ls -d ~/.local/opt/llvm-mingw-*/bin/i686-w64-mingw32-clang++ 2>/dev/null | tail -1)}
[ -x "$CXX" ] || { echo "i686-w64-mingw32-clang++ not found (install llvm-mingw or set CXX)" >&2; exit 1; }

mkdir -p dist
"$CXX" -std=c++17 -O2 -Wall -Wextra -shared -static \
  -o dist/race07bridge.sbp plugin/src/*.cpp \
  -Wl,--kill-at -luser32
echo "built dist/race07bridge.sbp"
