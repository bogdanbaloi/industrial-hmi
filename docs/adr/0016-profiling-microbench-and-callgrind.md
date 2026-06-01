# 0016. Profiling discipline: microbenchmark + whole-program callgrind, no flamegraph SVG in Phase 1

## Status

Accepted (2026-06).

## Context

The portfolio needed an answer to "how fast is this, and how do you
know it's not regressing?" beyond hand-wave. Two distinct questions
were on the table:

1. **Per-primitive cost** -- how fast does `AlertCenter::snapshot()`
   run at 1000 active alarms? How does `decodeReadResponse` scale with
   payload size? These map to operator-budget contracts (UI render
   budget, Modbus poll interval) and need to be measured per-function
   with statistical bounds.
2. **Whole-program distribution** -- across a realistic operator
   session, where is the time actually going? Spots dominant hot paths
   even when no individual function looks slow on its own.

These are not interchangeable. A microbenchmark suite that reports
`snapshot()` at 195us p50 says nothing about whether `snapshot()` is
the bottleneck in production; a whole-program profile that says
`snapshot()` is 0.3% of total instructions says nothing about whether
it would breach an operator budget at scale.

A third option -- **Brendan Gregg-style flame graph SVG** rendered
from `perf` output -- has the strongest visual signal in interview /
recruiter contexts but carries the most tooling friction:

- `perf_event_open(2)` is gated by `kernel.perf_event_paranoid`. WSL2's
  default kernel does not enable the relevant counters; GitHub-hosted
  CI runners typically refuse the syscall too.
- Rendering needs `flamegraph.pl` (Perl) + `stackcollapse-perf.pl`
  (Perl) -- third-party scripts to either vendor or fetch at runtime.
- `gperftools` is an alternative source of folded stacks but requires
  link-time instrumentation and adds a system dep.

We had to pick: one tool, two tools, three? With what tooling
constraints?

## Decision

### Two complementary profiling artefacts, both shipped

**REQ-PERF-001 -- microbenchmarks** via `google/benchmark`. Per-
primitive cost with p50/p90/p99 reducers. Lives under `benchmarks/`,
opt-in behind `BUILD_BENCHMARKS=ON`.

**REQ-PERF-002 -- whole-program callgrind profile** via Valgrind /
callgrind. Captured under a synthetic operator workload; annotated
top-N report committed for cross-release regression diffing. Lives
under `scripts/perf/`, no build-time flag required.

The two are pinned by separate requirements (not consolidated as
"REQ-PERF-001 covering both") because they answer different questions
with different tools, different artefacts, and different CI gates.
Treating them as one would obscure the distinction.

### Callgrind over `perf` for the whole-program path

Reasons (in priority order):

1. **It works where the project already runs.** WSL2 + GitHub-hosted CI
   runners refuse `perf_event_open` by default. callgrind is purely
   userspace Valgrind; it works anywhere Valgrind does, including the
   existing CI memcheck gate.
2. **Deterministic basic-block counting beats sampling for short
   workloads.** A 30-second operator session that bursts for 200 ms
   gets badly characterised by 99 Hz `perf` sampling. callgrind counts
   every basic block executed and produces stable, diffable output.
3. **Reuses existing infrastructure.** Valgrind is already on the CI
   gate (memcheck job). One more invocation is zero new system deps.
4. **Trade-off accepted: ~50x slowdown.** A workload that runs in 2 s
   on metal takes ~100 s under instrumentation. Fine for one-shot
   baseline capture; acceptable for an on-demand CI job; not viable
   for continuous profiling. We don't need continuous profiling.

### No flame graph SVG in Phase 1

The committed artefact is `scripts/perf/baseline.callgrind-annotate.top50.txt`
-- a text report listing the top 50 functions by inclusive cost share.
It is regression-diffable (the `%` column is the contract) and reads
top-to-bottom in a code review without a viewer.

Reasons for deferring the flame graph SVG:

1. **Rendering tools (`gprof2dot`, Graphviz `dot`, `flamegraph.pl`) are
   not on the default development image.** Adding them means either
   apt installs in `scripts/` setup docs (fine but friction), bundling
   scripts as vendored Perl/Python (clutters the repo), or a runtime
   download (CI-fragile).
2. **`callgrind` output -> flamegraph requires a custom converter.**
   The standard `stackcollapse-perf.pl` consumes `perf script` output,
   not callgrind. We would have to write or vendor a callgrind ->
   folded-stack translator.
3. **The diagnostic value is similar.** "Top 10 functions by % share"
   tells the reader the same story a flame graph would, with less
   visual punch but no tooling cost.

Phase 2 is allowed to add the SVG render once a developer has
graphviz + gprof2dot installed; the text baseline does not block.

### What we explicitly chose NOT to do

| Alternative | Why rejected |
|---|---|
| Only microbenchmarks (skip whole-program) | Hides which functions actually dominate budget under real workloads; recruiters get "we have benchmarks" but no story for "where does time actually go" |
| Only whole-program profile (skip microbenchmarks) | Loses the p50/p90/p99 contract per primitive; can't pin a tail-latency budget on a specific function |
| `perf` + flame graph SVG as Phase 1 | Doesn't run on WSL2 or default CI runners; would require sysadmin setup for anyone reproducing |
| `gperftools` (link-time CPU profiler) | Adds a system dep + a link-time flag; output still needs a converter for SVG; signal per setup-cost is worse than callgrind |
| Continuous profiling (e.g. Parca, Pyroscope) | Service architecture for a personal-portfolio HMI; effort-to-signal ratio is wrong |
| Tracy / easy_profiler instrumentation | Source-code instrumentation pollutes every measured TU; loses the "instrument once, profile anywhere" property of sampling/basic-block counting |

## Consequences

### Positive

- **Two artefacts answering two questions** with clean separation of
  contract and tooling.
- **Both run on the existing CI infrastructure.** No new system deps
  (Valgrind is already there; google/benchmark vendored via
  FetchContent).
- **Diffable baselines for both.** Microbenchmark JSON output +
  callgrind annotate text both produce stable rows that regression
  diffs spot trivially.
- **Honest about tooling friction.** The README for each makes the
  trade-offs explicit; readers see why we picked what we picked.

### Negative

- **No glossy flame graph SVG** in the repo until Phase 2. The
  callgrind annotate text is functional but visually duller than a
  flame graph would be.
- **Two REQs to maintain** instead of one consolidated REQ-PERF-X.
  Cost: one extra row in `REQUIREMENTS.md` + `TRACEABILITY.md`.
  Benefit: the distinction is explicit, which matters more than the
  count.

### Neutral

- Phase 2 work (SVG render, CI integration, per-subsystem workloads) is
  enumerated in `scripts/perf/README.md` -- deferred, not lost.

## References

- REQ-PERF-001 (microbenchmark coverage with p50/p90/p99)
- REQ-PERF-002 (whole-program callgrind + regression baseline)
- `benchmarks/README.md` (microbenchmark baseline numbers + scaling
  contract)
- `scripts/perf/README.md` (callgrind methodology + how to read the
  baseline)
- ADR-0014 (rejected Result-everywhere -- same "decided not to do X"
  pattern as the "no flame graph in Phase 1" choice here)
