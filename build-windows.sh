#!/bin/bash
# Windows MSYS2/Clang64 Build Script
# Run from MSYS2 Clang64 terminal

set -e  # Exit on error

echo "╔════════════════════════════════════════════════════════════╗"
echo "║  Industrial HMI - Windows MSYS2/Clang64 Build             ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

# Clean previous build
echo "🧹 Cleaning previous build..."
rm -rf build/debug build/release

# Configure Debug build
echo ""
echo "⚙️  Configuring Debug build..."
cmake --preset windows-msys2-debug

# Build Debug
echo ""
echo "🔨 Building Debug..."
cmake --build build/debug

# Check if executable exists
if [ -f "build/debug/industrial-hmi.exe" ]; then
    echo ""
    echo "✅ BUILD SUCCESSFUL!"
    echo ""
    echo "Executable: build/debug/industrial-hmi.exe"
    echo ""
    echo "To run:"
    echo "  ./build/debug/industrial-hmi.exe"
    echo ""
else
    echo ""
    echo "❌ BUILD FAILED - executable not found"
    exit 1
fi
