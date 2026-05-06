# Building and Packaging

## Prerequisites

### Linux (Ubuntu 22.04+ / Debian 12+)

```bash
sudo apt install cmake ninja-build g++ pkg-config \
    libgtkmm-4.0-dev libsqlite3-dev libboost-dev
```

### Windows (MSYS2 Clang64)

1. Install [MSYS2](https://www.msys2.org/)
2. Open **MSYS2 Clang64** terminal
3. Install dependencies:

```bash
pacman -S mingw-w64-clang-x86_64-toolchain \
    mingw-w64-clang-x86_64-cmake \
    mingw-w64-clang-x86_64-ninja \
    mingw-w64-clang-x86_64-gtkmm-4.0 \
    mingw-w64-clang-x86_64-sqlite3 \
    mingw-w64-clang-x86_64-boost
```

---

## Build

### Linux

```bash
# Debug (with AddressSanitizer + UBSanitizer)
./build-linux.sh

# Release (optimized, no sanitizers)
./build-linux.sh --release
```

Or manually:

```bash
cmake --preset debug
cmake --build build/debug -- -j$(nproc)
./build/debug/industrial-hmi
```

### Windows (from MSYS2 Clang64 terminal)

```bash
./build-windows.sh
./build/debug/industrial-hmi.exe
```

Or manually:

```bash
cmake --preset windows-msys2-debug
cmake --build build/debug
./build/debug/industrial-hmi.exe
```

---

## Package for Distribution

### Windows

Creates a standalone ZIP with the executable, all DLL dependencies,
GTK runtime files, and application assets. Runs on any Windows machine
without MSYS2 installed.

```bash
# From MSYS2 Clang64 terminal
./package-windows.sh            # package release build
./package-windows.sh -d         # package debug build
```

Output: `install/industrial-hmi_<type>_<git-hash>.zip`

### Linux

Creates a tarball with the executable, assets, a launch script,
and a dependency list. Users need GTK4 and SQLite installed via
their package manager.

```bash
./package-linux.sh              # package release build
./package-linux.sh -d           # package debug build
```

Output: `install/industrial-hmi_<type>_<git-hash>.tar.gz`

---

## CMake Presets

| Preset | Platform | Type | Sanitizers |
|--------|----------|------|------------|
| `debug` | Any | Debug | ASan + UBSan |
| `release` | Any | Release | Off |
| `windows-msys2-debug` | Windows | Debug | Off |
| `windows-msys2-release` | Windows | Release | Off |

---

## Edge AI Inference (Optional)

The Edge AI layer (`OnnxImageClassifier`) is opt-in through the
`BUILD_ML_CLASSIFIER` CMake option. The default build does not depend
on ONNX Runtime; turning the option on pulls a prebuilt distribution
into `build/onnxruntime/` and links it into the existing
`objectsMl` library.

```bash
# Step 1: download the prebuilt ONNX Runtime once.
bash scripts/setup-onnxruntime.sh

# Step 2: configure with the option turned on.
cmake -S . -B build/release -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=ON \
    -DBUILD_ML_CLASSIFIER=ON \
    -DONNXRUNTIME_ROOT="$(pwd)/build/onnxruntime"

# Step 3: build.
cmake --build build/release -- -j$(nproc)
```

For the integration test (`OnnxImageClassifierTest`) to actually load
a model -- rather than skip with "model not found" -- the Python
pipeline under `scripts/ml/` must produce
`assets/models/mobilenetv2_int8.onnx` first. See `scripts/ml/README.md`
for the four-step pipeline.

```bash
# After the C++ build above:
cd build/release
LD_LIBRARY_PATH=$(pwd)/../onnxruntime/lib \
    ctest --output-on-failure -R OnnxImageClassifierTest
```

The CI workflow's `ml-integration` job runs this exact sequence on
every PR.

---

## Running with Sanitizer Suppressions (Linux)

Debug builds on Linux enable AddressSanitizer, which reports known
leaks from third-party libraries (GTK, Pango, Fontconfig). Suppress
them with:

```bash
LSAN_OPTIONS=suppressions=asan_suppressions.txt ./build/debug/industrial-hmi
```
