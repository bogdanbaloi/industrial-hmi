# Fuzz harnesses

Continuous-coverage fuzzing of the wire parsers that turn bytes off a
socket into application state. The property under test is identical
across all three harnesses:

> A misbehaving peer sending arbitrary bytes must not crash the HMI,
> leak memory, or trip undefined behaviour. The parser may return any
> error code; what it may never do is corrupt the host process.

This pairs with the existing sanitizer / clang-tidy gates: those run
on a small, hand-chosen input set; libFuzzer drives orders of magnitude
more inputs per second through the same sanitizer instrumentation.

## Targets

| Binary | Parser under fuzz | Seed corpus |
|---|---|---|
| `fuzz_modbus_decode` | `app::integration::modbus::decodeReadResponse` | `corpus/modbus_decode/` (3 valid frames, incl. an exception response) |
| `fuzz_mqtt_publish` | `app::integration::mqtt::parsePublish` | `corpus/mqtt_publish/` (2 valid PUBLISH packets) |
| `fuzz_mqtt_remaining_length` | `app::integration::mqtt::decodeRemainingLength` | `corpus/mqtt_remaining_length/` (1/2/4-byte variable-length encodings) |

Each binary is its own libFuzzer driver. Corpora are isolated per
target so a finding in one parser cannot pollute the seeds for
another.

## Building

Requires Clang (libFuzzer is Clang-only). Default OFF.

```bash
CC=clang CXX=clang++ \
    cmake -B build/fuzz \
        -DBUILD_FUZZERS=ON \
        -DBUILD_TESTS=OFF \
        -DCMAKE_BUILD_TYPE=Debug
cmake --build build/fuzz -j
```

CMake auto-skips with a `STATUS` message when the compiler is not
Clang -- leaving `BUILD_FUZZERS=ON` in a preset is safe for GCC
developers.

## Running

```bash
# Local: a 60-second smoke run on the seed corpus, with sanitizers on.
./build/fuzz/fuzzers/fuzz_modbus_decode \
    -max_total_time=60 \
    fuzzers/corpus/modbus_decode

./build/fuzz/fuzzers/fuzz_mqtt_publish \
    -max_total_time=60 \
    fuzzers/corpus/mqtt_publish

./build/fuzz/fuzzers/fuzz_mqtt_remaining_length \
    -max_total_time=60 \
    fuzzers/corpus/mqtt_remaining_length

# CI-friendly: shorter time, JSON-ish line format already on by default.
./build/fuzz/fuzzers/fuzz_modbus_decode \
    -max_total_time=30 -runs=2000000 \
    fuzzers/corpus/modbus_decode
```

A finding writes a `crash-<sha1>` file to the working directory; the
file is the minimal input that triggers the bug. Replay it with the
same binary to confirm:

```bash
./build/fuzz/fuzzers/fuzz_modbus_decode crash-<sha1>
```

## Smoke-run baseline (Ryzen 7 5800X, WSL Ubuntu 24.04, Clang 18.1)

After a 4-second smoke run on the seed corpus, with ASan + UBSan +
libFuzzer coverage on:

| Target | exec/s | Coverage features |
|---|---:|---:|
| `fuzz_modbus_decode` | ~880k | 65 |
| `fuzz_mqtt_publish` | ~27k | 735 (vector-copy + exception path is heavier) |
| `fuzz_mqtt_remaining_length` | ~65k | 41 |

Zero crashes in the smoke run; the targets are ready for long-running
campaigns. The exec/s difference between Modbus and MQTT reflects the
parsers' actual cost shape (PDU is fixed-layout, PUBLISH carries
variable-length string + payload + try/catch). Treat the numbers as
order-of-magnitude; the contract is "no crashes", not the throughput.

## Why libFuzzer (and not AFL++ / honggfuzz)

- **In-process driver**: no fork-server per iteration; runs at full
  instrumentation speed. AFL++'s forkserver buys persistence at a
  per-iter cost the parsers above don't justify.
- **Same toolchain as the sanitizer gates**: ASan / UBSan are already
  built and gated in CI under Clang; layering libFuzzer on top reuses
  the instrumentation without dragging a second fuzzer's runtime.
- **Tiny harness**: 30 LOC per target with the standard
  `LLVMFuzzerTestOneInput` entry. No custom glue.
- **No orchestrator**: the binary is the fuzzer; CI just compiles and
  runs.

The trade-off: libFuzzer's mutation engine is good but not the strongest
in the field. For protocols where the input is structured (MQTT PUBLISH
with length prefixes, Modbus MBAP with checksum-style framing) a
grammar-aware fuzzer (e.g. honggfuzz with a grammar definition) would
explore deeper paths. We accept the trade-off because the current
parsers' surface is small enough that brute-force coverage already
exercises the relevant branches.

## What we are NOT fuzzing (and why)

- **JSON parsing** -- delegated to `nlohmann/json` (ADR-0015), which is
  fuzzed continuously upstream via OSS-Fuzz. Adding a thin wrapper
  fuzzer here would mostly re-discover bugs upstream catches first.
- **Modbus encode** -- the encoder produces bytes from typed inputs;
  it is not exposed to adversarial bytes. Its surface is already
  covered by `bench_modbus_pdu` (perf) + `ModbusPduTest` (correctness).
- **OPC-UA stack** -- ships as `open62541` (ADR-0009); the upstream
  project runs its own fuzzing infra. We don't shadow it.
- **Algorithms** (sort, hash, dedupe) -- these don't consume untrusted
  bytes from the network; their defects are caught by unit tests +
  sanitizer-instrumented runs of the integration tests.

The principle: fuzz the edges that meet adversarial inputs, not the
internals.

## Adding a new fuzz target

1. New file: `fuzzers/fuzz_<name>.cpp` with
   `LLVMFuzzerTestOneInput(const uint8_t*, size_t) -> int`.
2. Source list in `fuzzers/CMakeLists.txt`: `add_fuzz_target(<name>
   SOURCES fuzz_<name>.cpp <parser>.cpp)`. Direct compilation (no
   linking to objectsXxx) keeps the sanitizer surface small.
3. Seed corpus in `fuzzers/corpus/<name>/` -- 2-3 valid samples are
   enough to bootstrap coverage; libFuzzer finds the rest.
4. Update this README's target table.
5. If CI gains a fuzz job, add the new binary to its matrix.
