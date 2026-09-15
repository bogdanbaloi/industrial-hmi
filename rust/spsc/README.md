# hmi-spsc

Lock-free single-producer / single-consumer bounded ring buffer, in Rust.

A port of the C++ `app::core::SpscQueue` (`../../src/core/SpscQueue.h`,
ADR-0018) from this industrial-hmi portfolio. Same algorithm and the same
Lamport acquire/release memory ordering. The Rust version moves the
single-producer/single-consumer contract into the type system. Design
record: `../../docs/adr/0026-rust-spsc-polyglot-component.md`.

## Why this exists
Rust recurs as a preferred skill across target roles. This reimplements
the portfolio's best-tested concurrency primitive in Rust, turning a
skills gap into a defensible piece and a direct C++-vs-Rust concurrency
comparison for interviews.

## C++ -> Rust mapping
| C++ (`SpscQueue.h`) | Rust (`hmi-spsc`) | Note |
|---|---|---|
| one class, `push()` / `pop()` "producer/consumer only" by doc comment | `channel()` -> `(Producer, Consumer)`, neither `Clone` | contract enforced by the type system, not by discipline |
| `std::atomic<size_t>` head/tail, `memory_order_{acquire,release,relaxed}` | `AtomicUsize` + `Ordering::{Acquire,Release,Relaxed}` | identical protocol |
| `alignas(64)` on head and tail | cache-line-padded fields | false-sharing elimination |
| `std::array<T, N>` | `[UnsafeCell<MaybeUninit<T>>; N]` | no `T: Default` requirement; manual `Drop` |
| power-of-two `N`, `& (N - 1)` mask | const-generic `N`, `& (N - 1)` mask | capacity checked at compile time |
| ThreadSanitizer CI gate | Loom model test + Miri | exhaustive interleavings vs dynamic detection |

## Status
Phase 1 scaffold. The implementation lands step by step against
`REQUIREMENTS.md`. Phase 2 exposes the queue over a 3-function C ABI
(mirroring the existing `industrial_ml_ort` dlopen plugin) so the C++ HMI
can consume it.

## Build, test, lint
```
cargo build
cargo test
cargo clippy -- -D warnings
cargo fmt --check
```
Toolchain: `stable-x86_64-pc-windows-gnu` (self-contained, no MSVC
dependency, matches the MSYS2/CLANG64 C++ build).
