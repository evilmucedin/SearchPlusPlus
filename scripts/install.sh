#!/usr/bin/env bash
# install.sh — install SearchPlusPlus build dependencies on Linux and macOS.
#
# Installs: a C++20 compiler, CMake >= 3.20, Ninja, pkg-config, git, clang-format,
# and bootstraps vcpkg under $VCPKG_ROOT (default: $HOME/vcpkg).
#
# Supported platforms (auto-detected from /etc/os-release and `uname`):
#   - macOS (Homebrew; installs the Xcode Command Line Tools if missing)
#   - Ubuntu / Debian (apt-get)
#   - Fedora / RHEL / CentOS Stream / Rocky / Alma (dnf or yum)
#   - Arch / Manjaro (pacman)
#   - openSUSE (zypper)
#
# Usage:
#   scripts/install.sh                # install everything
#   scripts/install.sh --no-vcpkg     # skip the vcpkg clone+bootstrap step
#   scripts/install.sh --vcpkg-root P # clone vcpkg to P instead of $HOME/vcpkg
#
# Idempotent: re-running is safe and only installs what's missing.

set -euo pipefail

NO_VCPKG=0
VCPKG_ROOT_OVERRIDE=""
while [ $# -gt 0 ]; do
    case "$1" in
        --no-vcpkg) NO_VCPKG=1 ;;
        --vcpkg-root) VCPKG_ROOT_OVERRIDE="$2"; shift ;;
        -h|--help)
            sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *)
            echo "unknown flag: $1" >&2
            exit 2 ;;
    esac
    shift
done

VCPKG_ROOT="${VCPKG_ROOT_OVERRIDE:-${VCPKG_ROOT:-$HOME/vcpkg}}"

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m==>\033[0m %s\n' "$*" >&2; }
die() { printf '\033[1;31m==>\033[0m %s\n' "$*" >&2; exit 1; }

detect_platform() {
    case "$(uname -s)" in
        Darwin) echo "macos"; return ;;
        Linux) : ;;
        *) die "unsupported OS: $(uname -s) (Linux and macOS only; see install.ps1 for Windows)" ;;
    esac
    if [ -r /etc/os-release ]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        case "${ID:-} ${ID_LIKE:-}" in
            *ubuntu*|*debian*) echo "debian"; return ;;
            *fedora*|*rhel*|*centos*|*rocky*|*almalinux*) echo "fedora"; return ;;
            *arch*|*manjaro*) echo "arch"; return ;;
            *suse*) echo "suse"; return ;;
        esac
    fi
    die "unrecognized Linux distro; install build-essential/clang, cmake>=3.20, ninja, pkg-config, git, clang-format by hand"
}

install_macos() {
    if ! xcode-select -p >/dev/null 2>&1; then
        log "Installing Xcode Command Line Tools (a GUI prompt may appear)"
        xcode-select --install || true
        warn "Re-run this script once the Xcode CLT installation finishes."
        exit 0
    fi
    if ! command -v brew >/dev/null 2>&1; then
        die "Homebrew is required. Install it from https://brew.sh and re-run."
    fi
    log "brew install cmake ninja pkg-config git clang-format"
    brew install cmake ninja pkg-config git clang-format
}

install_debian() {
    local sudo_cmd
    sudo_cmd=$(need_sudo)
    log "apt-get update + install build deps"
    $sudo_cmd apt-get update
    $sudo_cmd apt-get install -y --no-install-recommends \
        build-essential clang cmake ninja-build pkg-config git \
        curl unzip tar zip ca-certificates clang-format
}

install_fedora() {
    local sudo_cmd
    sudo_cmd=$(need_sudo)
    local pkg_mgr
    if command -v dnf >/dev/null 2>&1; then pkg_mgr=dnf
    elif command -v yum >/dev/null 2>&1; then pkg_mgr=yum
    else die "neither dnf nor yum found"
    fi
    log "$pkg_mgr install build deps"
    $sudo_cmd "$pkg_mgr" install -y \
        gcc gcc-c++ clang cmake ninja-build pkgconfig git \
        curl unzip tar zip clang-tools-extra
}

install_arch() {
    local sudo_cmd
    sudo_cmd=$(need_sudo)
    log "pacman -S --needed build deps"
    $sudo_cmd pacman -S --needed --noconfirm \
        base-devel clang cmake ninja pkgconf git \
        curl unzip tar zip
    # clang-format ships inside the `clang` package on Arch (no separate package).
}

install_suse() {
    local sudo_cmd
    sudo_cmd=$(need_sudo)
    log "zypper install build deps"
    $sudo_cmd zypper --non-interactive install -y \
        gcc-c++ clang cmake ninja pkg-config git \
        curl unzip tar zip clang-tools
}

need_sudo() {
    if [ "$(id -u)" -eq 0 ]; then echo ""; else echo "sudo"; fi
}

bootstrap_vcpkg() {
    if [ -d "$VCPKG_ROOT/.git" ] && [ -x "$VCPKG_ROOT/vcpkg" ]; then
        log "vcpkg already present at $VCPKG_ROOT (skipping clone)"
        return
    fi
    if [ ! -d "$VCPKG_ROOT" ]; then
        log "Cloning vcpkg into $VCPKG_ROOT"
        git clone --depth 1 https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
    fi
    log "Bootstrapping vcpkg"
    "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
}

main() {
    local platform
    platform=$(detect_platform)
    log "Detected platform: $platform"
    case "$platform" in
        macos)  install_macos ;;
        debian) install_debian ;;
        fedora) install_fedora ;;
        arch)   install_arch ;;
        suse)   install_suse ;;
    esac

    if [ "$NO_VCPKG" -eq 0 ]; then
        bootstrap_vcpkg
        log "Done. Add the following to your shell rc file:"
        printf '    export VCPKG_ROOT=%q\n' "$VCPKG_ROOT"
        # The single-quoted string below is the literal text we want the user
        # to paste into their shell rc file — $VCPKG_ROOT should expand later,
        # at the user's shell, not here.
        # shellcheck disable=SC2016
        printf '    export CMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"\n'
    else
        log "Done (vcpkg skipped — pass --vcpkg-root or set VCPKG_ROOT before running build.sh)."
    fi
}

main "$@"
