# Industrial HMI

Cross-platform industrial Human-Machine Interface built with C++20 and GTK4.
Designed for equipment monitoring, quality control, and product management
in manufacturing environments.

[![Build Ubuntu](https://github.com/bogdanbaloi/industrial-hmi/actions/workflows/ci.yml/badge.svg)](https://github.com/bogdanbaloi/industrial-hmi/actions/workflows/ci.yml)

## Features

- **Dashboard** with real-time equipment status, quality checkpoints (dynamic Cairo gauges), and control panel
- **Products Database** with full CRUD operations via async SQLite and CSV export
- **Alerts Center** with info/warning/critical severities, per-alert dismiss, and resolved-alert history
- **Internationalization (i18n)** supporting 11 languages with in-app language selector and config persistence
- **Dark / Light themes** with Adwaita design tokens, theme-aware gauge rendering
- **8 color palettes** (Industrial, Nord, Paper, Right Sidebar, Dracula, CRT, Blueprint, Cockpit) with thumbnail picker, mode locks, and tooltips
- **Alternate UI layouts** — baseline sidebar, right-sidebar mirror, Blueprint top-bar with alerts/logs in popovers — swapped at runtime via GtkBuilder
- **Live simulation** with configurable auto-refresh from background Boost.Asio I/O thread
- **Two front-ends** sharing one presenter + model layer: GTK4 desktop (`industrial-hmi`) and headless console (`industrial-hmi-console`) — console is GTK-free, validates View-swap works
- **Cross-platform** builds on Linux (GCC) and Windows (Clang / MSYS2)
- **160+ unit tests** with GoogleTest / gmock across 12 test binaries
- **5 scenario tests** pipe scripted input through the console binary and diff stdout against golden files — exercises the full Model → Presenter → View pipeline without a display server
- **CI/CD pipeline** with GitHub Actions: build, test, coverage report, static analysis

## Architecture

Model-View-Presenter (MVP) with dependency injection, observer pattern, and interface-based testing.

```
src/
  core/               Application lifecycle, logging, error handling
    Application         Config-driven init, GTK bootstrap, shutdown
    LoggerBase/Impl     Abstract logger with C++20 std::vformat
    Result<T,E>         Monadic error handling (Rust-inspired)
    i18n                gettext integration, locale detection

  config/              JSON configuration with compile-time defaults
    ConfigManager       Singleton with language persistence
    config_defaults.h   constexpr fallback values

  model/               Data layer and abstractions
    DatabaseManager     SQLite CRUD (implements ProductsRepository)
    SimulatedModel      Equipment/quality simulation (implements ProductionModel)
    ProductsRepository  Read-side interface for product queries
    ProductionModel     Interface for dashboard model operations
    ModelContext         Boost.Asio background I/O thread

  presenter/           MVP orchestration (unit-testable, no GTK dependency)
    DashboardPresenter  Signal subscription, ViewModel builders, state machine
    ProductsPresenter   Product CRUD coordination, search routing
    BasePresenter       Thread-safe observer registration
    ViewObserver        Callback interface for View updates

  gtk/view/            GTK4 UI layer
    MainWindow          Window management, layout swap, language selector
    ThemeManager        Base theme + palette provider stacking, mode locks
    AlertsPanel         Observer-backed alerts list with severity routing
    DashboardPage       Equipment cards, quality gauges, control panel
    ProductsPage        ColumnView, dialogs, search, CSV export
    SettingsPage        Theme / palette / layout / i18n / logging controls
    DialogManager       Themed dialog factory (virtual methods for mocking)
    AboutDialog         App metadata dialog (F1)
    widgets/
      QualityGauge      Cairo-drawn arc gauge, theme-aware
      TrendChart        Cairo-drawn line chart with circular buffer

assets/
  ui/                  GtkBuilder XML layouts (multi-layout)
    main-window.ui             Baseline: left sidebar + notebook + log panel
    main-window-right.ui       Mirror: right-hand sidebar
    main-window-blueprint.ui   Top-bar with alerts/logs in popovers
    dashboard-page.ui          Work unit, equipment, quality, control panel
    products-page.ui           Toolbar, search, table, action buttons
    settings-page.ui           Theme, palette, layout, language, logging
  styles/              CSS stylesheets
    adwaita-theme.css  Core theme tokens, dark/light variants
    sidebar.css        Sidebar layout + controls, palette-card thumbnails
    dashboard.css      Dashboard card styling
    products.css       ColumnView + table styling
    themes/            Optional palette overlays (loaded on top of base)
      nord.css, paper.css, right.css      Dual-mode (dark + light)
      dracula.css, crt.css,
      blueprint.css, cockpit.css          Dark-only by design
  icons/               Application icons
  images/              Logos and illustrations

po/                    gettext translation catalogs (11 languages)
config/                app-config.json + runtime overrides
tests/                 GoogleTest/gmock suites (12 binaries, 160+ tests)
```

### UI Layout: GtkBuilder + Inline Widgets

Static layout lives in `assets/ui/*.ui` (GtkBuilder XML) so designers
can iterate without rebuilding. Dynamic widgets (Cairo-drawn gauges,
trend charts, GtkColumnView with SignalListItemFactory) are injected
into named containers from code at runtime.

```cpp
auto builder = Gtk::Builder::create_from_file(kDashboardPageUI);
auto* gaugeContainer = builder->get_widget<Gtk::Box>("qc_gauge_container_0");
gaugeContainer->append(*Gtk::make_managed<QualityGauge>());
```

### Dependency Injection

Both presenter classes accept their model dependency through the constructor.
Production wiring uses the singleton; tests inject gmock-backed mocks.

```cpp
// Production (default constructor)
auto presenter = std::make_shared<ProductsPresenter>();

// Test (injected mock)
MockProductsRepository repo;
auto presenter = std::make_shared<ProductsPresenter>(repo);
EXPECT_CALL(repo, getAllProducts()).WillOnce(Return(...));
```

## Tech Stack

| Component | Technology |
|-----------|-----------|
| Language | C++20 (concepts, format, jthread, source_location) |
| UI | GTK4 / gtkmm-4.0, Cairo for custom widgets |
| Database | SQLite3 (in-memory, prepared statements) |
| Async I/O | Boost.Asio io_context with work guard |
| i18n | GNU gettext, glibmm i18n macros |
| Testing | GoogleTest + gmock (12 binaries, 160+ tests) |
| Build | CMake 3.20+ with presets, Ninja |
| CI/CD | GitHub Actions (Ubuntu 24.04 + Windows MSYS2 Clang64) |
| Coverage | gcovr with HTML report artifact |
| Static Analysis | cppcheck |

## Supported Languages

English, Deutsch, Espanol, Espanol (Mexico), Suomi, Francais, Gaeilge,
Italiano, Portugues, Portugues (Brasil), Svenska.

Language is selectable from the sidebar dropdown and persists across restarts
via `config/app-config.json`. The `"auto"` setting respects the OS locale.

## Palettes and Layouts

Three classes of visual customization are exposed from the Settings page:

**Theme** — Dark or Light mode, toggles the `.light-mode` class on the main
window. All Cairo widgets (`QualityGauge`, `TrendChart`) query
`ThemeManager::isDarkMode()` at paint time so custom-drawn surfaces match.

**Palette** — optional CSS overlay loaded on top of the base stylesheet at
`GTK_STYLE_PROVIDER_PRIORITY_USER + 1`, redefining colors without touching
layout:

| Palette | Modes | Feel |
|---------|-------|------|
| Industrial | Dark + Light | Baseline (no overlay loaded) |
| Nord | Dark + Light | Polar / Snow Storm |
| Paper | Light only | Navy + white executive |
| Right Sidebar | Dark + Light | Mirror layout, teal accent |
| Dracula | Dark only | Purple / pink on slate |
| CRT | Dark only | Phosphor green on black, monospace |
| Blueprint | Dark only | Navy + cyan + cream, top-bar layout |
| Cockpit | Dark only | Mission control, heavy instrument bezels |

Mode-locked palettes are enforced in both directions — the incompatible Dark
or Light radio is disabled with a tooltip explaining why, and picking a
locked palette auto-snaps the Theme to its supported mode.

**Layout** — some palettes ship structurally different GtkBuilder trees:
Right Sidebar mirrors the sidebar to the right edge, Blueprint moves Alerts
and Logs into top-bar popovers so the Dashboard reclaims vertical space.
The layout is swapped at runtime via `MainWindow::reloadLayout` (detach old
root, parse new `.ui`, re-attach) so the swap is atomic and state-preserving.

## Building

### Linux (Ubuntu 24.04+)

```bash
sudo apt install cmake ninja-build g++ \
    libgtkmm-4.0-dev libsqlite3-dev libboost-dev

cmake --preset release
cmake --build build/release -- -j$(nproc)
./build/release/industrial-hmi
```

### Windows (MSYS2 Clang64)

```bash
pacman -S mingw-w64-clang-x86_64-{toolchain,cmake,ninja,gtkmm-4.0,sqlite3,boost,gettext,gtest}

./build-windows.sh
./build/debug/industrial-hmi.exe
```

See [BUILD.md](BUILD.md) for detailed instructions including packaging.

## Testing

```bash
# Configure with tests enabled
cmake --preset debug -DBUILD_TESTS=ON

# Build and run
cmake --build build/debug
cd build/debug && ctest --output-on-failure
```

Test suites:

| Binary | Scope | Tests |
|--------|-------|-------|
| test_result | Result\<T,E\> monadic operations | 22 |
| test_config_manager | Config load, language get/set/persist | 9 |
| test_database_manager | SQLite CRUD, search, soft delete | 12 |
| test_csv_serializer | Product CSV export round-trip | 8 |
| test_simulated_model | Equipment/quality simulation signals | 15 |
| test_logger | LoggerBase formatting, level filtering | 13 |
| test_base_presenter | Observer add/remove/notify dispatch | 7 |
| test_alert_center | Severity routing, dismiss, resolved history | 26 |
| test_products_presenter | Mock repository, ViewModel mapping | 8 |
| test_dashboard_presenter | Mock model, signal routing, state machine | 29 |
| test_dashboard_page | Confirm dialogs, presenter forwarding | 7 |
| test_products_page | Delete confirmation, soft-delete flow | 4 |

Plus **5 scenario tests** that drive the headless console binary
end-to-end (stdin pipe → stdout diff):

| Scenario | Covers |
|----------|--------|
| `boot-and-quit` | Baseline startup event sequence |
| `status-snapshot` | `status` command rendering |
| `start-stop-cycle` | Control-panel state machine transitions |
| `equipment-toggle` | `eq <id> on/off` argument parsing + toggle |
| `products-list` | Sync DB read path via ProductsPresenter |

Scenario inputs live in `tests/scenarios/*.txt`; expected outputs in
`*.expected`. The runner (`tests/scenarios/run-scenario.cmake`) strips
logger timestamp lines so only structural events and command output
participate in the byte-exact comparison. No display server, no Xvfb,
no GLib main loop — runs anywhere `ctest` runs.

Coverage reports are generated automatically in CI and available as
downloadable artifacts on each GitHub Actions run.

## Two front-ends, one core

Running the GTK desktop UI:
```bash
./build/debug/industrial-hmi
```

Running the headless console UI:
```bash
./build/debug/industrial-hmi-console
# Type `help` for the command list, `quit` to exit.
# Or pipe a script:
printf 'start\nstatus\nquit\n' | ./build/debug/industrial-hmi-console
```

Both binaries share `main.cpp` via an `#ifdef CONSOLE_MODE` switch,
and link the same Model + Presenter + Bootstrap libraries. The console
binary links **zero gtkmm** (verified by `nm`), which is the concrete
proof that the `ViewObserver` abstraction is a real View-swap seam and
not just marketing.

## Configuration

Settings are loaded from `config/app-config.json` with fallback to
compile-time defaults in `config_defaults.h`.

```json
{
  "i18n": { "language": "auto" },
  "logging": {
    "level": "INFO",
    "file": "logs/app.log",
    "max_file_size_mb": 5
  }
}
```

## Design Decisions

**MVP over MVC** - Presenters have no GTK dependency and can be fully unit
tested with mock observers and mock model interfaces.

**Interface-based DI** - ProductsRepository and ProductionModel abstractions
decouple presenters from singletons. Tests inject gmock mocks; production
wiring uses the default constructor.

**gettext for i18n** - Industry standard, integrates with GtkBuilder's
`translatable="yes"` attribute, ships compiled `.mo` catalogs via CMake.

**Cairo gauges over static SVGs** - Dynamic arc length reflects real pass-rate
percentage. Track color adapts to dark/light theme at draw time.

**Custom logging over spdlog** - Demonstrates C++20 std::vformat and SOLID
principles. Production deployment could swap to spdlog via the LoggerBase
abstraction.

## License

Proprietary - All Rights Reserved. See [LICENSE](LICENSE).

This source code may be viewed for interview evaluation purposes only.
