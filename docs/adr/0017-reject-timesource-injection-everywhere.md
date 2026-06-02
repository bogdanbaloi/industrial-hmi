# 0017. Reject TimeSource injection everywhere

## Status

Accepted (2026-06-02).

## Context

The codebase already injects a wall-clock function (`NowFn`) at two
specific seams where deterministic time control buys real testing
value:

- `AlertCenter::NowFn` — Phase 2 alarms (REQ-ALARM-002) use it so
  `tick()` auto-unshelve cases can be driven from a `FakeClock` in
  the test fixture without sleeping. The shelve deadlines are also
  surfaced via `shelvedSnapshot()` (Phase 4a, REQ-ALARM-005), where
  the precomputed `secondsRemaining` countdown is computed from the
  injected clock.
- `ThroughputMeter::NowFn` — the per-line throughput KPI
  (REQ-DASHBOARD-007) computes a rolling u/h from completion
  timestamps; the injected clock lets the test drive a "decays on
  stall" assertion against deterministic timestamps.

Both seams are header-only, take `std::function<TimePoint()>` by
value, default to `Clock::now`, and have a one-paragraph rationale
in their respective files.

A speculative Tier-C item in `.claude/ROADMAP.md` proposed
"TimeSource injection across the codebase" — that is, extending the
same pattern to every wall-clock reader. The natural next consumers
are:

- `HistorianMaintenance` — sleeps on a `condition_variable_any`
  keyed to a `std::stop_token`; the sweep cadence is read from the
  process clock when computing "older than X ms" cutoffs.
- `SqliteAuditLogger` / `AuditEvent` — timestamps every row with
  `std::chrono::system_clock::now()` at the write site.
- `Logger` / `LoggerImpl` — every line gets a `HH:MM:SS.mmm`
  timestamp from the system clock at format time.
- Any other reader of `std::chrono::system_clock::now()` /
  `std::chrono::steady_clock::now()` in `src/`.

The promise: "deterministic tests without sleeps, at scale" — same
signal that `AlertCenter` already buys, applied everywhere.

The cost: every reader of `now()` grows a constructor parameter, a
member, a default, and a paragraph of rationale. Every test fixture
that touches one grows a clock-injection helper. Producers /
composition roots wire one more thing.

A roadmap item said this was Tier-C, "L (~6-8 sessions)". An
in-session attempt at the first consumer (`HistorianMaintenance`)
made the trade-off concrete enough to decide explicitly rather than
defer.

## Decision

**Do not extend TimeSource injection to the remaining wall-clock
readers (`HistorianMaintenance`, `SqliteAuditLogger`, `Logger`,
ad-infinitum).** Keep the seam at `AlertCenter` and `ThroughputMeter`
exactly where it is today, and stop there.

### Why

1. **The diagnostic signal does not pay back the surface cost.**
   The existing seams catch a specific class of bug: state machines
   whose behaviour depends on a deadline crossing during a test
   step (`tick()` auto-unshelve, throughput decay-on-stall). The
   remaining readers (`HistorianMaintenance` sweep cutoff, log
   timestamps, audit-row timestamps) do not have that shape — their
   behaviour is "stamp the current time on a row" or "sleep until
   X". Neither benefits from a fake clock: the test margins are
   already huge (`HistorianMaintenance` thresholds are in hours),
   and the rows / lines under test do not gate any state machine
   the assertions check.

2. **The existing safety net catches the bugs we would otherwise be
   driving with a fake clock.** Sanitizer-instrumented CI runs
   (`asan-ubsan`, `tsan`), integration tests that exercise the
   historian + logger + audit pipeline against the real clock, and
   the layout-budget regression guard collectively cover the
   "actually-broken" cases. Replacing `Clock::now()` with
   `nowFn_()` does not catch a single bug those gates miss.

3. **The cost of consistency is paid every PR forever.** Every new
   `now()` reader has to either (a) take a `NowFn` member it does
   not need or (b) be policed in code review. The "policed in
   review" path is exactly the kind of discipline that decays
   silently. The "everyone takes a NowFn" path forces a paragraph
   of rationale per consumer for a benefit the tests never claim.

4. **Senior signal is in the rejection, not the implementation.**
   This decision pairs with ADR-0014 (rejected
   Result-everywhere + retry + circuit-breaker) and ADR-0016
   (rejected `perf` + flame graph SVG as Phase 1 profiling). All
   three follow the same pattern: a textbook-attractive
   generalisation that the project deliberately stops short of
   because the marginal cost grows linearly with code surface
   while the marginal benefit grows sublinearly. **The portfolio
   signal is "we know when not to extend a pattern", which a
   senior reviewer reads instantly.**

### Where the seam STAYS (do not regress these)

`AlertCenter` and `ThroughputMeter` keep their injected clocks.
This ADR is "do not extend the pattern", not "remove the existing
two seams". Both have concrete tests that depend on the injection
today (e.g. `FakeClock` in `AlertCenterTest`,
`ThroughputMeterTest`'s decay assertions).

### Reconsideration triggers

This ADR is not a permanent ban. The decision should be revisited
if EITHER of the following happens:

- A real bug ships in `HistorianMaintenance` / `SqliteAuditLogger` /
  `Logger` that a deterministic clock would have caught earlier.
  Note: "would have caught" requires demonstrating the bug ALONG
  the test surface a fake-clock test would have exercised, not just
  "fake clocks are nice in general".
- A `Needs:` clause in `REQUIREMENTS.md` requires deterministic
  time on a path that currently lacks it. That is, the requirement
  carries the decision, not the desire for symmetry.

In either case, extend the seam to that consumer only. Do not turn
the trigger into a global migration.

## Consequences

### Positive

- One ADR-fodder Tier-C item removed from the backlog cleanly,
  with the reasoning preserved instead of just deleted.
- Future code review has a citation to pin a "let's add a TimeSource
  here" suggestion against: ADR-0017, second-rejection rule.
- The `AlertCenter` and `ThroughputMeter` injections become
  load-bearing in story: they are the cases where the cost is
  justified. New consumers must clear the same bar.

### Negative

- Tests for `HistorianMaintenance` / `SqliteAuditLogger` / `Logger`
  continue to live with the real clock. They have to use safe
  margins (which they already do) and accept the rare flaky
  failure on a CPU-starved CI runner (which has not, to date,
  actually happened).
- A future maintainer who reads only `AlertCenter` may copy the
  pattern into a new consumer without reading this ADR. Mitigated
  by linking ADR-0017 from the relevant Tier-D row in `ROADMAP.md`
  + cross-reference from `AlertCenter.h::NowFn` docstring.

### Neutral

- No code changes ship with this ADR. The rejection is the
  artefact.

## Alternatives considered

| Alternative | Why rejected |
|---|---|
| Extend TimeSource everywhere | Marginal benefit per consumer is small; total surface cost grows linearly; tests would have to grow `FakeClock` plumbing for paths that do not test time-dependent behaviour |
| Extend only to `HistorianMaintenance` | Would establish "more is coming" momentum without a triggering bug; setup is the same as full extension minus one consumer, signal benefit minus most of the cost |
| Build a global `TimeProvider` singleton injected into a Service Locator | Hides the dependency, makes call-site analysis harder, defeats the whole purpose of seeing "this code reads the clock" at the constructor signature |
| Replace `std::chrono::system_clock::now` calls with a process-global function pointer | Same hiding problem + introduces a writable global mutable state for tests, the exact thing observer-pattern + DI is designed to avoid |

## References

- ADR-0014 (`Result<T, E>` at boundaries, not everywhere) — same
  "don't extend everywhere" pattern; the original of the three.
- ADR-0016 (Profiling discipline: microbench + callgrind, no
  flamegraph Phase 1) — also a "rejected design" ADR.
- REQ-ALARM-002 (Phase 2: shelve + auto-expiry on tick) — the
  REQ that justifies `AlertCenter::NowFn`.
- REQ-DASHBOARD-007 (live throughput KPI with decay-on-stall) —
  the REQ that justifies `ThroughputMeter::NowFn`.
- `.claude/ROADMAP.md` Tier-D — this ADR's matching row was moved
  from Tier-C to Tier-D as part of the rejection.
