//! Lock-free single-producer / single-consumer (SPSC) bounded ring buffer.
//!
//! Rust port of the C++ `app::core::SpscQueue` from this industrial-hmi
//! portfolio (`../../src/core/SpscQueue.h`, ADR-0018). Same Lamport
//! acquire/release protocol. The Rust design moves the "exactly one
//! producer, exactly one consumer" contract out of documentation and into
//! the type system: [`channel`] hands back two distinct, non-`Clone`
//! handles, a [`Producer`] and a [`Consumer`].
//!
//! - Design record: `../../docs/adr/0026-rust-spsc-polyglot-component.md`
//! - Requirements and their tests: `REQUIREMENTS.md`
//! - C++ -> Rust mapping and the build story: `README.md`, `BUILDLOG.md`

#![deny(warnings)]
#![deny(clippy::all)]
#![warn(clippy::pedantic)]
// Every `unsafe` block is spelled out explicitly, even inside an
// `unsafe fn`, so the SAFETY reasoning is never implicit.
#![forbid(unsafe_op_in_unsafe_fn)]

use std::mem::MaybeUninit;

#[cfg(loom)]
use loom::cell::UnsafeCell;
#[cfg(loom)]
use loom::sync::atomic::{AtomicUsize, Ordering};
#[cfg(loom)]
use loom::sync::Arc;

#[cfg(not(loom))]
use std::sync::atomic::{AtomicUsize, Ordering};
#[cfg(not(loom))]
use std::sync::Arc;

/// Cell abstraction so the same queue code runs under both builds. Under
/// `--cfg loom` it is loom's model-checked `UnsafeCell`; under a normal build
/// it is a thin wrapper over `std::cell::UnsafeCell` exposing loom's
/// `with` / `with_mut` closure API.
#[cfg(not(loom))]
struct UnsafeCell<T>(std::cell::UnsafeCell<T>);

#[cfg(not(loom))]
impl<T> UnsafeCell<T> {
    fn new(value: T) -> Self {
        Self(std::cell::UnsafeCell::new(value))
    }
    fn with<R>(&self, f: impl FnOnce(*const T) -> R) -> R {
        f(self.0.get())
    }
    fn with_mut<R>(&self, f: impl FnOnce(*mut T) -> R) -> R {
        f(self.0.get())
    }
}

/// A value on its own 64-byte cache line, so two threads writing two
/// different fields never invalidate each other's line (the false-sharing
/// elimination that is `alignas(64)` in the C++ version).
#[repr(align(64))]
struct CachePadded<T>(T);

/// State shared by the producer and the consumer, held behind an `Arc`.
///
/// `head` is consumer-owned (the consumer writes it, the producer only
/// reads it) and `tail` is producer-owned, mirroring the C++ layout.
struct Shared<T, const N: usize> {
    /// The ring buffer. `MaybeUninit` because a slot is only initialised
    /// once the producer has written it; `UnsafeCell` for the shared
    /// interior mutability the two threads need.
    buffer: [UnsafeCell<MaybeUninit<T>>; N],
    /// Consumer-owned read index (free-running, wraps naturally).
    head: CachePadded<AtomicUsize>,
    /// Producer-owned write index (free-running, wraps naturally).
    tail: CachePadded<AtomicUsize>,
}

impl<T, const N: usize> Shared<T, N> {
    /// Compile-time capacity check, mirroring the C++ `static_assert`s.
    /// Referenced from [`channel`] so it is evaluated at monomorphisation.
    const ASSERT_CAPACITY: () = assert!(
        N >= 2 && N.is_power_of_two(),
        "SPSC capacity N must be a power of two and at least 2",
    );

    /// Index-to-slot mask. Capacity is a power of two, so `index & MASK`
    /// replaces a modulo. Usable depth is `N - 1` (one slot reserved to
    /// tell "full" from "empty").
    const MASK: usize = N - 1;
}

// SAFETY: `Shared` holds `UnsafeCell`s, which are normally `!Sync`, but the
// SPSC protocol makes concurrent access data-race-free. The single producer
// only writes slots it owns (indices up to `tail`) and publishes them with a
// Release store to `tail`; the single consumer only reads slots the producer
// has released (observed with an Acquire load of `tail`) and never touches a
// slot the producer still owns. `head` and `tail` are atomics. Requiring
// `T: Send` covers moving each element across the producer/consumer thread
// boundary.
unsafe impl<T: Send, const N: usize> Sync for Shared<T, N> {}

impl<T, const N: usize> Drop for Shared<T, N> {
    fn drop(&mut self) {
        // At drop the `Arc` refcount reached zero, so no other thread holds
        // this state: relaxed loads are enough. Every slot in `[head, tail)`
        // still holds an initialised `T` that must be dropped exactly once.
        let mut head = self.head.0.load(Ordering::Relaxed);
        let tail = self.tail.0.load(Ordering::Relaxed);
        while head != tail {
            // SAFETY: indices in `[head, tail)` were published by the
            // producer and never consumed, so each holds an initialised `T`
            // dropped exactly once here.
            self.buffer[head & Self::MASK].with_mut(|slot| unsafe {
                (*slot).assume_init_drop();
            });
            head = head.wrapping_add(1);
        }
    }
}

/// The producer end. Owns the right to `push`. Not `Clone`, so a second
/// producer cannot be obtained: the single-producer half of the contract is
/// a compile-time guarantee.
pub struct Producer<T, const N: usize> {
    shared: Arc<Shared<T, N>>,
}

/// The consumer end. Owns the right to `pop`. Not `Clone`, so a second
/// consumer cannot be obtained.
pub struct Consumer<T, const N: usize> {
    shared: Arc<Shared<T, N>>,
}

/// Create a bounded SPSC queue of capacity `N` (a power of two, `>= 2`;
/// checked at compile time). Returns the two ends: move the [`Producer`] to
/// one thread and the [`Consumer`] to another.
#[must_use]
pub fn channel<T, const N: usize>() -> (Producer<T, N>, Consumer<T, N>) {
    // Force the compile-time capacity check to be evaluated.
    let () = Shared::<T, N>::ASSERT_CAPACITY;
    let shared = Arc::new(Shared::<T, N> {
        buffer: core::array::from_fn(|_| UnsafeCell::new(MaybeUninit::uninit())),
        head: CachePadded(AtomicUsize::new(0)),
        tail: CachePadded(AtomicUsize::new(0)),
    });
    (
        Producer {
            shared: Arc::clone(&shared),
        },
        Consumer { shared },
    )
}

impl<T, const N: usize> Producer<T, N> {
    /// Enqueue `item`. Never blocks and never allocates.
    ///
    /// # Errors
    /// Returns `Err(item)`, handing the value back, when the queue is full,
    /// so the caller decides whether to drop it or retry.
    pub fn push(&mut self, item: T) -> Result<(), T> {
        let shared = &*self.shared;
        let tail = shared.tail.0.load(Ordering::Relaxed);
        let next = tail.wrapping_add(1);
        // Full when advancing would catch the consumer's head. Acquire so we
        // observe the consumer's most recent slot release.
        if next.wrapping_sub(shared.head.0.load(Ordering::Acquire)) > Shared::<T, N>::MASK {
            return Err(item);
        }
        // SAFETY: this is the only producer; the slot at `tail` is not yet
        // published, so the consumer cannot observe it until the Release
        // store below. Writing an uninitialised slot is correct here.
        shared.buffer[tail & Shared::<T, N>::MASK].with_mut(|slot| unsafe {
            (*slot).write(item);
        });
        // RELEASE: publish the element before the consumer can observe the
        // advanced tail.
        shared.tail.0.store(next, Ordering::Release);
        Ok(())
    }
}

impl<T, const N: usize> Consumer<T, N> {
    /// Dequeue the next item, or `None` when the queue is empty. Never
    /// blocks.
    pub fn pop(&mut self) -> Option<T> {
        let shared = &*self.shared;
        let head = shared.head.0.load(Ordering::Relaxed);
        // ACQUIRE: observe the producer's published tail, and through the
        // release/acquire pair the element it wrote.
        if head == shared.tail.0.load(Ordering::Acquire) {
            return None;
        }
        // SAFETY: `tail` (Acquire) shows this slot was published by the
        // producer's Release store, so it holds an initialised `T` that only
        // this single consumer reads, exactly once.
        let value = shared.buffer[head & Shared::<T, N>::MASK]
            .with(|slot| unsafe { (*slot).assume_init_read() });
        // RELEASE: publish the freed slot to the producer.
        shared.head.0.store(head.wrapping_add(1), Ordering::Release);
        Some(value)
    }
}

#[cfg(all(test, not(loom)))]
mod tests {
    use super::{channel, CachePadded};
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::sync::Arc;

    // [utest->req~spsc-nonblocking~1]
    #[test]
    fn pop_on_empty_returns_none() {
        let (_p, mut c) = channel::<u32, 4>();
        assert_eq!(c.pop(), None);
    }

    // [utest->req~spsc-bounded~1]
    // [utest->req~spsc-nonblocking~1]
    #[test]
    fn fills_to_usable_depth_then_rejects() {
        // Capacity 4 -> usable depth 3 (one slot reserved).
        let (mut p, _c) = channel::<u32, 4>();
        assert!(p.push(1).is_ok());
        assert!(p.push(2).is_ok());
        assert!(p.push(3).is_ok());
        assert_eq!(p.push(4), Err(4));
    }

    // [utest->req~spsc-bounded~1]
    // [utest->req~spsc-contract~1]
    #[test]
    fn preserves_fifo_order() {
        let (mut p, mut c) = channel::<u32, 8>();
        for i in 0..5 {
            assert!(p.push(i).is_ok());
        }
        for i in 0..5 {
            assert_eq!(c.pop(), Some(i));
        }
        assert_eq!(c.pop(), None);
    }

    // [utest->req~spsc-bounded~1]
    #[test]
    fn wraps_around_free_running_indices() {
        // Cycle far past capacity to exercise index wrap and the mask.
        let (mut p, mut c) = channel::<u32, 4>();
        for round in 0..1000 {
            assert!(p.push(round).is_ok());
            assert_eq!(c.pop(), Some(round));
        }
    }

    // [utest->req~spsc-no-false-sharing~1]
    #[test]
    fn head_and_tail_are_cache_line_padded() {
        assert_eq!(std::mem::align_of::<CachePadded<AtomicUsize>>(), 64);
        assert!(std::mem::size_of::<CachePadded<AtomicUsize>>() >= 64);
    }

    struct DropCounter(Arc<AtomicUsize>);
    impl Drop for DropCounter {
        fn drop(&mut self) {
            self.0.fetch_add(1, Ordering::Relaxed);
        }
    }

    // [utest->req~spsc-bounded~1]
    #[test]
    fn drop_releases_buffered_items() {
        let dropped = Arc::new(AtomicUsize::new(0));
        {
            let (mut p, _c) = channel::<DropCounter, 8>();
            assert!(p.push(DropCounter(Arc::clone(&dropped))).is_ok());
            assert!(p.push(DropCounter(Arc::clone(&dropped))).is_ok());
            // Two items left buffered; dropping the ends drops Shared, which
            // must drain and drop them exactly once.
        }
        assert_eq!(dropped.load(Ordering::Relaxed), 2);
    }

    // [utest->req~spsc-ordering~1]
    #[test]
    #[cfg_attr(miri, ignore = "too slow under Miri; concurrency is covered by the loom test")]
    fn producer_and_consumer_across_threads() {
        const COUNT: usize = 100_000;
        let (mut p, mut c) = channel::<usize, 1024>();
        let producer = std::thread::spawn(move || {
            let mut i = 0;
            while i < COUNT {
                if p.push(i).is_ok() {
                    i += 1;
                } else {
                    std::thread::yield_now();
                }
            }
        });
        let mut next = 0;
        while next < COUNT {
            match c.pop() {
                Some(v) => {
                    assert_eq!(v, next);
                    next += 1;
                }
                None => std::thread::yield_now(),
            }
        }
        producer.join().unwrap();
        assert_eq!(next, COUNT);
    }
}

// Loom model test: explores all thread interleavings of a small run. Built
// only under `RUSTFLAGS="--cfg loom"`, where the core uses loom's atomics.
#[cfg(all(test, loom))]
mod loom_tests {
    use super::channel;

    // [utest->req~spsc-ordering~1]
    #[test]
    fn spsc_two_items_all_interleavings() {
        loom::model(|| {
            let (mut tx, mut rx) = channel::<usize, 4>();
            let producer = loom::thread::spawn(move || {
                tx.push(1).unwrap();
                tx.push(2).unwrap();
            });
            // Bounded and non-spinning (loom rejects unbounded spin loops):
            // two pops may race with the producer, two happen after the join.
            let a = rx.pop();
            let b = rx.pop();
            producer.join().unwrap();
            let c = rx.pop();
            let d = rx.pop();
            // Whatever arrived must be exactly 1 then 2, in order, across every
            // interleaving loom explores.
            let got: Vec<usize> = [a, b, c, d].into_iter().flatten().collect();
            assert_eq!(got, vec![1, 2]);
        });
    }
}
