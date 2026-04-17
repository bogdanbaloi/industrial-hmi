# Industrial HMI

Cross-platform industrial Human-Machine Interface built with C++20 and GTK4.
Designed for equipment monitoring, quality control, and product management
in manufacturing environments.

[![Build Ubuntu](https://github.com/bogdanbaloi/industrial-hmi/actions/workflows/ci.yml/badge.svg)](https://github.com/bogdanbaloi/industrial-hmi/actions/workflows/ci.yml)

## Features

- **Dashboard** with real-time equipment status, quality checkpoints (dynamic Cairo gauges), and control panel
- **Products Database** with full CRUD operations via async SQLite
- **Internationalization (i18n)** supporting 10 languages with in-app language selector and config persistence
- **Dark / Light themes** with Adwaita design tokens, theme-aware gauge rendering
- **Live simulation** with configurable auto-refresh from background Boost.Asio I/O thread
- **Cross-platform** builds on Linux (GCC) and Windows (Clang / MSYS2)
- **77+ unit tests** with GoogleTest / gmock across 8 test binaries
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
    MainWindow          Window management, theme, sidebar, language selector
    DashboardPage       Equipment cards, quality gauges, control panel
    ProductsPage        ColumnView, dialogs, search
    DialogManager       Themed dialog factory (virtual methods for mocking)
    AboutDialog         App metadata dialog (F1)
    widgets/
      QualityGauge      Cairo-drawn arc gauge, theme-aware
      TrendChart        Cairo-drawn line chart with circular buffer

assets/
  ui/                  GtkBuilder XML layouts
    main-window.ui     Sidebar, notebook, log panel
    dashboard-page.ui  Work unit, equipment, quality, control panel
    products-page.ui   Toolbar, search, table container, action buttons
  styles/              CSS stylesheets
    adwaita-theme.css  Core theme tokens, dark/light variants
    sidebar.css        Sidebar layout + controls
    dashboard.css      Dashboard card styling
    products.css       ColumnView + table styling
  icons/               Application icons
  images/              Logos and illustrations

po/                    gettext translation catalogs (11 languages)
config/                app-config.json + runtime overrides
tests/                 GoogleTest/gmock suites (9 binaries, ~95 tests)
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
| Testing | GoogleTest + gmock (8 binaries, 77+ tests) |
| Build | CMake 3.20+ with presets, Ninja |
| CI/CD | GitHub Actions (Ubuntu 24.04 + Windows MSYS2 Clang64) |
| Coverage | gcovr with HTML report artifact |
| Static Analysis | cppcheck |

## Supported Languages

English, Deutsch, Espanol, Espanol (Mexico), Suomi, Francais, Gaeilge,
Italiano, Portugues, Portugues (Brasil), Svenska.

Language is selectable from the sidebar dropdown and persists across restarts
via `config/app-config.json`. The `"auto"` setting respects the OS locale.

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
| test_result | Result\<T,E\> monadic operations | 21 |
| test_config_manager | Config load, language get/set/persist | 9 |
| test_database_manager | SQLite CRUD, search, soft delete | 11 |
| test_base_presenter | Observer add/remove/notify dispatch | 7 |
| test_products_presenter | Mock repository, ViewModel mapping | 8 |
| test_dashboard_presenter | Mock model, signal routing, state machine | 21 |
| test_dashboard_page | Confirm dialogs, presenter forwarding | 7 |
| test_products_page | Delete confirmation, soft-delete flow | 4 |

Coverage reports are generated automatically in CI and available as
downloadable artifacts on each GitHub Actions run.

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
