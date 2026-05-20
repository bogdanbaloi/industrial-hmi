# Building and Packaging

## Prerequisites

### Linux (Ubuntu 24.04+ / Debian 12+)

> CI runs on `ubuntu-24.04`. 22.04 may still work but is no longer
> tested; on older systems some gtkmm-4 features may regress.


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

### Demo with authentication enabled

Auth and historian default to **off** in the source-tree config so
unit tests + the no-auth dev path aren't gated by a login prompt.
To enable them for a local demo run of the native binary (Linux or
Windows), use the helper script:

```bash
./enable-auth.sh                # defaults to build/debug
./enable-auth.sh build/release  # or a different build dir
```

It flips `auth.enabled` + `historian.enabled` in
`<build-dir>/config/app-config.json` and ensures `<build-dir>/data/`
exists. Re-run after any build that refreshes config files from the
source tree.

The Docker compose stack uses its own config override
(`docker/app-config.docker.json`) and does not need this script.

Default credentials seeded on first launch: `operator / operpass`,
`maint / maintpass`, `admin / adminpass`.

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

### Architecture: ONNX Runtime as a runtime plugin

The host binary (`industrial-hmi.exe`) does NOT link libonnxruntime.
Instead, all ORT-touching code lives in a separate shared module
(`industrial_ml_ort.{dll,so,dylib}`) that the facade `dlopen`s /
`LoadLibrary`s on first construction of `OnnxImageClassifier`. The
host binary's `DT_NEEDED` set is identical with and without
`BUILD_ML_CLASSIFIER` -- ORT only enters the address space when the
operator clicks the Inspection tab.

This decoupling exists because ORT's prebuilt distribution bundles a
large set of dependencies (abseil, MLAS, custom allocators) that, when
loaded into a GTK4 process eagerly, can interact in
hard-to-predict ways with libglib's allocator across the GTK widget
class registration path. Loading via plugin keeps the boot path of
the GUI free of any ORT-side initialization.

### Known issue: WSL live demo

The `industrial-hmi` GUI binary crashes during GTK widget class
registration (`gtk_widget_class_add_binding_signal`) when launched
inside WSL2 (Ubuntu 24.04) with `BUILD_ML_CLASSIFIER=ON`. The crash
reproduces even with the plugin module on disk but never loaded
(no model file present), so it is not caused by the ORT shared
library being mapped into the process. The `OnnxImageClassifierTest`
integration test exercises the full plugin path (load + Run + softmax
+ label resolution) and passes; the GitHub CI `ml-integration` job
runs on native Ubuntu 24.04 (not WSL) and the test binary path is
clean. The WSL2 GTK4 corruption appears to be specific to the WSL2
environment + the `BUILD_ML_CLASSIFIER=ON` link line and is not on
the deployment path. For visual demos, build on native Linux or
MSYS2 / Windows.

---

## Running with Sanitizer Suppressions (Linux)

Debug builds on Linux enable AddressSanitizer, which reports known
leaks from third-party libraries (GTK, Pango, Fontconfig). Suppress
them with:

```bash
LSAN_OPTIONS=suppressions=asan_suppressions.txt ./build/debug/industrial-hmi
```
