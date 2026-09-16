//! C ABI (FFI) surface: exposes the SPSC queue to C and C++ callers with the
//! same `create / push / pop / destroy` shape as the industrial-hmi ONNX
//! dlopen plugin. Built into the crate's `cdylib` (a shared library that C can
//! load). See ADR-0027.
//!
//! The Rust producer/consumer split is preserved across the boundary:
//! `hmi_spsc_create` hands back TWO opaque handles, so the single-producer /
//! single-consumer contract still holds when the two ends live on different C
//! threads (each thread touches only its own handle, so there is no aliasing).

use crate::{channel, Consumer, Producer};

/// Fixed capacity for the FFI queue: a power of two, usable depth `N - 1`.
const FFI_CAPACITY: usize = 1024;

/// The payload that crosses the C ABI: a plain, C-layout telemetry sample.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Sample {
    /// Milliseconds since the epoch.
    pub ts_ms: u64,
    /// The measured value.
    pub value: f64,
}

/// Opaque producer handle. One thread pushes through it.
pub struct SpscProducer(Producer<Sample, FFI_CAPACITY>);

/// Opaque consumer handle. One other thread pops through it.
pub struct SpscConsumer(Consumer<Sample, FFI_CAPACITY>);

/// Create a queue and hand back its two ends through the out-parameters. Each
/// is an owned handle the caller must later free with the matching destroy
/// function.
///
/// # Safety
/// `out_producer` and `out_consumer` must be valid, non-null pointers to
/// writable `*mut` slots.
#[no_mangle]
pub unsafe extern "C" fn hmi_spsc_create(
    out_producer: *mut *mut SpscProducer,
    out_consumer: *mut *mut SpscConsumer,
) {
    let (producer, consumer) = channel::<Sample, FFI_CAPACITY>();
    let producer_handle = Box::into_raw(Box::new(SpscProducer(producer)));
    let consumer_handle = Box::into_raw(Box::new(SpscConsumer(consumer)));
    // SAFETY: the caller guarantees both out-params are valid writable pointers.
    unsafe {
        *out_producer = producer_handle;
        *out_consumer = consumer_handle;
    }
}

/// Push one sample. Returns `true` if stored, `false` if the queue was full.
/// PRODUCER THREAD ONLY.
///
/// # Safety
/// `handle` must be a live handle from `hmi_spsc_create`, used by a single
/// producer thread.
#[no_mangle]
pub unsafe extern "C" fn hmi_spsc_push(handle: *mut SpscProducer, sample: Sample) -> bool {
    // SAFETY: the caller guarantees `handle` is a live, exclusively-used producer.
    let producer = unsafe { &mut *handle };
    producer.0.push(sample).is_ok()
}

/// Pop one sample into `out`. Returns `true` if one was written, `false` if the
/// queue was empty. CONSUMER THREAD ONLY.
///
/// # Safety
/// `handle` must be a live handle from `hmi_spsc_create` used by a single
/// consumer thread, and `out` must be a valid writable pointer.
#[no_mangle]
pub unsafe extern "C" fn hmi_spsc_pop(handle: *mut SpscConsumer, out: *mut Sample) -> bool {
    // SAFETY: the caller guarantees `handle` is a live, exclusively-used consumer.
    let consumer = unsafe { &mut *handle };
    match consumer.0.pop() {
        Some(sample) => {
            // SAFETY: the caller guarantees `out` is a valid writable pointer.
            unsafe { *out = sample };
            true
        }
        None => false,
    }
}

/// Free a producer handle returned by `hmi_spsc_create`.
///
/// # Safety
/// `handle` must come from `hmi_spsc_create` and must not be used afterwards.
/// A null pointer is ignored.
#[no_mangle]
pub unsafe extern "C" fn hmi_spsc_producer_destroy(handle: *mut SpscProducer) {
    if !handle.is_null() {
        // SAFETY: the caller guarantees `handle` came from create and is now unused.
        drop(unsafe { Box::from_raw(handle) });
    }
}

/// Free a consumer handle returned by `hmi_spsc_create`.
///
/// # Safety
/// `handle` must come from `hmi_spsc_create` and must not be used afterwards.
/// A null pointer is ignored.
#[no_mangle]
pub unsafe extern "C" fn hmi_spsc_consumer_destroy(handle: *mut SpscConsumer) {
    if !handle.is_null() {
        // SAFETY: the caller guarantees `handle` came from create and is now unused.
        drop(unsafe { Box::from_raw(handle) });
    }
}

#[cfg(all(test, not(loom)))]
mod tests {
    use super::{
        hmi_spsc_consumer_destroy, hmi_spsc_create, hmi_spsc_pop, hmi_spsc_producer_destroy,
        hmi_spsc_push, Sample, SpscConsumer, SpscProducer,
    };

    // [utest->req~spsc-ffi~1]
    #[test]
    fn ffi_roundtrip_single_thread() {
        let mut producer: *mut SpscProducer = std::ptr::null_mut();
        let mut consumer: *mut SpscConsumer = std::ptr::null_mut();
        // SAFETY: both out-params are valid writable pointers.
        unsafe { hmi_spsc_create(&raw mut producer, &raw mut consumer) };

        let pushed = Sample {
            ts_ms: 42,
            value: 3.5,
        };
        // SAFETY: `producer` is a live handle just created.
        assert!(unsafe { hmi_spsc_push(producer, pushed) });

        let mut got = Sample {
            ts_ms: 0,
            value: 0.0,
        };
        // SAFETY: `consumer` is live and `got` is a valid writable pointer.
        assert!(unsafe { hmi_spsc_pop(consumer, &raw mut got) });
        assert_eq!(got.ts_ms, 42);
        assert!((got.value - 3.5).abs() < f64::EPSILON);

        // Empty now.
        // SAFETY: same live handle and valid out pointer.
        assert!(!unsafe { hmi_spsc_pop(consumer, &raw mut got) });

        // SAFETY: both handles came from create and are used here for the last time.
        unsafe {
            hmi_spsc_producer_destroy(producer);
            hmi_spsc_consumer_destroy(consumer);
        }
    }
}
