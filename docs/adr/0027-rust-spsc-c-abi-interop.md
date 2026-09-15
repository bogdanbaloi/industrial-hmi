# ADR-0027: Expose the Rust SPSC over a C ABI (C++/Rust interop)

## Status
Accepted (2026-09-15). Phase 2 of the Rust SPSC component (ADR-0026).

## Context
Phase 1 shipped a standalone Rust SPSC lock-free queue (`rust/spsc`, ADR-0026).
To demonstrate polyglot interop, the C++ side must be able to call the Rust
queue across a stable boundary, the same way the C++ host already loads the
ONNX inference plugin (ADR-0019: a separate shared module behind a small C ABI,
loaded with dlopen / LoadLibrary).

## Decision
Expose the queue over a C ABI and prove it from C++.

- **A C ABI, built into a `cdylib`.** The crate gains `crate-type = ["lib",
  "cdylib"]`: `lib` for the Rust tests, `cdylib` for the shared library C and
  C++ load. The FFI surface lives in `rust/spsc/src/ffi.rs` as
  `#[no_mangle] extern "C"` functions. C is the lingua franca both languages
  speak at the binary level, so Rust exports "the C way" and C++ calls it "the
  C way".
- **The producer/consumer split is preserved across the boundary.**
  `hmi_spsc_create` returns TWO opaque handles (a `SpscProducer*` and a
  `SpscConsumer*`). One thread pushes through the producer handle, one other
  thread pops through the consumer handle. This keeps the single-producer /
  single-consumer contract sound from C: each thread touches only its own
  handle, so there is never aliasing of the same memory from two threads.
- **A `#[repr(C)]` payload.** The queue carries `Sample { uint64_t ts_ms;
  double value; }`, a plain C-layout struct, because a C ABI cannot carry Rust
  generics.
- **Windows toolchain `x86_64-pc-windows-gnu`.** The Rust cdylib and the C++
  host (MSYS2 / MinGW) are ABI-compatible because both are MinGW-based (this is
  why ADR-0026 chose the GNU toolchain).
- **Proof by a C++ harness.** `rust/spsc/ffi-test/main.cpp` loads the cdylib
  (LoadLibrary on Windows, dlopen on Linux), resolves the five symbols, and
  drives the queue from a producer thread and a consumer thread, asserting
  every sample arrives in order. A CI job runs it on Linux.

## Alternatives rejected
- **A single opaque handle holding both ends.** Simpler C API, but then a
  concurrent `push` and `pop` would each take `&mut` of the same handle from
  two threads, which is undefined behaviour in Rust. The two-handle split is
  the sound design and mirrors the Rust type-level split.
- **Link the Rust code statically into the C++ binary.** Simpler, but loses the
  runtime-plugin isolation and the "loaded like the ONNX plugin" story.
- **Expose the generic queue directly.** Impossible: a C ABI has no generics,
  so a concrete element type and capacity must be chosen.

## Consequences
- The crate now also builds a shared library; the C ABI is a stable surface.
- A CI `ffi` job builds the cdylib, compiles the C++ harness and runs it.
- Honesty rail: this is an interop DEMONSTRATION, not a change to the HMI hot
  path. The C++ HMI keeps its own C++ `SpscQueue` on latency-sensitive paths;
  routing that hot path through FFI would add call overhead and defeat the
  point of a lock-free queue. The value here is the demonstrated C++/Rust C ABI
  skill, mirroring the ONNX plugin seam.
