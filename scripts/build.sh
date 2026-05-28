#!/usr/bin/env bash
# build.sh — configure, build, and test SearchPlusPlus on Linux and macOS.
#
# Assumes scripts/install.sh has been run (vcpkg present, CMake / Ninja / clang
# installed). Honors $VCPKG_ROOT (and $VCPKG_INSTALLATION_ROOT for CI parity).
#
# Usage:
#   scripts/build.sh                       # default preset (Debug + ASan/UBSan)
#   scripts/build.sh --preset release      # RelWithDebInfo + LTO
#   scripts/build.sh --preset asan         # forced ASan/UBSan
#   scripts/build.sh --no-test             # configure + build only
#   scripts/build.sh -j 8                  # cap build parallelism
#   scripts/build.sh -- --target spp_core  # forward remaining args to cmake --build

set -euo pipefail

PRESET="default"
RUN_TESTS=1
JOBS=""
EXTRA_BUILD_ARGS=()

while [ $# -gt 0 ]; do
    case "$1" in
        --preset) PRESET="$2"; shift ;;
        --no-test|--no-tests) RUN_TESTS=0 ;;
        -j|--jobs) JOBS="$2"; shift ;;
        --) shift; EXTRA_BUILD_ARGS=("$@"); break ;;
        -h|--help)
            sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *)
            echo "unknown flag: $1" >&2
            exit 2 ;;
    esac
    shift
done

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m==>\033[0m %s\n' "$*" >&2; exit 1; }

# Resolve the vcpkg toolchain file. Prefer an explicit $CMAKE_TOOLCHAIN_FILE,
# then $VCPKG_ROOT (developer machines), then $VCPKG_INSTALLATION_ROOT (CI).
# Returns empty if no vcpkg checkout is found — in that case we let CMake
# fall back to system dependencies (e.g. Homebrew-installed gtest+benchmark).
resolve_toolchain() {
    if [ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]; then
        echo "$CMAKE_TOOLCHAIN_FILE"; return
    fi
    local root="${VCPKG_ROOT:-${VCPKG_INSTALLATION_ROOT:-}}"
    if [ -z "$root" ] && [ -f "$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake" ]; then
        root="$HOME/vcpkg"
    fi
    if [ -z "$root" ]; then return; fi
    local tc="$root/scripts/buildsystems/vcpkg.cmake"
    if [ ! -f "$tc" ]; then
        die "VCPKG_ROOT=$root but $tc does not exist — run scripts/install.sh"
    fi
    echo "$tc"
}

TOOLCHAIN=$(resolve_toolchain)
if [ -n "$TOOLCHAIN" ]; then
    log "Using vcpkg toolchain: $TOOLCHAIN"
else
    log "No vcpkg toolchain found; relying on system-installed dependencies"
fi
log "Preset: $PRESET"

log "Configure"
if [ -n "$TOOLCHAIN" ]; then
    cmake --preset "$PRESET" -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN"
else
    cmake --preset "$PRESET"
fi

log "Build"
# ${EXTRA_BUILD_ARGS[@]+...} expands to nothing when the array is unset/empty —
# the workaround for bash 3.2 (macOS) erroring on `${arr[@]}` under `set -u`.
if [ -n "$JOBS" ]; then
    cmake --build --preset "$PRESET" -j "$JOBS" ${EXTRA_BUILD_ARGS[@]+"${EXTRA_BUILD_ARGS[@]}"}
else
    cmake --build --preset "$PRESET" -j ${EXTRA_BUILD_ARGS[@]+"${EXTRA_BUILD_ARGS[@]}"}
fi

if [ "$RUN_TESTS" -eq 1 ]; then
    log "Test"
    ctest --preset "$PRESET" --output-on-failure
fi

log "Done."
