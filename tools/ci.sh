#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-tmp
export TMPDIR="$PWD/build-tmp"
case "${1:-}" in
  format)
    python3 tools/format.py
    ;;
  format-check)
    python3 tools/format.py --check
    ;;
  host)
    python3 tools/check_project.py
    # Keep assertions/debug symbols while avoiding interpreter-speed protocol
    # simulations (each complete transaction transfers four 8 MiB captures).
    cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Debug \
      '-DCMAKE_CXX_FLAGS_DEBUG=-g -O1'
    cmake --build build-host --parallel
    ctest --test-dir build-host --output-on-failure
    ;;
  sanitize)
    cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
      '-DCMAKE_CXX_FLAGS_DEBUG=-g -O1' \
      "-DCMAKE_CXX_FLAGS=${CXXFLAGS:-} -fsanitize=address,undefined -fno-omit-frame-pointer" \
      '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
    cmake --build build-sanitize --parallel
    ctest --test-dir build-sanitize --output-on-failure
    ;;
  native)
    if [ -z "${AMIGA_TOOLCHAIN_ROOT:-}" ]; then
      AMIGA_TOOLCHAIN_ROOT=$(python3 tools/fetch_toolchain.py)
    fi
    cmake --fresh -S . -B build-amiga -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/amiga.cmake" \
      -DAMIGA_TOOLCHAIN_ROOT="$AMIGA_TOOLCHAIN_ROOT" -DCMAKE_BUILD_TYPE=Release
    cmake --build build-amiga --parallel
    ;;
  package)
    python3 tools/package_release.py
    lha t dist/*-amiga-68000.lha
    python3 tools/verify_release.py dist
    ;;
  *) echo "usage: $0 format|format-check|host|sanitize|native|package" >&2; exit 2 ;;
esac
