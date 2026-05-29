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

Verified by: DashboardPage::setCompact behaviour;
MultiStationDashboardPage manual.

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
dismissible chrome. Resolved conditions **shall** clear their
alert automatically.

Verified by: AlertCenterTest.

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
