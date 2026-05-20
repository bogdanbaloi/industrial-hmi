# Linux Build - Quick Start

## Prerequisites (One-time setup)

Tested on **Ubuntu 24.04 LTS**. Debian 12+ and Fedora 41+ work with
package-name substitutions.

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  ninja-build \
  pkg-config \
  libgtkmm-4.0-dev \
  libsqlite3-dev \
  libsodium-dev \
  libboost-dev \
  gettext
```

Optional dependencies (auto-detected by CMake, only needed if you
turn the matching feature on):

```bash
# Modbus TCP backend (BUILD_MODBUS_BACKEND=ON, default)
# -- no extra package, uses Boost.Asio.

# OPC-UA backend (BUILD_OPCUA_BACKEND=ON, default OFF)
# -- pulls open62541 via CMake FetchContent on first configure.

# Edge AI ONNX classifier (BUILD_ML_CLASSIFIER=ON)
bash scripts/setup-onnxruntime.sh
```

## Build

### Option 1: Automated (Recommended)

```bash
./build-linux.sh                 # debug build with sanitizers
./build-linux.sh --release       # release build
```

### Option 2: Manual

```bash
cmake --preset debug
cmake --build build/debug -- -j$(nproc)
```

## Run

```bash
./build/debug/industrial-hmi
```

### Demo with authentication enabled

Auth + historian default to **off** in the source-tree config so
unit tests aren't gated by a login prompt. For a local demo run
with the login flow + audit log enabled, use the helper:

```bash
./enable-auth.sh                 # default: build/debug
./build/debug/industrial-hmi
```

Default credentials seeded on first launch: `operator / operpass`,
`maint / maintpass`, `admin / adminpass`.

The Docker compose stack uses its own config override
(`docker/app-config.docker.json`) and does not need this script.

### Headless console front-end

Same source tree, same presenters, zero gtkmm at link time:

```bash
./build/debug/industrial-hmi-console
```

Type `help` for available commands. Useful for CI scenarios,
scripting, and demos without a display server.

## Troubleshooting

### "gtkmm-4.0 not found" during CMake configure
Older Ubuntu / Debian releases ship gtkmm-3 by default. Confirm
the gtkmm-4.0 package is installed:

```bash
pkg-config --modversion gtkmm-4.0   # should print 4.10 or newer
```

### "libsodium not found"
```bash
sudo apt install libsodium-dev
```

### Sanitizer false positives during shutdown
The codebase ships a `valgrind.supp` file for third-party library
leaks (SQLite globals, etc.) that are not user-fixable. If running
under Valgrind directly:

```bash
valgrind --suppressions=valgrind.supp ./build/debug/industrial-hmi
```

### "model not found" in QualityInspectionPage
The Edge AI inspection feature needs an ONNX model file at
`assets/models/mobilenetv2_int8.onnx`. The Python pipeline that
produces it is documented in `scripts/ml/README.md`. The C++ test
suite skips at runtime when the model is missing, so the rest of
the build is safe to run without it.

## Development

### VS Code Setup
Open the repo root directly:

```bash
cd /path/to/industrial-hmi
code .
```

The CMake Tools extension picks up `CMakePresets.json` and offers
the `debug` / `release` presets in the status bar.

### CMake Presets
- `debug` - Debug build with AddressSanitizer + UndefinedBehaviorSanitizer.
- `release` - Optimized build, no sanitizers.

For ThreadSanitizer + Valgrind coverage, see the `.github/workflows/
ci.yml` jobs `sanitizers-tsan` and `valgrind` -- they configure
dedicated build directories with the right flags.

## Documentation

See [`BUILD.md`](BUILD.md) for the comprehensive build + packaging
guide covering both Linux and Windows. This file is the Linux-only
Quick Start; Windows users start with [`WINDOWS_BUILD.md`](WINDOWS_BUILD.md).
