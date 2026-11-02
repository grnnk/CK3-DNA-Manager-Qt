#!/usr/bin/env bash
#
# Build and stage CK3 DNA Manager (Qt) into a portable Linux folder.
#
# Without --appimage the staged folder links against the system Qt 6 runtime.
# With --appimage, linuxdeploy bundles Qt and its plugins into a self-contained
# AppImage; combining it with --tar also archives the bundled AppDir.
#
# Examples:
#   ./tools/deploy.sh
#   ./tools/deploy.sh --config Debug
#   ./tools/deploy.sh --config Release --tar --version 1.0.1
#   ./tools/deploy.sh --config Release --appimage --tar --version 1.0.1
#   ./tools/deploy.sh --config Release --skip-build

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"

CONFIG="Release"
GENERATOR=""
QT_PATH="${QT_ROOT:-}"
BUILD_DIR=""
DIST_DIR=""
VERSION="1.0.1"
SKIP_BUILD=0
SKIP_TESTS=0
INSTALL_DEPS=0
CREATE_APPIMAGE=0
CREATE_TAR=0

usage() {
    cat <<'EOF'
Usage: tools/deploy.sh [options]

Options:
  -c, --config <name>       Build type: Debug, Release, RelWithDebInfo, MinSizeRel
                            (default: Release)
  -G, --generator <name>    CMake generator: Ninja or "Unix Makefiles"
  -q, --qt-path <path>      Qt installation prefix (default: $QT_ROOT or system Qt)
  -b, --build-dir <path>    Build directory (default: build/linux-<config>)
  -d, --dist-dir <path>     Staging directory (default: dist/linux-<config>)
      --version <version>   Version used in the archive name (default: 1.0.1)
      --install-deps        Install the Qt 6 development packages before building
      --skip-build          Reuse an existing build
      --skip-tests          Do not run the test suite while building
      --appimage            Create a self-contained AppImage (requires
                            linuxdeploy and linuxdeploy-plugin-qt on PATH)
      --tar                 Also create CK3-DNA-Manager-<version>-linux-<arch>.tar.gz
                            (self-contained when used with --appimage)
  -h, --help                Show this help
EOF
}

fail() {
    printf 'error: %s\n' "$*" >&2
    exit 1
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
        -d|--dist-dir)
            [[ $# -ge 2 ]] || fail "$1 requires a value"
            DIST_DIR="$2"
            shift 2
            ;;
        --version)
            [[ $# -ge 2 ]] || fail "$1 requires a value"
            VERSION="$2"
            shift 2
            ;;
        --skip-build)
            SKIP_BUILD=1
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
        --tar)
            CREATE_TAR=1
            shift
            ;;
        --appimage)
            CREATE_APPIMAGE=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            fail "unknown option: $1 (use --help for usage)"
            ;;
    esac
done

case "$CONFIG" in
    Debug|Release|RelWithDebInfo|MinSizeRel) ;;
    *) fail "unsupported configuration '$CONFIG'" ;;
esac

cd "$PROJECT_ROOT"

config_lower="$(printf '%s' "$CONFIG" | tr '[:upper:]' '[:lower:]')"
if [[ -z "$BUILD_DIR" ]]; then
    if [[ "$GENERATOR" == "Unix Makefiles" ]]; then
        BUILD_DIR="build/linux-make-$config_lower"
    else
        BUILD_DIR="build/linux-$config_lower"
    fi
fi
if [[ -z "$DIST_DIR" ]]; then
    DIST_DIR="dist/linux-$config_lower"
fi

# --- Build -----------------------------------------------------------------

if [[ "$SKIP_BUILD" -eq 0 ]]; then
    build_args=(--config "$CONFIG")
    if [[ -n "$GENERATOR" ]]; then
        build_args+=(--generator "$GENERATOR")
    fi
    if [[ -n "$QT_PATH" ]]; then
        build_args+=(--qt-path "$QT_PATH")
    fi
    if [[ -n "$BUILD_DIR" ]]; then
        build_args+=(--build-dir "$BUILD_DIR")
    fi
    if [[ "$SKIP_TESTS" -eq 1 ]]; then
        build_args+=(--skip-tests)
    fi
    if [[ "$INSTALL_DEPS" -eq 1 ]]; then
        build_args+=(--install-deps)
    fi

    "$SCRIPT_DIR/build.sh" "${build_args[@]}"
fi

EXE_PATH="$BUILD_DIR/ck3-dna-manager-qt"
[[ -x "$EXE_PATH" ]] || fail "the application was not built: $EXE_PATH"

# --- Stage -----------------------------------------------------------------

realpath_portable() {
    if command -v realpath >/dev/null 2>&1; then
        realpath -m -- "$1"
    else
        (cd "$1" 2>/dev/null && pwd -P) || printf '%s\n' "$1"
    fi
}

DIST_ABS="$(realpath_portable "$DIST_DIR")"
PROJECT_ABS="$(realpath_portable "$PROJECT_ROOT")"
BUILD_ABS="$(realpath_portable "$BUILD_DIR")"

[[ "$DIST_ABS" != "/" ]] || fail "refusing to use / as the deployment folder"
[[ "$DIST_ABS" != "$PROJECT_ABS" ]] || fail "refusing to use the project root as the deployment folder"
[[ "$DIST_ABS" != "$BUILD_ABS" && "$DIST_ABS" != "$BUILD_ABS"/* ]] \
    || fail "refusing to deploy inside the build folder"

if [[ -e "$DIST_ABS" ]]; then
    printf 'Cleaning %s\n' "$DIST_ABS"
    rm -rf -- "$DIST_ABS"
fi
mkdir -p -- "$DIST_ABS"

cp -- "$EXE_PATH" "$DIST_ABS/"

PACKAGE_README="$PROJECT_ROOT/docs/PACKAGE_README.md"
[[ -f "$PACKAGE_README" ]] || fail "package README not found: $PACKAGE_README"
cp -- "$PACKAGE_README" "$DIST_ABS/README.md"
if [[ -f "$PROJECT_ROOT/LICENSE" ]]; then
    cp -- "$PROJECT_ROOT/LICENSE" "$DIST_ABS/"
fi

cp -- "$PROJECT_ROOT/resources/app.desktop" "$DIST_ABS/ck3-dna-manager-qt.desktop"
cp -- "$PROJECT_ROOT/resources/icons/app.png" "$DIST_ABS/ck3-dna-manager-qt.png"

# --- Optional AppImage -----------------------------------------------------

ARCH="$(uname -m)"
APPIMAGE_PATH=""
APPDIR_ABS=""

if [[ "$CREATE_APPIMAGE" -eq 1 ]]; then
    LINUXDEPLOY="$(command -v linuxdeploy || true)"
    [[ -n "$LINUXDEPLOY" ]] || fail \
        "linuxdeploy was not found on PATH. Download it from https://github.com/linuxdeploy/linuxdeploy/releases and add it to PATH."
    [[ -x "$LINUXDEPLOY" ]] || fail \
        "linuxdeploy is not executable: $LINUXDEPLOY. Run: chmod +x '$LINUXDEPLOY'"
    LINUXDEPLOY_PLUGIN_QT="$(command -v linuxdeploy-plugin-qt || true)"
    [[ -n "$LINUXDEPLOY_PLUGIN_QT" ]] || fail \
        "linuxdeploy-plugin-qt was not found on PATH. Download it from https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases and add it to PATH."
    [[ -x "$LINUXDEPLOY_PLUGIN_QT" ]] || fail \
        "linuxdeploy-plugin-qt is not executable: $LINUXDEPLOY_PLUGIN_QT. Run: chmod +x '$LINUXDEPLOY_PLUGIN_QT'"

    qmake=""
    if [[ -n "$QT_PATH" ]]; then
        for candidate in "$QT_PATH/bin/qmake" "$QT_PATH/bin/qmake6"; do
            if [[ -x "$candidate" ]]; then
                qmake="$candidate"
                break
            fi
        done
    fi
    if [[ -z "$qmake" ]]; then
        for candidate in qmake6 qmake qmake-qt5; do
            candidate_path="$(command -v "$candidate" || true)"
            if [[ -n "$candidate_path" && -x "$candidate_path" ]]; then
                qmake="$candidate_path"
                break
            fi
        done
    fi
    [[ -n "$qmake" ]] || fail \
        "qmake was not found. Pass --qt-path or install the Qt 6 development packages."

    qt_plugins="$("$qmake" -query QT_INSTALL_PLUGINS)"
    [[ -d "$qt_plugins" ]] || fail "Qt plugin directory not found: $qt_plugins"
    [[ -f "$qt_plugins/sqldrivers/libqsqlite.so" ]] || fail \
        "the Qt SQLite driver was not found in $qt_plugins/sqldrivers. Install the Qt SQLite plugin package."

    APPDIR_DIR="$PROJECT_ROOT/dist/appimage-$config_lower/AppDir"
    APPDIR_ABS="$(realpath_portable "$APPDIR_DIR")"
    rm -rf -- "$APPDIR_ABS"
    mkdir -p -- "$APPDIR_ABS/usr/bin" \
        "$APPDIR_ABS/usr/share/applications" \
        "$APPDIR_ABS/usr/share/icons/hicolor/256x256/apps"

    cp -- "$EXE_PATH" "$APPDIR_ABS/usr/bin/ck3-dna-manager-qt"
    cp -- "$PROJECT_ROOT/resources/app.desktop" \
        "$APPDIR_ABS/usr/share/applications/ck3-dna-manager-qt.desktop"
    cp -- "$PROJECT_ROOT/resources/icons/app.png" \
        "$APPDIR_ABS/usr/share/icons/hicolor/256x256/apps/ck3-dna-manager-qt.png"
    cp -- "$PACKAGE_README" "$APPDIR_ABS/README.md"
    if [[ -f "$PROJECT_ROOT/LICENSE" ]]; then
        cp -- "$PROJECT_ROOT/LICENSE" "$APPDIR_ABS/"
    fi

    appimage_work="$PROJECT_ROOT/build/appimage-$config_lower"
    rm -rf -- "$appimage_work"
    mkdir -p -- "$appimage_work"

    # linuxdeploy's Qt plugin deploys every SQL driver in the Qt installation.
    # Only SQLite is used, and the other drivers may have missing dependencies
    # (for example libpq), which would abort the deployment. Point qmake at a
    # pruned plugin tree so only the SQLite driver is bundled.
    pruned_plugins="$appimage_work/qt-plugins"
    mkdir -p -- "$pruned_plugins"
    for entry in "$qt_plugins"/*; do
        [[ -e "$entry" ]] || continue
        base="$(basename -- "$entry")"
        if [[ "$base" == "sqldrivers" ]]; then
            mkdir -p -- "$pruned_plugins/sqldrivers"
            ln -s -- "$entry/libqsqlite.so" "$pruned_plugins/sqldrivers/libqsqlite.so"
        else
            ln -s -- "$entry" "$pruned_plugins/$base"
        fi
    done

    qmake_wrapper="$appimage_work/qmake"
    export DEPLOY_QMAKE="$qmake"
    export DEPLOY_QT_PLUGINS="$pruned_plugins"
    cat > "$qmake_wrapper" <<'EOF'
#!/bin/sh
"$DEPLOY_QMAKE" "$@" | awk -v plugins="$DEPLOY_QT_PLUGINS" '
    /^QT_INSTALL_PLUGINS:/ { print "QT_INSTALL_PLUGINS:" plugins; next }
    { print }
'
EOF
    chmod +x -- "$qmake_wrapper"

    # linuxdeploy invokes the Qt plugin as a subprocess. An AppImage plugin can
    # fail when re-executed through AppImageLauncher's binfmt handler, so
    # extract it once and run the extracted AppRun directly.
    plugin_cmd="$LINUXDEPLOY_PLUGIN_QT"
    if APPIMAGE_EXTRACT_AND_RUN=1 "$LINUXDEPLOY_PLUGIN_QT" --appimage-version >/dev/null 2>&1; then
        printf 'Extracting linuxdeploy-plugin-qt...\n'
        if ! (
            cd "$appimage_work"
            rm -rf -- squashfs-root
            APPIMAGE_EXTRACT_AND_RUN=1 "$LINUXDEPLOY_PLUGIN_QT" --appimage-extract >/dev/null
        ); then
            fail "could not extract linuxdeploy-plugin-qt"
        fi
        [[ -x "$appimage_work/squashfs-root/AppRun" ]] || fail \
            "could not find the extracted linuxdeploy-plugin-qt AppRun"
        plugin_cmd="$appimage_work/squashfs-root/AppRun"
    fi

    plugin_wrapper="$appimage_work/linuxdeploy-plugin-qt"
    export DEPLOY_PLUGIN_QT="$plugin_cmd"
    cat > "$plugin_wrapper" <<'EOF'
#!/bin/sh
APPIMAGE_EXTRACT_AND_RUN=1 exec "$DEPLOY_PLUGIN_QT" "$@"
EOF
    chmod +x -- "$plugin_wrapper"

    # linuxdeploy looks for plugins next to its own executable before searching
    # PATH. Run a copy from the work directory so the wrapper above is used.
    cp -- "$LINUXDEPLOY" "$appimage_work/linuxdeploy"
    chmod +x -- "$appimage_work/linuxdeploy"

    export QMAKE="$qmake_wrapper"
    export PATH="$appimage_work:$PATH"
    if [[ -n "$QT_PATH" && -d "$QT_PATH/lib" ]]; then
        export LD_LIBRARY_PATH="$QT_PATH/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    fi

    printf 'Creating AppImage with linuxdeploy...\n'
    (
        cd "$appimage_work"
        APPIMAGE_EXTRACT_AND_RUN=1 "$appimage_work/linuxdeploy" \
            --appdir "$APPDIR_ABS" \
            --executable "$APPDIR_ABS/usr/bin/ck3-dna-manager-qt" \
            --desktop-file "$APPDIR_ABS/usr/share/applications/ck3-dna-manager-qt.desktop" \
            --icon-file "$APPDIR_ABS/usr/share/icons/hicolor/256x256/apps/ck3-dna-manager-qt.png" \
            --plugin qt \
            --output appimage
    )

    generated_appimage="$(find "$appimage_work" -maxdepth 1 -name '*.AppImage' -print -quit)"
    [[ -n "$generated_appimage" ]] || fail "linuxdeploy finished without producing an AppImage"

    APPIMAGE_PATH="$PROJECT_ROOT/CK3-DNA-Manager-$VERSION-linux-$ARCH.AppImage"
    mv -f -- "$generated_appimage" "$APPIMAGE_PATH"
    rm -rf -- "$appimage_work"
fi

# --- Optional archive ------------------------------------------------------

if [[ "$CREATE_TAR" -eq 1 ]]; then
    archive="$PROJECT_ROOT/CK3-DNA-Manager-$VERSION-linux-$ARCH.tar.gz"
    rm -f -- "$archive"
    printf 'Creating %s\n' "$archive"
    if [[ "$CREATE_APPIMAGE" -eq 1 ]]; then
        # The AppDir contains the bundled Qt runtime, so this archive is
        # self-contained as well.
        tar -czf "$archive" -C "$APPDIR_ABS" .
    else
        tar -czf "$archive" -C "$DIST_ABS" .
    fi
fi

SIZE_MB="$(du -sm -- "$DIST_ABS" 2>/dev/null | cut -f1 || printf '?')"

printf '\n'
printf 'Deployed to: %s\n' "$DIST_ABS"
printf 'Total size : %s MB\n' "$SIZE_MB"
printf 'Run        : %s\n' "$DIST_ABS/ck3-dna-manager-qt"
if [[ "$CREATE_TAR" -eq 1 ]]; then
    printf 'Archive    : %s\n' "$PROJECT_ROOT/CK3-DNA-Manager-$VERSION-linux-$ARCH.tar.gz"
fi
if [[ "$CREATE_APPIMAGE" -eq 1 ]]; then
    printf 'AppImage   : %s\n' "$APPIMAGE_PATH"
    printf 'AppDir     : %s\n' "$APPDIR_ABS"
fi
