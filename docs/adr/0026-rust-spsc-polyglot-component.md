# ADR-0026: Rust SPSC as a polyglot portfolio component

## Status
Accepted (2026-09-15). Phase 1 (standalone crate) in progress. Phase 2
(C ABI integration into the C++ HMI) planned.

## Context
Rust recurs as a required or preferred skill across target roles. The
portfolio's strongest, best-tested concurrency primitive is the C++
`app::core::SpscQueue` (`src/core/SpscQueue.h`, ADR-0018, REQ-ARCH-010):
a lock-free single-producer / single-consumer ring buffer. Re-expressing
it in Rust turns a recurring skills gap into a defensible portfolio piece
and yields a direct C++-vs-Rust concurrency comparison for interviews.

## Decision
Add a Rust component to this repo rather than a separate project.

- **Location `rust/spsc/` at the repo root**, not under `src/`. A Rust
  crate needs its own `src/` and build tree; nesting it under the C++
  `src/` would force a `src`-inside-`src` layout and risk CMake globbing
  the crate's build output.
- **A standalone library crate `hmi-spsc`, zero runtime dependencies.**
  Own every line for interview defensibility. Loom and Miri are test-only
  tools, never runtime deps.
- **The SPSC contract is enforced by the type system.** `channel()`
  returns distinct, non-`Clone` `Producer` and `Consumer` handles, so
  "exactly one producer, one consumer" is a compile-time guarantee rather
  than the comment-plus-ThreadSanitizer discipline the C++ version relies
  on.
- **Same acquire/release (Lamport) memory ordering** as the C++ original.
- **Same engineering discipline:** this ADR, a `REQUIREMENTS.md` with
  OpenFastTrace ids and tagged tests, clippy + rustfmt as the lint gate,
  Loom (exhaustive interleavings) as the ThreadSanitizer analogue, and
  Miri for undefined behaviour in the unsafe code.
- **Windows toolchain `x86_64-pc-windows-gnu`:** self-contained (no MSVC
  build tools required), matches the MSYS2/CLANG64 C++ build and the
  future C ABI.

## Alternatives rejected
- **Separate repo or crate outside the portfolio.** Loses the "one core,
  many consumers" cohesion that makes the Qt and MCP components strong; an
  orphan crate tells a weaker story.
- **Inside `src/`.** Yields `src/.../src` and risks CMake scanning the
  crate's build output.
- **Depend on `crossbeam` for the ring buffer or cache padding.** Faster
  to write, but removes the very thing being demonstrated: hand-built,
  defensible lock-free code.

## Consequences
- The repo becomes polyglot (C++ plus Rust). A CI Rust job (cargo
  test / clippy / fmt, then Loom / Miri) runs alongside the C++ matrix.
- Phase 2 exposes the queue over a 3-function C ABI, mirroring the
  existing `industrial_ml_ort` dlopen / LoadLibrary plugin seam, so the
  C++ HMI can consume the Rust queue.
