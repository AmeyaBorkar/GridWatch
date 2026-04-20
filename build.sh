#!/usr/bin/env bash
# build.sh — portable build script (fallback for environments without GNU make).
# Does the same work as the Makefile. Use make if available.
set -e

CC="${CC:-C:/Tools/bin/gcc.cmd}"
AR="${AR:-C:/msys64/mingw64/bin/ar.exe}"

CFLAGS=(-std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter -O2 -g -Iinclude)
LDLIBS=(-lm)

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

BUILD=build
mkdir -p "$BUILD"

target="${1:-all}"

compile_obj() {
  local src=$1
  local obj=$2
  mkdir -p "$(dirname "$obj")"
  "$CC" "${CFLAGS[@]}" -c "$src" -o "$obj"
}

build_lib() {
  echo "[build] libdispatch.a"
  local objs=()
  while IFS= read -r src; do
    local obj="$BUILD/${src%.c}.o"
    compile_obj "$src" "$obj"
    objs+=("$obj")
  done < <(find src/ds src/sim -name '*.c' | sort)
  rm -f "$BUILD/libdispatch.a"
  "$AR" rcs "$BUILD/libdispatch.a" "${objs[@]}"
}

build_tui() {
  echo "[build] dispatch.exe"
  local objs=()
  while IFS= read -r src; do
    local obj="$BUILD/${src%.c}.o"
    compile_obj "$src" "$obj"
    objs+=("$obj")
  done < <(find src/tui -name '*.c' | sort)
  "$CC" "${CFLAGS[@]}" -o "$BUILD/dispatch.exe" "${objs[@]}" "$BUILD/libdispatch.a" "${LDLIBS[@]}"
}

build_shared() {
  echo "[build] libdispatch.dll"
  local srcs=()
  while IFS= read -r src; do srcs+=("$src"); done < <(find src/ds src/sim -name '*.c' | sort)
  "$CC" "${CFLAGS[@]}" -DDISPATCH_BUILD_SHARED -shared -o "$BUILD/libdispatch.dll" \
    "${srcs[@]}" "${LDLIBS[@]}" -Wl,--out-implib,"$BUILD/libdispatch.dll.a"
}

build_tests() {
  echo "[build] tests"
  for t in tests/*.c; do
    local name; name=$(basename "$t" .c)
    "$CC" "${CFLAGS[@]}" -o "$BUILD/$name.exe" "$t" "$BUILD/libdispatch.a" "${LDLIBS[@]}"
  done
}

run_tests() {
  local failed=0
  for t in "$BUILD"/test_*.exe; do
    echo "---- $(basename "$t") ----"
    if "$t"; then echo "  ok"; else echo "  FAIL"; failed=1; fi
  done
  return $failed
}

case "$target" in
  all)    build_lib; build_tui ;;
  shared) build_shared ;;
  tests)  build_lib; build_tests ;;
  check)  build_lib; build_tests; run_tests ;;
  run)    build_lib; build_tui; "$BUILD/dispatch.exe" ;;
  clean)  rm -rf "$BUILD" ;;
  *)      echo "Usage: $0 {all|shared|tests|check|run|clean}"; exit 2 ;;
esac
