#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
[ -f build/CMakeCache.txt ] || cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
exec ./build/zombies "$@"
