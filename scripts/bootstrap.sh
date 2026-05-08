#!/usr/bin/env bash
# bootstrap.sh — one-command provisioner for Aurora Mail on macOS and Linux.
#
# Detects the host platform, installs build dependencies via the system package
# manager (Homebrew on macOS, apt-get on Debian/Ubuntu), then configures, builds
# and tests the project through CMake presets.
#
# The script is idempotent: re-running it on an already-provisioned machine is
# safe and will simply skip already-installed packages.
#
# Usage:
#   ./scripts/bootstrap.sh                # release build (default)
#   ./scripts/bootstrap.sh debug          # debug build with unit tests
#   ./scripts/bootstrap.sh release        # release build (explicit)
#   AURORA_SKIP_DEPS=1 ./scripts/bootstrap.sh   # skip apt/brew install step
#   AURORA_SKIP_BUILD=1 ./scripts/bootstrap.sh  # only install dependencies

set -euo pipefail

BUILD_TYPE="${1:-release}"
case "${BUILD_TYPE}" in
    debug|Debug|DEBUG)     BUILD_TYPE="debug" ;;
    release|Release|RELEASE) BUILD_TYPE="release" ;;
    *)
        echo "error: build type must be 'debug' or 'release', got '${BUILD_TYPE}'" >&2
        exit 1
        ;;
esac

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${REPO_ROOT}"

log()  { printf '\033[1;36m[bootstrap]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[bootstrap]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31m[bootstrap]\033[0m %s\n' "$*" >&2; exit 1; }

UNAME="$(uname -s)"
case "${UNAME}" in
    Darwin) PLATFORM="macos"; PRESET="macos-${BUILD_TYPE}" ;;
    Linux)  PLATFORM="linux"; PRESET="linux-${BUILD_TYPE}" ;;
    *)      die "Unsupported host platform '${UNAME}'. Use scripts/bootstrap.ps1 on Windows." ;;
esac

log "Host:    ${UNAME} ($(uname -m))"
log "Preset:  ${PRESET}"
log "Repo:    ${REPO_ROOT}"

# ---------------------------------------------------------------------------
# Dependency installation
# ---------------------------------------------------------------------------
install_macos_deps() {
    command -v brew >/dev/null 2>&1 \
        || die "Homebrew is required on macOS. Install from https://brew.sh first."

    local pkgs=(
        cmake
        ninja
        qt
        qtkeychain
        boost
        openssl@3
        gmime
        nlohmann-json
        pkg-config
        clang-format
    )
    log "Installing/updating Homebrew formulae: ${pkgs[*]}"
    brew install "${pkgs[@]}"

    # clazy is optional (Qt-aware static analyzer). Try to install but tolerate
    # absence — the dev target degrades gracefully in cmake/AuroraDevTools.cmake.
    brew install clazy 2>/dev/null || warn "clazy not installed (optional, used by aurora-clazy target only)"

    HOMEBREW_PREFIX="$(brew --prefix)"
    export HOMEBREW_PREFIX
    log "HOMEBREW_PREFIX=${HOMEBREW_PREFIX}"
}

install_linux_deps() {
    if ! command -v apt-get >/dev/null 2>&1; then
        warn "apt-get not found. Install dependencies manually for your distribution:"
        warn "  Required: cmake (>=3.22), ninja-build, qt6-base-dev, qt6-tools-dev,"
        warn "            libboost-all-dev (>=1.83), libssl-dev, libgmime-3.0-dev,"
        warn "            qt6keychain-dev, libsecret-1-dev, nlohmann-json3-dev,"
        warn "            pkg-config, clang-format, clang-tidy."
        warn "  Optional: libgtest-dev (for unit tests), clazy."
        return 0
    fi

    log "Installing apt packages (sudo required)"
    sudo apt-get update -y
    # Notes on package names:
    #  • qt6-base-dev / qt6-tools-dev cover Widgets, Network, Sql, Concurrent
    #    and the LinguistTools (lupdate/lrelease).
    #  • qt6-l10n-tools brings the linguist binary (separate package on noble).
    #  • qt6keychain-dev requires Ubuntu 24.04 (noble) or later. On 22.04 you
    #    must build QtKeychain from source and re-run with AURORA_SKIP_DEPS=1.
    #  • Boost 1.83+ is satisfied by libboost-all-dev on Ubuntu 24.04+ only.
    sudo apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        pkg-config \
        qt6-base-dev \
        qt6-tools-dev \
        qt6-l10n-tools \
        libqt6sql6-sqlite \
        libboost-all-dev \
        libssl-dev \
        libgmime-3.0-dev \
        qt6keychain-dev \
        libsecret-1-dev \
        nlohmann-json3-dev \
        clang-format \
        clang-tidy \
        libgtest-dev \
        || die "apt-get install failed. Re-run after fixing the errors above."

    sudo apt-get install -y --no-install-recommends clazy 2>/dev/null \
        || warn "clazy not installed (optional, used by aurora-clazy target only)"
}

if [[ -z "${AURORA_SKIP_DEPS:-}" ]]; then
    case "${PLATFORM}" in
        macos) install_macos_deps ;;
        linux) install_linux_deps ;;
    esac
else
    log "AURORA_SKIP_DEPS set, skipping dependency installation"
    if [[ "${PLATFORM}" == "macos" ]] && command -v brew >/dev/null 2>&1; then
        HOMEBREW_PREFIX="$(brew --prefix)"
        export HOMEBREW_PREFIX
    fi
fi

if [[ -n "${AURORA_SKIP_BUILD:-}" ]]; then
    log "AURORA_SKIP_BUILD set, dependency installation complete — exiting before configure"
    exit 0
fi

# ---------------------------------------------------------------------------
# Configure / build / test
# ---------------------------------------------------------------------------
log "Configuring with preset '${PRESET}'"
cmake --preset "${PRESET}"

log "Building with preset '${PRESET}'"
cmake --build --preset "${PRESET}" --parallel

if [[ "${BUILD_TYPE}" == "debug" ]]; then
    log "Running tests with preset '${PRESET}'"
    ctest --preset "${PRESET}" || warn "Some tests failed (see output above)"
fi

ARTIFACT_HINT="build/${PRESET}/desktop/aurora-mail"
if [[ "${PLATFORM}" == "macos" ]]; then
    ARTIFACT_HINT="build/${PRESET}/desktop/aurora-mail.app"
fi
log "Done. Artifact: ${ARTIFACT_HINT}"
