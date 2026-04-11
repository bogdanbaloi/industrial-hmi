# Windows Build Setup (MSYS2/Clang64)

## Prerequisites

This project uses MSYS2 with Clang64 environment for Windows builds.

### 1. Install MSYS2

Download and install from: https://www.msys2.org/

Default installation path: `C:\msys64`

### 2. Install Build Tools

Open **MSYS2 Clang64** terminal and run:

```bash
# Update package database
pacman -Syu

# OPTION A: Install complete toolchain (RECOMMENDED - everything you need)
pacman -S mingw-w64-clang-x86_64-toolchain

# This installs: clang, clang++, make, ar, ld, and all build tools

# Install CMake and Ninja
pacman -S mingw-w64-clang-x86_64-cmake
pacman -S mingw-w64-clang-x86_64-ninja

# OPTION B: Install individually (if you want more control)
pacman -S mingw-w64-clang-x86_64-clang
pacman -S mingw-w64-clang-x86_64-cmake
pacman -S mingw-w64-clang-x86_64-ninja
pacman -S mingw-w64-clang-x86_64-binutils  # Provides ar, ranlib, etc.
pacman -S make

# Install pkg-config (required for finding libraries)
pacman -S mingw-w64-clang-x86_64-pkgconf
```

### 3. Install GTK4 Dependencies

```bash
# GTK4 and dependencies
pacman -S mingw-w64-clang-x86_64-gtk4
pacman -S mingw-w64-clang-x86_64-gtkmm-4.0

# Additional libraries
pacman -S mingw-w64-clang-x86_64-sqlite3
pacman -S mingw-w64-clang-x86_64-boost
pacman -S mingw-w64-clang-x86_64-nlohmann-json
```

### 4. Verify Installation

```bash
# Check compiler and build tools
clang++ --version
cmake --version
ninja --version
make --version

# Check archiver and binutils
ar --version
ranlib --version

# Check pkg-config
pkg-config --version

# Verify GTK4 (after installing dependencies)
pkg-config --modversion gtk4
pkg-config --modversion gtkmm-4.0
```

Expected output:
```
clang version 18.x.x or newer
cmake version 3.20.0 or newer
ninja 1.11.x or newer
make 4.x or newer
GNU ar 2.x or newer
gtk4 4.x.x
gtkmm-4.0 4.x.x
```

If any command fails with "command not found", install the missing package.

## Building the Project

### Configure with CMake Presets

In MSYS2 Clang64 terminal:

```bash
cd /path/to/github-portfolio

# Option 1: Debug build (with Ninja)
cmake --preset windows-msys2-debug

# Option 2: Release build (with Ninja)
cmake --preset windows-msys2-release

# Build
cmake --build build/debug
# or
cmake --build build/release
```

### Manual Configuration (without presets)

If presets don't work:

```bash
# Create build directory
mkdir -p build
cd build

# Configure with MSYS Makefiles
cmake .. \
  -G "MSYS Makefiles" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build
cmake --build .
```

### With Ninja (faster)

```bash
mkdir -p build
cd build

cmake .. \
  -G "Ninja" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++

ninja
```

## VSCode Configuration

### Install VSCode Extensions

- C/C++ (Microsoft)
- CMake Tools (Microsoft)
- clangd (LLVM)

### Select CMake Preset

1. Open Command Palette: `Ctrl+Shift+P`
2. Type: "CMake: Select Configure Preset"
3. Choose: **Windows MSYS2 Debug (Clang64)**
4. Build: `Ctrl+Shift+P` → "CMake: Build"

### Alternative: Select Kit

If presets don't show:

1. `Ctrl+Shift+P` → "CMake: Select a Kit"
2. Choose: **Clang 18.x.x (MSYS2)**
3. Configure: `Ctrl+Shift+P` → "CMake: Configure"
4. Build: `Ctrl+Shift+P` → "CMake: Build"

## Running the Application

```bash
# From MSYS2 terminal:
cd build/debug
./industrial-hmi.exe

# Or from VSCode:
# Press F5 (if launch.json is configured)
```

## Common Issues

### Issue: "CMAKE_MAKE_PROGRAM not set" or "CMAKE_AR not found"
**Solution:** Install complete toolchain:
```bash
pacman -S mingw-w64-clang-x86_64-toolchain
pacman -S mingw-w64-clang-x86_64-ninja
```

Or install individually:
```bash
pacman -S make
pacman -S mingw-w64-clang-x86_64-binutils  # Provides ar, ranlib
```

Verify:
```bash
which make     # Should show: /clang64/bin/make
which ar       # Should show: /clang64/bin/ar
which ninja    # Should show: /clang64/bin/ninja
```

### Issue: "Ninja not found"
**Solution:**
```bash
pacman -S mingw-w64-clang-x86_64-ninja
```

### Issue: "CMAKE_CXX_COMPILER not set"
**Solution:** Use MSYS2 presets or manually set:
```bash
cmake .. -DCMAKE_CXX_COMPILER=clang++
```

### Issue: "gtk4 not found"
**Solution:**
```bash
pacman -S mingw-w64-clang-x86_64-gtk4
pacman -S mingw-w64-clang-x86_64-gtkmm-4.0
```

### Issue: "Cannot find -lgtk-4"
**Solution:** Make sure you're in MSYS2 **Clang64** terminal, not MinGW64!
```bash
# Wrong terminal shows: /mingw64
# Correct terminal shows: /clang64
```

### Issue: Sanitizers not working on Windows
**Solution:** Sanitizers (ASan, UBSan) are disabled on Windows MSYS2 presets because they're not fully supported. Use Linux or WSL for sanitizer testing.

## Environment Variables

MSYS2 Clang64 automatically sets these:
```bash
PATH includes: /clang64/bin
CC=clang
CXX=clang++
PKG_CONFIG_PATH=/clang64/lib/pkgconfig
```

Verify with:
```bash
echo $PATH | grep clang64
which clang++
```

## Build Performance

**Ninja vs Make:**
- Ninja: ~2-3x faster (parallel by default)
- Make: Simpler, more portable

**Recommendation:** Use Ninja for development

## Troubleshooting

### Clean build:
```bash
rm -rf build
cmake --preset windows-msys2-debug
cmake --build build/debug
```

### Verbose build:
```bash
cmake --build build/debug --verbose
```

### Check dependencies:
```bash
ldd build/debug/industrial-hmi.exe
```

## Additional Tools (Optional)

```bash
# Debugger
pacman -S mingw-w64-clang-x86_64-gdb

# Profiler
pacman -S mingw-w64-clang-x86_64-perf

# Static analyzer
pacman -S mingw-w64-clang-x86_64-clang-analyzer
```

## Notes

- Always use **MSYS2 Clang64** terminal (not MinGW64, not MSYS)
- Clang64 uses Clang compiler with MinGW runtime
- Better C++20 support than MinGW GCC
- Compatible with Windows native APIs
- Can link against MSVC libraries if needed

## Success Verification

After successful build, you should see:
```
[100%] Built target industrial-hmi
```

Binary location:
```
build/debug/industrial-hmi.exe
```

Run it:
```bash
./build/debug/industrial-hmi.exe
```

You should see the GTK4 application window open!
