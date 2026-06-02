# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Config hot reload Phase 1 (REQ-CORE-006)

Closes the config story arc started by ADR-0015 (nlohmann parser +
ConfigValidator + JSON Schema spec). Operators can now edit
`config/app-config.json` and trigger a reload without restarting the
binary, with the same atomic-or-nothing semantics fail-fast startup
already enforces.

#### Added

- `ConfigManager::reload()` -- re-reads the configured `configPath`,
  parses with `nlohmann::json::parse`, flattens into a TEMPORARY map,
  swaps in atomically, runs `ConfigValidator::validate()`, and rolls
  back to the previous state on any failure (file missing, parse
  error, semantic-validation rejection). Returns `bool`; logs a
  warning through the injected logger on each rejection path.
- ConfigManagerTest gains 5 new reload cases: success-after-edit
  (language changed `it` -> `de`), missing-file rejection,
  truncated-JSON rejection, validator rejection on unknown language
  code, no-init sentinel (reload before initialize is a no-op
  returning `false`).
- REQ-CORE-006 in `REQUIREMENTS.md` + `TRACEABILITY.md` row,
  referencing ADR-0015 (same validation gate that vets the boot-time
  config now vets every reload).

#### Scope -- Phase 1 deliberately does NOT add

- A file-system watcher. Consumers decide WHEN to call `reload()`:
  Settings page button, SIGHUP, timer, OPC-UA method. Cadence and
  trigger policy stay out of ConfigManager.
- A change-notification callback. Consumers diff before/after or
  re-poll on their existing tick path.
- Re-application of derived state (i18n re-bind, logger
  re-configure, theme re-apply). Those belong to the call site, not
  to ConfigManager.

Scope = the atomic data swap with the validation gate.

### Callgrind profiling Phase 2 -- CI workflow + diff helper + alarm-storm baseline (REQ-PERF-002, ADR-0016)

Phase 1 (already on main) shipped the capture script + workload-baseline
+ committed top-50 reference. Phase 2 wires it into CI so every
opt-in PR produces fresh artefacts plus an in-summary diff against
the committed baseline. Adds a second alarm-focused workload + its
baseline. Phase 3 (SVG render, PR-comment integration, per-backend
workloads) remains deferred.

#### Added

- `.github/workflows/profiling.yml` -- new CI job triggered on
  `workflow_dispatch` and on PRs labelled `run-profiling`. Configures
  release, builds `industrial-hmi-console`, runs
  `capture-callgrind.sh` on each workload, uploads top-50 + raw
  artefacts (30 / 7 day retention), and appends the markdown diff
  table to the GitHub job summary so reviewers see it without
  downloading anything.
- `scripts/perf/diff-baseline.sh` -- compares two callgrind annotate
  files and emits a markdown table of functions whose `%` column
  moved by >= `$THRESHOLD` (default 1.0). Stable runs return a
  one-liner so the workflow can skip the table.
- `scripts/perf/workload-alarm-storm.txt` -- 64-command stdin
  workload that stresses the AlertCenter lifecycle: 30 iterations of
  equipment-off -> `alerts` snapshot -> `dismiss` (acknowledge) ->
  equipment-on across 3 lines. Pairs with the broad workload-baseline.
- `scripts/perf/baseline-alarm-storm.callgrind-annotate.top50.txt` --
  reference report for the new workload (Ryzen 7 5800X / WSL Ubuntu
  24.04 / GCC 13.3, ~11.3M Ir total).

#### Changed

- `scripts/perf/baseline.callgrind-annotate.top50.txt` -- regenerated
  with the cleaner 4-line header + 50-line body format the diff
  script + CI consume. The Phase 1 file had a duplicate-rows bug
  (head -30 + grep | head -60 overlapped on the top-5 entries); the
  rebuild is mechanical and changes no contract beyond removing
  duplicate rows.
- `scripts/perf/README.md` -- documents the new Phase 2 artefacts +
  the deferred Phase 3 backlog.

### ISA-18.2 alarms Phase 4b -- AlertsPanel Shelved UI (REQ-ALARM-005)

UI half of Phase 4. Phase 4a (just merged) shipped the model+presenter
API: `shelvedSnapshot()` + `ShelvedView` with `secondsRemaining`
countdown. Phase 4b consumes it in the GTK panel so the operator sees
shelved alarms as a distinct subsection ordered by most-imminent
expiry, with a per-row countdown widget.

#### Added

- `AlertsPanel::renderActiveAndShelved()` -- replaces the previous
  `renderActive()` path. Renders TWO subsections in the Active view:
  active alarms by priority on top, Shelved inventory by deadline
  ascending underneath. The Shelved header is suppressed when empty
  so an operator with active-only alarms sees the same layout as
  before Phase 4b.
- `AlertsPanel::appendShelvedHeader(count)` -- "SHELVED (n)" divider
  with a tooltip explaining auto-unshelve semantics.
- `AlertsPanel::buildShelvedCard(ShelvedView)` -- card layout mirrors
  the active card but the timestamp slot is the countdown, and the
  action row offers Unshelve only (Acknowledge is not meaningful
  while shelved -- operator unshelves first).
- `AlertsPanel::formatCountdown(seconds)` -- public static helper,
  unit-testable without GTK. Renders MM:SS for any positive duration
  (zero-padded seconds) and "EXPIRED" for zero / negative (defensive).
- `AlertsPanelTest` -- 7 cases pinning the formatter contract: zero
  -> EXPIRED, negative -> EXPIRED, pad rule under one minute, rollover
  to minutes, default 5-min shelve case, long-shelve stays MM:SS,
  secs<10 zero-pad rule.

#### Changed

- `AlertCenter::snapshot()` now FILTERS Shelved entries (lifting the
  Phase 4a compatibility note). The operator-facing inventory split
  is the contract: `snapshot()` = active list, `shelvedSnapshot()` =
  shelved list. The `stateOf` test helper in AlertCenterTest checks
  both lists so existing assertions on shelved-alarm state hold.

#### Compatibility

- All 84 ctest targets pass post-change (+1 vs Phase 4a baseline of
  83 for the new AlertsPanelTest binary).
- No producer-side change. Producers (presenter, integration bridges)
  raise / clear / shelve through the existing AlertCenter API.

### ISA-18.2 alarms Phase 4a -- operator-visible Shelved inventory (REQ-ALARM-005)

Additive read API on `AlertCenter` so the panel can render shelved
alarms as a distinct subsection ordered by most-imminent expiry,
instead of folding them into the active list with a SHELVED badge
(the Phase 1-3 behaviour). Phase 4b will rewire the GTK AlertsPanel
UI to consume both lists; Phase 4a only ships the model+presenter
contract + tests.

#### Added

- `AlertCenter::ShelvedView` struct -- pairs the alarm view model
  with the absolute wall-clock `deadline` AND a precomputed
  `secondsRemaining` countdown. The panel renders "4:37 left"
  directly from `secondsRemaining` without owning a clock; tests
  that inject a fake clock assert on either field without knowing
  AlertCenter's `NowFn`.
- `AlertCenter::shelvedSnapshot()` -- thread-safe snapshot of the
  currently-shelved alarms, sorted by deadline ascending
  (most-imminent first). Filters out any non-Shelved entries.
- `secondsRemaining` clamps to zero for entries past their deadline
  but not yet swept by `tick()` -- the panel renders "EXPIRED"
  without juggling negative durations.
- 7 new AlertCenterTest cases covering: empty inventory, filtered
  inclusion (only Shelved), deadline-ascending order across mixed
  shelve durations, countdown tracks injected clock, clamp-to-zero
  past deadline, swept after tick, deadline matches the shelve call.
- REQ-ALARM-005 in `REQUIREMENTS.md` + `TRACEABILITY.md` row.

#### Compatibility

- `snapshot()` is **unchanged** -- still includes Shelved entries so
  the existing AlertsPanel UI keeps working without modification.
  Phase 4b will migrate the UI to render two lists from the two
  snapshots and stop including Shelved in `snapshot()`.

### Whole-program callgrind profile + regression baseline (REQ-PERF-002)

Phase 1 of the profiling discipline: capture pipeline + workload +
committed top-N reference report. Pairs with the per-primitive
microbenchmarks under `benchmarks/` -- benchmarks pin the budget per
function, the callgrind report shows which functions dominate the
budget under a real session.

#### Added

- `scripts/perf/capture-callgrind.sh` -- runs Valgrind / callgrind on
  the headless console binary with a stdin workload, writes raw
  `callgrind.out` + `callgrind_annotate` report to
  `scripts/perf/output/` (gitignored).
- `scripts/perf/workload-baseline.txt` -- synthetic operator session
  driving distinct hot paths: production lifecycle, equipment toggles,
  alarm snapshots, product reads, status loop.
- `scripts/perf/baseline.callgrind-annotate.top50.txt` -- trimmed
  top-50 inclusive-cost reference report from the Ryzen 7 5800X / WSL
  Ubuntu 24.04 / GCC 13 capture. The contract diffed across releases
  is the `%` column on the top-N rows, NOT the absolute Ir counts.
- `scripts/perf/README.md` -- why callgrind not `perf` (kernel access
  on WSL2 + CI runners, sampling vs deterministic, reuses the existing
  Valgrind gate), how to read the baseline (which categories live in
  the top 10 + what climbing into the top would mean for our code),
  Phase 2 backlog (gprof2dot SVG, CI integration, per-subsystem
  workloads).
- REQ-PERF-002 in `REQUIREMENTS.md` + `TRACEABILITY.md` row.

#### Initial baseline observations (Ryzen 7 5800X reference)

- 10.7 M instructions total under the baseline workload.
- ~30% dynamic linker symbol resolution (`do_lookup_x`,
  `_dl_relocate_object`) -- one-shot startup cost, amortises with
  workload length.
- ~5% glibc malloc / free traffic.
- ~3% nlohmann JSON lexer (config parse during bootstrap; matches
  `bench_config_parse` baseline of ~42 us p50).
- ~1.5% `std::vformat` (logger format-string lowering).
- No function under our `src/` tree above 5% inclusive -- a regression
  signal if that ever flips.

### Fuzz the wire parsers (REQ-INTEGRATION-006)

Continuous-coverage fuzzing of the parsers that turn untrusted bytes off
a socket into application state. Pairs with the existing ASan / UBSan /
TSan / clang-tidy gates -- those run on hand-chosen inputs, libFuzzer
drives orders of magnitude more inputs through the same sanitizer
instrumentation.

The property under test, identical across all targets:
> A misbehaving peer sending arbitrary bytes must not crash the HMI,
> leak memory, or trip undefined behaviour. Any error code is
> acceptable; corrupting the host process is not.

#### Added

- `fuzzers/fuzz_modbus_decode.cpp` -- Modbus MBAP/PDU decoder
  (`decodeReadResponse`). Synthesises `ResponseContext` from the first
  5 input bytes so the fuzzer drives the TransactionIdMismatch /
  UnitIdMismatch / FunctionCodeMismatch branches without scripting a
  state machine.
- `fuzzers/fuzz_mqtt_publish.cpp` -- MQTT 3.1.1 PUBLISH parser
  (`parsePublish`). Inbound subscriber path; the harness catches and
  ignores std exceptions because the production caller already does --
  the bug we're hunting is a SILENT memory defect, not a thrown one.
- `fuzzers/fuzz_mqtt_remaining_length.cpp` -- variable-byte length
  decoder. Smallest-surface, highest-frequency parser in the codebase
  (every MQTT packet hits it first), so it gets its own harness.
- `fuzzers/corpus/<target>/` -- minimal seed corpora (2-3 valid frames
  per target, including an exception-response Modbus PDU).
- `fuzzers/README.md` with running protocol, smoke-run baseline numbers
  (Ryzen 7 5800X, WSL Ubuntu 24.04, Clang 18.1: ~880k exec/s on Modbus,
  ~27k on MQTT PUBLISH, ~65k on MQTT remaining-length), libFuzzer vs
  AFL++ trade-off rationale, and what we explicitly do NOT fuzz (JSON
  parse, OPC-UA stack, encoders, internal algorithms).
- REQ-INTEGRATION-006 in `REQUIREMENTS.md` + `TRACEABILITY.md` row.

#### Build system

- `BUILD_FUZZERS` CMake option (default OFF). Auto-skips with a STATUS
  message on non-Clang toolchains so the flag can stay ON in a preset
  for mixed-compiler dev teams.
- Sanitizer + libFuzzer flags applied per-target
  (`-fsanitize=fuzzer,address,undefined`) so the rest of the build tree
  is untouched when BUILD_FUZZERS is the only opt-in.

#### Smoke-run findings

Zero crashes in a 4-second per-target smoke run on the seed corpora
under ASan + UBSan + libFuzzer coverage. Targets are ready for
long-running campaigns; CI integration tracked separately.

### Reproducible microbenchmarks on hot paths (REQ-PERF-001)

Adds google/benchmark v1.9.0 (vendored via FetchContent, default OFF behind
`BUILD_BENCHMARKS=ON`) and three benchmark binaries that turn the
portfolio's perf claims from hand-wave into numbers a recruiter can run.

#### Added

- `benchmarks/bench_alert_center.cpp` -- AlertCenter `raise()` /
  `acknowledge()` / `snapshot()` at N=10/100/1000 active alarms. Reports
  p50 / p90 / p99 via custom `ComputeStatistics` reducers; tail latency
  is the operator-budget contract, not the mean.
- `benchmarks/bench_modbus_pdu.cpp` -- `encodeReadRequest` (unchecked +
  checked) and `decodeReadResponse` at qty=1/10/125. Quantifies the
  `~1ns` bounds-check overhead recommended on config-driven entry points.
- `benchmarks/bench_config_parse.cpp` -- cold-start
  `ConfigManager::initialize()` on a realistic ~30-key config. Numbers
  for the ADR-0015 parser-swap accounting.
- `benchmarks/README.md` with baseline p50 numbers captured on
  AMD Ryzen 7 5800X / WSL Ubuntu 24.04 / GCC 13.3, the scaling contract
  per operation (O(1) / O(N) / O(N log N)), and instructions for
  capturing a per-machine baseline JSON for regression comparison.
- REQ-PERF-001 in `REQUIREMENTS.md` + new `perf` category +
  `TRACEABILITY.md` row.

#### Notes on the captured numbers (Ryzen 7 5800X reference)

- `AlertCenter::snapshot()` at N=1000 active alarms: ~195us p50, leaving
  >99% of a 100ms render budget.
- `AlertCenter::raise()` refresh, N=1000: ~2.9us; O(N) linear scan on
  key dedupe.
- `encodeReadRequestChecked` vs unchecked: +1ns (rounding error vs
  ~50us socket RTT). Conclusion: always use the checked variant on
  config-driven entry points.
- `decodeReadResponse` at qty=125 (spec max): 161ns, 8 orders below
  the 250ms default poll interval.
- `ConfigManager::initialize` on realistic config: 42us p50 -- in the
  noise vs ~tens-of-ms GTK context creation during bootstrap.

### Config JSON schema + validator (REQ-CORE-005, ADR-0015)

Replaces the hand-rolled flat-key JSON parser in `ConfigManager` with
`nlohmann/json` (v3.11.3, vendored via FetchContent), and adds a
semantic-validation pass between "config parsed" and "the rest of
startup uses it". The public ConfigManager accessor API is unchanged
-- every existing call site compiles and runs without modification.

#### Added

- `nlohmann/json` v3.11.3 via FetchContent at
  `_deps/json-vendor/include/`, SYSTEM include so clang-tidy ignores
  the ~25k-line single header. Mirrors the Boost.SML vendor-routing
  pattern from ADR-0009.
- `src/config/ConfigValidator.h/.cpp` -- runtime semantic check on
  the parsed ConfigManager. Rules cover log-level / language-code
  enums, positive-only durations / counts / sizes, integer port
  ranges (1..65535) gated on the owning backend being enabled, and
  cross-field invariants. Returns every violation in one pass so the
  operator sees all problems in a single round-trip.
- `schemas/app-config.schema.json` -- JSON Schema draft-07 spec
  documenting the same contract for IDE / tooling consumers.
- `StartupErrorCode::ConfigInvalid` + `ConfigInvalidError` -- distinct
  from `ConfigCorruptError` so the failure dialog can phrase
  "bad values" (edit the file) instead of "broken file" (reinstall).
- Bootstrap stage 2.5 runs the validator after `ConfigManager::initialize`
  and raises `ConfigInvalidError` with every violation joined.
- ConfigValidatorTest (9 cases): minimal-valid config accepted, unknown
  log level rejected, unknown language code rejected, "auto" language
  accepted, non-positive window dimensions rejected (both collected),
  out-of-range TCP port rejected when backend enabled, same port
  ignored when backend disabled, historian batch_size=0 rejected when
  enabled, multi-violation collection.
- ADR-0015 documents the two-pronged decision: nlohmann for parsing,
  C++ validator + JSON Schema as the audited spec.

#### Changed

- `ConfigManager` becomes `.h` + `.cpp` (was header-only). The heavy
  `nlohmann/json.hpp` include is confined to `ConfigManager.cpp` via
  PIMPL -- the rest of the codebase pays zero compile-time cost for
  the parser swap.
- `objectsConfig` is now a STATIC library (was INTERFACE). STATIC
  rather than OBJECT so symbols propagate transitively through the
  `objectsCore -> objectsConfig` consumer chain without per-target
  `target_link_libraries` plumbing.
- README / CHANGELOG number reconciliation folded into this entry:
  79 ctest targets (was 76), 60 requirements (was 50), 131 OFT
  specobjects (was 109). The numbers had drifted as alarms Phase 1-3,
  SystemState SML, real OEE, and recipe management landed without
  these aggregate counters being updated.

#### Fixed

- Two long-standing parser bugs are now impossible at the tokeniser
  level (instead of being patched around): brace-in-string-value
  desync, and key-order sensitivity when a JSON formatter sorted
  top-level keys.

### ISA-18.2 alarms Phase 3 -- audit journal (REQ-ALARM-004)

Wire alarm lifecycle transitions into the existing audit log so a
forensic auditor can reconstruct every operator interaction with an
alarm: when it fired, when it was acknowledged, when it was shelved (and
by whom), when the condition returned to normal, when it auto-expired
off the shelf, and when it was finally resolved.

#### Added

- `AlertCenter::setAuditCallback(AuditFn)` -- a `(action, key)` callback
  invoked from every mutator on a genuine lifecycle transition (RAISE,
  ACK, RTN, RESOLVE, SHELVE, UNSHELVE, EXPIRE, REALARM). A refresh of an
  already-active alarm is intentionally NOT journalled.
- `MainWindow` wires the callback to the existing `AuditLogger` under
  `category::kAlert` when auth is enabled; the producer (DashboardPresenter)
  doesn't change. AlertCenter stays decoupled from `auth/*` -- the
  composition root owns the translation into an `AuditEvent`.
- AlertCenterTest covers every transition + the no-journal-on-refresh
  invariant; uses a recording callback (no FakeAuditLogger needed).

### ISA-18.2 alarms Phase 2 -- shelve + priority (REQ-ALARM-002, -003)

Extends the Phase 1 alarm lifecycle with the two ISA-18.2 affordances
that turn a working alarm panel into a managed one.

#### Added

- `AlarmState::Shelved` + `AlertCenter::shelve(key, duration)` /
  `unshelve(key)` / `tick()`. The shelve clock is constructor-injected
  (`std::function<TimePoint()>`) so tests are deterministic without
  sleeps; the GUI drives `tick()` from the existing model-tick path in
  `DashboardPresenter::handleNewWorkUnit` (no Glib timer).
- `priority` field on `AlertViewModel` + `kAlarmPriority{Emergency,High,
  Medium,Low}` constants. `AlertCenter::snapshot()` returns the active
  list ordered by priority ascending (stable for ties).
- AlertsPanel: priority badge (P1..P4), state badge picks up the new
  SHELVED label, per-alarm Shelve / Unshelve buttons (5-min default
  shelve duration -- typical ISA-18.2 "give me a moment" window).
- DashboardPresenter assigns priority by alarm kind on raise: system
  fault Emergency, equipment-error / quality-critical High,
  equipment-offline / quality-warning Medium, default Low.

### Real OEE KPI (closes Phase 8F) (REQ-DASHBOARD-008)

The dashboard OEE card was the last KPI carrying a fake formula
(`baseline + (passRate - pivot) * weight` -- pass-rate only, no real
Availability or Performance). Replace it with the industrial-standard
Vorne / OEE composite, computed in the model from live signals.

#### Added

- `ProductionTypes::OeeMetrics` (availability, performance, quality,
  composite) + `kPerformanceTargetUph` + `kOeeWorldClassPct` constants.
- `ProductionModel::oeeSnapshot()` virtual; `SimulatedModel` computes
  Availability from the fraction of equipment in a running state,
  Performance from `throughputUnitsPerHour / kPerformanceTargetUph`
  (clamped to 100%), Quality from the average checkpoint pass rate;
  composite is `A * P * Q`. `MirrorModel` returns zeros (the passive
  secondary doesn't own the source signals).
- `WorkUnitViewModel::oeePct` carries the composite; the presenter
  forwards it on every work-unit notify.

#### Changed

- `DashboardPage` no longer computes OEE itself. The KPI strip card is
  updated in `updateWorkUnitWidgets` from the VM, mirroring how
  THROUGHPUT was already wired. The placeholder constants
  (`kOeeFormulaBaseline / Pivot / QualityWeight / kOeeInitialPct`) and
  the "Phase 8F" comment are gone.

### SystemState FSM Phase 2 + 3 -- safe-state on Fault (REQ-STATE-002, -003)

The Boost.SML state machine introduced in Phase 1 now enforces a proper
lifecycle and ships the safety story:

#### Phase 2 (REQ-STATE-002)

- Transition table tightened: the Phase 1 permissive transitions
  (`RUNNING+Calibrate`, `CALIBRATION+Start`, `ERROR+Start/Stop`) are
  gone. Invalid commands are silently dropped by SML -- state stays
  put, observers do not fire.

#### Phase 3 -- safe-state (REQ-STATE-003 + wire to REQ-ALARM-001)

- `SystemStateMachine::fault(reason)` event drives the SM to ERROR from
  any state and records the reason. Once in ERROR, only `reset()` can
  leave; every other command is dropped (operator-visible lock-out).
- `SystemStateMachine::lastFaultReason()` exposes the recorded reason;
  reset clears it.
- `ProductionModel::lastFaultReason()` virtual added; `SimulatedModel`
  forwards from the SM, `MirrorModel` returns empty (mirror, never
  faults locally). `SimulatedModel::triggerFault(reason)` is a concrete
  (non-virtual) hook for simulator + demo.
- `DashboardPresenter::handleSystemStateChanged` raises a Critical
  ISA-18.2 alarm keyed `system-error` carrying the reason when the
  state transitions into ERROR; recovery (Reset) clears the alarm
  condition, which moves it to `RtnUnack` per the existing alarm
  lifecycle until the operator acknowledges it.

### Formal SystemState FSM via Boost.SML, Phase 1 (REQ-STATE-001)

The production-line top-level lifecycle (IDLE / RUNNING / ERROR /
CALIBRATION) now routes through a formal Boost.SML transition table
instead of ad-hoc `currentState_` assignments scattered across command
bodies. ASPICE-friendly artefact: a single auditable transition table.

#### Added

- Boost.SML (boost-ext/sml v1.1.11) via FetchContent, pinned + routed
  into `_deps/sml-vendor/` so clang-tidy's `src/.*` header filter doesn't
  match the vendored header. SYSTEM include keeps third-party
  diagnostics out of our gates.
- `src/model/SystemStateMachine.{h,cpp}` -- PIMPL wrapper so the SML
  template explosion stays in the .cpp. Thin header API
  (start/stop/reset/calibrate/calibrationDone + state() + onStateChanged).
- `SystemStateMachineTest` covering basic transitions + the
  observer-fires-only-on-real-transitions contract.

#### Changed

- `SimulatedModel` no longer mutates a raw `SystemState currentState_`;
  it dispatches through the SM and the SM's observer fires
  `notifyStateChange()`. Idempotent commands (e.g. `start` from
  RUNNING) no longer trigger phantom view refreshes.

Phase 2 (guards + invalid-transition policy) and Phase 3
(Fault/Acknowledge/Recover + safe-state wired to AlertCenter) are next.

### ISA-18.2 alarm lifecycle (Phase 1) (REQ-ALARM-001)

Alarms now follow an ISA-18.2 / IEC 62682 lifecycle instead of a plain
raise/clear store. The safety-relevant change: a condition returning to
normal while **unacknowledged** no longer disappears -- it becomes
Returned-to-Normal-Unacknowledged and stays visible until the operator
acknowledges it, so a transient fault can't be missed.

#### Added

- `AlarmState` (UnackActive / AckActive / RtnUnack) on `AlertViewModel`;
  `AlertCenter::acknowledge(key)` operator action + hand-rolled state
  machine over the existing raise/clear condition inputs.
- AlertsPanel shows a lifecycle badge (UNACK / ACK / RTN); the per-alarm
  button is now **Acknowledge** (was an immediate dismiss).
- AlertCenter state-machine unit tests + updated model integration test
  (offline -> recovery -> acknowledge).

#### Changed

- `clear(key)` is now a *condition-inactive* input: it resolves an
  acknowledged alarm but moves an unacknowledged one to RtnUnack.
  Producers (DashboardPresenter) are unchanged.

### Live throughput KPI (Phase 8F)

The dashboard THROUGHPUT card now shows a real, model-measured production
rate (completed work units per hour) instead of a hardcoded placeholder.

#### Added

- `ThroughputMeter` (pure, clock-injected): computes units/hour from a
  trailing time-window of completion timestamps; decays toward 0 on a
  stall. `WorkUnit::throughputUnitsPerHour` carries the value through the
  existing `onWorkUnitChanged` seam to `WorkUnitViewModel::throughputUph`.

#### Changed

- `SimulatedModel` records a completion on each genuine work-unit rollover
  and recomputes the rate every tick; the meter is cleared on
  `resetSystem` / `loadProduct` (a reset is not a completion).
- `DashboardPage` updates the THROUGHPUT card from the work-unit ViewModel
  (Ok at/above the nominal target, Warning below); the static
  `kThroughputPlaceholder` is gone. Covers REQ-DASHBOARD-007.

### Multi-station support (Primary/Secondary first instance)

Opt-in via `ui.multistation_enabled` in config. When enabled the HMI
instantiates a second `ProductionModel` (`MirrorModel`) for the secondary
role and a `PrimaryToSecondaryBridge` linking it to the singleton
`SimulatedModel` primary. The new `MultiStationDashboardPage` hosts
two `DashboardPage` instances side by side, each bound to its own
presenter; the bridge appears in the sidebar `BackendHealthBar`
alongside TCP / MQTT / Modbus / OPC-UA. Single-station deployments
are unaffected by default. See ADR-0011 and
`docs/design/multi-station-primary-secondary.md` for the full design.

### Added

- `PrimaryToSecondaryBridge` integration backend (in-process bridge linking
  two `ProductionModel` instances; 6 unit tests).
- `MirrorModel` concrete `ProductionModel` for the secondary role: passive
  state holder driven by the bridge, no internal simulation.
- `MultiStationDashboardPage` view that composes two `DashboardPage`
  instances in a horizontal split.
- `ConfigManager::isMultiStationEnabled()` getter (defaults false).
- `Application::setSecondaryProductionModel()` injector.
- ADR-0011 capturing the design + alternatives + roadmap for N-station,
  fleet view, and the cross-process MQTT-backed bridge replacement.

### Product recipes drive the line (Phase 9)

Each product can carry a *recipe* (process spec) persisted in SQLite.
The operator hits **Load Recipe** on a product and
`ProductionModel::loadProduct` pushes those values onto the dashboard:
the work-unit operation count and every quality card's target line come
from the recipe, matched onto live checkpoints by name (not position).

#### Added

- `Recipe` / `CheckpointTarget` domain types; `recipes` SQLite table +
  `RecipesRepository`.
- `ProductionModel::loadProduct` (interface + `SimulatedModel` +
  `MirrorModel`); `QualityCheckpoint::passRateTarget` field.
- `ProductsPresenter::loadRecipe` (DI setter) + a **Load Recipe** button
  on the Products page.

### Recipe editor -- create / edit recipes from the UI (REQ-PRODUCTS-004)

Recipes were previously read-only (seed-only). The Products page now has
an **Edit Recipe** button opening a dialog to set the operation count and
a pass-rate target per quality checkpoint -- including for a product that
has no recipe yet.

#### Added

- `RecipesWriter` interface (write side) + `DatabaseManager::upsertRecipe`
  (one transaction: upsert the operation count, replace the checkpoint
  targets wholesale). Mirrors the historian's reader/writer ISP split.
- `ProductionModel::getQualityCheckpoints()` enumeration so the editor can
  offer a field per known checkpoint without hard-coding names.
- `ProductsPresenter::getRecipeForEditing` (merges stored values with the
  model's checkpoints) + `saveRecipe` (validates ops >= 1 and targets
  0..100, audits "RECIPE_SAVE") behind a `setRecipeEditing` DI setter.
- `RecipeWriteIntegrationTest` (real DB round-trip + replacement) and
  ProductsPresenterTest save/validation/merge cases.

### Requirements traceability migrated to OpenFastTrace

The homemade lightweight-markdown matrix is replaced by OpenFastTrace
(OFT) specobjects + `[utest->req~...]` coverage tags, validated as a CI
gate on every PR.

#### Added / Changed

- `docs/requirements/` in OFT specobject format (60 requirements with
  `Needs: utest`); tests carry `// [utest->req~xxx~1]` tags.
- CI `traceability` job: `oft trace` gate + click-through HTML report
  artefact. OFT result: 131 total, 0 defects.
- ADR-0013 (OFT adoption) supersedes ADR-0012 (lightweight markdown).

### Test coverage expansion + layout-regression guard

#### Added

- Integration tests wiring **real** components (no mocks): ingest bridge
  -> real model (MQTT + Modbus), model -> presenter -> AlertCenter,
  recipe load -> SQLite -> model, historian round-trip.
- Unit tests: `TimeFormat`, `MirrorModel`, historian degraded-open,
  extracted dialog-helper headers (`PasswordValidation`, `AvatarMime`,
  `UserEditValidation`), `Toast`, multi-station page.
- `DashboardPageTest.CompactPaneFitsMultiStationWidthBudget` -- a
  layout-budget guard that fails if a compact pane's minimum width
  exceeds the multi-station budget (covers REQ-DASHBOARD-006), so the
  recurring "clipped sidebar" regression is caught automatically.
- Windows `ws2_32` linking for the new GUI/integration test targets.

## [1.2.0] - 2026-05-20

### User management + audit polish (B2.5)

Admin surface for user CRUD + self-service profile + reusable Toast
widget + granular audit filters + CSV export for compliance walks.
Extends the auth track (B2) into a complete day-to-day admin
experience.

- **`UsersPresenter`** -- synchronous RBAC facade over
  `UserRepository`, `PasswordHasher`, `Session`, `AuditLogger`.
  Verbs: `list`, `create`, `update`, `remove`, `resetPassword`,
  `changeOwnPassword`, `setOwnAvatar`, `clearOwnAvatar`, `getAvatar`.
  Defence-in-depth: refuses self-delete + self-disable so the
  binary can't lock itself out. Emits a USER-category audit row
  per verb (success OR failure with the rejection reason).
- **Admin UI (`UsersPage`)** -- admin-only Gtk::Grid with Add /
  Edit / Reset Password / Delete buttons per row. Toast feedback
  on every action (success auto-dismisses, error stays).
- **Self-service (`ProfileDialog`)** -- avatar upload + change-own-
  password (verified against stored hash). Available to every
  signed-in role via the `UserBadge` `Profile` button.
- **Avatars**: BLOB storage in `auth.sqlite` with 256 KiB cap +
  MIME whitelist (image/png, image/jpeg) at the repository
  boundary. Fallback to generated initials over a hashed-from-
  username palette colour (`AvatarPlaceholder` -- pure logic,
  GTK-free, reusable in future Qt/web front-ends).
- **Schema migration v1 -> v2 via `PRAGMA user_version`** ladder.
  ALTER TABLE ADD COLUMN for `display_name`, `avatar_mime`,
  `avatar_blob`, `updated_at`. Online + idempotent at any scale.
- **`Session::signalChanged`** sigc::signal emitted on every
  `setUser` / `clear` so `UserBadge` re-renders immediately when
  an admin edits their own row mid-session. Emit is outside the
  mutex so observers can re-enter `currentUser()` without
  deadlocking.
- **Sign-out flow rebuilds MainWindow** from an idle callback
  (under `gtkApp->hold()` to survive the zero-windows transition)
  so role-gated pages (`UsersPage`, `AuditLogPage`) re-evaluate
  for the newly signed-in user. Auth + model + historian
  singletons live on main()'s stack and survive the swap.
- **`LoginDialog` polish**: HeaderBar CSD with the
  `.dialog-titlebar-dark` palette class so the titlebar matches
  the dark content instead of falling back to native Windows
  chrome. `#ifdef _WIN32` path centres the window via
  `SetWindowPos` against the active monitor (GTK4 dropped
  positioning APIs; Win32 doesn't centre small modals by
  default).
- **AuditLogPage filters granulare**: Action dropdown (alpha
  list of every verb the codebase emits), Result dropdown
  (All / SUCCESS / FAILURE), Range picker (All time / Last
  hour / 24h / 7 days) translating to `fromTs` at refresh
  time. AuditQuery + SqliteAuditLogger gain `action` + `result`
  fields; backwards-compatible.
- **AuditLogPage CSV export** -- compliance-grade RFC 4180 with
  UTF-8 BOM + CRLF + non-localised header so the file stays
  diffable across deployments + languages. Suggested filename
  `audit-log-YYYY-MM-DDTHH:MM:SSZ.csv`. Toast on success +
  error.
- **Reusable `Toast` widget** -- Gtk::Revealer-based banner with
  `.success` / `.error` CSS classes from the existing palette.
  Configurable duration + position via Options struct;
  success auto-dismisses (3.5s default), error stays until
  manually dismissed so failure reasons aren't lost.
- **`enable-auth.sh` helper** at the repo root -- flips
  `auth.enabled` + `historian.enabled` in the build's runtime
  config + ensures `data/` exists. Useful for local demo runs;
  the source-tree config keeps both off by default so unit
  tests aren't gated by a login prompt.
- **Tests (78 new)**: SqliteUserRepositoryTest extended from
  11 to 25 (avatar BLOB round-trips, size + MIME validation,
  PRAGMA migration idempotency), AvatarPlaceholderTest (13),
  UsersPresenterTest (40 -- 28 RBAC + **12 audit-format tests
  pinning row content per verb**, including the assertion that
  plaintext passwords NEVER appear in audit details).

### Per-module documentation (D1)

Every module under `src/` now ships a standalone `README.md` that
reads as a library reference: why it exists, SOLID per interface,
API surface class by class, embedding-in-another-project guide,
threading model, testing coverage, out-of-scope items called out.

- `src/auth/README.md` -- Authentication + RBAC + audit core.
- `src/integration/README.md` -- Four backends (TCP, MQTT 3.1.1/5.0,
  Modbus TCP, OPC-UA) + telemetry bridges + serializers.
- `src/presenter/README.md` -- MVP backbone, ViewObserver pattern,
  RBAC integration.
- `src/historian/README.md` -- Time-series store + batch flush +
  retention policy.
- `src/ml/README.md` -- ONNX Runtime via plugin (dlopen workaround
  for ORT heap corruption).
- `src/gtk/view/README.md` -- GTK4 view layer (page registry,
  theme system, custom Cairo widgets, modal dialog lifecycle,
  sign-out rebuild story).
- `src/model/README.md` -- MVP M layer (SimulatedModel,
  ProductionModel interface, DatabaseManager, Boost.Asio context).
- `src/core/README.md` -- Bootstrap two-phase logger, Result<T,E>,
  typed startup exceptions, i18n mechanism, TimeFormat.
- `src/config/README.md` -- JSON config policy + compiled defaults +
  applyI18n pattern.

Root README updated with a "Module documentation" navigation
section + stale metrics refreshed (62 ctest targets, 68% coverage
on 9,442 instrumented lines).

### Auth + audit log (B2)

Username/password authentication with Argon2id hashing, three-role
RBAC, and a full audit trail of operator-attributed actions. Opt-in
via `auth.enabled` in `app-config.json`; off by default so existing
deployments behave as before.

- **`objectsAuth`**: new GTK-free OBJECT lib with:
  - `Role.h` -- Operator / Maintenance / Admin enum + permission
    helpers; numerically ordered for `role >= required` checks.
  - `User.h` + DTO.
  - `PasswordHasher` interface + `Argon2PasswordHasher` (libsodium)
    on the INTERACTIVE profile (~50 ms hash).
  - `UserRepository` interface + `SqliteUserRepository` with
    NOCASE UNIQUE username column.
  - `Session` -- thread-safe holder; reads return snapshot copies.
  - `AuthService` -- login (canonicalised username; same
    InvalidCredentials result for both unknown user + bad password
    to mitigate user enumeration), logout, idempotent
    `seedDefaultUsersIfEmpty()` for demo accounts on first run.
- **Audit pipeline**: `AuditEvent` DTO, `AuditLogger` interface,
  `SqliteAuditLogger` concrete writing into the same auth.sqlite
  file with a `(category, ts)` compound index. AuthService records
  LOGIN success + every failure path and LOGOUT; DashboardPresenter
  records PRODUCTION START / STOP / RESET / CALIBRATE plus
  EQUIPMENT ENABLE / DISABLE; ProductsPresenter records PRODUCT
  ADD / UPDATE / DELETE (success + failure variants).
- **UI**: `LoginDialog` modal Gtk::Window with its own inner
  Glib::MainLoop (gtkmm-4 dropped `Dialog::run()`); shown before
  the main window when auth is enabled. `AuditLogPage` admin-only
  tab with category + username filters and 5 s auto-refresh.
  `UserBadge` sidebar widget shows the active user + role +
  "Sign out" button; sign out closes the main window so the next
  launch comes back through the login dialog.
- **Role-based gating**: each Page's `applyRole()` trims controls
  to what the active role can do. Operator loses Calibration +
  Reset on the Dashboard and the Add / Edit / Delete actions on
  Products; Maintenance gets the lot except user management +
  audit log; Admin sees everything. Disabled buttons carry a
  "Requires Maintenance role" tooltip so the operator knows why.
- **Docker**: builder stage installs `libsodium-dev`; runtime
  stages install `libsodium23`. The docker-compose config opts in
  to auth so the demo prompts for credentials on first connect.
- **Tests (37 new)**: Argon2PasswordHasherTest (6),
  SqliteUserRepositoryTest (11), AuthServiceTest (10),
  SqliteAuditLoggerTest (10). 59/59 tests passing on Linux.

### Historian: tiered retention with downsampling (B1 phase 2)

- **Three-tier schema**: `samples` (raw, 1s cadence, last hour),
  `samples_1m` (1-minute averages, last 24h), `samples_1h` (1-hour
  averages, archive). Each table carries the same `(field, entity,
  ts)` compound index. Schema bootstrap is idempotent; existing
  Phase-1 databases gain the two new tables on next open with zero
  migration.
- **`SqliteHistoryStore::demoteOlderThan(src, dst, age, bucket)`** --
  one atomic transaction that aggregates source rows older than the
  age cutoff into `bucket`-aligned averages (`(ts/bucket)*bucket`
  group key, `AVG(value)` aggregator), inserts them into the
  destination tier, and deletes the originals. No double-counting,
  no gap window where rows sit in neither tier.
- **Query routing**: `query()` picks the coarsest tier whose
  resolution still suits the requested range (range <= 1h: Raw;
  <= 24h: Minute; > 24h: Hour). Trade-off documented in the header
  -- a fancier router could split a 24h range across raw + minute,
  but the chart only has 300 pixels and the shape is the same.
- **`HistorianMaintenance`** worker: one `std::jthread` sleeping on
  a `condition_variable_any` keyed to its `stop_token`. Each sweep
  runs two `demoteOlderThan` passes (raw -> minute, minute -> hour).
  Default cadence 1 min; raw retention 1 h, minute retention 24 h.
  `stop()` wakes the cv and joins inside ms regardless of the sweep
  interval -- the destructor and the composition root's shutdown
  path both rely on this.
- **6 new SQLite-store tests** (demotion atomicity, bucket
  alignment, tier query routing) and **5 new worker tests** (single
  sweep behaviour, chained raw -> minute -> hour, jthread shutdown
  deadlock smoke). 55/55 passing on Linux.

### Time-series Historian (B1)

- **New `objectsHistorian` OBJECT library** with two narrow
  interfaces -- `HistoryWriter` (`write(span<HistoryRecord>)`) and
  `HistoryReader` (`query(field, entity, range)` + `totalSamples()`).
  ISP applied: bridges depend on the writer; the UI depends on the
  reader; `SqliteHistoryStore` happens to implement both.
- **`SqliteHistoryStore`** opens a WAL-mode SQLite file at the
  configured path. Schema is a single
  `samples(ts, field, entity, value)` table with a compound index
  on `(field, entity, ts)` for typical "this series, last hour"
  lookups. Batched INSERT inside one transaction; failed open is
  surfaced as a startup warning and the historian is skipped (no
  crash). 10 hermetic tests against `:memory:`.
- **`HistorianBridge`** subscribes to the existing
  `ProductionModel` signals (quality pass rate, equipment supply
  level, system state) and batches rows to the writer. Buffer
  flushes inline on size cap (32 rows default) or age cap (5 s);
  destructor flushes the tail. No new model surface required.
- **GTK `HistoryPage`** mounted only when the historian opened
  successfully. Queries the reader on construction and after
  every range change (1h / 24h / 7d) / Refresh. Six `TrendChart`
  widgets: 3 quality pass rates + 3 equipment supply levels,
  sharing the 0..100 axis the dashboard gauges already use.
- **`historian.enabled = false` by default** in `app-config.json`;
  the docker-compose demo config flips it on so the page is
  visible from the first `docker compose up`.
- **19 new tests total** (10 store + 5 bridge + 4 page); 54/54
  passing on Linux.

### Model: analog inbound surface (A3)

- **`ProductionModel` gains two analog setters** alongside the
  existing `setEquipmentEnabled` boolean:
  `setEquipmentSupplyLevel(id, level)` (int 0..100 %) and
  `setQualityPassRate(id, rate)` (float 0..100 %). Ingest bridges
  (Modbus today, MQTT / OPC-UA in a follow-up) translate inbound
  sensor data through these setters.
- **`SimulatedModel` yields to external data.** The new
  `externalPassRateOverrides_` set tracks checkpoints that an
  ingest bridge has taken ownership of; `tickSimulation()` stops
  drifting `passRate` for those ids so the next tick does not
  clobber a fresh reading. `supplyLevel` was never touched by the
  simulator autonomously, so its setter is a simple clamp + emit.
  Matches what an HMI talking to a real PLC actually wants: the
  simulator is a development convenience, real data is
  authoritative.
- **Modbus ingest dispatches to both new setters.** Two new
  `FieldKind` values join `EquipmentEnabled` in
  `ModbusRegisterMap`: `EquipmentSupplyLevel` and `QualityPassRate`.
  A `scale` field on `RegisterMapping` handles the common PLC
  fixed-point conventions (raw 850 with scale 0.1 -> 85.0%).
  Bridge dedup gains parallel int / float caches so a noisy
  sensor reporting the same percent across consecutive polls
  does not generate redundant setter calls.
- **Default register map exposes three blocks.** `main.cpp`'s
  `registerModbusBackend` now registers boolean enabled bits at
  the configured base, analog supply registers at `0x10`, and
  analog quality registers at `0x20`. All three blocks are
  per-deployment retargetable via `network.modbus.*` config keys
  (`supply_base_address` / `supply_scale` / `quality_base_address`
  / `quality_scale` / `quality_count`).
- **`examples/modbus_slave_simulator.py` drives the analog
  blocks too.** A new `analog_driver` task sweeps supply 30..100%
  (triangular) and pass rate 92..99% (small cosine drift) at half
  the boolean toggler's cadence. End-to-end demo now shows moving
  level bars on the dashboard, not just ON/OFF switches.
- **Tests: 21 new.** 9 `SimulatedModelTest` cases (set + clamp
  upper / clamp lower / out-of-range / pass-rate sticky-after-set
  / unknown-id). 12 `ModbusIngestBridgeTest` cases (scale 1.0 +
  scale 0.1, dedup, per-entity independence, scale-change re-fires,
  float matcher for 987 * 0.1f). `Mock` + `Capturing` ProductionModel
  subclasses in three other test files pick up matching overrides.

### Modbus primary backend (A1)

- **End-to-end Modbus/TCP primary.** New `objectsModbus` object library
  parallel to `objectsOpcUa`: hand-rolled MBAP framing + PDU codec
  (FC03 + FC04 read), Boost.Asio synchronous client with
  `io_context::run_for()` timeouts and lazy reconnect, register map +
  ingest bridge wiring values into `ProductionModel.setEquipmentEnabled`,
  `std::jthread` poll loop with interruptible cooperative cancellation,
  `ModbusBackend` IntegrationBackend aggregate. Zero new third-party
  dependencies -- the wire codec is hand-rolled the same way MQTT is.
- **Strategy + DIP via `ModbusReader` interface.** The poll loop
  depends on the abstract reader, not the concrete `ModbusClient`.
  Tests run against a `FakeModbusReader` (in-memory map of register
  values) so the cadence + dispatch + change-detection logic is
  covered without opening a socket.
- **I/O panel pill comes for free.** `ModbusBackend::name() = "Modbus"`
  flows through `BackendHealthPresenter` -> `BackendHealthViewModel`
  -> view layer without any UI code change. State semantics:
  Disconnected (loop stopped) / Connecting (loop up, no successful
  read yet) / Connected (last read OK) / Degraded (had successes but
  client currently down). metricsSummary formats
  `"N regs | ok / fail"` for the tooltip.
- **Config:** `network.modbus.{enabled,host,port,slave_id,
  equipment_base_address,equipment_count,poll_interval_ms,
  connect_timeout_ms,request_timeout_ms}` block in `app-config.json`,
  off by default. `BUILD_MODBUS_BACKEND=ON` default in CMake; an
  off build links cleanly without modbus symbols.
- **Tests: 54 across 5 suites.** ModbusPdu (19 -- happy paths,
  framing failure classes, exception responses, 125-register
  boundary), ModbusClient (8 -- loopback via FakeModbusSecondary +
  reconnect + exception caching), ModbusIngestBridge (12 -- dedup,
  per-entity tracking, out-of-range guard), ModbusPollLoop (9 --
  pollOnce dispatch + start/stop cycle + cancellation budget +
  destructor join), ModbusBackend (6 -- lifecycle aggregate). All
  green; FakeModbusSecondary mirrors the MqttClientTest acceptor pattern.
- **Demo:** `examples/modbus_slave_simulator.py` (pymodbus 3.7
  async secondary) toggles holding registers round-robin so the HMI's
  equipment switches flip in lockstep. Integrated into
  `examples/factory_simulation.py` so a single command runs four
  protocols (TCP + MQTT + OPC-UA + Modbus) against the HMI.

### Edge AI inference -- runtime plugin

- **`industrial_ml_ort` shared module + facade pattern.** All ORT-
  touching code moved into a dedicated SHARED MODULE library that
  `OnnxImageClassifier.cpp` `dlopen`s / `LoadLibrary`s on first
  construction. The host binary `industrial-hmi.exe` no longer links
  libonnxruntime; ORT enters the process address space only when an
  inspection actually happens. Avoids the runtime conflict between
  ORT's bundled dependencies (abseil, MLAS, custom allocators) and
  GTK4's libglib allocator that otherwise corrupts the heap during
  widget class registration on Linux + Windows GUI paths.
- C++ exports a stable C ABI (`industrial_ml_create_onnx_classifier`,
  `industrial_ml_destroy_classifier`) so the facade -- plugin contract
  is compiler-agnostic. Cross-DLL ownership is symmetrical: each side
  owns what it allocates, the host calls the plugin's destroy
  function pointer to release the classifier instance.
- New `cmake/FindOnnxRuntime.cmake` only used by the plugin target;
  the facade in `objectsMl` only needs `<dlfcn.h>` / `<windows.h>`.

### Edge AI inference -- UI integration

- **Phase 2a -- `QualityInspectionPresenter`.** Synchronous orchestrator
  with DI on `const ImageClassifier&` + `const ImageDecoder&`. Notifies
  `ViewObserver::onInspectionStarted/Completed/Failed` exactly once per
  call; never throws (catches exceptions from the decoder / classifier
  and routes them through the failed callback). 6 GoogleTest cases.
- **Phase 2b -- `QualityInspectionPage` (GTK4).** Notebook tab with
  `Gtk::FileDialog` picker (PNG / JPEG / BMP filter), `Gtk::Picture`
  preview, top-K results list with `Gtk::LevelBar` confidence bars.
  Each inspection runs on a `std::jthread` member; observer callbacks
  arrive on the worker thread and marshal back to the GTK main loop
  via `Glib::signal_idle.connect_once` -- the same pattern
  `DatabaseManager` uses for SQLite callbacks.
- **Phase 2c -- `MainWindow` integration.** When `BUILD_ML_CLASSIFIER`
  is on AND `assets/models/mobilenetv2_int8.onnx` + `imagenet_labels.txt`
  are present, the window instantiates the decoder + labels +
  `OnnxImageClassifier` + presenter + page and registers the page in
  the Notebook. Missing artefacts log a warning and skip the tab so
  the rest of the UI keeps working unchanged.
- `objectsMl` converted from OBJECT to STATIC: needed so
  `objectsPresenter` (also OBJECT) can transitively propagate
  `ImageDecoder` symbols to test binaries; the linker dead-strips
  unused symbols so the size impact is nil.

### Edge AI inference

- **Phase 0 -- Python model preparation pipeline.** New `scripts/ml/`
  folder with `export_model.py` (MobileNetV2 -> FP32 ONNX, opset 17,
  dynamic batch axis), `quantize_model.py` (dynamic INT8 of `MatMul`
  / `Linear` layers via `onnxruntime.quantization`),
  `sanity_check.py` (PyTorch FP32 vs ONNX FP32 numerical, INT8 top-1
  + confidence delta), `benchmark.py` (warmup + 200 measurement
  iterations, p50 / p90 / p95 / p99 latency, hostname + CPU metadata
  in JSON), and `export_labels.py` (one-shot fetch of the canonical
  1000-class ImageNet labels). Result: 75% size reduction (13.3 MB
  -> 3.5 MB) with top-1 match preserved.
- **Phase 1a -- C++ foundation.** New `objectsMl` library with
  `Image` (RGB POD), `ImageDecoder` (stb_image facade, RGB-forced),
  `Preprocessor` interface + `ImageNetPreprocessor` (bilinear
  resize 256 / centre-crop 224 / float32 normalize with torchvision
  ImageNet mean+std, NCHW output), and `ImageNetLabels` (line-based
  loader, BOM + CRLF tolerant). stb_image vendored via FetchContent
  pinned to a specific commit, routed to `_deps/stb-vendor/` so
  clang-tidy treats it as third-party.
- **Phase 1b -- Inference layer.** `ImageClassifier` abstract
  interface, header-only `FakeImageClassifier` for upstream tests,
  and `OnnxImageClassifier` (ONNX Runtime backend, pimpl so ORT
  headers stay out of the public surface). New CMake option
  `BUILD_ML_CLASSIFIER` (default OFF) gates the ORT-dependent source
  + test. Hand-rolled `cmake/FindOnnxRuntime.cmake` + one-shot
  `scripts/setup-onnxruntime.sh` download helper.
- **CI -- `ml-integration` job.** Runs the Python pipeline (export
  + quantize + labels), pulls a prebuilt ONNX Runtime, configures
  with `BUILD_ML_CLASSIFIER=ON`, and executes the
  `OnnxImageClassifierTest` integration test against the freshly
  produced model on every PR. End-to-end story validated in CI.

### Testing -- ML

- **`ImageNetLabelsTest`** (9 cases) -- vector + file constructors,
  bounds, BOM and CRLF tolerance, missing / empty file errors.
- **`ImageNetPreprocessorTest`** (8 cases) -- output shape, throws
  on bad input, ImageNet normalisation maths against analytic
  expected values.
- **`ImageDecoderTest`** (4 cases) -- contract tests + a hand-rolled
  1x1 BMP byte buffer to verify the RGB / channel-order pipeline.
- **`ImageClassifierTest`** (7 cases) -- interface contract pinned
  through `FakeImageClassifier`: descending confidence sort, k-bound,
  k-zero / k-negative throws, label resolution, stable name.
- **`OnnxImageClassifierTest`** (3 cases) -- end-to-end load / Run /
  softmax / top-K / label resolution against the real INT8 model.
  Skips gracefully when the model is absent.

### Integration layer (framework reuse across verticals)

- **`Serializer` interface** with `CsvSerializer` + `JsonSerializer`
  concretes -- the legacy CsvSerializer was promoted to instance-based
  and now sits behind the abstraction. Shared `objectsIntegration`
  library; `objectsCore` stays GTK-free.
- **`IntegrationBackend` interface** + **`IntegrationManager`**
  composition root. Backends implement `start` / `stop` / `isRunning`
  / `name`; the manager fans out lifecycle calls and tolerates
  per-backend exceptions so one bad backend can't kill the others.
- **`TelemetryPublisher` interface** -- agnostic `publish(topic,
  payload)` surface that any messaging-style backend implements.
  Decouples the domain bridge from the wire protocol so the same
  shell ships across manufacturing / pharma / smart-building / energy
  verticals.
- **`TcpBackend`** -- line protocol over Boost.Asio (own io_context
  + jthread, isolated from ModelContext). Mirrors the console
  command set so the same scripts drive either via `nc localhost
  5555`. Defensive `dynamic_cast<Gtk::Window*>` already in place
  from PR #45 keeps the dialog parent path null-safe.
- **`MqttPublisher`** -- hand-rolled MQTT 3.1.1 publisher in ~200
  lines (CONNECT, CONNACK, PUBLISH, PINGREQ, PINGRESP, DISCONNECT)
  with no paho/mqtt_cpp dependency. QoS 0, plain TCP, work_guard-
  pinned io_context. Implements both `IntegrationBackend` and
  `TelemetryPublisher` so it can drop into any bridge.
- **`ProductionTelemetryBridge`** -- the manufacturing reference
  bridge. Subscribes to `ProductionModel` signals and publishes
  `<prefix>/state`, `<prefix>/equipment/<id>/state`,
  `<prefix>/quality/<id>/rate`. ~80 lines; new verticals write
  their own bridge against the same `TelemetryPublisher`.
- **Config schema extension** -- `network.tcp.enabled / port` and
  `network.mqtt.enabled / broker_host / broker_port / client_id /
  topic_prefix`. Both default disabled; opt-in per deployment.
- **Wiring in `main.cpp`** -- two if-blocks after Bootstrap register
  TCP and / or MQTT depending on config. Same wiring path for GTK
  and console binaries; the console binary stays GTK-free even
  with both backends enabled (`nm` proves it).

### Testing

- **`MqttPacketTest`** (31 cases) -- byte-exact assertion of the
  MQTT 3.1.1 wire format against spec sections 2.2.3 (variable-
  length encoding), 1.5.3 (UTF-8 strings), 3.1 (CONNECT), 3.2
  (CONNACK), 3.3 (PUBLISH), 3.12 (PINGREQ), 3.14 (DISCONNECT).
- **`MqttPublisherTest`** (10 cases) -- in-process `MockMqttBroker`
  TCP server captures the publisher's frames; covers handshake,
  publish ordering, heartbeat cadence, clean disconnect,
  ECONNREFUSED.
- **`ProductionTelemetryBridgeTest`** (8 cases) -- callback
  plumbing in isolation; uses a `CapturingPublisher` test double,
  no socket I/O.
- **`TcpBackendTest`** (18 cases) -- real loopback TCP, mocked
  `ProductionModel` + `ProductsRepository`, exercises every
  command + error path.
- **`IntegrationManagerTest`** (9 cases) -- lifecycle composition
  + per-backend exception tolerance.
- **`CsvSerializerTest` extended** with round-trip + read-side tests
  (the original was write-only).
- **`JsonSerializerTest`** (15 cases) -- output shape, RFC 8259
  string escaping, round-trip, parse errors, Liskov substitutability.
- **37 ctest targets total** (was 31 in v1.1.0). Coverage stays
  79%.

### Architecture

- **Headless console front-end** (`industrial-hmi-console`) sharing Model +
  Presenter + Bootstrap with the GTK desktop binary. Full command set:
  `help`, `status`, `start`, `stop`, `reset`, `calibrate`, `eq <id> on|off`,
  `alerts`, `dismiss <key>`, `products`, `view <id>`, `quit`. Implements
  `ViewObserver` so presenters never branch per front-end. The binary
  links zero gtkmm (validated by `nm`).
- **Staged Bootstrap** (`src/core/Bootstrap.{h,cpp}`) orchestrates startup
  across both front-ends: stderr logger -> config -> configured logger ->
  i18n -> SQLite. Resolves the classic config/logger chicken-and-egg via
  a two-phase logger; `objectsCore` is now GTK-free and `objectsAppGtk`
  holds the GTK bootstrap glue.
- **Fail-fast startup errors** -- typed `CriticalStartupError` hierarchy
  (`ConfigMissing`, `ConfigCorrupt`, `DatabaseInit`, `LoggerBootstrap`)
  thrown from Bootstrap/Application, caught in `main()`, surfaced through
  a native reporter (MessageBoxW on Windows GUI, stderr elsewhere) with
  documented exit codes (0/1/2/3). Dialog body is localised via gettext.
- **Policy / mechanism split** -- `ConfigManager::applyI18n()` owns the
  language policy; `src/core/i18n` stays a pure gettext adapter with no
  dependency on glibmm. DB init moved from `Application::initDatabase`
  to `Bootstrap` Stage 5 so both front-ends start from the same
  initialised SQLite.
- **MainWindow keyboard dispatcher extracted** into `MainWindowKeyDispatch`
  -- a pure free function + `KeyDispatchContext` struct so every F-key
  shortcut can be unit-tested without instantiating MainWindow.
- **Defensive dialog parent lookups** in `ProductsPage`
  (showAddProductDialog, showEditProductDialog, showProductDetail) --
  `dynamic_cast<Gtk::Window*>` now falls back to a parentless dialog
  constructor instead of dereferencing a null pointer, mirroring the
  pattern already used by `DialogManager::createMessageDialog`.

### Testing & CI

- **Test coverage: 43% -> 79%** across 4095 lines, verified by gcovr on
  every PR and surfaced at the top of the Actions run page.
- **31 ctest targets** organised as:
  - 10 scenario-based E2E tests piping stdin into the console binary
    and diffing stdout against `*.expected` golden files
  - 20+ unit test binaries (model, presenter, view, config, core)
  - 1 coverage-dedicated Xvfb job booting the real GTK binary to lift
    MainWindow / SettingsPage / DialogManager / AlertsPanel / LiveClock
    / gauges / charts out of 0%
- **View-layer tests under real GTK** via `ViewTestMain.cpp` (gtk_init)
  + `xvfb-run` on Linux CI. Includes `DashboardPageTest`,
  `ProductsPageTest` (12 cases), `SettingsPageTest` (25 cases), and
  `DialogManagerTest` (11 cases that dispatch `Gtk::Dialog::response()`
  programmatically).
- **Async presenter tests** -- `ProductsPresenterAsyncTest` drives a
  Glib::MainLoop to pump signal_idle queues, exercising the Boost.Asio
  -> ModelContext -> signal_idle marshaling path that a synchronous
  harness can't observe.
- **Coverage-focused xdotool smoke** -- CI coverage job boots the GTK
  binary under Xvfb, sends F1 to open AboutDialog, Escape to close,
  then lets `HMI_EXIT_AFTER_MS` drive a clean atexit so gcov .gcda
  files flush. Lifts `AboutDialog.cpp` to 98%.
- **LoggerImplTest** (23 cases) covers FileLogger rotation workflow,
  CompositeLogger propagation + `isEnabled` fan-out, NullLogger no-ops,
  CallbackLogger reentrancy guard via atomic flag.
- **I18nTest** (7 cases) covers the gettext adapter: forceLanguage /
  propagateLangToLanguage / resolveLocaleDir (absolute, relative-exists,
  missing), `gEnvOwned` reset on "auto -> explicit -> auto".
- **CI: Node.js 24 opt-in** via `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24`
  eliminates 3 Node 20 deprecation warnings on upload-artifact@v5.
- **Dual-platform CI** -- Ubuntu 24.04 (GCC 13 + pkg-config) and Windows
  MSYS2 CLANG64 (vcpkg-free); both build + test on every PR. Coverage
  job runs only on Ubuntu (one source of truth for the gcovr report).
- **Clang-tidy + cppcheck** gates on every PR, with the clang-tidy
  report uploaded as an artifact.

### Features

- **Color palettes** (8 total) -- Industrial (baseline), Nord, Paper,
  Right Sidebar, Dracula, CRT, Blueprint, Cockpit. Loaded as a second
  CSS provider layered over the base stylesheet.
- **Thumbnail palette picker** in Settings with four colour swatches
  per card, palette name, and a mode badge ("Dark + Light", "Dark only",
  "Light only").
- **Mode-locked palettes** -- Tier 2 palettes are single-mode by design
  (Paper = light-only; Dracula / CRT / Blueprint / Cockpit = dark-only).
  The incompatible Dark/Light radio is disabled with a tooltip, and
  picking a locked palette auto-snaps the Theme.
- **Alternate UI layouts** -- Right Sidebar mirrors the sidebar to the
  right; Blueprint moves Alerts and Logs into top-bar popovers. Swapped
  at runtime via `MainWindow::reloadLayout` with an atomic
  detach/parse/re-attach.
- **Alerts Center** with info / warning / critical severities, per-alert
  dismiss, and resolved-alert history. 26 dedicated tests.
- **Products CSV export** with round-trip unit tests + error dialog on
  unwritable paths.
- **i18n grown to 11 languages** (added `es_MX`, `ga`, `pt_BR`, `sv`).

### Tooling

- **CMakePresets.json** for modern CMake workflow.
- **Doxyfile** for API documentation generation.
- **Sanitizers** support (AddressSanitizer, UBSanitizer) via
  `-DENABLE_SANITIZERS=ON`.
- **Code coverage** (gcov/gcovr) with HTML reports published as a CI
  artifact.
- **.editorconfig** for consistent formatting.

### Style

- **Banner separator cleanup** -- 372 `// ----`, `# ====`, `/* ==== */`,
  `<!-- ==== -->` decorative comment lines stripped across 37 source,
  test, CMake, shell, CSS, and UI files. Replaced with single-line
  `// Title` comments where the label was meaningful; dropped otherwise.

### Fixed

- **Dracula** combobox double-border (flattened the inner GTK button).
- **Nord**: restored Light variant stripped by an earlier unwrap script.
- **Blueprint / Cockpit**: removed stray light-mode CSS sections (both
  are dark-only palettes by design).
- **Paper**: ColumnView header text no longer invisible on light-on-light;
  notebook stack background forced to paper cream.
- **Settings "Show logs"** checkbox preserves the user's choice across
  palette transitions (Blueprint forces a log tail, but the user's
  preference is restored when leaving Blueprint).
- **Log panel sizing**: `log_panel` `height-request` reduced from 150
  to 70 across palettes to stay within the 1200 px window budget.

## [1.0.0] - 2026-04-09

### Added

- **Async I/O Context** with Boost.Asio and `std::jthread` (C++20 RAII)
  -- non-blocking database operations, single I/O thread for async work,
  thread-safe callback marshaling via `Glib::signal_idle()`.
- **Theme Toggle UI** in sidebar (Dark Mode / Light Mode radios,
  real-time switching, ThemeManager integration).
- **vcpkg.json manifest** for Windows dependencies (manifest mode,
  reproducible builds).
- **Cross-platform support** -- Linux (Ubuntu 24.04+, Debian, Fedora) +
  Windows (10/11, Server 2022). Platform-agnostic CMake; vcpkg on
  Windows, pkg-config on Linux.
- **CI/CD pipeline** (GitHub Actions) with dual-platform builds, code
  quality checks (clang-tidy, cppcheck), documentation verification,
  automated releases with artifacts.
- **Adwaita themes with design tokens** -- 30+ CSS variables (colours,
  spacing, typography), gradient sidebar backgrounds.
- **Complete CRUD operations** -- soft delete with `deleted_at`
  timestamp, input validation with error dialogs, async confirmation
  dialogs, search.
- **Dependency Injection** -- refactored from singleton anti-pattern;
  explicit dependencies via constructor injection, testable
  architecture with mock support.
- **MVP architecture** -- Model (`DatabaseManager`, `SimulatedModel`,
  `ModelContext`), View (GTK4 pages), Presenter (`DashboardPresenter`,
  `ProductsPresenter`).
- **Modern C++20 features** -- Concepts, `std::jthread`, ranges,
  `constexpr`, `[[nodiscard]]`.

### Changed

- `DatabaseManager`: added async methods alongside sync methods.
- `ProductsPresenter`: updated to async signatures with callbacks.
- `ProductsPage`: updated UI to handle async operations.

### Fixed

- UI freezing during database operations (now async).
- Theme consistency across components.
- Windows build with vcpkg.

## [0.5.0] - 2026-04-07

### Added

- Initial MVP architecture implementation.
- GTK4 user interface.
- SQLite database integration.
- Pharmaceutical manufacturing domain model.
- Basic CRUD operations (synchronous).
- Singleton pattern (later refactored to DI in 1.0.0).

### Known Issues (resolved in 1.0.0)

- Blocking database operations.
- Singleton pattern.
- Linux-only build.

---

## Upgrade Notes

### From 0.5.0 to 1.0.0

**Breaking changes** -- `ProductsPresenter::{addProduct, updateProduct,
deleteProduct}` now require a callback parameter:

```cpp
// 0.5.0
bool ok = presenter_->addProduct(code, name, status, stock, quality);
if (!ok) showError();

// 1.0.0
presenter_->addProduct(code, name, status, stock, quality,
    [this](bool ok) { if (!ok) showError(); });
```

**New dependencies** -- Boost.Asio (already pulled in by signals2);
C++20 compiler (GCC 10+, MSVC 2019+, Clang 10+).

[Unreleased]: https://github.com/bogdanbaloi/industrial-hmi/compare/v1.2.0...HEAD
[1.2.0]: https://github.com/bogdanbaloi/industrial-hmi/releases/tag/v1.2.0
[1.0.0]: https://github.com/bogdanbaloi/industrial-hmi/releases/tag/v1.0.0
[0.5.0]: https://github.com/bogdanbaloi/industrial-hmi/releases/tag/v0.5.0
