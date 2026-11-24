#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

# UCRT64 的 bin 必须排在 Git Bash 的 /mingw64/bin 前，否则 cc1plus 会加载错版本 DLL 静默失败
PATH="${MSYS2_BIN:-/d/C++/MSYS2/ucrt64/bin}:$PATH"
GXX="${GXX:-g++}"
CXXFLAGS=(
  -std=c++17 -fno-exceptions -fno-rtti
  -finput-charset=UTF-8 -fexec-charset=UTF-8
  -ffunction-sections -fdata-sections
)

mkdir -p build

mode="${1:-test}"
case "$mode" in
  test)
    "$GXX" "${CXXFLAGS[@]}" -O2 -static \
      tests/selftest.cpp src/core.cpp -o build/selftest.exe
    ./build/selftest.exe
    ;;
  probe)
    "$GXX" "${CXXFLAGS[@]}" -O2 -static \
      tests/taskbar_probe.cpp src/taskbar.cpp -luser32 -o build/taskbar_probe.exe
    echo "built: build/taskbar_probe.exe (run it manually to test; taskbar becomes transparent for 3s)"
    ;;
  *)
    echo "usage: $0 [test|probe]" >&2
    exit 1
    ;;
esac