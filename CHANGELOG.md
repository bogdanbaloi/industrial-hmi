# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
