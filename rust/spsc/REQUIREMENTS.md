# hmi-spsc requirements

Ported from the C++ `app::core::SpscQueue` (industrial-hmi). Same
traceability discipline as the parent repo: each requirement carries an
OpenFastTrace id and is `Needs: utest`, covered by a tagged test with a
`// [utest->req~<id>~1]` comment. OpenFastTrace is language-agnostic (it
traces text tags, not C++), so the same jar the C++ side uses runs over
this crate too.

> Scaffold note: tests are added in steps 6-8, so these requirements are
> not OFT-covered yet. This file defines the contract the implementation
> and its tests must satisfy.

## REQ-SPSC-BOUNDED
`req~spsc-bounded~1`
The queue **shall** be a bounded ring buffer whose capacity is a power of
two, addressing slots by mask (`index & (N - 1)`), with a usable depth of
`N - 1` (one slot reserved to distinguish full from empty).
Verified by: unit test.
Needs: utest

## REQ-SPSC-CONTRACT
`req~spsc-contract~1`
The single-producer / single-consumer contract **shall** be enforced by
the type system: construction yields exactly one `Producer` and one
`Consumer` handle, neither `Clone`, so a second producer or consumer
cannot be obtained safely.
Verified by: unit test (plus the compile-time handle types).
Needs: utest

## REQ-SPSC-ORDERING
`req~spsc-ordering~1`
Cross-thread publication **shall** use the Lamport acquire/release
protocol: the producer publishes a slot with a Release store to `tail`,
the consumer observes it with an Acquire load, and symmetrically for
`head`, so the consumer never reads a slot before the producer finished
writing it.
Verified by: threaded test and a Loom model test.
Needs: utest

## REQ-SPSC-NONBLOCKING
`req~spsc-nonblocking~1`
`push` **shall** return the item back to the caller when the queue is full
and `pop` **shall** return `None` when empty. Neither **shall** block or
allocate.
Verified by: unit test.
Needs: utest

## REQ-SPSC-NO-FALSE-SHARING
`req~spsc-no-false-sharing~1`
`head` and `tail` **shall** reside on separate cache lines so the producer
and consumer never invalidate each other's cache line by writing their own
index (false-sharing elimination, matching the C++ `alignas(64)`).
Verified by: unit test (alignment / offset assertion).
Needs: utest
