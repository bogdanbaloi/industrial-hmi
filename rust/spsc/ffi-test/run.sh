#!/usr/bin/env bash
# Build the Rust cdylib, compile the C++ harness against the C ABI header, and
# run it so C++ drives the Rust SPSC across the boundary. Used locally (WSL) and
# by the CI "ffi" job. Linux/macOS (dlopen); the harness also supports Windows
# (LoadLibrary) when built there.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
crate="$(cd "$here/.." && pwd)"
cd "$crate"

echo "== building the Rust cdylib =="
cargo build

echo "== compiling the C++ harness =="
cxx="${CXX:-c++}"
"$cxx" -std=c++17 -Wall -Wextra -I include ffi-test/main.cpp -o target/debug/ffi_harness -ldl -lpthread

echo "== running (C++ -> Rust over the C ABI) =="
./target/debug/ffi_harness "$crate/target/debug/libhmi_spsc.so"
