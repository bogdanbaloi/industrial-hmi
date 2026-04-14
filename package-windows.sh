#!/bin/bash

# Package industrial-hmi for Windows distribution
# Collects executable, assets, DLLs, and GTK runtime into a ZIP
# Run from MSYS2 Clang64 terminal

set -e

# -- Environment check ---------------------------------------------------
if [[ -z "$MSYSTEM_PREFIX" ]]; then
    echo "Please run in a MSYS2 shell"
    exit 1
fi

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
            echo "  -t TARGET_DIR  Output directory for ZIP"
            exit 0
            ;;
    esac
done

PROJ_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_TYPE="${BUILD_TYPE:-release}"
BUILD_DIR="${BUILD_DIR:-${PROJ_ROOT}/build/${BUILD_TYPE}}"
TARGET_DIR="${TARGET_DIR:-${PROJ_ROOT}/install}"
TMP_DIR="${TARGET_DIR}/_package_temp"
ZIPFILE="industrial-hmi_${BUILD_TYPE}_$(git rev-parse --short HEAD 2>/dev/null || echo 'local').zip"

EXE="${BUILD_DIR}/industrial-hmi.exe"

if [[ ! -f "$EXE" ]]; then
    echo "Executable not found: $EXE"
    echo "Run ./build-windows.sh first"
    exit 1
fi

# Windows system paths (excluded from DLL collection)
WIN_SYSTEM32="/c/windows/system32"
WIN_SYSWOW64="/c/windows/syswow64"
WIN_WINSXS="/c/windows/winsxs/"

is_system_dll() {
    local path="${1,,}"
    [[ "$path" == "$WIN_SYSTEM32"* ]] || \
    [[ "$path" == "$WIN_SYSWOW64"* ]] || \
    [[ "$path" == "$WIN_WINSXS"* ]]
}

# -- Prepare temp folder --------------------------------------------------
rm -rf "$TMP_DIR"
mkdir -p "$TMP_DIR"
mkdir -p "$TARGET_DIR"

echo "================================================================"
echo "  Packaging industrial-hmi ($BUILD_TYPE)"
echo "================================================================"
echo ""

# -- Copy executable ------------------------------------------------------
echo "Copying executable..."
cp "$EXE" "$TMP_DIR/"

# -- Copy application data ------------------------------------------------
for dir in assets config ui; do
    if [[ -d "${BUILD_DIR}/${dir}" ]]; then
        echo "Copying ${dir}/..."
        cp -r "${BUILD_DIR}/${dir}" "$TMP_DIR/"
    else
        echo "Warning: ${dir}/ not found in build directory"
    fi
done

# -- Collect non-system DLLs ----------------------------------------------
echo "Collecting DLL dependencies..."

ldd "$EXE" | grep "=> /" | while read -r line; do
    DLL_PATH=$(echo "$line" | awk '{print $3}')
    if [[ -f "$DLL_PATH" ]] && ! is_system_dll "$DLL_PATH"; then
        cp -u "$DLL_PATH" "$TMP_DIR/"
    fi
done

# -- GDK Pixbuf loaders ---------------------------------------------------
PIXBUF_SRC="${MSYSTEM_PREFIX}/lib/gdk-pixbuf-2.0/2.10.0"
PIXBUF_DST="$TMP_DIR/lib/gdk-pixbuf-2.0/2.10.0"

if [[ -d "$PIXBUF_SRC" ]]; then
    echo "Copying GDK Pixbuf loaders..."
    mkdir -p "$PIXBUF_DST"
    cp -r "$PIXBUF_SRC"/* "$PIXBUF_DST/"

    # Copy loader DLL dependencies
    for loader in "$PIXBUF_DST/loaders"/*.dll 2>/dev/null; do
        [[ -f "$loader" ]] || continue
        ldd "$loader" 2>/dev/null | grep "=> /" | awk '{print $3}' | while read -r dep; do
            if [[ -f "$dep" ]] && ! is_system_dll "$dep"; then
                cp -u "$dep" "$TMP_DIR/" 2>/dev/null
            fi
        done
    done
else
    echo "Warning: GDK Pixbuf loaders not found at $PIXBUF_SRC"
fi

# -- GSettings schemas ----------------------------------------------------
SCHEMAS_SRC="${MSYSTEM_PREFIX}/share/glib-2.0/schemas"
SCHEMAS_DST="$TMP_DIR/share/glib-2.0/schemas"

if [[ -f "$SCHEMAS_SRC/gschemas.compiled" ]]; then
    echo "Copying GSettings schemas..."
    mkdir -p "$SCHEMAS_DST"
    cp "$SCHEMAS_SRC/gschemas.compiled" "$SCHEMAS_DST/"
else
    echo "Warning: gschemas.compiled not found"
fi

# -- GTK icon theme (Adwaita) ---------------------------------------------
ICONS_SRC="${MSYSTEM_PREFIX}/share/icons/Adwaita"
if [[ -d "$ICONS_SRC" ]]; then
    echo "Copying Adwaita icon theme..."
    mkdir -p "$TMP_DIR/share/icons"
    cp -r "$ICONS_SRC" "$TMP_DIR/share/icons/"
fi

HICOLOR_SRC="${MSYSTEM_PREFIX}/share/icons/hicolor"
if [[ -d "$HICOLOR_SRC" ]]; then
    mkdir -p "$TMP_DIR/share/icons"
    cp -r "$HICOLOR_SRC" "$TMP_DIR/share/icons/"
fi

# -- gdbus.exe (required for GTK on Windows) ------------------------------
GDBUS="${MSYSTEM_PREFIX}/bin/gdbus.exe"
if [[ -f "$GDBUS" ]]; then
    echo "Copying gdbus.exe..."
    cp -u "$GDBUS" "$TMP_DIR/"
fi

# -- Create ZIP ------------------------------------------------------------
echo ""
echo "Creating ZIP: $ZIPFILE"
(cd "$TMP_DIR" && zip -r -q "../$ZIPFILE" .)

# Move ZIP and clean up
mv "$TARGET_DIR/$ZIPFILE" "$TARGET_DIR/" 2>/dev/null || true
rm -rf "$TMP_DIR"

echo ""
echo "================================================================"
echo "  Package created: $TARGET_DIR/$ZIPFILE"
echo "================================================================"
echo ""
du -h "$TARGET_DIR/$ZIPFILE"
