/* C ABI for the Rust hmi-spsc lock-free SPSC queue (Phase 2, ADR-0027).
 *
 * Same create / push / pop / destroy shape as the industrial-hmi ONNX dlopen
 * plugin. The producer/consumer split is preserved: hmi_spsc_create hands back
 * two opaque handles, so one thread pushes through the producer handle and one
 * other thread pops through the consumer handle. */
#ifndef HMI_SPSC_H
#define HMI_SPSC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handles; the real layout lives in Rust. */
typedef struct SpscProducer SpscProducer;
typedef struct SpscConsumer SpscConsumer;

/* The payload that crosses the boundary: a plain, C-layout telemetry sample. */
typedef struct Sample {
    uint64_t ts_ms; /* milliseconds since the epoch */
    double value;   /* the measured value */
} Sample;

/* Create a queue; both out-params receive owned handles to free with the
 * matching destroy function. */
void hmi_spsc_create(SpscProducer** out_producer, SpscConsumer** out_consumer);

/* Push one sample. Returns true if stored, false if full. Producer thread only. */
bool hmi_spsc_push(SpscProducer* handle, Sample sample);

/* Pop one sample into *out. Returns true if written, false if empty. Consumer
 * thread only. */
bool hmi_spsc_pop(SpscConsumer* handle, Sample* out);

/* Free the handles from hmi_spsc_create. A null pointer is ignored. */
void hmi_spsc_producer_destroy(SpscProducer* handle);
void hmi_spsc_consumer_destroy(SpscConsumer* handle);

#ifdef __cplusplus
}
#endif

#endif /* HMI_SPSC_H */
