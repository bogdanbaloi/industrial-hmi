# Industrial HMI — Functional Requirements

Requirements catalogue for the Industrial HMI codebase, in
[OpenFastTrace (OFT)](https://github.com/itsallcode/openfasttrace)
specobject Markdown format. OFT parses the `` `req~<name>~<version>` ``
markers below and cross-checks them against `[impl-> ...]` /
`[utest-> ...]` coverage tags in source files and unit tests.

## Why this exists

Industrial / safety-critical software (automotive ISO 26262, medical
IEC 62304, industrial IEC 61508, avionics DO-178C) lives or dies by
traceability. This file is the **single source of truth for what the
HMI does**. OFT generates `TRACEABILITY.md` and an HTML report from
the markers below + the coverage tags in source.

Tests verifying a requirement carry `// [utest->req~<name>~<version>]`
at the top of the file. Production code carries `// [impl->req~...]`
when relevant. OFT's CI gate fails the build if any MUST/SHOULD
requirement is uncovered.

## ID scheme

```
req~<category>-<number>~<version>
       ^         ^         ^
       |         |         |__ revision, bump on requirement change
       |         |__ 3 digits, zero-padded
       |__ lowercase domain tag (see Categories)
```

Markdown headings keep the `REQ-CATEGORY-NUM` upper-case style for
human readability; the OFT marker below each heading uses the
lowercase canonical form.

## Categories

| Tag | Domain |
|---|---|
| `arch` | Cross-cutting architectural rules (MVP layering, observer pattern) |
| `auth` | Authentication, RBAC, audit log |
| `core` | Bootstrap, error handling, logging, configuration |
| `dashboard` | Single-station dashboard surface + widgets |
| `historian` | Time-series persistence + maintenance |
| `i18n` | Multi-language support |
| `inspection` | Edge AI quality inspection (ONNX runtime) |
| `integration` | Backend protocols (TCP, MQTT, Modbus, OPC-UA) |
| `multistation` | Primary / secondary deployment + bridge |
| `perf` | Performance budgets + microbenchmark coverage |
| `products` | Product catalogue + CRUD |
| `quality` | Quality checkpoints, pass rate, alerts |
| `settings` | Runtime preferences (theme, palette, log panel) |

## Priority tiers

- **MUST** — system fails its mission without it
- **SHOULD** — strong expectation, degraded UX without it
- **NICE** — opt-in or polish

## Coverage convention

Each requirement declares which downstream artifact types must cover
it via `Needs:`:

- `Needs: utest` — verified by unit test (most functional reqs)
- `Needs: impl` — verified by code-level evidence only (architectural
  rules whose verification is a build / CMake / lint check rather
  than a runtime assertion)
- No `Needs:` line — manual-only verification documented in
  `Verified by:`; OFT will not block CI on these

---

## ARCH — Architecture

### REQ-ARCH-001 (MUST) — MVP layer separation

`req~arch-001~1`

The HMI **shall** keep business logic out of the View layer. Views
**shall** receive state exclusively through ViewModel structs and
forward user actions exclusively to Presenters via method calls.

Verified by: code review (no `model::` includes in `gtk/view/`),
DashboardPresenterTest, ProductsPresenterTest.

ADR: 0001 (MVP Layer Boundaries).

Needs: utest

### REQ-ARCH-002 (MUST) — Two front-ends from one main

`req~arch-002~1`

The same `main.cpp` **shall** produce both a GTK4 binary and a
headless console binary. The console binary **shall** link zero
gtkmm symbols so the View abstraction is verified mechanically at
link time.

Verified by: CMake target `industrial-hmi-console` builds and
links without gtkmm; CI matrix exercises both binaries.

ADR: 0002 (Two Front-ends, One Main).

### REQ-ARCH-003 (MUST) — Observer pattern, no sigc in domain

`req~arch-003~1`

Cross-layer notifications **shall** use the project's
`ViewObserver` abstraction. Domain code (`model/`, `presenter/`)
**shall not** depend on sigc / gtkmm signal types so the headless
binary remains valid.

Verified by: BasePresenterTest.

ADR: 0003 (Observer Pattern, not sigc).

Needs: utest

### REQ-ARCH-004 (SHOULD) — Staged bootstrap

`req~arch-004~1`

Application startup **shall** proceed in deterministic stages
(logger -> config -> configured logger -> i18n -> integration
backends -> UI). Failure in an early stage **shall** abort with a
human-readable error in a startup dialog (GTK) or stderr line
(console).

Verified by: StartupErrorsTest, StartupDialogTest.

ADR: 0004 (Staged Bootstrap).

Needs: utest

### REQ-ARCH-005 (MUST) — IntegrationBackend uniformity

`req~arch-005~1`

Every external protocol (TCP, MQTT, Modbus, OPC-UA, in-process
bridge) **shall** implement the same `IntegrationBackend`
interface. The `IntegrationManager` **shall** treat them
uniformly for start / stop / health reporting.

Verified by: IntegrationManagerTest, TcpBackendTest,
MqttClientTest, ModbusBackendTest, OpcUaBackendTest,
PrimaryToSecondaryBridgeTest.

ADR: 0005 (IntegrationBackend Interface).

Needs: utest

### REQ-ARCH-006 (SHOULD) — Runtime palette + layout hot-swap

`req~arch-006~1`

Switching the active palette in Settings **shall** rebuild the
main window layout without an application restart. The user's
active tab and runtime toggles **shall** be preserved across the
swap.

Verified by: manual; CI smoke check on multiple palettes.

ADR: 0008 (Runtime Palette Layout Swap).

### REQ-ARCH-007 (NICE) — ML plugin opt-in at build time

`req~arch-007~1`

ONNX Runtime dependency **shall** be opt-in via
`BUILD_ML_CLASSIFIER` CMake flag. The binary produced without
ML **shall** drop the symbol set entirely.

Verified by: CMake configures + builds with both
`-DBUILD_ML_CLASSIFIER=OFF` and `=ON` in CI matrix.

ADR: 0009 (ML Plugin Conditional Compile).

### REQ-ARCH-008 (MUST) — Multi-threaded safety

`req~arch-008~1`

Every thread that publishes domain events **shall** marshal to the
GTK main thread via `Glib::signal_idle()` before touching widgets.
ThreadSanitizer **shall** report zero races on the CI test suite.

Verified by: TSan CI job on every PR.

### REQ-ARCH-009 (NICE) — Config: policy vs mechanism

`req~arch-009~1`

Configuration **shall** be policy (what's enabled, what's tuned),
not behaviour (no code paths in config). Code paths gated by
config **shall** degrade gracefully when the value is missing.

ADR: 0010 (Config Policy vs Mechanism).

### REQ-ARCH-010 (SHOULD) — Lock-free SPSC ring buffer for strict producer/consumer handoffs

`req~arch-010~1`

The codebase **shall** provide a bounded, lock-free single-producer /
single-consumer ring buffer (`app::core::SpscQueue<T, kCapacity>`) for
decoupling a latency-sensitive producer thread from a slower consumer
thread without a shared mutex. The capacity **shall** be a compile-time
power of two (enforced by `static_assert`); `push()` and `pop()`
**shall** be `[[nodiscard]] noexcept` and **shall** never block, with
`push()` returning `false` on overflow (drop semantics) so the producer
is never stalled by back-pressure.

The implementation **shall** be race-free under ThreadSanitizer
(`-fsanitize=thread`, the REQ-ARCH-008 CI gate): the producer's element
publish is a release store on `tail_`, the consumer's observe is an
acquire load on `tail_`, and the consumer's slot release is a release
store on `head_`. `head_` and `tail_` **shall** be cache-line-aligned
to avoid false sharing.

The queue **shall** be applied only where the producer/consumer pair is
genuinely 1:1 (e.g. the Modbus poll thread -> ingest bridge seam);
fan-in seams (MQTT multi-topic, GTK-thread historian bridge) **shall**
keep their mutex-based handoff (see ADR-0018, which rejects
generalising lock-free to non-SPSC seams).

ADR: 0018 (Lock-free SPSC queue for the ingest hot path, and only there).

Verified by: SpscQueueTest (round-trip, full/empty, FIFO, wrap-around,
size, and a two-jthread StressProducerConsumer that verifies the
triangular-sum invariant under ThreadSanitizer).

Needs: utest

---

## AUTH — Authentication & Authorisation

### REQ-AUTH-001 (MUST) — Password hashing via Argon2id

`req~auth-001~1`

User passwords **shall** be hashed with Argon2id (libsodium) using
INTERACTIVE memory/op limits. Plaintext **shall not** appear in
the on-disk store at any point.

Verified by: Argon2PasswordHasherTest.

Needs: utest

### REQ-AUTH-002 (MUST) — Three-tier RBAC

`req~auth-002~1`

The system **shall** distinguish Operator / Maintenance / Admin
roles. Each role's permitted actions **shall** be defined in
`auth::Role.h` and gated at both the presenter and view layers
(defence-in-depth).

Verified by: UsersPresenterTest, AuthServiceTest;
DashboardPage::applyRole + UsersPage gating in code.

ADR: 0006 (Auth Defence-in-Depth).

Needs: utest

### REQ-AUTH-003 (MUST) — Audit log for sensitive actions

`req~auth-003~1`

User actions on sensitive surfaces (Reset, Calibration, user
management, password change) **shall** be recorded in a SQLite
audit log with timestamp, user, action, and target.

Verified by: SqliteAuditLoggerTest.

Needs: utest

### REQ-AUTH-004 (SHOULD) — Audit log CSV export

`req~auth-004~1`

Audit log entries **shall** be exportable as RFC 4180 compliant
CSV, with timestamp and event fields preserved through round-trip.

Verified by: CsvSerializerTest + AuditLogPage manual.

Needs: utest

### REQ-AUTH-005 (NICE) — Avatar placeholder for users without an image

`req~auth-005~1`

Users without an uploaded avatar **shall** display an
initials-based placeholder with a deterministic colour drawn from
a fixed palette (so the same user always gets the same colour
across sessions).

Verified by: AvatarPlaceholderTest.

Needs: utest

### REQ-AUTH-006 (SHOULD) — Session: bad-password feedback

`req~auth-006~1`

Failed login **shall** report a generic "bad credentials" message
(no leak of "user exists but password wrong" vs "user doesn't
exist"). Audit log **shall** distinguish the two for investigations.

Verified by: AuthServiceTest.

Needs: utest

---

## CORE — Bootstrap, Errors, Config

### REQ-CORE-001 (MUST) — Result<T,E> for fallible operations

`req~core-001~1`

Operations that can fail in user-relevant ways (config load,
database open, network connect) **shall** return `Result<T, E>`
rather than throw, to keep the failure path explicit.

Verified by: ResultTest.

Needs: utest

### REQ-CORE-002 (MUST) — Top-level exception guard

`req~core-002~1`

`main()` **shall** wrap the run loop in a try/catch covering
known startup errors, std::exception, and unknown exceptions,
with a distinct exit code per family (2 startup-fatal, 1 std,
3 unknown).

Verified by: StartupErrorsTest, ExceptionHandlerTest.

Needs: utest

### REQ-CORE-003 (SHOULD) — Configurable structured logging

`req~core-003~1`

Logger **shall** support runtime log-level changes, file +
console outputs, and rotation by size + file count.

Verified by: LoggerTest, LoggerImplTest.

Needs: utest

### REQ-CORE-004 (SHOULD) — Config from JSON, validated

`req~core-004~1`

Application configuration **shall** load from a JSON file
specified by `kConfigPath`. Missing keys **shall** fall back to
typed defaults; malformed JSON **shall** abort startup with a
clear error.

Verified by: ConfigManagerTest.

Needs: utest

---

### REQ-CORE-005 (SHOULD) — Semantic validation of configuration

`req~core-005~1`

Beyond JSON syntactic validity (REQ-CORE-004), the configuration
**shall** be validated against a documented semantic contract before
the rest of startup commits to its values. The contract covers
enumerations (log level, language code), positive-only durations /
counts / sizes, integer port ranges (1..65535) when their owning
backend is enabled, and required-when-enabled fields.

The runtime enforcement lives in `src/config/ConfigValidator.cpp`.
The auditable spec lives at `schemas/app-config.schema.json`
(JSON Schema draft-07). Drift between the two is caught by code
review and by the validator's own unit tests.

When validation fails, startup **shall** raise
`ConfigInvalidError` (distinct from `ConfigCorruptError`) listing
every violation in a single error so the operator can fix all
issues in one round-trip. See ADR-0015.

Verified by: ConfigValidatorTest.

Needs: utest

### REQ-CORE-009 (NICE) — Hot-reload listeners: end-to-end re-apply after watcher-triggered reload

`req~core-009~1`

`ConfigManager` **shall** expose an `addReloadListener` API that
registers a `void()` callback to fire after every reload that
successfully replaces the in-memory flat-key map. Listeners **shall
NOT** fire on rejected reloads (parse error, validator NACK,
missing file) -- the previous configuration is still live and
nothing downstream has changed.

Listeners **shall** be invoked AFTER the manager has released its
internal mutex and the new state is observable to getters, so a
listener that reads `getLanguage()` / `getXxx()` sees the new value.
A listener **may** call back into `setLanguage`, `setPalette`, or
even `reload()` recursively without deadlock.

A listener that throws **shall NOT** prevent the remaining listeners
from running. The manager catches + logs through the injected
Logger and continues iterating.

`Bootstrap` **shall** register one listener that re-invokes
`applyI18n()` so a language edit in `config/app-config.json` takes
effect end-to-end: the watcher (REQ-CORE-007) notices the mtime
advance -> `reload()` swaps the map (REQ-CORE-006) -> listener
re-binds gettext to the new `LC_MESSAGES`. The operator never
restarts the binary.

Out of scope for Phase 3b: re-applying logger configuration mid-
flight (active log writers would need synchronised reconfiguration
and the value is low); re-applying database connection pool /
integration-backend topology (every backend snapshots its config at
construction; their re-configuration is a separate roadmap item).

Verified by: ConfigManagerTest.ReloadListenerFiresOnAcceptedReload,
ReloadListenerDoesNotFireOnRejectedReload,
ReloadListenerSeesPostReloadState,
ReloadListenerExceptionDoesNotBreakPipeline.

ADR: 0015, 0017.

Needs: utest

### REQ-CORE-008 (NICE) — Internal synchronisation: race-free concurrent readers vs reload

`req~core-008~1`

`ConfigManager` **shall** guard every read and write of its in-memory
flat-key map (`config_`) with an internal mutex, so a `reload()` call
running on a background thread (typically `ConfigFileWatcher`'s
`std::jthread`) is race-free against concurrent getter calls on any
other thread. Previously (Phase 2 of REQ-CORE-006), thread-safety was
"single-writer + single-reader by convention" and ThreadSanitizer
correctly flagged the cross-thread pattern as a data race; Phase 3a
closes that constraint inside the manager so consumers no longer have
to synchronise externally.

Implementation **shall** use `std::recursive_mutex` (not
`std::mutex`): `reload()` holds the write lock across
`ConfigValidator::validate(*this)` for the swap-and-validate window,
and the validator recurses back through the public getters which
acquire the same lock on the same thread. A non-recursive mutex would
self-deadlock there. The performance trade-off is acceptable: getter
calls are sub-microsecond and rare compared to the alarm / dashboard
hot paths, while the recursive primitive removes an entire class of
test-only scaffolding (separate "internal" un-locked accessors for
the validator).

`setLanguage()` and `setPalette()` **shall** lock only the in-memory
write and release the lock BEFORE invoking the disk-persist helper
(`persistLanguage` / `persistPalette`). Holding a mutex across fsync
would stall every getter for an unbounded duration on a slow
filesystem.

Verified by: ConfigManagerTest.ConcurrentReadersDuringReload, run
under ThreadSanitizer in CI (`ctest --label-regex tsan`). The test
spawns a reader thread looping `getLanguage` / `getWindowWidth` /
`getDialogMessage` while the main thread hammers `reload()` against
two alternating valid configs; TSan-clean.

ADR: 0015 + 0017 (no time-source abstraction needed here; the only
clock involved is the OS-level mutex implementation).

Needs: utest

### REQ-CORE-007 (NICE) — Watch the config file and trigger reload on edit

`req~core-007~1`

A `ConfigFileWatcher` **shall** poll the configured app-config path
at a caller-supplied interval (default 1 s) and, whenever the file's
last-write-time advances, call `ConfigManager::reload()`. The watcher
itself **shall NOT** interpret reload failures beyond logging them --
the manager's REQ-CORE-006 contract (atomic re-read + re-validate +
rollback on any failure) already covers parse errors and validator
rejections.

Cross-platform implementation **shall** use polling
(`std::filesystem::last_write_time`) rather than OS-specific
event-driven primitives (inotify / kqueue /
ReadDirectoryChangesW). Three reasons: (a) one code path works on
Linux, Windows MSYS2 CLANG64, and the headless console; (b) the
operator's perception of "instant" is the cadence (~1 s) the poll
runs at, so event-driven sub-second latency buys nothing for a
hand-edited config file; (c) the polling loop is trivial to test
deterministically via a `pollOnce()` method that runs one iteration
on the caller's thread.

The watcher **shall** own a `std::jthread` (Glib timer would not
run on the console binary). Iterations sleep on a
`condition_variable` keyed to the stop token so shutdown returns
within milliseconds, not the full poll interval.

Verified by: ConfigFileWatcherTest (7 cases: no-change baseline,
edit triggers reload, second-poll-after-edit reports no change,
reload-rejected still reports the file change, missing file
preserves previous config, background-thread start/stop lifecycle
including idempotent stop, background thread detects an edit).

ADR: 0015 (validator + schema as spec, same gate vets every reload)
+ 0017 (rejected TimeSource everywhere; the watcher uses the real
process clock per the same rejection rationale).

Needs: utest

### REQ-CORE-006 (NICE) — Hot reload configuration without restart

`req~core-006~1`

`ConfigManager` **shall** expose a `reload()` method that re-reads the
configured `configPath` from disk, runs `ConfigValidator` against the
new contents, and atomically replaces the in-memory flat-key map IF
AND ONLY IF parse + validation both pass. On any failure (file
missing, parse error, validator rejection), the previous in-memory
state **shall** be preserved exactly so getter consumers never observe
a half-applied or invalid configuration.

The method **shall** return `bool`: `true` on successful swap, `false`
on rejection. The caller (or the injected logger) sees a warning on
each rejection path.

This is Phase 1 of the hot-reload story. Phase 1 deliberately omits:
a file-system watcher (consumers decide WHEN to call `reload()`:
Settings page button, SIGHUP, timer, OPC-UA method); a
change-notification callback (consumers diff before/after or re-poll
on their existing tick path); re-application of derived state (i18n
re-bind, logger re-configure, theme re-apply) -- those are the
caller's job. Scope = the atomic data swap with validation gate.

Verified by: ConfigManagerTest (5 cases: successful reload after
edit, missing-file rejection, parse-error rejection,
validator-rejection, reload-without-initialize sentinel).

ADR: 0015 (validator + schema as spec) -- the same validation gate
that vets the boot-time config now vets every reload.

Needs: utest

---

## DASHBOARD — Single-station surface

### REQ-DASHBOARD-001 (MUST) — Equipment cards reflect live state

`req~dashboard-001~1`

Each equipment station card **shall** display the current
status (Online / Offline / Processing / Error), supply level,
and enable/disable toggle that propagates to the model.

Verified by: DashboardPageTest, DashboardPresenterTest.

Needs: utest

### REQ-DASHBOARD-002 (MUST) — Quality checkpoint cards

`req~dashboard-002~1`

Each quality checkpoint card **shall** display pass-rate
percentage, units inspected, defects found, and a Cairo gauge
that visually encodes the pass rate magnitude + status colour.

Verified by: DashboardPageTest.

Needs: utest

### REQ-DASHBOARD-003 (SHOULD) — KPI top strip (OEE / Throughput / Pass Rate)

`req~dashboard-003~1`

The dashboard **shall** display three headline KPIs at the top:
OEE, Throughput, Pass Rate. Pass Rate **shall** be the live
aggregate from quality observers; OEE and Throughput **may** be
placeholder values until model fields exist.

Verified by: BigNumberCardTest + DashboardPage integration
visible in single-station mode.

Needs: utest

### REQ-DASHBOARD-004 (SHOULD) — Session uptime donut

`req~dashboard-004~1`

The dashboard **shall** display a session-uptime donut chart
showing time spent in each SystemState (Idle / Running / Error /
Calibration) with the running-share percentage in the centre.

Verified by: DonutChartWidgetTest + DashboardPage integration.

Needs: utest

### REQ-DASHBOARD-005 (MUST) — Control panel role gating

`req~dashboard-005~1`

Calibration and Reset buttons **shall** be disabled when the
active role lacks `canCalibrate` / `canResetSystem` permissions.
A tooltip **shall** explain why.

Verified by: DashboardPageTest (role gating cases).

Needs: utest

### REQ-DASHBOARD-006 (NICE) — Compact mode for multi-station

`req~dashboard-006~1`

When hosted in a narrow pane (multi-station mode), the dashboard
**shall** shrink Cairo gauges, hide trend charts, and hide the
KPI top strip to fit the reduced width budget.

Verified by: DashboardPageTest.CompactPaneFitsMultiStationWidthBudget
(compact pane minimum width stays within the per-pane budget so two
panes + the sidebar fit a 1536 logical-px viewport).

Needs: utest

### REQ-DASHBOARD-007 (NICE) — Live throughput KPI

`req~dashboard-007~1`

The dashboard THROUGHPUT KPI **shall** display the live production rate
(completed work units per hour) measured by the model from recent
work-unit completions, not a static placeholder. The rate **shall**
decay toward zero when the line stalls and reset when a new batch is
loaded.

Verified by: ThroughputMeterTest (rate computation, window eviction,
reset) + DashboardPresenterTest.ForwardsModelThroughputIntoViewModel
(model rate reaches the view's ViewModel unchanged).

Needs: utest

### REQ-DASHBOARD-008 (NICE) — Real OEE composite (A * P * Q)

`req~dashboard-008~1`

The dashboard OEE KPI **shall** display the live OEE composite
(Availability * Performance * Quality, the Vorne / OEE Industry
Standards formulation), each component derived from real model signals:
Availability from the fraction of equipment in a running state,
Performance from the live throughput vs the nominal target, Quality
from the average checkpoint pass rate. The view **shall not** compute
the formula itself; the model exposes the decomposition through
`ProductionModel::oeeSnapshot()` and the presenter forwards the
composite onto `WorkUnitViewModel::oeePct` so the figure is auditable.

Verified by: DashboardPresenterTest.ForwardsModelOeeIntoViewModel
(model OEE composite reaches the view's ViewModel unchanged).

Needs: utest

### REQ-DASHBOARD-009 (SHOULD) — Demo fault-inject control + sparkline visibility

`req~dashboard-009~1`

The Dashboard control panel **shall** expose an "Inject Fault" button
that, when clicked by a user holding at least the Maintenance role,
drives the system to the ERROR safe-state via the fault-injection path
(Boost.SML SystemStateMachine -> locked ERROR -> P1 ISA-18.2
system-error alarm), producing the full visible recovery arc (alarm
lights UNACK in the AlertsPanel, operator acknowledges, system resets).
The button **shall** be insensitive (but visible) for Operator-role
users, consistent with the `canResetSystem` / `canCalibrate` gating
policy (REQ-DASHBOARD-005, ADR-0006).

The fault path is a simulator-only concern and **shall NOT** be added
to the `ProductionModel` interface; the composition root injects it as
a callback (`DashboardPresenter::setFaultInjector`) so the presenter
never depends on the concrete model type (ADR-0001).

The per-checkpoint TrendChart sparklines on the single-station
Dashboard **shall** be visible by default; the `setCompact(true)` path
used by multi-station layout **shall** continue to hide them so the
per-pane width budget (REQ-DASHBOARD-006) is not violated.

Verified by: DashboardPageTest.InjectFaultButtonConfirmedCallsInjector,
InjectFaultCancelledDoesNotFireInjector,
InjectFaultButtonGatedByRole_OperatorCannotInject,
SparklineVisibleInSingleStationMode, SparklineHiddenInCompactMode.

ADR: 0001, 0006

Needs: utest

---

## HISTORIAN — Time-series persistence

### REQ-HISTORIAN-001 (MUST) — SQLite 3-tier schema

`req~historian-001~1`

Telemetry **shall** be persisted in a SQLite store with raw (1s),
1-minute, and 1-hour aggregation tiers.

Verified by: SqliteHistoryStoreTest.

Needs: utest

### REQ-HISTORIAN-002 (SHOULD) — Batched writes

`req~historian-002~1`

Telemetry writes **shall** batch (default 32 rows or 5 s,
whichever first) to keep IO load bounded.

Verified by: HistorianBridgeTest.

Needs: utest

### REQ-HISTORIAN-003 (SHOULD) — Automatic tier demotion

`req~historian-003~1`

A background maintenance loop **shall** periodically demote raw
samples older than 60 s into 1-minute aggregates, and 1-minute
older than 60 m into 1-hour aggregates.

Verified by: HistorianMaintenanceTest.

Needs: utest

### REQ-HISTORIAN-004 (SHOULD) — Degraded-open

`req~historian-004~1`

If the historian DB fails to open (read-only filesystem,
corruption), the rest of the application **shall** keep running
without the History tab. A log warning **shall** explain why.

Verified by: HistorianDegradedOpenTest (failed init -> inert store).

ADR: 0007 (Historian Degraded Open).

Needs: utest

### REQ-HISTORIAN-005 (SHOULD) — History page correctness and legibility

`req~historian-005~1`

The History page **shall** clear each TrendChart's internal data
buffer before populating it on every refresh cycle, so that a range
change (e.g. "Last 7 days" -> "Last hour") does not display stale
samples from the previous query window once the ring wraps. The
TrendChart widget **shall** expose a `clear()` method that resets
its ring buffer counters and triggers a redraw.

Chart entity labels **shall** be 1-based ("Checkpoint 1, 2, 3" /
"Line 1, 2, 3"), not zero-indexed engineering view, because the
History page is operator-facing.

The History page footer **shall** render "No data yet" when the
historian holds zero samples, and a thousands-grouped sample count
("Total samples: 12,345") otherwise, so a cold-start state is not
misread as "Total samples: 0" failure-mode and a 7-digit count
stays scannable.

The TrendChart widget's latest-value overlay **shall** respect a
caller-configurable unit suffix (`setUnit`), defaulting to `%` so
existing callers keep current rendering. This unblocks future
series whose value is not a percentage without re-templating the
widget.

A `Gtk::Spinner` **shall** animate next to the Refresh button while
a manual refresh is in flight. Auto-refresh ticks **shall NOT**
animate the spinner (silent 5-second background re-queries should
not strobe the toolbar).

Verified by: TrendChartTest (ConstructionLeavesPointCountAtZero,
AddPointIncrementsPointCount, ClearResetsPointCount,
AddPointAfterClearBuildsFromZero, ClearOnEmptyChartIsNoop,
SetUnitDoesNotCrashAndChartStaysUsable), HistoryPageTest
(FooterShowsNoDataWhenTotalIsZero,
FooterShowsThousandsCountWhenNonZero,
RefreshReQueriesAllSixSeries).

ADR: 0001 (MVP layer boundaries -- fix stays in View), 0014
(Result at boundaries -- no new error surface needed).

Needs: utest

### REQ-HISTORIAN-006 (SHOULD) — Per-checkpoint chart visibility toggle

`req~historian-006~1`

The History page **shall** render a row of labelled `Gtk::CheckButton`
toggles, one per TrendChart (three quality: "Checkpoint 1/2/3"; three
supply: "Line 1/2/3"), all checked by default. Toggling a checkbox
**shall** immediately show or hide the corresponding TrendChart so an
operator can reduce visual noise by hiding charts irrelevant to the
current task.

The auto-refresh cycle and the `reader_.query()` calls **shall**
continue for hidden charts, so a re-enabled chart shows current data
without a manual Refresh. Toggle state is session-only and is not
persisted. A refresh **shall NOT** force a hidden chart back to
visible.

ADR: 0001 (MVP layer boundaries -- implementation stays entirely in
the View; no presenter split triggered), 0014 (Result at boundaries --
no new error surface introduced).

Verified by: HistoryPageToggleTest (AllChartsVisibleByDefault,
HidingQualityChartMakesItInvisible, HidingSupplyChartMakesItInvisible,
ReshowingChartRestoresVisibility, RefreshDoesNotForceHiddenChartVisible,
AllSixQueriesStillRunWhenChartHidden).

Needs: utest

---

## I18N — Internationalisation

### REQ-I18N-001 (SHOULD) — Gettext-backed translations

`req~i18n-001~1`

User-visible strings **shall** flow through `_()` / `gettext()`.
A live language switch in Settings **shall** rebuild the page set
and re-translate all `.ui`-declared strings.

Verified by: I18nTest.

Needs: utest

---

## INSPECTION — Edge AI quality inspection (opt-in)

### REQ-INSPECTION-001 (NICE) — ONNX Runtime inference

`req~inspection-001~1`

When BUILD_ML_CLASSIFIER is on AND the model + labels files exist
on disk, the application **shall** expose a Quality Inspection
tab that classifies user-supplied images via an ONNX model.

Verified by: ImageClassifierTest, OnnxImageClassifierTest,
QualityInspectionPresenterTest.

### REQ-INSPECTION-002 (NICE) — Image preprocessing pipeline

`req~inspection-002~1`

Images **shall** be decoded (stb_image), resized + normalised to
the model's input tensor, and dispatched through the
classifier. Top-K predictions **shall** be returned with
confidence scores.

Verified by: ImageDecoderTest, ImageNetPreprocessorTest,
ImageNetLabelsTest.

---

## INTEGRATION — Backend protocols

### REQ-INTEGRATION-001 (MUST) — TCP backend

`req~integration-001~1`

The system **shall** support a TCP listener that emits telemetry
in JSON or CSV (configurable) to connected clients.

Verified by: TcpBackendTest.

Needs: utest

### REQ-INTEGRATION-002 (MUST) — MQTT v3.1.1 / v5.0

`req~integration-002~1`

The system **shall** publish telemetry over MQTT to a configured
broker and **shall** subscribe to inbound sensor topics. Both
v3.1.1 and v5.0 packet formats **shall** be supported.

Verified by: MqttClientTest, MqttPacketTest,
ProductionTelemetryBridgeTest, SensorIngestBridgeTest.

Needs: utest

### REQ-INTEGRATION-003 (MUST) — Modbus TCP master

`req~integration-003~1`

The system **shall** poll a configurable set of Modbus holding
registers at a configurable interval and surface the values as
equipment supply levels.

Verified by: ModbusBackendTest, ModbusClientTest,
ModbusPduTest, ModbusPollLoopTest, ModbusIngestBridgeTest.

Needs: utest

### REQ-INTEGRATION-004 (MUST) — OPC-UA server + client

`req~integration-004~1`

The system **shall** expose its production state via an OPC-UA
server (open62541) and **shall** ingest from an external OPC-UA
server when configured as a client.

Verified by: OpcUaBackendTest, Open62541ServerIntegrationTest,
Open62541ClientIntegrationTest, OpcUaIngestBridgeTest,
FactoryCommandSinkTest, FactoryNodeMapTest.

Needs: utest

### REQ-INTEGRATION-005 (SHOULD) — Backend health surfacing

`req~integration-005~1`

Every active backend **shall** report its connection state
(Connected / Disconnected / Connecting / Degraded) so the
sidebar BackendHealthBar can colour-code each row.

Verified by: BackendHealthPresenterTest, IntegrationManagerTest.

Needs: utest

### REQ-INTEGRATION-006 (MUST) — Wire parsers robust to adversarial input

`req~integration-006~1`

The wire parsers that consume bytes off the network -- Modbus MBAP/PDU
decoder, MQTT PUBLISH parser, MQTT variable-byte length decoder --
**shall** be continuously fuzz-tested under sanitizer instrumentation
(AddressSanitizer + UndefinedBehaviorSanitizer + libFuzzer coverage).

A misbehaving peer (PLC, broker, man-in-the-middle) sending arbitrary
bytes **shall not** crash the HMI, leak memory, or trip undefined
behaviour. Returning any error code is acceptable; corrupting the host
process is not.

Harnesses live in `fuzzers/`, opt-in behind `BUILD_FUZZERS=ON` (Clang
required; CMake auto-skips with a STATUS message on GCC). Seed corpora
under `fuzzers/corpus/<target>/`. See `fuzzers/README.md` for the
running protocol + smoke-run baseline numbers.

Verified by: `fuzz_modbus_decode`, `fuzz_mqtt_publish`,
`fuzz_mqtt_remaining_length` (built under `-DBUILD_FUZZERS=ON`).

Needs: utest

---

## PERF — Performance budgets

### REQ-PERF-001 (SHOULD) — Reproducible microbenchmarks on hot paths

`req~perf-001~1`

Hot paths the operator-visible budget depends on **shall** carry
reproducible microbenchmarks under `benchmarks/`, built behind an
opt-in `BUILD_BENCHMARKS=ON` flag using
[google/benchmark](https://github.com/google/benchmark) v1.9.0
(vendored via FetchContent).

The initial coverage is:
- `bench_alert_center` — `raise()`, `acknowledge()`, `snapshot()` at
  N=10/100/1000 active alarms (ISA-18.2 alarm-storm path)
- `bench_modbus_pdu` — `encodeReadRequest` / `decodeReadResponse` at
  qty=1/10/125 (master poll-loop path)
- `bench_config_parse` — cold-start `ConfigManager::initialize()` on
  a realistic config (ADR-0015 parser-swap accounting)

Reporting **shall** include p50 / p90 / p99 (not just mean / stddev)
because tail latency is the operator-budget contract; means hide the
worst-1% case behind the bulk of the distribution. The captured
baseline numbers live in `benchmarks/README.md`.

Verified by: `benchmarks/bench_alert_center`, `bench_modbus_pdu`,
`bench_config_parse` (built when `BUILD_BENCHMARKS=ON`).

Needs: utest

### REQ-PERF-002 (SHOULD) — Whole-program profile + regression baseline

`req~perf-002~1`

Beyond per-primitive microbenchmarks (REQ-PERF-001), the project
**shall** carry a whole-program CPU profile captured under a synthetic
operator workload, with a trimmed top-N report committed alongside the
source so regressions in the hot-path distribution are caught by file
diff.

The capture **shall** use Valgrind / callgrind rather than `perf`
because:

1. `perf` requires kernel access that WSL2 + GitHub-hosted CI runners
   do not grant by default.
2. callgrind is deterministic (basic-block instrumentation) where
   `perf` is sampling -- the diffable baseline depends on stability.
3. Valgrind is already on the project's CI gate; one more invocation
   is zero new infrastructure.

The trimmed top-50 report lives at
`scripts/perf/baseline.callgrind-annotate.top50.txt`; the capture
pipeline is `scripts/perf/capture-callgrind.sh` driven by
`scripts/perf/workload-baseline.txt`. The contract diffed across
releases is the `%` column on the top-N rows (per-function share of
total instructions retired) -- not the absolute counts, which depend
on workload byte-stream + libc version + dynamic-linker symbol count.

Verified by: `scripts/perf/baseline.callgrind-annotate.top50.txt`
(diff against a fresh run after any meaty merge).

---

## MULTISTATION — Primary / secondary deployment

### REQ-MULTISTATION-001 (MUST) — Multi-station opt-in

`req~multistation-001~1`

Multi-station mode **shall** be opt-in via
`ui.multistation_enabled` config flag. Default (off) **shall**
render the standard single-station Dashboard with no behavioural
change.

Verified by: ConfigManagerTest +
MainWindow::createDashboardPages branch coverage.

ADR: 0011 (Multi-station Support).

Needs: utest

### REQ-MULTISTATION-002 (MUST) — Side-by-side dashboards

`req~multistation-002~1`

When multi-station is on, the Dashboard tab **shall** host two
DashboardPage instances side by side, one for the primary
SimulatedModel and one for the secondary MirrorModel, each with
its own DashboardPresenter.

Verified by: MultiStationDashboardPageTest (two station columns +
title), MirrorModelTest (secondary model contract).

Needs: utest

### REQ-MULTISTATION-003 (MUST) — PrimaryToSecondaryBridge forwards equipment supply

`req~multistation-003~1`

The bridge **shall** subscribe to the primary model's equipment
status events and forward `setEquipmentSupplyLevel(id, level)`
to the secondary model, using matching 0-based ids.

Verified by: PrimaryToSecondaryBridgeTest.

Needs: utest

### REQ-MULTISTATION-004 (SHOULD) — PrimaryToSecondaryBridge forwards quality pass rate

`req~multistation-004~1`

The bridge **shall** subscribe to the primary model's quality
checkpoint events and forward `setQualityPassRate(id, rate)` to
the secondary model so the secondary dashboard's quality cards
track the primary in real time.

Verified by: PrimaryToSecondaryBridgeTest.

Needs: utest

### REQ-MULTISTATION-005 (MUST) — Bridge metrics on BackendHealthBar

`req~multistation-005~1`

The bridge **shall** appear in the BackendHealthBar with a
"Primary->Secondary" name, current Connected/Disconnected state,
and a forwarded-event counter in the metrics tooltip.

Verified by: PrimaryToSecondaryBridgeTest::metricsSummary.

Needs: utest

---

## PRODUCTS — Product catalogue

### REQ-PRODUCTS-001 (MUST) — Product CRUD

`req~products-001~1`

The system **shall** allow adding, editing, viewing, and
soft-deleting products. Code uniqueness **shall** be enforced
at insert time.

Verified by: ProductsPresenterTest, ProductsPageTest.

Needs: utest

### REQ-PRODUCTS-002 (SHOULD) — Async load + search filter

`req~products-002~1`

Product list loads + search filtering **shall** run off the GTK
main thread so the UI stays responsive on large catalogues.

Verified by: ProductsPresenterAsyncTest.

Needs: utest

### REQ-PRODUCTS-003 (SHOULD) — Load product recipe onto the line

`req~products-003~1`

The operator **shall** be able to load a product's recipe from the
Products page onto the production line. Loading a recipe **shall**
make the product the active work unit (fresh work-unit id, reset
progress + inspection counts, operation count from the recipe) and
apply each recipe checkpoint target onto the matching quality
checkpoint by name. A product with no recipe **shall** surface a
"no recipe defined" message rather than loading a silent default.

Verified by: ProductsPresenterTest (loadRecipe paths),
SimulatedModelTest (loadProduct), DatabaseManager recipes seed.

ADR: 0011 (Multi-station — secondary stays passive on load).

Needs: utest

### REQ-PRODUCTS-004 (SHOULD) — Create / edit a product recipe

`req~products-004~1`

The operator **shall** be able to create or edit a product's recipe from
the Products page: the operation count and a pass-rate target per quality
checkpoint. The editor **shall** present a field for every known
checkpoint even when the product has no recipe yet (existing values
preserved, missing ones defaulted). Saving **shall** validate the
operation count (>= 1) and each pass-rate target (0..100), persist the
recipe (replacing any prior checkpoint targets wholesale), and record an
audit event. Editing **shall** require product-edit permission.

Verified by: ProductsPresenterTest (saveRecipe validation + writer
routing, getRecipeForEditing merge), RecipeWriteIntegrationTest (real DB
upsert round-trip + wholesale target replacement).

ADR: 0011 (Multi-station — secondary stays passive on load).

Needs: utest

---

## QUALITY — Pass rate, alerts

### REQ-QUALITY-001 (MUST) — Pass-rate classification

`req~quality-001~1`

Each checkpoint pass rate **shall** be classified as Passing
(>=98%), Warning (>=90%), or Critical (<90%). The DashboardPage
quality card colour **shall** track the classification.

Verified by: DashboardPresenterTest, DashboardPageTest.

Needs: utest

### REQ-QUALITY-002 (SHOULD) — Alert center

`req~quality-002~1`

Persistent alerts (equipment offline, checkpoint below target)
**shall** appear in the sidebar AlertsPanel with timestamp +
acknowledge chrome. Resolved conditions **shall** be driven through
the alarm lifecycle (see REQ-ALARM-001), not silently dropped.

Verified by: AlertCenterTest.

Needs: utest

---

## ALARMS — ISA-18.2 alarm management

### REQ-ALARM-001 (SHOULD) — Alarm lifecycle (ISA-18.2 / IEC 62682)

`req~alarm-001~1`

Alarms **shall** follow a lifecycle distinguishing the process
condition (active / returned-to-normal) from operator acknowledgement.
A first occurrence **shall** be Unacknowledged-Active; the operator
acknowledging it **shall** make it Acknowledged-Active (still shown
while the condition holds). A condition returning to normal while
unacknowledged **shall** transition to Returned-to-Normal-Unacknowledged
and **shall remain visible** until acknowledged (a transient fault must
not vanish unseen); acknowledging it then resolves the alarm to history.
A re-activating condition **shall** re-alarm (back to
Unacknowledged-Active).

Verified by: AlertCenterTest (lifecycle state-machine cases),
AlertCenterModelIntegrationTest (model -> presenter -> AlertCenter
offline -> recovery -> acknowledge).

ADR: standards basis ANSI/ISA-18.2-2016, IEC 62682.

Needs: utest

### REQ-ALARM-002 (SHOULD) — Shelve with auto-expiry

`req~alarm-002~1`

The operator **shall** be able to temporarily shelve an alarm for a
specified duration. Shelving **shall** transition the alarm to the
Shelved state; the underlying process condition **shall** continue to
be tracked silently (raise / clear update an internal flag without
changing the visible state). When the deadline expires (driven by the
existing model tick, not a separate timer), the alarm **shall** auto-
unshelve, restoring UnackActive when the condition is still active or
RtnUnack when it cleared while shelved. The operator **shall** also be
able to release a shelved alarm manually before the deadline.

Verified by: AlertCenterTest (shelve / unshelve / auto-expiry /
condition-tracking-while-shelved cases). The shelve clock is injected
via a constructor parameter so the tests are deterministic without
sleeps.

ADR: standards basis ANSI/ISA-18.2-2016 (alarm shelving).

Needs: utest

### REQ-ALARM-003 (SHOULD) — Priority distinct from severity

`req~alarm-003~1`

Each alarm **shall** carry an ISA-18.2 priority (1 = Emergency, 4 = Low)
set by the producer at raise time, distinct from severity (which
describes the condition's badness rather than the operator's required
response speed). The active alarms list in the UI **shall** be ordered
by priority ascending (most urgent first), with ties keeping insertion
order. The presenter **shall** assign priority by alarm semantics:
system-fault Emergency, equipment-error / quality-critical High,
equipment-offline / quality-warning Medium, default Low.

Verified by: AlertCenterTest (`SnapshotIsOrderedByPriorityAscending`,
`EqualPrioritiesKeepInsertionOrder`).

Needs: utest

### REQ-ALARM-004 (SHOULD) — Alarm lifecycle audit journal

`req~alarm-004~1`

Every alarm lifecycle transition (RAISE, ACK, RTN, RESOLVE, SHELVE,
UNSHELVE, EXPIRE, REALARM) **shall** be journalled to the existing audit
log under the `ALERT` category when audit is wired. A refresh of an
already-active alarm (producer re-raises while state is unchanged)
**shall NOT** be journalled -- only genuine lifecycle transitions
matter. `AlertCenter` **shall** stay decoupled from `auth/*`: the
composition root supplies a `(action, key)` callback that translates
into an `AuditEvent`. The hook **shall** be optional so headless builds
and unit tests stay clean.

Verified by: AlertCenterTest (happy-path RAISE/ACK/RESOLVE, RTN-then-
ack, REALARM, SHELVE/UNSHELVE, EXPIRE auto-unshelve, no-journal-on-
refresh).

ADR: standards basis ANSI/ISA-18.2-2016 (alarm history / journal).

Needs: utest

### REQ-ALARM-005 (SHOULD) — Operator-visible Shelved inventory

`req~alarm-005~1`

`AlertCenter` **shall** expose `shelvedSnapshot()` returning the
currently-shelved alarms paired with the absolute deadline + the
precomputed `secondsRemaining` countdown. The list **shall** be sorted
by deadline ascending (most-imminent expiry first) so the panel renders
the operator-attention order natively, without re-sorting on each
redraw.

`secondsRemaining` **shall** clamp to zero for entries whose deadline
has already passed but have not yet been swept by `tick()` -- the panel
renders "EXPIRED" without juggling a negative duration.

The countdown is computed from the `NowFn` injected at construction so
unit tests drive deterministic deadlines without sleeps (same pattern
as REQ-ALARM-002).

**Phase 4b (now shipped)**: the GTK `AlertsPanel` renders Active +
Shelved as two distinct subsections in its Active view, top-to-bottom.
Each shelved row shows a countdown ("4:37 left" / "EXPIRED") in place
of the raise timestamp; the action row offers Unshelve only
(Acknowledge is not meaningful while shelved -- the alarm is out of
the active inventory by operator choice). The `snapshot()` API now
filters Shelved entries (Phase 4a had kept them in for UI compat);
the Shelved subsection header is suppressed when empty so an operator
with active-only alarms sees the same layout as before Phase 4b.

Verified by: AlertCenterTest (7 cases pinning empty inventory,
filtered inclusion, deadline-asc ordering, countdown tracks injected
clock, clamp-to-zero past deadline, swept after tick, deadline matches
shelve call) + AlertsPanelTest (7 cases on the
`AlertsPanel::formatCountdown` static helper: EXPIRED at zero,
EXPIRED on negative defensive input, pad rule for under-one-minute,
rollover into minutes, default 5-min shelve case, long-shelve stays
MM:SS, secs<10 zero-pad rule).

ADR: standards basis ANSI/ISA-18.2-2016 (shelved-alarm management
section) -- making the inventory operator-visible turns a working
shelve mechanism into a managed one.

Needs: utest

---

## STATE — Production lifecycle state machine

### REQ-STATE-001 (SHOULD) — Formal SystemState transition table

`req~state-001~1`

The production system top-level lifecycle (`SystemState::{IDLE, RUNNING,
ERROR, CALIBRATION}`) **shall** be expressed as a formal transition table
rather than ad-hoc state assignments scattered across command bodies.
Producer commands (`startProduction`, `stopProduction`, `resetSystem`,
`startCalibration`) **shall** route through the table so the transitions
are a single auditable artefact. State-change observers
(`onSystemStateChanged`) **shall** fire only on real transitions; an
idempotent command (e.g. `start` from `RUNNING`) **shall not** produce a
phantom view refresh.

Verified by: SystemStateMachineTest (transition + observer cases).

ADR: state machine implemented with Boost.SML (boost-ext/sml v1.1.11,
header-only, PIMPL so the template explosion stays in the .cpp).

Needs: utest

### REQ-STATE-002 (SHOULD) — Invalid transitions are dropped, not applied

`req~state-002~1`

Commands issued from a state with no matching transition (e.g.
`startCalibration` from RUNNING) **shall** be silently dropped: the
state machine **shall not** advance, and downstream observers
**shall not** fire. This replaces the Phase 1 permissive behaviour
where every command unconditionally re-assigned `currentState_`.

Verified by: SystemStateMachineTest (invalid-transition drop cases:
calibrate-from-running, start-from-calibration, start-from-error).

Needs: utest

### REQ-STATE-003 (SHOULD) — Safe-state on fault locks production out

`req~state-003~1`

A `Fault(reason)` event **shall** drive the state machine to ERROR from
any state and record the reason. While in ERROR, the only command that
**shall** advance the state is `reset()`; every other producer event
**shall** be dropped. The reason **shall** be exposed via
`lastFaultReason()` so consumers can carry it into operator-facing
surfaces, and **shall** be cleared when the SM leaves ERROR via reset.

The presenter **shall** raise a Critical ISA-18.2 alarm (see
REQ-ALARM-001) keyed `system-error` carrying the fault reason when the
state transitions into ERROR, and clear the alarm condition when it
leaves ERROR.

Verified by: SystemStateMachineTest (Fault entry + lock-out + reason
exposure + reset clears reason), DashboardPresenterTest
(`ErrorStateRaisesSystemErrorAlarmWithReason`).

Needs: utest

---

## SETTINGS — Runtime preferences

### REQ-SETTINGS-001 (SHOULD) — Theme switch (light / dark)

`req~settings-001~1`

The user **shall** be able to toggle light/dark theme at runtime
without restart. The change **shall** propagate to all
Cairo-drawn widgets (gauges, charts, donut, big-number cards).

Verified by: SettingsPageTest + DashboardPage::refreshThemedWidgets.

Needs: utest

### REQ-SETTINGS-002 (NICE) — Palette swap

`req~settings-002~1`

The user **shall** be able to switch between visual palettes
(Adwaita / Blueprint / Cockpit / CRT / Dracula / Nord / Paper /
Right) at runtime; structural palettes (Blueprint top-bar)
**shall** trigger a layout reload.

Verified by: Manual; covered by REQ-ARCH-006.

### REQ-SETTINGS-003 (NICE) — Log panel dock

`req~settings-003~1`

A log panel **shall** be available either as a bottom dock
(default layout) or a top-bar popover (Blueprint layout),
tailing the app log file at a configurable refresh rate.

Verified by: MainWindow::applyAutoRefresh manual.

---

## Living document

New features land with a new REQ-ID **before** the implementation
PR; the PR description references the REQ-ID. Existing
requirements get a "deprecated" annotation rather than deletion
when superseded. `TRACEABILITY.md` is regenerated by `oft trace`
as part of the PR that ships the change.
