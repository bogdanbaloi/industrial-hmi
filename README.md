# Industrial HMI

Cross-platform industrial Human-Machine Interface built with C++20 and GTK4.
Designed for equipment monitoring, quality control, and product management
in manufacturing environments.

## Screenshots

### Dashboard - Equipment Monitoring
![Dashboard](docs/screenshots/dashboard-dark.png)

### Products Database - CRUD Operations
![Products](docs/screenshots/products-dark.png)

## Features

- **Dashboard** with real-time equipment status, quality checkpoints, and control panel
- **Products Database** with full CRUD operations (Create, Read, Update, Delete)
- **Dark/Light theme** switching with Adwaita design tokens
- **Live data simulation** with configurable auto-refresh from background I/O thread
- **Log panel** with real-time log viewing (verbose logging toggle)
- **Fullscreen/windowed** mode with F11/ESC keyboard shortcuts
- **Cross-platform** builds on Linux (GCC) and Windows (Clang/MSYS2)
- **CI/CD pipeline** with GitHub Actions (Ubuntu + Windows MSYS2)

## Architecture

Model-View-Presenter (MVP) with dependency injection and observer pattern.

```
src/
  core/           Application lifecycle, logging, error handling
    Application.h/cpp     Init/teardown, config-driven logging
    LoggerBase.h          Abstract logger with std::vformat
    LoggerImpl.h          Console, File (rotating), Callback loggers
    config_defaults.h     Constexpr configuration defaults
    Result.h              Monadic Result<T,E> error handling

  config/          JSON configuration with fallback defaults
    ConfigManager.h       Singleton, value_or pattern
    config_defaults.h     Compile-time constants

  model/           Data layer and async I/O
    DatabaseManager.h     SQLite CRUD with prepared statements
    SimulatedModel.h      Equipment/quality simulation
    ModelContext.h         Boost.Asio background thread

  presenter/       MVP orchestration
    DashboardPresenter    Equipment + quality ViewModel builders
    ProductsPresenter     Product CRUD coordination
    ViewObserver.h        Observer interface for View updates

  gtk/view/        GTK4 UI layer
    MainWindow            Window management, theme, signals
    DashboardPage         Equipment cards, quality gauges, controls
    ProductsPage          Product table, dialogs, search
    ThemeManager           Dark/light theme with CSS classes
    DialogManager          Themed dialog factory with DI
```

## Tech Stack

| Component | Technology |
|-----------|-----------|
| Language | C++20 (concepts, std::format, std::jthread, std::source_location) |
| UI Framework | GTK4 / gtkmm-4.0 |
| Database | SQLite3 (in-memory, prepared statements) |
| Async I/O | Boost.Asio io_context with work guard |
| Build | CMake 3.20+ with presets, Ninja |
| CI/CD | GitHub Actions (Ubuntu 24.04 + Windows MSYS2) |
| Static Analysis | cppcheck, clang-tidy |

## Building

### Linux (Ubuntu 22.04+)

```bash
sudo apt install cmake ninja-build g++ \
    libgtkmm-4.0-dev libsqlite3-dev libboost-dev

cmake --preset release
cmake --build build/release -- -j$(nproc)
./build/release/industrial-hmi
```

### Windows (MSYS2 Clang64)

```bash
pacman -S mingw-w64-clang-x86_64-toolchain \
    mingw-w64-clang-x86_64-cmake \
    mingw-w64-clang-x86_64-ninja \
    mingw-w64-clang-x86_64-gtkmm-4.0 \
    mingw-w64-clang-x86_64-sqlite3 \
    mingw-w64-clang-x86_64-boost

./build-windows.sh
./build/debug/industrial-hmi.exe
```

See [BUILD.md](BUILD.md) for detailed instructions including packaging.

## Configuration

Application settings are loaded from `config/app-config.json` with fallback
to compile-time defaults in `config_defaults.h`. If the config file is missing,
the application starts with default values.

```json
{
  "logging": {
    "level": "INFO",
    "file": "logs/app.log",
    "max_file_size_mb": 5,
    "max_files": 3,
    "console": true
  }
}
```

## Design Decisions

**Custom logging over spdlog** - Demonstrates C++20 std::vformat, SOLID
principles (LoggerBase abstraction, CompositeLogger, CallbackLogger),
and production patterns (rotation, flush, shutdown lifecycle).

**Custom JSON parser over nlohmann/json** - Shows understanding of parsing
fundamentals. Production deployment would use a proven library.

**MVP over MVC** - Better testability. Presenters have no GTK dependency
and can be unit tested with mock observers.

**Singleton for global services** - ConfigManager, DatabaseManager,
SimulatedModel use Meyer's singleton. Application class manages
initialization order and shutdown sequence.

**Config defaults in constexpr header** - Single source of truth for
fallback values. No magic strings scattered across getters.

## Known Limitations

- ViewObserver interface is broad (16 methods) - could be split per page
- Presenters access model singletons directly - could use injected interfaces
- SimulatedModel combines multiple responsibilities - could be split
- CSS theme support has minor GTK4 compatibility gaps on some properties

## License

MIT License - see [LICENSE](LICENSE)
