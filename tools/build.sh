#!/usr/bin/env bash
#
# Configure, build and test CK3 DNA Manager (Qt) on Linux and other Unix-like
# systems with CMake, using Ninja or the Unix Makefiles generator.
#
# Examples:
#   ./tools/build.sh
#   ./tools/build.sh --config Debug
#   ./tools/build.sh --config Release --skip-tests
#   ./tools/build.sh --config Release --screenshots
#   ./tools/build.sh --qt-path "$HOME/Qt/6.8.3/gcc_64" --jobs 8
#   ./tools/build.sh --generator "Unix Makefiles"
#   ./tools/build.sh --install-deps

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"

CONFIG="Release"
GENERATOR=""
QT_PATH="${QT_ROOT:-}"
BUILD_DIR=""
SCREENSHOTS=0
SKIP_TESTS=0
INSTALL_DEPS=0
JOBS=""
EXTRA_CMAKE_ARGS=()

usage() {
    cat <<'EOF'
Usage: tools/build.sh [options] [-- extra CMake arguments]

Options:
  -c, --config <name>       Build type: Debug, Release, RelWithDebInfo, MinSizeRel
                            (default: Release)
  -G, --generator <name>    CMake generator: Ninja or "Unix Makefiles"
                            (default: Ninja when available, otherwise Unix Makefiles)
  -q, --qt-path <path>      Qt installation prefix (default: $QT_ROOT, qmake6 or system Qt)
  -b, --build-dir <path>    Build directory (default: build/linux-<config> or
                            build/linux-make-<config>)
  -j, --jobs <count>        Parallel build jobs (default: CMake chooses)
      --install-deps        Install the Qt 6 development packages with the
                            system package manager (asks for sudo if needed)
      --screenshots         Build the offscreen screenshot helper
      --skip-tests          Do not run the test suite
  -h, --help                Show this help

Examples:
  ./tools/build.sh
  ./tools/build.sh -c Debug -j 8
  ./tools/build.sh -q "$HOME/Qt/6.8.3/gcc_64"
  ./tools/build.sh -- -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
EOF
}

fail() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

install_dependencies() {
    local sudo_cmd=()
    if [[ "$(id -u)" -ne 0 ]]; then
        command -v sudo >/dev/null 2>&1 \
            || fail "installing dependencies requires root; rerun as root or install Qt 6 manually"
        sudo_cmd=(sudo)
    fi

    printf 'Installing Qt 6 development packages...\n'
    if command -v apt-get >/dev/null 2>&1; then
        "${sudo_cmd[@]}" apt-get install -y \
            qt6-base-dev qt6-svg-dev qt6-image-formats-plugins libqt6sql6-sqlite
    elif command -v dnf >/dev/null 2>&1; then
        "${sudo_cmd[@]}" dnf install -y \
            qt6-qtbase-devel qt6-qtsvg-devel qt6-qtimageformats
    elif command -v pacman >/dev/null 2>&1; then
        "${sudo_cmd[@]}" pacman -S --needed --noconfirm \
            qt6-base qt6-svg qt6-imageformats
    elif command -v zypper >/dev/null 2>&1; then
        "${sudo_cmd[@]}" zypper install -y \
            qt6-base-devel qt6-svg-devel qt6-imageformats
    else
        fail "no supported package manager found; install the Qt 6 development packages manually"
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -c|--config)
            [[ $# -ge 2 ]] || fail "$1 requires a value"
            CONFIG="$2"
            shift 2
            ;;
        -G|--generator)
            [[ $# -ge 2 ]] || fail "$1 requires a value"
            GENERATOR="$2"
            shift 2
            ;;
        -q|--qt-path)
            [[ $# -ge 2 ]] || fail "$1 requires a value"
            QT_PATH="$2"
            shift 2
            ;;
        -b|--build-dir)
            [[ $# -ge 2 ]] || fail "$1 requires a value"
            BUILD_DIR="$2"
            shift 2
            ;;
        -j|--jobs)
            [[ $# -ge 2 ]] || fail "$1 requires a value"
            JOBS="$2"
            shift 2
            ;;
        --screenshots)
            SCREENSHOTS=1
            shift
            ;;
        --install-deps)
            INSTALL_DEPS=1
            shift
            ;;
        --skip-tests)
            SKIP_TESTS=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            EXTRA_CMAKE_ARGS=("$@")
            break
            ;;
        *)
            fail "unknown option: $1 (use --help for usage)"
            ;;
    esac
done

case "$CONFIG" in
    Debug|Release|RelWithDebInfo|MinSizeRel) ;;
    *) fail "unsupported configuration '$CONFIG' (use Debug, Release, RelWithDebInfo or MinSizeRel)" ;;
esac

if [[ -z "$GENERATOR" ]]; then
    if command -v ninja >/dev/null 2>&1; then
        GENERATOR="Ninja"
    else
        GENERATOR="Unix Makefiles"
    fi
fi

case "$GENERATOR" in
    Ninja|"Unix Makefiles") ;;
    *) fail "unsupported generator '$GENERATOR' (use Ninja or \"Unix Makefiles\")" ;;
esac

CMAKE="$(command -v cmake || true)"
[[ -n "$CMAKE" ]] || fail "CMake was not found on PATH. Install CMake 3.21 or newer."

# --- Dependencies ----------------------------------------------------------

if [[ "$INSTALL_DEPS" -eq 1 ]]; then
    install_dependencies
fi

# --- Locate Qt -------------------------------------------------------------

qt_prefix_is_valid() {
    local prefix="$1"
    [[ -f "$prefix/lib/cmake/Qt6/Qt6Config.cmake" ]] && return 0
    [[ -f "$prefix/lib64/cmake/Qt6/Qt6Config.cmake" ]] && return 0
    # Debian/Ubuntu multiarch layout: /usr/lib/<triplet>/cmake/Qt6
    compgen -G "$prefix/lib/*/cmake/Qt6/Qt6Config.cmake" >/dev/null && return 0
    return 1
}

if [[ -n "$QT_PATH" ]]; then
    qt_prefix_is_valid "$QT_PATH" \
        || fail "Qt 6 was not found at '$QT_PATH' (expected lib/cmake/Qt6/Qt6Config.cmake)."
elif command -v qmake6 >/dev/null 2>&1; then
    QT_PATH="$(qmake6 -query QT_INSTALL_PREFIX)"
elif command -v qmake >/dev/null 2>&1; then
    QT_PATH="$(qmake -query QT_INSTALL_PREFIX)"
fi

if [[ -n "$QT_PATH" ]] && ! qt_prefix_is_valid "$QT_PATH"; then
    # A qmake result may point at Qt 5; fall back to system discovery.
    QT_PATH=""
fi

if [[ -z "$QT_PATH" ]]; then
    printf 'Qt path: letting CMake discover Qt 6 (set QT_ROOT or use --qt-path if this fails)\n' >&2
fi

# --- Build directory -------------------------------------------------------

config_lower="$(printf '%s' "$CONFIG" | tr '[:upper:]' '[:lower:]')"
if [[ -z "$BUILD_DIR" ]]; then
    if [[ "$GENERATOR" == "Ninja" ]]; then
        BUILD_DIR="build/linux-$config_lower"
    else
        BUILD_DIR="build/linux-make-$config_lower"
    fi
fi

cd "$PROJECT_ROOT"

if [[ "$SCREENSHOTS" -eq 1 ]]; then
    SCREENSHOTS_VALUE="ON"
else
    SCREENSHOTS_VALUE="OFF"
fi

# --- Configure -------------------------------------------------------------

printf 'CK3 DNA Manager build\n'
printf '  Configuration : %s\n' "$CONFIG"
printf '  Qt            : %s\n' "${QT_PATH:-<system>}"
printf '  Build folder  : %s\n' "$BUILD_DIR"
printf '  CMake         : %s\n' "$CMAKE"
printf '  Generator     : %s\n' "$GENERATOR"
printf '\n'

configure_args=(
    -S "$PROJECT_ROOT"
    -B "$BUILD_DIR"
    -G "$GENERATOR"
    "-DCMAKE_BUILD_TYPE=$CONFIG"
    -DBUILD_TESTING=ON
    "-DBUILD_SCREENSHOTS=$SCREENSHOTS_VALUE"
)
if [[ -n "$QT_PATH" ]]; then
    configure_args+=("-DCMAKE_PREFIX_PATH=$QT_PATH")
fi
if [[ ${#EXTRA_CMAKE_ARGS[@]} -gt 0 ]]; then
    configure_args+=("${EXTRA_CMAKE_ARGS[@]}")
fi

configure_log="$(mktemp "${TMPDIR:-/tmp}/ck3-build-configure.XXXXXX")"
set +e
"$CMAKE" "${configure_args[@]}" 2>&1 | tee "$configure_log"
configure_status=${PIPESTATUS[0]}
set -e

if [[ "$configure_status" -ne 0 ]]; then
    if grep -q 'Qt6Config.cmake' "$configure_log"; then
        printf '\nQt 6 development files were not found.\n\n' >&2
        printf 'Install the Qt 6 development packages for your distribution, for example:\n' >&2
        printf '  Debian/Ubuntu: sudo apt install qt6-base-dev qt6-svg-dev\n' >&2
        printf '  Fedora:        sudo dnf install qt6-qtbase-devel qt6-qtsvg-devel\n' >&2
        printf '  Arch:          sudo pacman -S qt6-base qt6-svg\n\n' >&2
        printf 'Or let this script install them:\n' >&2
        printf '  ./tools/build.sh --install-deps\n\n' >&2
        printf 'If Qt is installed in a custom prefix (for example with the Qt online\n' >&2
        printf 'installer), rerun with --qt-path /path/to/Qt/6.x.y/gcc_64 or set QT_ROOT.\n' >&2
    fi
    rm -f "$configure_log"
    exit "$configure_status"
fi
rm -f "$configure_log"

# --- Build -----------------------------------------------------------------

if [[ -n "$JOBS" ]]; then
    "$CMAKE" --build "$BUILD_DIR" --parallel "$JOBS"
else
    "$CMAKE" --build "$BUILD_DIR" --parallel
fi

EXE_PATH="$BUILD_DIR/ck3-dna-manager-qt"
[[ -x "$EXE_PATH" ]] || fail "the build finished but the executable was not found: $EXE_PATH"

# --- Tests -----------------------------------------------------------------

if [[ "$SKIP_TESTS" -eq 0 ]]; then
    TEST_EXE="$BUILD_DIR/ck3_tests"
    [[ -x "$TEST_EXE" ]] || fail "the test executable was not built: $TEST_EXE"

    printf '\nRunning tests...\n'
    if command -v ctest >/dev/null 2>&1; then
        ctest --test-dir "$BUILD_DIR" --output-on-failure
    else
        "$TEST_EXE"
    fi
fi

printf '\nBuilt: %s\n' "$EXE_PATH"
if [[ "$SCREENSHOTS" -eq 1 ]]; then
    printf 'Screenshot helper: %s\n' "$BUILD_DIR/ck3_screenshots"
fi
