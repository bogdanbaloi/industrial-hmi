# Industrial HMI

Cross-platform industrial Human-Machine Interface in modern C++20.
Equipment monitoring, quality control, and product database management
for manufacturing-floor terminals -- shipped as both a GTK4 desktop UI
and a headless console binary, sharing one tested Model + Presenter
core.

[![CI](https://github.com/bogdanbaloi/industrial-hmi/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/bogdanbaloi/industrial-hmi/actions/workflows/ci.yml)
![Coverage](https://img.shields.io/badge/coverage-68%25-green)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![Platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20Windows-lightgrey)

---

## Highlights

- **Two front-ends, one core**: GTK4 desktop (`industrial-hmi`) +
  headless console (`industrial-hmi-console`) built from the same
  `main.cpp` via `#ifdef CONSOLE_MODE`. The console binary links
  **zero gtkmm** -- concrete proof that the `ViewObserver` abstraction
  is a real View-swap seam, not just marketing.
- **68% test coverage** verified by gcovr in CI on every PR, across
  9,442 instrumented lines and **76 ctest targets**: scenario-based
  E2E, async presenter tests with `Glib::MainLoop` pump, view-layer
  tests under real GTK via Xvfb, dialog dispatch via programmatic
  `response()`, plus integration tests that wire **real** components
  end-to-end (ingest bridge -> real model -> presenter -> AlertCenter;
  recipe load -> SQLite -> model) instead of mocks. Auth + presenter +
  integration backends sit between 75% and 100%; GUI dialogs sit at 0%
  by design (exercised via Xvfb smoke tests instead).
- **Every module ships as a standalone library** -- 9 README.md
  files under `src/` (auth / integration / presenter / historian /
  ml / gtk-view / model / core / config), each with API surface,
  SOLID-per-interface rationale, threading model, and an
  embedding-in-another-project guide. Drop the auth core into a Qt
  app, the integration backends into a CLI daemon, the historian
  into a logging tool -- zero rewrites, just composition root
  changes.
- **Staged Bootstrap** resolves the classic config-vs-logger
  chicken-and-egg with a two-phase logger; both front-ends share the
  same `Bootstrap` orchestrator (logger -> config -> configured
  logger -> i18n -> SQLite).
- **Fail-fast typed startup errors** -- `ConfigMissing`,
  `ConfigCorrupt`, `DatabaseInit`, `LoggerBootstrap` -- caught in
  `main()` and surfaced through a native reporter (MessageBoxW on
  Windows GUI, stderr elsewhere) with documented exit codes (0/1/2/3).
- **8 color palettes** with a thumbnail picker, mode locks (Tier 2
  palettes are single-mode by design), and **alternate window layouts**
  (Right Sidebar mirror; Blueprint top-bar with alerts/logs in
  popovers) swapped at runtime by parsing a different `.ui`.
- **Multi-station support** -- opt-in via `ui.multistation_enabled`,
  the HMI hosts two `ProductionModel` instances linked by a
  `PrimaryToSecondaryBridge` (an `IntegrationBackend` like every other
  protocol) and renders both stations side by side in a single
  `MultiStationDashboardPage`. First instance of the multi-station
  architecture; extends to N-station and to cross-process MQTT-backed
  bridges without view or presenter changes. See ADR-0011.
- **Product recipes drive the line** -- each product can carry a
  *recipe* (process spec) persisted in SQLite: how many operations a
  work unit completes plus a per-checkpoint pass-rate target. The
  operator hits **Load Recipe** on a product and `ProductionModel::
  loadProduct` pushes those values onto the dashboard -- the work-unit
  operation count and every quality card's target line come from the
  recipe, matched onto live checkpoints by name (not position) so the
  recipe is decoupled from checkpoint creation order.
- **11 UI languages** via gettext, runtime switch (no restart) -- the
  page tree is rebuilt so every `_()` and every `translatable="yes"`
  re-resolves against the new catalog.
- **Bidirectional industrial integration on every protocol that
  supports it** -- MQTT carries outbound telemetry and inbound sensor
  traffic on one socket (publish + subscribe bridges). OPC-UA runs
  the HMI as both server (SCADA reads our state) and client (we read
  PLC telemetry), the canonical "data hub" topology. TCP exposes a
  command surface for external supervisors. **Modbus primary** polls
  remote slaves / PLCs on a configurable register map and drives the
  same Model setters as every other inbound channel. Every channel
  surfaces a uniform `IntegrationBackend` pill in the dashboard's I/O
  panel, with state semantics tuned per protocol (Connecting vs.
  Connected reflects "service up" vs. "an actual peer talking").
- **Auth + audit log** -- optional username/password authentication
  with **Argon2id (libsodium) hashing**, three-role RBAC
  (Operator / Maintenance / Admin), and a SQLite-backed audit trail
  recording every operator-attributed action (login + logout,
  production state changes, equipment toggles, product CRUD).
  `LoginDialog` gates the main window when `auth.enabled`; admin-only
  `AuditLogPage` renders the trail with filter + 5 s auto-refresh.
  Role-based gating disables Calibration + Reset for Operators and
  the Add / Edit / Delete actions on Products. Off by default;
  opt-in via `auth.enabled` in `app-config.json`.
- **Time-series Historian with tiered downsampling** -- optional
  SQLite-backed persistence for quality pass rates, equipment
  supply levels, and system state transitions. `HistorianBridge`
  subscribes to the same `ProductionModel` signals every other
  consumer uses (one source of truth); batched INSERTs in WAL mode
  amortise the commit cost. **Three-tier schema** (raw / 1-min
  averages / 1-hour averages) with a `std::jthread` maintenance
  worker that demotes aged rows in one atomic transaction --
  industrial-historian pattern, query routes to the right tier
  based on the requested range. Dedicated **History page** renders
  six trend charts side by side (range picker: 1h / 24h / 7d).
  Off by default; opt-in via `historian.enabled` in `app-config.json`.
- **Cross-platform CI** on Ubuntu 24.04 (GCC 13 + pkg-config) and
  Windows MSYS2 CLANG64; clang-tidy + cppcheck gates every PR.

## Quick Start

### Linux (Ubuntu 24.04+)

```bash
sudo apt install cmake ninja-build g++ \
    libgtkmm-4.0-dev libsqlite3-dev libboost-dev \
    libgtest-dev libgmock-dev xvfb
cmake --preset release
cmake --build build/release -- -j$(nproc)
./build/release/industrial-hmi
```

### Windows (MSYS2 Clang64)

```bash
pacman -S mingw-w64-clang-x86_64-{toolchain,cmake,ninja,gtkmm-4.0,sqlite3,boost,gettext,gtest}
./build-windows.sh
./build/release/industrial-hmi.exe
```

### Headless console (any platform)

```bash
./build/release/industrial-hmi-console
# Type `help` for the command list, `quit` to exit. Pipe a script:
printf 'start\nstatus\nquit\n' | ./build/release/industrial-hmi-console
```

### Client scripts (Python)

The [`examples/`](examples/) directory ships a Python script per
backend role, so a customer / engineer / interview can exercise every
protocol end-to-end without a custom client:

```bash
pip install -r examples/requirements.txt    # paho-mqtt + asyncua + pymodbus
python examples/tcp_control.py              # walk SYSTEM + equipment switches via TCP
python examples/mqtt_subscribe_telemetry.py # tail outbound MQTT publishes
python examples/mqtt_publish_sensor.py 0 off# drive A-LINE off via inbound MQTT
python examples/opcua_read_state.py         # browse + read the OPC-UA address space
python examples/opcua_subscribe_equipment.py# live notifications via OPC-UA subscribe
python examples/modbus_slave_simulator.py   # host a Modbus TCP secondary, toggle registers
python examples/factory_simulation.py       # orchestrate all four protocols in parallel
```

The I/O panel pills on the dashboard mirror the script's state in
real time -- run an OPC-UA reader, the OPC-UA pill turns green; spin
up `modbus_slave_simulator.py` and the Modbus pill flips Connecting
-> Connected within one poll interval (1s default).

See [BUILD.md](BUILD.md) for full instructions, packaging, and i18n
catalog regeneration.

## Architecture

Model-View-Presenter with dependency injection, observer pattern, and
interface-based testing. The View can be swapped wholesale (GTK4 or
console) without touching Presenter / Model.

```
                 +-------------------+
                 |  ViewObserver     | <-- pure interface, GTK-free
                 +---------+---------+
                           ^
            +--------------+---------------+
            |                              |
   +--------+--------+            +--------+--------+
   | DashboardPage   |            |  ConsoleView    |
   | ProductsPage    |            |  (jthread       |
   | SettingsPage    |            |   stdin reader) |
   +--------+--------+            +--------+--------+
            ^                              ^
            |       sigc::signal /         |
            |       observer dispatch      |
            +--------------+---------------+
                           |
                +----------+----------+
                | DashboardPresenter  | <-- BasePresenter (mutex + observer list)
                | ProductsPresenter   |     no GTK, no SQL
                +----------+----------+
                           |
                  ProductionModel
                  ProductsRepository  <-- abstractions; tests inject mocks
                           ^
            +--------------+---------------+
            |                              |
   +--------+--------+            +--------+--------+
   | SimulatedModel  |            | DatabaseManager |
   | (RNG-driven)    |            | (SQLite + Asio) |
   +-----------------+            +-----------------+
```

### Staged Bootstrap

```
1. bootstrapLogger()      stderr-only, INFO -- so stages 2+ can warn
2. config.initialize()    JSON load; fatal on missing/corrupt
3. configureLogger()      level/path/rotation from config
4. config.applyI18n()     ConfigManager picks language; calls core/i18n
5. db.initialize()        SQLite open; fatal on failure
6. frontend.run()         GTK main loop OR console event loop
```

`src/core/Bootstrap.{h,cpp}` owns this sequence so both `main.cpp`
branches enter their front-ends from the same prepared state.

### Module documentation

Each `src/` module ships a standalone `README.md` that reads as a
library reference (architecture, SOLID per interface, API surface,
embedding guide, threading, testing). Recommended entry points if
you're evaluating the codebase:

- **[`src/auth/`](src/auth/README.md)** -- Authentication, RBAC,
  audit trail (Argon2id, three-role permission model, SQLite audit
  log with CSV export).
- **[`src/integration/`](src/integration/README.md)** -- Four
  network backends (TCP, MQTT 3.1.1/5.0, Modbus TCP, OPC-UA) +
  telemetry bridges + serializers.
- **[`src/presenter/`](src/presenter/README.md)** -- MVP backbone,
  ViewObserver pattern, RBAC integration, six concrete presenters.
- **[`src/historian/`](src/historian/README.md)** -- Time-series
  store with batch flush + retention policy.
- **[`src/ml/`](src/ml/README.md)** -- ONNX Runtime via plugin
  (dlopen workaround for ORT heap corruption), preprocessor +
  swappable classifier.
- **[`src/gtk/view/`](src/gtk/view/README.md)** -- GTK4 view layer
  (page registry, theme system, custom Cairo widgets, modal
  dialog lifecycle).
- **[`src/model/`](src/model/README.md)** -- MVP M layer
  (SimulatedModel + ProductionModel interface, DatabaseManager,
  Boost.Asio context).
- **[`src/core/`](src/core/README.md)** -- Shared utilities
  (Bootstrap two-phase logger, Result<T,E>, typed startup
  exceptions, i18n mechanism, TimeFormat).
- **[`src/config/`](src/config/README.md)** -- JSON config policy
  + compiled defaults + applyI18n pattern.

### Project layout

```
src/
  core/                 GTK-free utilities + i18n adapter
    Bootstrap           Staged startup orchestrator
    LoggerBase/Impl     Console / File / Composite / Null / Callback
    Result<T,E>         Monadic error type (Rust-inspired)
    StartupErrors       Typed CriticalStartupError hierarchy
    ExceptionHandler    safeExecute / ExceptionGuard helpers
    i18n                Pure gettext adapter (no glibmm)

  config/               Policy owner (loads JSON, applies i18n / theme)
    ConfigManager       Singleton facade; injectable Logger
    config_defaults.h   constexpr fallbacks

  model/                Data layer + abstractions
    DatabaseManager     SQLite + async Boost.Asio io_context
    SimulatedModel      Deterministic equipment/quality simulator
    ModelContext        Background I/O thread + signal_idle marshaling

  presenter/            MVP orchestration -- no GTK dependency
    DashboardPresenter / ProductsPresenter
    BasePresenter       Thread-safe observer registration
    AlertCenter         Severity-routed alert bus
    ViewObserver        Empty-default callback interface

  app/                  GTK Application bootstrap (separated from core)
    Application         Adopts Bootstrap, runs Gtk::Application

  gtk/view/             GTK4 UI layer
    MainWindow          Window, layout swap, language rebuild
    MainWindowKeyDispatch  Pure free function for F1..F11/Esc
    DialogManager       Themed dialog factory (virtual, mockable)
    ThemeManager        Base theme + palette stacking, mode locks
    pages/              Dashboard / Products / Settings
    widgets/            QualityGauge, TrendChart, AlertsPanel, LiveClock

  console/              Headless front-end (no gtkmm)
    ConsoleView         ViewObserver impl + jthread stdin reader
    InitConsole         Composition root for the console binary

  integration/          Format-agnostic data + telemetry backends
    Serializer          CSV / JSON concretes (interface-driven)
    IntegrationBackend  start / stop / isRunning lifecycle abstraction
    IntegrationManager  Composition root, per-backend exception tolerance
    TcpBackend          Line-protocol server over Boost.Asio
    TelemetryPublisher  Generic publish(topic, payload) interface
    TelemetrySubscriber Generic subscribe(filter, callback) interface
    MqttPacket          Hand-rolled MQTT 3.1.1 wire format
    MqttClient          MQTT client (full duplex on one socket, no paho)
    ProductionTelemetryBridge   Manufacturing -> MQTT outbound bridge
    SensorIngestBridge          MQTT -> Manufacturing inbound bridge
    opcua/              OPC-UA (BUILD_OPCUA_BACKEND=ON), open62541-backed
    modbus/             Modbus primary (BUILD_MODBUS_BACKEND=ON, default),
                        hand-rolled MBAP framing over Boost.Asio. Reader
                        interface + concrete TCP client + register map +
                        ingest bridge + jthread poll loop + IntegrationBackend
                        aggregate. Strategy pattern via ModbusReader; the
                        poll loop tests run against a FakeModbusReader (in-
                        memory) without opening a socket.
      OpcUaServer / Open62541Server   Server role (browseable address space)
      FactoryNodeMap                  Domain mapping for the server role
      OpcUaBackend                    IntegrationBackend facade (server)
      OpcUaClient / Open62541Client   Client role (subscribe to remote PLC)

  ml/                   Edge AI inference (BUILD_ML_CLASSIFIER=ON)
    Image / ImageDecoder        RGB POD + stb_image decoder facade
    Preprocessor / ImageNetPreprocessor   Resize / centre-crop / normalize
    ImageNetLabels      Class id -> label string loader
    Classification      {classId, label, confidence} POD
    ImageClassifier     Abstract interface + FakeImageClassifier double
    OnnxImageClassifier ONNX Runtime backend (pimpl, ORT out of public API)

assets/
  ui/                   GtkBuilder XML layouts (multi-layout)
  styles/               Base CSS + 8 palette overlays
  icons/  images/       App resources
  models/               imagenet_labels.txt (.onnx artifacts gitignored)

scripts/
  ml/                   Python pipeline: export / quantize / sanity / benchmark
  setup-onnxruntime.sh  Prebuilt ONNX Runtime download helper

po/                     gettext catalogs (11 languages)
config/                 app-config.json
cmake/                  FindOnnxRuntime.cmake
tests/                  76 ctest targets (see Testing section)
```

## Extensibility -- how to add X

Every extension point below is **localised to one or two files**.
The architecture pays this dividend: a recruiter reading the
codebase can see exactly where a new feature lands, and the
diffs stay reviewable.

### Add a new page to the GTK front-end

Cost: ~1 hour for a non-trivial page.

1. Build a ViewModel struct under `src/presenter/modelview/` (plain
   aggregate, one field per piece of state the view renders).
2. Add the corresponding callback to `src/presenter/ViewObserver.h`
   with an **empty default implementation** so existing views
   compile unchanged.
3. Subclass `BasePresenter` in `src/presenter/`, subscribe to the
   model signals you need in `initialize()`, transform to the
   ViewModel, call `notifyAll(...)`.
4. Subclass `Page` in `src/gtk/view/pages/`, implement
   `ViewObserver` with the callback from step 2, draw the
   ViewModel into widgets.
5. Register in `src/gtk/view/MainWindow.cpp::buildPages()` --
   one new `registerPage(...)` call. Gate by role if needed
   (the existing `AuditLogPage` / `UsersPage` registrations are
   the template).

Zero touches to the model, the auth core, integration backends,
or any other page. **The console front-end keeps running**
because it just doesn't override the new ViewObserver callback.

### Add a new presenter (no UI yet)

1. Define the ViewModel(s) under `src/presenter/modelview/`.
2. Subclass `BasePresenter`, take the model + collaborators by
   reference in the constructor (interfaces only, not concretes).
3. Wire from `MainWindow::buildPages()` (or any composition root
   that already builds presenters).

Test stays GTK-free: real presenter + real auth core + fake
`ViewObserver` recording calls. Pattern lives in every
`tests/*PresenterTest.cpp`.

### Add a new UI language

Cost: ~30 minutes plus translation time.

1. Create `po/<lang>.po` from `po/template.pot` (every translation
   tool -- POEdit, Crowdin, Lokalise -- handles this).
2. Translate the strings; the keys are the original English
   text inside `_(...)`.
3. Add the language to the dropdown in `SettingsPage::buildUi()`.
4. Add the language code to the locale list in
   `src/core/Bootstrap.cpp::detectLanguage()`.

No string changes anywhere else. The runtime language switch
already exists: change in Settings → page tree rebuilds → every
`_()` re-resolves against the new catalog.

### Add a new integration backend

Cost: ~3-5 hours depending on protocol complexity.

1. Subclass `IntegrationBackend` in `src/integration/<protocol>/`,
   implement `start() / stop() / state() / name() /
   metricsSummary()`.
2. Take `ProductionModel&` (or whatever subset the protocol
   needs) by reference in the constructor.
3. Run the I/O loop on a `std::jthread` or a Boost.Asio
   `io_context::run` worker -- never block the calling thread.
4. Wire from `main.cpp`: `manager.registerBackend(std::make_unique
   <YourBackend>(...))` -- one line.

The dashboard's I/O panel auto-discovers the new backend through
`IntegrationManager::backends()`; no UI change needed.

Reference templates in the codebase: `TcpBackend` (line-protocol
server, simplest), `MqttClient` (long-lived client connection),
`OpcUaBackend` (heavy protocol with both server + client roles).

### Add a new serialisation format

1. Subclass `Serializer` in `src/integration/`, implement
   `writeProducts` + `readProducts`.
2. Inject into the backend that needs it via the constructor.

No backend code changes. The existing `CsvSerializer` and
`JsonSerializer` are the templates.

### Add a new auth backend (e.g. LDAP, OAuth claim cache)

1. Subclass `UserRepository` in `src/auth/`, implement the CRUD +
   avatar methods.
2. Swap the concrete in `main.cpp`: replace
   `SqliteUserRepository` with your new one in the
   `AuthService` constructor.

Zero changes to `AuthService`, `LoginDialog`, `UsersPresenter`,
audit log, or any presenter. Tests prove it: every auth test
uses `MockUserRepository` for the same surface.

### Add a new storage backend (e.g. Postgres)

Same pattern as auth -- the repository interface is the seam.
Three places to add a Postgres concrete:

- `src/auth/PostgresUserRepository.{h,cpp}` (alongside SQLite)
- `src/model/PostgresProductsRepository.{h,cpp}` (alongside
  DatabaseManager)
- `src/historian/PostgresHistoryStore.{h,cpp}` (alongside
  SqliteHistoryStore)

Composition root in `main.cpp` chooses by config flag. The auth
service, presenters, bridges, history page never know which
backend they're talking to.

### Add a new audit event verb

1. Emit the event from wherever the action lands (presenter,
   service). Use the existing `AuditEvent` shape:
   `{category, action, details, result, ...}` -- no new types.
2. Add the verb string to `kActionList` in
   `src/gtk/view/pages/AuditLogPage.cpp` (alphabetical, drives
   the Action filter dropdown).
3. Test the row format in
   `tests/UsersPresenterTest.cpp` (the 12 audit-format tests
   are the template).

No schema migration. The audit log table is already verb-
agnostic.

### Add a new front-end (Qt, web, REST)

This is what the project is built for. The architecture promise:

1. Implement `ViewObserver` in your toolkit (Qt: a `QObject`
   subclass; web: a websocket endpoint serialising ViewModels
   as JSON; REST: a polling adapter).
2. Wire your presenters from a new composition root -- copy
   `src/main.cpp` minus the GTK-specific bits, replace
   `MainWindow` construction with your toolkit's window.
3. Done. Auth, model, integration, historian, presenters --
   every module under `src/` works unchanged.

The console front-end (`industrial-hmi-console`) is the
existence proof: it links **zero gtkmm** and uses the same
presenters as the GTK binary.

---

## Test Strategy

Coverage is measured by **gcovr** on the Ubuntu CI job and posted at
the top of every PR's Actions run. Currently **68% across 9,442
instrumented lines** (auth, presenter, and integration backends sit
between 75% and 100%; GUI dialogs sit at 0% by design -- they're
exercised via Xvfb-backed smoke tests instead), achieved by combining
several testing styles instead of one monoculture:

| Category | What it covers | Examples |
|---|---|---|
| **Unit tests** | Pure C++ logic, no GTK | `ResultTest`, `LoggerImplTest`, `I18nTest`, `DatabaseManagerTest` |
| **Presenter tests** | Presenter <-> Model contracts via gmock | `DashboardPresenterTest`, `ProductsPresenterTest` |
| **Async presenter tests** | Boost.Asio `io_context` -> `signal_idle` marshaling | `ProductsPresenterAsyncTest` (drives a `Glib::MainLoop`) |
| **View-layer tests** | Real GTK widget tree under Xvfb | `DashboardPageTest`, `ProductsPageTest`, `SettingsPageTest`, `MultiStationDashboardPageTest`, `ToastTest` |
| **Real-component integration** | Several real classes wired together, no mocks | `IngestBridgeRealModelIntegrationTest`, `AlertCenterModelIntegrationTest`, `RecipeLoadIntegrationTest` |
| **Layout-regression guard** | Measured widget min-width vs. a budget | `DashboardPageTest.CompactPaneFitsMultiStationWidthBudget` (multi-station sidebar can't re-clip) |
| **Dialog dispatch tests** | DialogManager API via programmatic `response()` | `DialogManagerTest` (11 cases) |
| **Refactor-driven tests** | Logic extracted from GTK glue for pure testing | `MainWindowKeyDispatchTest`, `DialogHelpersTest`, `UserEditValidationTest` |
| **Scenario tests** | Full Model -> Presenter -> View pipeline via stdin/stdout | 10 scenarios piping commands through the console binary |

The **coverage CI job** also boots the GTK binary briefly under Xvfb,
sends F1 to open the AboutDialog, Escape to close, then lets a
`HMI_EXIT_AFTER_MS` timer fire `Gtk::Application::quit()` so atexit
runs and gcov flushes `.gcda` -- which is why MainWindow / pages /
widgets ever leave 0%.

A few methods (`showAddProductDialog`, `showEditProductDialog`) are
intentionally **not** unit-tested because they call
`Gtk::Dialog::set_titlebar` from a path that requires a fully-started
`Gtk::Application` (the `startup` signal must have fired) -- which
can't be done from a unit-test fixture without spinning a main loop
that would deadlock the test. Coverage for them comes from the
scenario suite + the running app itself; the parent-null defensive
fallback in production code (`dynamic_cast<Gtk::Window*>(get_root())`
-> parentless dialog constructor) is exercised by construction. This
is documented in code, not skipped silently.

### Running tests

```bash
cmake --preset debug -DBUILD_TESTS=ON
cmake --build build/debug
cd build/debug && xvfb-run ctest --output-on-failure
```

On Linux all 76 targets are green; on Windows MSYS2 we run the same
suite minus a few view-layer tests that need a live `Gtk::Application`
context (skipped via runtime check, not silenced).

### Selected test binaries

| Binary | Scope | Tests |
|---|---|---|
| `test_result` | Result\<T, E\> monadic operations | 22 |
| `test_logger_impl` | FileLogger rotation, Composite, Null, Callback | 23 |
| `test_database_manager` | SQLite CRUD, search, soft delete | 12 |
| `test_dashboard_presenter` | Mock model, signal routing, state machine | 29 |
| `test_alert_center` | Severity routing, dismiss, resolved history | 26 |
| `test_settings_page` | Handlers + sync guard + palette mode-lock | 25 |
| `test_dialog_manager` | Show* methods + programmatic response | 11 |
| `test_main_window_key_dispatch` | F1..F11/Esc dispatcher | 13 |
| `test_products_page` | Delete confirm + observer + CSV export | 12 |
| `test_products_presenter_async` | async addProduct/updateProduct/deleteProduct | 5 |
| `test_i18n` | gettext adapter (forceLanguage / resolveLocaleDir) | 7 |

Plus 10 scenario tests under `tests/scenarios/*.txt` -- each pipes
commands into `industrial-hmi-console` and diffs stdout against a
`*.expected` golden file. The runner
(`tests/scenarios/run-scenario.cmake`) strips logger timestamp lines
so only structural events participate in the byte-exact comparison.

## Two front-ends, one core

```bash
# GTK desktop binary
./build/release/industrial-hmi

# Headless console binary -- same Bootstrap, same Presenter, same DB
./build/release/industrial-hmi-console
```

Both binaries share `main.cpp` via an `#ifdef CONSOLE_MODE` switch and
link the same Model + Presenter + Bootstrap libraries. The console
binary links **zero gtkmm**:

```bash
# Linux
ldd ./build/release/industrial-hmi-console | grep -E 'gtk|gdk|glibmm'
# (empty -- no GTK runtime dependency)

nm -D ./build/release/industrial-hmi-console | grep -E 'gtk_'
# (empty -- no GTK symbols)
```

The console front-end exists not as a fallback but as a **swap proof**:
it forces the View seam to be honest. If the presenter ever leaks GTK
into its API, the console binary won't link. CI catches it.

## Integration Layer (industrial framework, not just an HMI)

The same architectural discipline that lets the GTK and console front-ends
coexist also makes this codebase a **deployable framework**, not just a
manufacturing HMI demo. Every long-lived I/O channel implements
`IntegrationBackend`; every messaging-style sink implements
`TelemetryPublisher`; the domain logic is a separate "bridge" class.
Wiring three together is two lines in `main.cpp`.

### Architecture

```
                +-------------------------+
                |   IntegrationBackend    |   (lifecycle interface)
                +-------------------------+
                            ^
              +-------------+-------------+
              |                           |
   +---------------------+    +---------------------------+
   |    TcpBackend       |    |       MqttClient          |
   |  (line protocol     |    |  (MQTT 3.1.1 hand-rolled  |
   |   over TCP)         |    |   wire format, no paho;   |
   |                     |    |   one socket, full duplex)|
   +---------------------+    +---------------------------+
                                       ^   ^
                            also implements |
                                       |   |
                +----------------------+   +----------------------+
                |                                                  |
        +---------------------+                       +-----------------------+
        |  TelemetryPublisher |                       |  TelemetrySubscriber  |
        |   (abstract publish)|                       |   (abstract subscribe)|
        +---------------------+                       +-----------------------+
                ^                                                  ^
                | calls publish()                                  | calls subscribe()
                |                                                  |
   +-------------------------------+              +-----------------------------+
   |  ProductionTelemetryBridge    |              |    SensorIngestBridge       |
   |  (model events -> topics;     |              |  (topics -> Model setters;  |
   |   outbound telemetry)         |              |   inbound field traffic)    |
   +-------------------------------+              +-----------------------------+
                ^                                                  |
                | subscribes signals                               | mutates
                |                                                  v
                              +---------------------+
                              |   ProductionModel   |   (single source of truth)
                              +---------------------+
```

One MQTT socket carries **both directions**, the way every real-world
library does it (paho, AsyncMQTT5, mosquitto-c). SoC lives in the
bridge layer instead: `ProductionTelemetryBridge` translates Model
events into outbound publishes, `SensorIngestBridge` translates
inbound publishes into Model mutations. Either side can be deleted or
swapped without touching the other.

### Vertical reuse: same shell, different domain

The MQTT client knows nothing about manufacturing. The bridges know
nothing about MQTT. To deploy this framework in another vertical you
write **one** new bridge class against the same `TelemetryPublisher`
(or `TelemetrySubscriber` for inbound):

| Vertical                | Domain model            | Bridge          | Publisher reuse |
|-------------------------|-------------------------|-----------------|-----------------|
| Manufacturing (default) | `ProductionModel`       | `ProductionTelemetryBridge` | Ships in repo |
| Pharma / lab            | `LabInstrumentModel`    | `LabTelemetryBridge`        | MqttClient unchanged |
| Smart-building          | `HvacModel`             | `HvacTelemetryBridge`       | MqttClient unchanged |
| Energy / SCADA          | `BreakerModel`          | `BreakerTelemetryBridge`    | MqttClient unchanged |

A bridge is typically 50-100 lines of pure callback plumbing. The MQTT
wire-format code, connection lifecycle, work-guard / heartbeat machinery,
and CONNACK error handling all stay shared.

### Configuration (opt-in per deployment)

Both backends are **disabled by default** -- a fresh install never opens
a port or dials out to a broker. Operators turn them on via
`config/app-config.json`:

```json
"network": {
  "tcp": {
    "enabled": true,
    "port": 5555
  },
  "mqtt": {
    "enabled": true,
    "broker_host": "broker.example.com",
    "broker_port": 1883,
    "client_id": "factory-42",
    "topic_prefix": "factory-42/line-A",
    "subscriber": {
      "enabled": true,
      "topic_prefix": "factory-42/sensors"
    }
  },
  "opcua": {
    "enabled": true,
    "port": 4840,
    "client": {
      "enabled": true,
      "endpoint": "opc.tcp://plc-1.factory-42.local:4840"
    }
  }
}
```

The publish-side topic prefix and the subscribe-side prefix live in
separate keys so a deployment can keep its outbound telemetry
namespace distinct from the inbound sensor namespace -- e.g.
`factory-42/line-A/*` for everything we publish, `factory-42/sensors/*`
for everything we listen to.

### TCP backend protocol

Line-oriented text, mirrors the console binary's command set so the
same shell scripts drive either:

```bash
$ printf 'status\nproducts\nquit\n' | nc localhost 5555
{"state":"running","running":true}
6
{"productCode": "PROD-001", "name": "Product A", ...}
{"productCode": "PROD-002", ...}
...
BYE
```

Supported commands: `status`, `products`, `eq <id> on|off`,
`production start|stop|reset`, `help`, `quit`.

### MQTT backend topics (outbound)

Hand-rolled MQTT 3.1.1 client over plain TCP. Outbound side publishes
manufacturing telemetry on every Model event, schema configurable via
`topic_prefix`:

| Topic                                 | Payload example         |
|---------------------------------------|-------------------------|
| `<prefix>/state`                      | `running`               |
| `<prefix>/equipment/<id>/state`       | `ok` / `fault`          |
| `<prefix>/quality/<id>/rate`          | `98.5`                  |

Subscribe with `mosquitto_sub -h broker.example.com -t 'factory-42/#' -v`.

### MQTT subscriber (inbound)

The same `MqttClient` also listens for inbound traffic from field
sensors or supervisory systems. `SensorIngestBridge` translates each
incoming PUBLISH into a `ProductionModel` mutation, so the dashboard
reflects external state without any front-end-specific wiring.

Default topic schema (configurable via `subscriber.topic_prefix`):

| Topic                                          | Accepted payloads                       | Model effect |
|------------------------------------------------|-----------------------------------------|--------------|
| `<sensor_prefix>/equipment/<id>/state`         | `on`, `off`, `1`, `0`, `true`, `false`  | `setEquipmentEnabled(id, ...)` |

Payload parsing is case-insensitive, whitespace-trimmed. Unknown
payloads are dropped silently -- a misbehaving sensor must not be
able to crash the HMI.

End-to-end demo (Linux / WSL / MSYS2):

```bash
mosquitto -p 1883 &                          # local broker
./industrial-hmi                             # MQTT pill flips to green
mosquitto_pub -t industrial-hmi-sensors/equipment/0/state -m off
# A-LINE switch flips off on the dashboard; presenter -> view loop
# carries the change without any code path specific to MQTT.
```

### Why hand-rolled MQTT?

| Decision                  | Rationale |
|---------------------------|-----------|
| No paho-mqtt-cpp dep      | Zero new system packages; CI install time unchanged |
| MQTT 3.1.1 only           | Full 5.0 properties + shared subscriptions out of scope |
| Single full-duplex client | One socket, two roles -- matches paho / AsyncMQTT5; SoC kept at the bridge layer (publish bridge + subscribe bridge are separate classes) |
| QoS 0                     | Industrial telemetry is transient; QoS 1/2 would need state machines for marginal benefit |
| Plain TCP, no TLS         | Production deployments tunnel through stunnel; wire format unchanged |

Coverage: `MqttPacketTest` (48 cases) verifies byte-exact wire format
for CONNECT / CONNACK / PUBLISH / SUBSCRIBE / SUBACK / UNSUBSCRIBE /
UNSUBACK / PINGREQ / DISCONNECT against the spec;
`MqttClientTest` (14 cases) drives the client against an in-process
mock broker, both publish and subscribe round-trips;
`ProductionTelemetryBridgeTest` (8 cases) and
`SensorIngestBridgeTest` (6 cases) verify the two domain bridges in
isolation against a fake `TelemetryPublisher` / `TelemetrySubscriber`.

### OPC-UA backend (industrial protocol, bidirectional)

OPC-UA is the standard industrial automation protocol -- Siemens,
Beckhoff, Rockwell PLCs all expose telemetry through it natively. The
HMI plays **both** roles depending on how it sits in the topology:

- **server**: expose `ProductionModel` state for a supervisory layer
  (SCADA / MES) to subscribe to. The third backend in the I/O panel.
- **client**: dial a PLC's OPC-UA endpoint, subscribe to its nodes,
  feed values into the model. A separate pill in the I/O panel.

Each role is opt-in via config, so a deployment can run either alone
or both at once. The "data hub" pattern -- HMI as a relay between
field-bus PLCs and supervisory dashboards -- is the canonical
industrial topology this enables.

#### Server role (outbound, browseable address space)

Exposes `ProductionModel` state as a browsable OPC-UA address space
rooted at `Objects/Factory/`:

```
Objects/Factory/
  State                                Int32   (SystemState enum)
  EquipmentLines/
    Line<id>/{Status, SupplyLevel, Message}
  QualityCheckpoints/
    Checkpoint<id>/{Name, Status, PassRate, UnitsInspected, ...}
  WorkUnit/
    {Id, ProductId, CompletedOperations, TotalOperations}
```

Architecture is interface-first (4 pure abstracts in
`src/integration/opcua/`):
- `OpcUaServer` -- lifecycle + typed write surface
- `OpcUaNodeMap` -- strategy: domain state -> address-space writes
- `OpcUaCommandSink` -- inbound method dispatch (Phase 3)
- `OpcUaConfig` -- value type for endpoint settings

Concrete impls:
- `Open62541Server` -- wraps the open62541 v1.5.4 C stack via pimpl
- `FactoryNodeMap` -- manufacturing-domain mapping (replaceable for
  pharma / energy / smart-building deployments)
- `OpcUaBackend` -- facade: composes the above behind
  `IntegrationBackend` so `IntegrationManager` orchestrates OPC-UA
  identically to TCP / MQTT

#### Client role (inbound, subscribe to a PLC)

Inverse direction: HMI dials a remote OPC-UA endpoint (real PLC,
simulator, or our own server on loopback for testing) and creates
monitored items on its nodes. The same `OpcUaServer` write surface
that the server role uses to push our state is mirrored by typed
subscribe callbacks on the client side:

```
remote OPC-UA server          Open62541Client                ProductionModel
(PLC / simulator / loopback)        |                              ^
   |                                | subscribe(path, callback)    |
   | DataChangeNotification -----> | -- typed dispatch --> bridge ->|
```

The client is itself an `IntegrationBackend` so it shares the I/O
panel pill semantics with every other backend:

- `OpcUaClient` (abstract) -- subscribe API + IntegrationBackend
  lifecycle. Mirror of `OpcUaServer`.
- `Open62541Client` (concrete) -- wraps `UA_Client*` via pimpl. One
  `UA_Client_run_iterate` worker thread drains Publish responses;
  pre-start `subscribe()` calls are queued and replayed once the
  session is up (same pattern as `MqttClient::subscribe`).

#### Build + coverage

Build is opt-in via `-DBUILD_OPCUA_BACKEND=ON`; FetchContent pulls
open62541 and statically links it (~2.4 MB binary contribution). The
host binary builds and runs unchanged when the flag is off.

Coverage: `OpcUaBackendTest` + `FactoryNodeMapTest` (12 mock-based
unit tests, no open62541 dep) plus two integration tests that link
the real C stack and run server + client on loopback --
`Open62541ServerIntegrationTest` validates the address-space write
side, `Open62541ClientIntegrationTest` validates monitored-item
dispatch + the subscribe-before-start / subscribe-after-start /
lifecycle state matrix.

## Time-series Historian

Optional persistent storage for the scalar telemetry the model
publishes -- quality pass rates, equipment supply levels, system
state transitions. Off by default; flip `historian.enabled = true`
in `app-config.json` to wire it up.

```
ProductionModel (signals)
        │
        ▼
HistorianBridge       <-- subscribes to existing model callbacks,
        │                 no new model surface
        │ batched
        ▼
HistoryWriter  (abstract)        HistoryReader  (abstract)
        △                              △
        └──────────┬───────────────────┘
                   │
            SqliteHistoryStore
            (WAL + (field, entity, ts) compound index)
                   │
        ┌──────────┴──────────┐
        ▼                     ▼
   bridge writes        HistoryPage reads
                        (range picker + 6 charts)
```

Design points worth calling out:

- **One source of truth**. The bridge reuses the same
  `ProductionModel::onEquipmentStatusChanged` /
  `onQualityCheckpointChanged` / `onSystemStateChanged` signals every
  other consumer (presenters, MQTT publisher, OPC-UA server) already
  subscribes to. Persistence becomes "another observer," not a model
  refactor.
- **Writer + Reader split** (ISP). Bridges only see
  `HistoryWriter` (`write(span<HistoryRecord>) -> n_accepted`); the
  UI only sees `HistoryReader` (`query(field, entity, range)`).
  `SqliteHistoryStore` happens to implement both; consumers depend
  on the surface they need.
- **WAL mode + batched INSERT in one transaction**. The bridge holds
  a small in-memory queue and flushes on size cap (default 32 rows)
  or age cap (default 5 s) -- whichever comes first. Concurrent
  readers don't block the writer; the History page can query while
  the bridge is mid-flush.
- **Three-tier schema with automatic downsampling**.
  `samples` (raw, ~1s cadence, last 1h), `samples_1m` (1-minute
  averages, last 24h), `samples_1h` (1-hour averages, archive). All
  three share the same `(field, entity, ts)` compound index and
  `(ts, field TEXT, entity, value REAL)` column shape; the only
  difference is row density. `HistorianMaintenance` is a
  `std::jthread` worker that sweeps every minute -- raw rows older
  than 1h fold into minute buckets via
  `INSERT INTO samples_1m SELECT (ts/60000)*60000, field, entity,
  AVG(value) ... GROUP BY ...`, all inside one transaction with the
  matching DELETE so a reader never sees a row in both tiers (or
  in neither). The query path picks the coarsest tier with enough
  resolution for the range -- 300 pixels per chart, no point
  pulling 86 400 raw rows for a 24h zoom-out.
- **Graceful degradation**. If SQLite can't open the configured
  path (permission denied, read-only fs), main() logs a warning and
  drops the bridge + page -- the rest of the binary keeps running.
  Same opt-in degradation pattern as the ML and OPC-UA paths.

Coverage: `SqliteHistoryStoreTest` (16 tests -- roundtrip, batch,
index, limit, order, plus tiered-retention demotion atomicity,
bucket alignment, query routing per tier), `HistorianBridgeTest`
(5 fixture-based signal-to-row tests with a `FakeHistoryWriter`),
`HistorianMaintenanceTest` (5 worker tests -- raw -> minute, fresh
rows untouched, chained raw -> minute -> hour, jthread shutdown
deadlock smoke), `HistoryPageTest` (4 GUI tests asserting the page
queries all six series with the correct lookback window on
construction).

## Edge AI Inference

A second integration vertical: load a quantized neural network and
classify images on the same industrial PC the HMI runs on -- no
cloud round-trip, no GPU required. The story is built in two phases
that mirror the deployment-side workflow of a real Edge AI team.

**Phase 0 -- Model preparation pipeline (Python).** Under
`scripts/ml/`, four scripts produce the deployment artifact:

| Script | Purpose |
|---|---|
| `export_model.py` | Pull pre-trained MobileNetV2 weights from torchvision, trace the forward pass, write an FP32 `.onnx` file (opset 17) |
| `quantize_model.py` | Dynamic INT8 quantization of `MatMul` and `Linear` layers (no calibration data needed) |
| `sanity_check.py` | Cross-validate PyTorch FP32 against ONNX FP32 (`atol 1e-4`); compare INT8 top-1 + confidence delta against FP32 |
| `benchmark.py` | Warmup + 200 measurement iterations; reports p50/p90/p95/p99 latency with hostname + CPU metadata |
| `export_labels.py` | One-shot fetch of the canonical 1000-class ImageNet label list to `assets/models/imagenet_labels.txt` |

Phase 0 demonstrates the deployment-side knowledge that gets a model
ready for production: ONNX export, dynamic quantization (75% size
reduction on MobileNetV2), and a measurement methodology that reports
percentiles instead of averages.

**Phase 1 -- C++ inference layer.** Under `src/ml/`, an
`ImageClassifier` interface plus an ONNX Runtime backend:

```
src/ml/
  Image.h                       Decoded RGB pixel buffer (POD)
  ImageDecoder.{h,cpp}          stb_image facade for PNG / JPEG / BMP
  Preprocessor.h                Format-agnostic interface
  ImageNetPreprocessor.{h,cpp}  Resize 256 / centre-crop 224 / normalize
  ImageNetLabels.{h,cpp}        Class id -> label string (BOM tolerant)
  Classification.h              {classId, label, confidence} POD
  ImageClassifier.h             Abstract interface, throws on bad input
  FakeImageClassifier.h         Header-only test double for upstream tests
  OnnxImageClassifier.{h,cpp}   ORT-backed classifier (pimpl: ORT
                                headers stay out of public surface)
```

The interface design follows MVP: presenters depend on
`ImageClassifier&`, never on a concrete subclass. Swapping the runtime
(libtorch, TensorRT, OpenVINO) is a new subclass; nothing upstream
changes. `FakeImageClassifier` lets UI tests run without ONNX Runtime
in their link line.

The ORT-backed classifier is built behind a CMake option so the
existing build path stays unchanged when the Edge AI pieces are not
needed:

```bash
bash scripts/setup-onnxruntime.sh           # one-shot prebuilt download
cmake -S . -B build/release -G Ninja \
    -DBUILD_TESTS=ON \
    -DBUILD_ML_CLASSIFIER=ON \
    -DONNXRUNTIME_ROOT="$(pwd)/build/onnxruntime"
cmake --build build/release -- -j$(nproc)
ctest --output-on-failure -R 'ImageClassifierTest|OnnxImageClassifierTest'
```

Coverage: 7 `ImageClassifierTest` cases pin the contract every
concrete classifier must respect (sort by descending confidence,
honour `k`, throw on `k <= 0`, stable name); 3 `OnnxImageClassifierTest`
cases load the real INT8 model end-to-end (skip gracefully when the
artifact is absent so the test binary stays runnable everywhere).
The CI `ml-integration` job runs the Python pipeline + the C++ build
+ the integration test on every PR.

**Phase 2 -- HMI integration.** The inference layer wires into the
GTK front-end as a new Notebook tab:

```
src/presenter/
  QualityInspectionPresenter.{h,cpp}    Sync orchestrator: file path ->
                                        decode -> classify -> top-K ->
                                        ViewObserver callbacks
src/presenter/modelview/
  InspectionResultViewModel.h           top-K rows + source path + latency
src/gtk/view/pages/
  QualityInspectionPage.{h,cpp}         File picker, preview, results list
                                        with LevelBar confidence bars
```

Threading is the same pattern the rest of the app uses for async work:
each inspection runs on a `std::jthread` member of the page; observer
callbacks arrive on the worker thread and marshal back to the GTK main
loop via `Glib::signal_idle.connect_once`. Reassigning the jthread
joins the previous run, so concurrent fires serialise by construction.

`MainWindow::createAllPages()` only registers the tab when the
`BUILD_ML_CLASSIFIER` build option is on AND the model + labels are
on disk under `assets/models/`. Missing artefacts log a warning and
skip the tab; the rest of the UI keeps working. 6 presenter
GoogleTest cases pin the success / failure / cancellation paths.

## Tech Stack

| Layer | Technology |
|---|---|
| Language | C++20 (concepts, format, jthread, source_location, ranges) |
| UI | GTK4 / gtkmm-4.0, Cairo for custom widgets |
| Database | SQLite3 (in-memory, prepared statements) |
| Async I/O | Boost.Asio io_context with work guard, posted via std::jthread |
| Integration | TCP line protocol (Boost.Asio) + MQTT 3.1.1 hand-rolled client (full duplex, no paho dep) + OPC-UA via open62541 |
| Edge AI | MobileNetV2 INT8 ONNX (PyTorch export pipeline) + ONNX Runtime CPU EP, image decoding via stb_image |
| i18n | GNU gettext, custom adapter (no glibmm i18n macros) |
| Testing | GoogleTest + gmock (76 ctest targets) |
| Build | CMake 3.20+ with presets, Ninja generator |
| CI/CD | GitHub Actions (Ubuntu 24.04 + Windows MSYS2 CLANG64) |
| Coverage | gcovr (HTML + text + step-summary on every PR) |
| Static Analysis | clang-tidy (strict) + cppcheck |

## Palettes and Layouts

Three classes of visual customization, all from the Settings page:

**Theme** -- Dark or Light, toggles `.light-mode` on the main window.
All Cairo widgets (`QualityGauge`, `TrendChart`) query
`ThemeManager::isDarkMode()` at paint time so custom-drawn surfaces
match.

**Palette** -- optional CSS overlay loaded at
`GTK_STYLE_PROVIDER_PRIORITY_USER + 1`, redefining colors without
touching layout:

| Palette | Modes | Feel |
|---|---|---|
| Industrial | Dark + Light | Baseline (no overlay loaded) |
| Nord | Dark + Light | Polar / Snow Storm |
| Paper | Light only | Navy + white executive |
| Right Sidebar | Dark + Light | Mirror layout, teal accent |
| Dracula | Dark only | Purple / pink on slate |
| CRT | Dark only | Phosphor green on black, monospace |
| Blueprint | Dark only | Navy + cyan + cream, top-bar layout |
| Cockpit | Dark only | Mission control, heavy instrument bezels |

Mode locks are enforced in both directions: incompatible Dark/Light
radios are disabled with an explanatory tooltip, and picking a locked
palette auto-snaps the Theme.

**Layout** -- some palettes ship structurally different `.ui` files:
Right Sidebar mirrors the sidebar; Blueprint moves Alerts and Logs
into top-bar popovers. The swap happens via `MainWindow::reloadLayout`
(detach old root, parse new `.ui`, re-attach) so it's atomic and
state-preserving.

## Internationalization

11 languages: English, Deutsch, Espanol, Espanol (Mexico), Suomi,
Francais, Gaeilge, Italiano, Portugues, Portugues (Brasil), Svenska.

Selectable from the Settings page; persists across restarts via
`config/app-config.json`. The `"auto"` setting respects the OS
locale. The `core/i18n` adapter is a pure gettext wrapper -- it has
no glibmm dependency, so it ships in `objectsCore` (the shared
GTK-free library used by the console binary).

A live language switch tears down + rebuilds the page tree so every
`_()` call and every `translatable="yes"` GtkBuilder property
re-resolves against the new catalog. No restart needed.

## Configuration

Settings load from `config/app-config.json` with fallback to
compile-time defaults in `src/config/config_defaults.h`.

```json
{
  "i18n": { "language": "auto" },
  "logging": {
    "level": "INFO",
    "file": "logs/app.log",
    "max_file_size_mb": 5
  },
  "theme": "dark",
  "palette": "industrial"
}
```

Missing or corrupt config is **fatal at startup** (typed
`ConfigMissingError` / `ConfigCorruptError`), surfaced through the
native reporter and exit code 2 -- the binary refuses to silently fall
back to defaults because operating an HMI on hidden defaults is more
dangerous than refusing to launch.

## Design decisions

**MVP over MVC** -- Presenters have no GTK dependency and are fully
unit-tested with mock observers and mock model interfaces. The
console front-end is the structural proof that the seam holds.

**Two-phase logger** -- a stderr-only "bootstrap logger" exists for
the few hundred milliseconds it takes to load config; the configured
logger (level/path/rotation from JSON) replaces it once available.
Resolves the chicken-and-egg of "config wants to log warnings, but
logging policy is in config".

**Defensive dynamic_cast on dialog parents** -- every place that
constructs a child `Gtk::Dialog` from `get_root()` falls back to the
parentless overload if the cast returns null. Same pattern
`DialogManager::createMessageDialog` already used; applied to
`ProductsPage::showProductDetail` / `showAddProductDialog` /
`showEditProductDialog` to fix a latent null-deref + make those
methods tolerant of unrooted widget trees in tests.

**`std::vformat` over templated logging** -- the public `Logger::info`
is a thin templated wrapper that captures `std::source_location` via
a defaulted constructor parameter, then routes into a single
non-template `vformat` call. No code bloat per format-string variant.

**Async DB writes via Boost.Asio + signal_idle** -- the io_context
runs on a single background `std::jthread`; completion callbacks are
posted back to the GTK main thread via `Glib::signal_idle()` so View
observers always see updates on the same thread that owns the widgets.

## License

Proprietary -- All Rights Reserved. See [LICENSE](LICENSE).

This source code may be viewed for interview evaluation purposes only.
