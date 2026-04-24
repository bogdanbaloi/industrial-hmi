# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Architecture

- **Headless console front-end** (`industrial-hmi-console`) sharing Model +
  Presenter + Bootstrap with the GTK desktop binary. Full command set:
  `help`, `status`, `start`, `stop`, `reset`, `calibrate`, `eq <id> on|off`,
  `alerts`, `dismiss <key>`, `products`, `view <id>`, `quit`. Implements
  `ViewObserver` so presenters never branch per front-end. The binary
  links zero gtkmm (validated by `nm`).
- **Staged Bootstrap** (`src/core/Bootstrap.{h,cpp}`) orchestrates startup
  across both front-ends: stderr logger → config → configured logger →
  i18n → SQLite. Resolves the classic config/logger chicken-and-egg via
  a two-phase logger; `objectsCore` is now GTK-free and `objectsAppGtk`
  holds the GTK bootstrap glue.
- **Fail-fast startup errors** — typed `CriticalStartupError` hierarchy
  (`ConfigMissing`, `ConfigCorrupt`, `DatabaseInit`, `LoggerBootstrap`)
  thrown from Bootstrap/Application, caught in `main()`, surfaced through
  a native reporter (MessageBoxW on Windows GUI, stderr elsewhere) with
  documented exit codes (0/1/2/3). Dialog body is localised via gettext.
- **Policy / mechanism split** — `ConfigManager::applyI18n()` owns the
  language policy; `src/core/i18n` stays a pure gettext adapter with no
  dependency on glibmm. DB init moved from `Application::initDatabase`
  to `Bootstrap` Stage 5 so both front-ends start from the same
  initialised SQLite.
- **MainWindow keyboard dispatcher extracted** into `MainWindowKeyDispatch`
  — a pure free function + `KeyDispatchContext` struct so every F-key
  shortcut can be unit-tested without instantiating MainWindow.
- **Defensive dialog parent lookups** in `ProductsPage`
  (showAddProductDialog, showEditProductDialog, showProductDetail) —
  `dynamic_cast<Gtk::Window*>` now falls back to a parentless dialog
  constructor instead of dereferencing a null pointer, mirroring the
  pattern already used by `DialogManager::createMessageDialog`.

### Testing & CI

- **Test coverage: 43% → 79%** across 4095 lines, verified by gcovr on
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
- **Async presenter tests** — `ProductsPresenterAsyncTest` drives a
  Glib::MainLoop to pump signal_idle queues, exercising the Boost.Asio
  → ModelContext → signal_idle marshaling path that a synchronous
  harness can't observe.
- **Coverage-focused xdotool smoke** — CI coverage job boots the GTK
  binary under Xvfb, sends F1 to open AboutDialog, Escape to close,
  then lets `HMI_EXIT_AFTER_MS` drive a clean atexit so gcov .gcda
  files flush. Lifts `AboutDialog.cpp` to 98%.
- **LoggerImplTest** (23 cases) covers FileLogger rotation workflow,
  CompositeLogger propagation + `isEnabled` fan-out, NullLogger no-ops,
  CallbackLogger reentrancy guard via atomic flag.
- **I18nTest** (7 cases) covers the gettext adapter: forceLanguage /
  propagateLangToLanguage / resolveLocaleDir (absolute, relative-exists,
  missing), `gEnvOwned` reset on "auto → explicit → auto".
- **CI: Node.js 24 opt-in** via `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24`
  eliminates 3 Node 20 deprecation warnings on upload-artifact@v5.
- **Dual-platform CI** — Ubuntu 24.04 (GCC 13 + pkg-config) and Windows
  MSYS2 CLANG64 (vcpkg-free); both build + test on every PR. Coverage
  job runs only on Ubuntu (one source of truth for the gcovr report).
- **Clang-tidy + cppcheck** gates on every PR, with the clang-tidy
  report uploaded as an artifact.

### Features

- **Color palettes** (8 total) — Industrial (baseline), Nord, Paper,
  Right Sidebar, Dracula, CRT, Blueprint, Cockpit. Loaded as a second
  CSS provider layered over the base stylesheet.
- **Thumbnail palette picker** in Settings with four colour swatches
  per card, palette name, and a mode badge ("Dark + Light", "Dark only",
  "Light only").
- **Mode-locked palettes** — Tier 2 palettes are single-mode by design
  (Paper = light-only; Dracula / CRT / Blueprint / Cockpit = dark-only).
  The incompatible Dark/Light radio is disabled with a tooltip, and
  picking a locked palette auto-snaps the Theme.
- **Alternate UI layouts** — Right Sidebar mirrors the sidebar to the
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

- **Banner separator cleanup** — 372 `// ----`, `# ====`, `/* ==== */`,
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
  — non-blocking database operations, single I/O thread for async work,
  thread-safe callback marshaling via `Glib::signal_idle()`.
- **Theme Toggle UI** in sidebar (Dark Mode / Light Mode radios,
  real-time switching, ThemeManager integration).
- **vcpkg.json manifest** for Windows dependencies (manifest mode,
  reproducible builds).
- **Cross-platform support** — Linux (Ubuntu 24.04+, Debian, Fedora) +
  Windows (10/11, Server 2022). Platform-agnostic CMake; vcpkg on
  Windows, pkg-config on Linux.
- **CI/CD pipeline** (GitHub Actions) with dual-platform builds, code
  quality checks (clang-tidy, cppcheck), documentation verification,
  automated releases with artifacts.
- **Adwaita themes with design tokens** — 30+ CSS variables (colours,
  spacing, typography), gradient sidebar backgrounds.
- **Complete CRUD operations** — soft delete with `deleted_at`
  timestamp, input validation with error dialogs, async confirmation
  dialogs, search.
- **Dependency Injection** — refactored from singleton anti-pattern;
  explicit dependencies via constructor injection, testable
  architecture with mock support.
- **MVP architecture** — Model (`DatabaseManager`, `SimulatedModel`,
  `ModelContext`), View (GTK4 pages), Presenter (`DashboardPresenter`,
  `ProductsPresenter`).
- **Modern C++20 features** — Concepts, `std::jthread`, ranges,
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

**Breaking changes** — `ProductsPresenter::{addProduct, updateProduct,
deleteProduct}` now require a callback parameter:

```cpp
// 0.5.0
bool ok = presenter_->addProduct(code, name, status, stock, quality);
if (!ok) showError();

// 1.0.0
presenter_->addProduct(code, name, status, stock, quality,
    [this](bool ok) { if (!ok) showError(); });
```

**New dependencies** — Boost.Asio (already pulled in by signals2);
C++20 compiler (GCC 10+, MSVC 2019+, Clang 10+).

[Unreleased]: https://github.com/bogdanbaloi/industrial-hmi/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/bogdanbaloi/industrial-hmi/releases/tag/v1.0.0
[0.5.0]: https://github.com/bogdanbaloi/industrial-hmi/releases/tag/v0.5.0
