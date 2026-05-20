#!/bin/bash
# Linux Build Script
# Tested on Ubuntu 24.04 (GCC 13); Debian 12+ and Fedora 41+ work
# with package-name substitutions per LINUX_BUILD.md.

set -e

PRESET="debug"
BUILD_DIR="build/debug"

# --- Parse args -------------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --release)
            PRESET="release"
            BUILD_DIR="build/release"
            shift
            ;;
        -h|--help)
            cat <<EOF
Usage: $0 [--release] [-h|--help]

Options:
  --release   Build optimized release (no sanitizers).
              Default is debug (ASan + UBSan).
  -h, --help  Show this help.
EOF
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            echo "Try: $0 --help" >&2
            exit 2
            ;;
    esac
done

echo "================================================================"
echo "  Industrial HMI - Linux Build (${PRESET})"
echo "================================================================"
echo ""

# Clean previous build of the same flavour.
echo "Cleaning previous build..."
rm -rf "${BUILD_DIR}"

echo ""
echo "Configuring ${PRESET} build..."
cmake --preset "${PRESET}"

echo ""
echo "Building ${PRESET}..."
cmake --build "${BUILD_DIR}" -- -j"$(nproc)"

# Check if executable exists.
if [ -f "${BUILD_DIR}/industrial-hmi" ]; then
    echo ""
    echo "BUILD SUCCESSFUL"
    echo ""
    echo "Executable: ${BUILD_DIR}/industrial-hmi"
    echo ""
    echo "To run:"
    echo "  ./${BUILD_DIR}/industrial-hmi"
    echo ""
    echo "To enable the login flow (auth + audit + historian):"
    echo "  ./enable-auth.sh ${BUILD_DIR}"
    echo "  ./${BUILD_DIR}/industrial-hmi"
    echo ""
else
    echo ""
    echo "BUILD FAILED - executable not found at ${BUILD_DIR}/industrial-hmi"
    exit 1
fi
