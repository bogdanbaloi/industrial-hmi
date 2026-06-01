# Benchmarks

Microbenchmarks for the hot paths an industrial HMI cares about, run with
[google/benchmark](https://github.com/google/benchmark) (v1.9.0, vendored
via FetchContent). The harness produces statistical summaries
(p50 / p90 / p99 across N repetitions) rather than mean ± stddev, because
a tail-latency excursion that breaches an ISA-18.2 operator-response
window is what causes the incident; the average is not the contract.

## Why these specific paths?

Three targets cover the operator-visible failure modes:

| Target | What it measures | Why |
|---|---|---|
| `bench_alert_center` | `raise()` / `acknowledge()` / `snapshot()` under 10 / 100 / 1000 active alarms | Alarm-storm tail latency: the regulatory budget for "operator saw the safety event" is set in ms, so the codepath that takes the AlertCenter mutex on every producer tick + every UI redraw has to stay deep below it. |
| `bench_modbus_pdu` | `encodeReadRequest` / `decodeReadResponse` at qty=1 / 10 / 125 | Master polling N slaves at 100ms cycles produces 10N codec roundtrips/s. The codec must be small enough that the poll budget is dominated by socket I/O, not by us. |
| `bench_config_parse` | `ConfigManager::initialize()` on a realistic ~30-key config | Cold-start latency story for ADR-0015 (parser swap from hand-rolled to nlohmann/json). Quantifies "what did we pay for correctness?". |

## Building + running

Benchmarks are an explicit opt-in -- baseline builds and CI fast jobs
skip them so configure time stays low.

```bash
cmake -B build/bench -DBUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/bench -j

# Run all
./build/bench/benchmarks/bench_alert_center
./build/bench/benchmarks/bench_modbus_pdu
./build/bench/benchmarks/bench_config_parse

# Run one, JSON output for CI ingestion
./build/bench/benchmarks/bench_alert_center \
    --benchmark_format=json --benchmark_out=alerts.json

# Filter to a specific suite
./build/bench/benchmarks/bench_alert_center \
    --benchmark_filter=Snapshot --benchmark_repetitions=20
```

## Baseline numbers

Captured on an **AMD Ryzen 7 5800X (8C/16T, 3.8GHz base)** under
**WSL Ubuntu 24.04**, **GCC 13.3**, **release mode, no LTO**, idle
machine. Treat the absolute values as an order-of-magnitude check;
the **shape** of each curve (constant vs O(N) vs O(N log N)) is the
contract that should hold across hardware.

### AlertCenter

| Operation | N=10 | N=100 | N=1000 | Scaling |
|---|---:|---:|---:|---|
| `raise()` refresh existing | 93 ns | 356 ns | 2.9 us | O(N) -- dedupe by linear scan |
| `acknowledge()` | 44 ns | 269 ns | 2.7 us | O(N) -- find-by-key linear scan |
| `snapshot()` | 1.0 us | 17 us | 195 us | O(N log N) -- copy + stable_sort by priority |

`raise()` on an empty center (the first-ever alarm) ~12.3 us p50 dominated
by the first vector allocation; once the active set is non-empty the
refresh path is the realistic steady-state cost.

**What this means in practice.** Even at 1000 concurrent active alarms
(well above the ISA-18.2 "manageable" threshold of ~100), the UI-thread
snapshot stays at 195 us p50, which leaves 99.8 % of a 100 ms render
budget for everything else. Producer-side raise / ack are sub-microsecond
up to ~100 alarms, the working range the operator can plausibly act on.

### Modbus TCP PDU codec

| Operation | qty=1 | qty=10 | qty=125 | Scaling |
|---|---:|---:|---:|---|
| `encodeReadRequest` | 14.6 ns | 13.6 ns | 14.3 ns | O(1) -- fixed PDU size |
| `encodeReadRequestChecked` | 14.8 ns | 14.6 ns | 14.7 ns | O(1) + bounds check |
| `decodeReadResponse` | 33 ns | 44 ns | 161 ns | O(qty) -- byte-by-byte payload extract |

**What this means in practice.** The bounds-check on
`encodeReadRequestChecked` costs ~1 ns above the unchecked variant -- a
rounding error vs the ~50us round-trip socket latency. Conclusion in
ModbusBackend.cpp comments: "always use the checked variant on config-
driven entry points". The decode is linear in payload length (no hidden
quadratic behaviour) and even at the spec maximum 125 registers stays at
161 ns -- 8 orders of magnitude below the 250 ms default poll interval.

### ConfigManager parse

| Workload | p50 |
|---|---:|
| Realistic config (~30 leaf keys, 3 nesting levels) | 42.6 us |

**What this means in practice.** The parser swap from the hand-rolled
flat parser to nlohmann/json (ADR-0015) buys correctness on two
known-buggy edge cases (brace-in-string desync, formatter-driven key
reordering) at a one-shot cold-start cost measured in microseconds.
Total bootstrap latency on the reference machine is dominated by GTK
context creation (tens of milliseconds) -- the parse path is in the
noise.

## Re-running on your hardware

Run the suite once to capture YOUR baseline, commit the JSON to your
fork, then run again in CI to detect regressions. The shape contract is
what matters: if `BM_AlertCenter_Snapshot` goes from O(N log N) to
something steeper, an internal invariant changed.

```bash
./build/bench/benchmarks/bench_alert_center \
    --benchmark_format=json --benchmark_out=baseline-alerts.json \
    --benchmark_repetitions=20

# After a change:
./build/bench/benchmarks/bench_alert_center \
    --benchmark_format=json --benchmark_out=after-alerts.json \
    --benchmark_repetitions=20

# Compare with the upstream tool:
python -m pip install -r tools/google_benchmark/requirements.txt
python tools/google_benchmark/compare.py benchmarks \
    baseline-alerts.json after-alerts.json
```

## Why p50 / p90 / p99, not mean

In a control-room context, "what does the average operator see?" is the
wrong question. The right one is "what does the WORST 1 % of operator
interactions look like during a fault cascade?". p99 quantifies that;
mean hides it behind the bulk of the distribution. Google Benchmark
exposes both via its `ComputeStatistics` hook -- our AlertCenter suite
wires the three percentile reducers explicitly so the output table is
operator-meaningful, not just statistically clean.
