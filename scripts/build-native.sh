#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NDK_DIR="${1:?usage: scripts/build-native.sh /path/to/android-ndk-r27c}"
[[ -f "$NDK_DIR/build/cmake/android.toolchain.cmake" ]] || { echo 'NDK toolchain not found' >&2; exit 1; }
cmake -S "$ROOT_DIR/module/src/main/cpp" -B "$ROOT_DIR/out/native" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$NDK_DIR/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_static \
  -DCMAKE_CXX_FLAGS='-std=c++20 -Wall -Wextra'
cmake --build "$ROOT_DIR/out/native" --parallel
bash "$ROOT_DIR/scripts/package.sh"
bash "$ROOT_DIR/scripts/verify_stealth.sh" "$ROOT_DIR/out/native/liblineage_hide.so"
