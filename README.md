# Industrial HMI

Cross-platform industrial Human-Machine Interface in modern C++20.
Equipment monitoring, quality control, and product database management
for manufacturing-floor terminals -- shipped as both a GTK4 desktop UI
and a headless console binary, sharing one tested Model + Presenter
core.

[![CI](https://github.com/bogdanbaloi/industrial-hmi/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/bogdanbaloi/industrial-hmi/actions/workflows/ci.yml)
![Coverage](https://img.shields.io/badge/coverage-79%25-brightgreen)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![Platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20Windows-lightgrey)

---

## Highlights

- **Two front-ends, one core**: GTK4 desktop (`industrial-hmi`) +
  headless console (`industrial-hmi-console`) built from the same
  `main.cpp` via `#ifdef CONSOLE_MODE`. The console binary links
  **zero gtkmm** -- concrete proof that the `ViewObserver` abstraction
  is a real View-swap seam, not just marketing.
- **79% test coverage** verified by gcovr in CI on every PR, across
  4000+ lines and **31 ctest targets**: scenario-based E2E, async
  presenter tests with `Glib::MainLoop` pump, view-layer tests under
  real GTK via Xvfb, dialog dispatch via programmatic `response()`.
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
- **11 UI languages** via gettext, runtime switch (no restart) -- the
  page tree is rebuilt so every `_()` and every `translatable="yes"`
  re-resolves against the new catalog.
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

assets/
  ui/                   GtkBuilder XML layouts (multi-layout)
  styles/               Base CSS + 8 palette overlays
  icons/  images/       App resources

po/                     gettext catalogs (11 languages)
config/                 app-config.json
tests/                  31 ctest targets (see Testing section)
```

## Test Strategy

Coverage is measured by **gcovr** on the Ubuntu CI job and posted at
the top of every PR's Actions run. Currently **79% across 4095
lines**, achieved by combining several testing styles instead of one
monoculture:

| Category | What it covers | Examples |
|---|---|---|
| **Unit tests** | Pure C++ logic, no GTK | `ResultTest`, `LoggerImplTest`, `I18nTest`, `DatabaseManagerTest` |
| **Presenter tests** | Presenter <-> Model contracts via gmock | `DashboardPresenterTest`, `ProductsPresenterTest` |
| **Async presenter tests** | Boost.Asio `io_context` -> `signal_idle` marshaling | `ProductsPresenterAsyncTest` (drives a `Glib::MainLoop`) |
| **View-layer tests** | Real GTK widget tree under Xvfb | `DashboardPageTest`, `ProductsPageTest`, `SettingsPageTest` |
| **Dialog dispatch tests** | DialogManager API via programmatic `response()` | `DialogManagerTest` (11 cases) |
| **Refactor-driven tests** | Logic extracted from GTK glue for pure testing | `MainWindowKeyDispatchTest` |
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

On Linux all 31 targets are green; on Windows MSYS2 we run the same
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

## Tech Stack

| Layer | Technology |
|---|---|
| Language | C++20 (concepts, format, jthread, source_location, ranges) |
| UI | GTK4 / gtkmm-4.0, Cairo for custom widgets |
| Database | SQLite3 (in-memory, prepared statements) |
| Async I/O | Boost.Asio io_context with work guard, posted via std::jthread |
| i18n | GNU gettext, custom adapter (no glibmm i18n macros) |
| Testing | GoogleTest + gmock (31 ctest targets) |
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
