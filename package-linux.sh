#!/bin/bash

# Package industrial-hmi for Linux distribution
# Creates a tarball with executable, assets, and a launch script
# Run from the project root

set -e

# -- Parse arguments ------------------------------------------------------
while getopts "hdb:t:" arg; do
    case $arg in
        d) BUILD_TYPE=debug ;;
        b) BUILD_DIR=$OPTARG ;;
        t) TARGET_DIR=$OPTARG ;;
        h|*)
            echo "Usage: $0 [-h] [-d] [-b BUILD_DIR] [-t TARGET_DIR]"
            echo ""
            echo "  -h             Show this help"
            echo "  -d             Package debug build (default: release)"
            echo "  -b BUILD_DIR   Explicit build directory"
            echo "  -t TARGET_DIR  Output directory for archive"
            exit 0
            ;;
    esac
done

PROJ_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_TYPE="${BUILD_TYPE:-release}"
BUILD_DIR="${BUILD_DIR:-${PROJ_ROOT}/build/${BUILD_TYPE}}"
TARGET_DIR="${TARGET_DIR:-${PROJ_ROOT}/install}"
PKG_NAME="industrial-hmi_${BUILD_TYPE}_$(git rev-parse --short HEAD 2>/dev/null || echo 'local')"
TMP_DIR="${TARGET_DIR}/${PKG_NAME}"

EXE="${BUILD_DIR}/industrial-hmi"

if [[ ! -f "$EXE" ]]; then
    echo "Executable not found: $EXE"
    echo "Run ./build-linux.sh first"
    exit 1
fi

# -- Prepare ---------------------------------------------------------------
rm -rf "$TMP_DIR"
mkdir -p "$TMP_DIR/bin"
mkdir -p "$TARGET_DIR"

echo "================================================================"
echo "  Packaging industrial-hmi ($BUILD_TYPE) for Linux"
echo "================================================================"
echo ""

# -- Copy executable -------------------------------------------------------
echo "Copying executable..."
cp "$EXE" "$TMP_DIR/bin/"
chmod +x "$TMP_DIR/bin/industrial-hmi"

# -- Copy application data -------------------------------------------------
for dir in assets config ui; do
    if [[ -d "${BUILD_DIR}/${dir}" ]]; then
        echo "Copying ${dir}/..."
        cp -r "${BUILD_DIR}/${dir}" "$TMP_DIR/"
    else
        echo "Warning: ${dir}/ not found in build directory"
    fi
done

# -- Create launch script --------------------------------------------------
cat > "$TMP_DIR/run.sh" << 'LAUNCHER'
#!/bin/bash
# Launch industrial-hmi from the package directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
exec ./bin/industrial-hmi "$@"
LAUNCHER
chmod +x "$TMP_DIR/run.sh"

# -- List runtime dependencies ---------------------------------------------
echo ""
echo "Checking runtime dependencies..."
DEPS_FILE="$TMP_DIR/DEPENDENCIES.txt"
cat > "$DEPS_FILE" << EOF
Industrial HMI - Runtime Dependencies
======================================

Install these packages before running:

  Ubuntu/Debian:
    sudo apt install libgtkmm-4.0-0 libsqlite3-0

  Fedora:
    sudo dnf install gtkmm4.0 sqlite

  Arch:
    sudo pacman -S gtkmm-4.0 sqlite

Shared library dependencies:
EOF

ldd "$EXE" | grep "=> /" | awk '{print "  " $1 " => " $3}' >> "$DEPS_FILE"

# Check for missing libraries
MISSING=$(ldd "$EXE" 2>/dev/null | grep "not found" || true)
if [[ -n "$MISSING" ]]; then
    echo "WARNING: Missing libraries detected:"
    echo "$MISSING"
    echo ""
    echo "$MISSING" >> "$DEPS_FILE"
else
    echo "All runtime dependencies satisfied."
fi

# -- Create tarball ---------------------------------------------------------
echo ""
echo "Creating archive: ${PKG_NAME}.tar.gz"
(cd "$TARGET_DIR" && tar czf "${PKG_NAME}.tar.gz" "$PKG_NAME")

rm -rf "$TMP_DIR"

echo ""
echo "================================================================"
echo "  Package created: $TARGET_DIR/${PKG_NAME}.tar.gz"
echo "================================================================"
echo ""
du -h "$TARGET_DIR/${PKG_NAME}.tar.gz"
