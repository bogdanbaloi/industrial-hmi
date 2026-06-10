# 0018. Lock-free SPSC queue for the ingest hot path (and only there)

## Status

Accepted (2026-06-10).

## Context

Every integration backend that polls or receives telemetry hands the
samples to an ingest bridge, which writes them into the model. Today
that handoff happens **on the I/O worker thread**: e.g.
`ModbusPollLoop::pollOnce()` runs on its own `std::jthread`, does a
blocking Boost.Asio socket read per register (with connect/request
timeouts), and then calls `bridge_.onRegisterChanged()` directly. The
bridge call ends up taking the model's `std::mutex`, which is also
held by the GTK side when it snapshots state for the dashboard.

That couples two latencies that have no business being coupled: the
Modbus poll cadence (driven by hard socket timeouts) and the model's
lock-hold time (driven by whatever the GTK reader is doing). If the
model lock is contended, the I/O thread stalls inside dispatch instead
of going back to servicing the wire.

The clean decoupling for a strict one-producer / one-consumer seam is
a bounded **lock-free SPSC ring buffer**: the producer pushes and
returns immediately (drop on overflow, never block); a separate
consumer drains at its own pace. This ADR records the decision to add
that primitive (`app::core::SpscQueue<T, N>`, REQ-ARCH-010) and to
apply it **only** where the producer/consumer relationship is genuinely
1:1.

## Decision

1. Add a header-only `SpscQueue<T, kCapacity>` in `src/core/`:
   - `kCapacity` is a power of two (compile-time `static_assert`) so
     index-to-slot is a mask, not a modulo. One slot is reserved to
     tell full from empty, giving a usable depth of `kCapacity - 1`.
   - `head_` (consumer-owned) and `tail_` (producer-owned) are
     `std::atomic<std::size_t>`, each `alignas(64)` on its own cache
     line to eliminate false sharing.
   - **Memory ordering** is the Lamport SPSC protocol. There are
     exactly four ordered atomic accesses, and they must stay exactly
     these:
     - producer publish: `tail_.store(next, release)` — releases the
       element write that precedes it;
     - producer full-check: `head_.load(acquire)`;
     - consumer observe: `tail_.load(acquire)` — acquires the
       producer's element write;
     - consumer free-slot: `head_.store(head + 1, release)`.
     A `relaxed` load on either observed index is a data race
     ThreadSanitizer flags; the release/acquire pair is what makes the
     consumer's read of a slot happen-after the producer's write to it.
   - `push()`/`pop()` are `[[nodiscard]] noexcept`; overflow returns
     `false` (drop), it never blocks or throws.

2. Overflow semantics are **drop-the-sample**. For a telemetry poll
   loop this is correct: the next cycle reads a fresh value, so a
   dropped sample is just slightly-staler data, never corruption. A
   `droppedSamples_` counter makes the drop observable.

3. Wire it at the `ModbusPollLoop` -> `ModbusIngestBridge` seam (a
   strict SPSC: one poll thread produces, one dedicated drain thread
   consumes). The poll thread never touches the bridge again. *(The
   cross-thread wiring is REQ-ARCH-010 Phase 2; the primitive + its
   ThreadSanitizer stress test is Phase 1.)*

## Rejected alternatives

- **Keep the `std::mutex` handoff everywhere.** The status quo is
  correct and simple, and it stays the default for every seam that is
  NOT strict-SPSC (see below). It is rejected *only* for the Modbus
  poll seam, where the I/O-latency-vs-lock-hold coupling is real.

- **Apply lock-free to every ingest seam.** Rejected — the same
  "don't generalise a sharp tool" discipline as ADR-0014 (Result),
  ADR-0016 (profiling) and ADR-0017 (TimeSource). The other ingest
  seams are NOT single-producer/single-consumer:
  - `SensorIngestBridge` (MQTT) has multiple topic callbacks → fan-in,
    not SPSC. A lock-free MPSC queue is a different, harder primitive
    with no payoff here.
  - `PrimaryToSecondaryBridge` fires on the primary model's observer
    chain — no dedicated worker thread to decouple.
  - `HistorianBridge` is driven from the GTK main thread already; there
    is nothing to hand off.
  Forcing SPSC onto a fan-in seam would be incorrect (two producers on
  a single-producer queue is UB), so the queue's own contract scopes
  its use.

- **`boost::lockfree::spsc_queue`.** A correct, battle-tested option.
  Rejected for the portfolio because a ~70-line hand-rolled queue with
  an explicit, reviewable memory-ordering argument and a TSan stress
  test demonstrates the competence the dependency would hide, and it
  keeps the core free of another Boost component. (Boost.Asio is
  already a dep; Boost.Lockfree would be a new one.)

- **A blocking bounded queue (mutex + condition_variable).** Simpler
  to write, but it reintroduces exactly the blocking-on-the-I/O-thread
  coupling the change exists to remove. The producer must never wait.

## Consequences

- One small, well-tested concurrency primitive enters `src/core/`,
  reusable for any future strict-SPSC seam.
- The Modbus poll thread is decoupled from model-dispatch latency.
- The ThreadSanitizer CI gate (REQ-ARCH-008) now also covers a
  hand-rolled lock-free structure — the `StressProducerConsumer` test
  pushes/pops 1,000,000 items across two `jthread`s and verifies the
  triangular-sum invariant under `-fsanitize=thread`.
- Reconsideration trigger: if a future backend needs one consumer
  servicing *several* poll loops, the SPSC contract no longer holds and
  an MPSC (or a per-loop SPSC fan-in) design must be evaluated — at
  which point this ADR is revisited rather than silently violated.
