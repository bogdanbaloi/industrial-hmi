#!/bin/bash
# Ubuntu/Linux Build Script
# Tested on Ubuntu 22.04+ / Debian 12+

set -e

echo "================================================================"
echo "  Industrial HMI - Linux Build"
echo "================================================================"
echo ""

# -- Check & install dependencies ------------------------------------
install_deps() {
    echo "Checking dependencies..."

    local MISSING=()

    command -v cmake  >/dev/null 2>&1 || MISSING+=(cmake)
    command -v ninja  >/dev/null 2>&1 || MISSING+=(ninja-build)
    command -v g++    >/dev/null 2>&1 || MISSING+=(g++)
    command -v pkg-config >/dev/null 2>&1 || MISSING+=(pkg-config)

    pkg-config --exists gtkmm-4.0 2>/dev/null || MISSING+=(libgtkmm-4.0-dev)
    pkg-config --exists sqlite3   2>/dev/null || MISSING+=(libsqlite3-dev)

    if [ ! -d /usr/include/boost ]; then
        MISSING+=(libboost-dev)
    fi

    if [ ${#MISSING[@]} -gt 0 ]; then
        echo ""
        echo "Missing packages: ${MISSING[*]}"
        echo ""
        read -rp "Install them now? (requires sudo) [Y/n] " answer
        if [[ "$answer" =~ ^[Nn] ]]; then
            echo "Aborting. Install manually:"
            echo "  sudo apt install ${MISSING[*]}"
            exit 1
        fi
        sudo apt update
        sudo apt install -y "${MISSING[@]}"
        echo ""
        echo "Dependencies installed."
    else
        echo "All dependencies found."
    fi
}

# -- Parse arguments -------------------------------------------------
BUILD_TYPE="debug"
PRESET="debug"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --release)
            BUILD_TYPE="release"
            PRESET="release"
            shift
            ;;
        --skip-deps)
            SKIP_DEPS=1
            shift
            ;;
        --clean)
            echo "Cleaning all build directories..."
            rm -rf build/
            echo "Done."
            exit 0
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --release    Build in Release mode (default: Debug)"
            echo "  --skip-deps  Skip dependency check"
            echo "  --clean      Remove all build directories and exit"
            echo "  -h, --help   Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# -- Install deps ----------------------------------------------------
if [ -z "$SKIP_DEPS" ]; then
    install_deps
fi

# -- Clean previous build of this type -------------------------------
echo ""
echo "Cleaning previous $BUILD_TYPE build..."
rm -rf "build/$BUILD_TYPE"

# -- Configure -------------------------------------------------------
echo ""
echo "Configuring $BUILD_TYPE build (preset: $PRESET)..."
cmake --preset "$PRESET"

# -- Build -----------------------------------------------------------
echo ""
echo "Building $BUILD_TYPE..."
cmake --build "build/$PRESET" -- -j"$(nproc)"

# -- Verify ----------------------------------------------------------
EXE="build/$PRESET/industrial-hmi"

if [ -f "$EXE" ]; then
    echo ""
    echo "BUILD SUCCESSFUL"
    echo ""
    echo "Executable: $EXE"
    echo "Size:       $(du -h "$EXE" | cut -f1)"
    echo ""

    # Suppress known third-party leaks when running with sanitizers
    SUPPRESSION_FILE="asan_suppressions.txt"
    if [ "$BUILD_TYPE" = "debug" ] && [ -f "$SUPPRESSION_FILE" ]; then
        echo "To run (with sanitizer leak suppressions):"
        echo "  LSAN_OPTIONS=suppressions=$SUPPRESSION_FILE ./$EXE"
    else
        echo "To run:"
        echo "  ./$EXE"
    fi
    echo ""
else
    echo ""
    echo "BUILD FAILED - executable not found"
    exit 1
fi
