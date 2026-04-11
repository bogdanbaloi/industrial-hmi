# Windows Build - Quick Start

## Prerequisites (One-time setup)

1. **Install MSYS2** from https://www.msys2.org/
2. **Open MSYS2 Clang64 terminal** (NOT MSYS2 MSYS!)
3. **Install dependencies:**

```bash
pacman -S \
  mingw-w64-clang-x86_64-toolchain \
  mingw-w64-clang-x86_64-cmake \
  mingw-w64-clang-x86_64-ninja \
  mingw-w64-clang-x86_64-gtk4 \
  mingw-w64-clang-x86_64-gtkmm-4.0 \
  mingw-w64-clang-x86_64-sqlite3 \
  mingw-w64-clang-x86_64-boost \
  mingw-w64-clang-x86_64-nlohmann-json \
  mingw-w64-clang-x86_64-pkgconf
```

## Build

### Option 1: Automated (Recommended)

```bash
./build-windows.sh
```

### Option 2: Manual

```bash
rm -rf build
cmake --preset windows-msys2-debug
cmake --build build/debug
```

## Run

```bash
./build/debug/industrial-hmi.exe
```

## Troubleshooting

### "CMake not found"
- Make sure you're in **MSYS2 Clang64** terminal (NOT MSYS2 MSYS)
- Verify: `which cmake` should show `/clang64/bin/cmake`

### "Ninja not found"
```bash
pacman -S mingw-w64-clang-x86_64-ninja
```

### "pkg-config not found"
```bash
pacman -S mingw-w64-clang-x86_64-pkgconf
```

### Build fails with missing library
```bash
pacman -Ss <library-name>  # Search for package
pacman -S mingw-w64-clang-x86_64-<package>  # Install
```

## Development

### VSCode Setup
Launch VSCode from MSYS2 Clang64 terminal to inherit PATH:

```bash
cd /c/Users/YourName/Downloads/github-portfolio
code .
```

### CMake Presets
- `windows-msys2-debug` - Debug build with symbols
- `windows-msys2-release` - Optimized release build

## Documentation
See `docs/WINDOWS_BUILD_SETUP.md` for comprehensive setup guide.
