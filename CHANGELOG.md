# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Modbus master backend (A1)

- **End-to-end Modbus/TCP master.** New `objectsModbus` object library
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
  boundary), ModbusClient (8 -- loopback via FakeModbusSlave +
  reconnect + exception caching), ModbusIngestBridge (12 -- dedup,
  per-entity tracking, out-of-range guard), ModbusPollLoop (9 --
  pollOnce dispatch + start/stop cycle + cancellation budget +
  destructor join), ModbusBackend (6 -- lifecycle aggregate). All
  green; FakeModbusSlave mirrors the MqttClientTest acceptor pattern.
- **Demo:** `examples/modbus_slave_simulator.py` (pymodbus 3.7
  async slave) toggles holding registers round-robin so the HMI's
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

[Unreleased]: https://github.com/bogdanbaloi/industrial-hmi/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/bogdanbaloi/industrial-hmi/releases/tag/v1.0.0
[0.5.0]: https://github.com/bogdanbaloi/industrial-hmi/releases/tag/v0.5.0
