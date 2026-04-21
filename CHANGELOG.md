# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Color palettes** (8 total) — Industrial (baseline), Nord, Paper,
  Right Sidebar, Dracula, CRT, Blueprint, Cockpit. Loaded as a second CSS
  provider stacked on top of the base stylesheet.
- **Thumbnail palette picker** in Settings with four colour swatches per
  card, palette name, and a mode badge ("Dark + Light", "Dark only",
  "Light only").
- **Mode-locked palettes** — Tier 2 palettes are single-mode by design
  (Paper is light-only; Dracula / CRT / Blueprint / Cockpit are dark-only).
  The incompatible Dark/Light radio is disabled with a tooltip, and picking
  a locked palette auto-snaps the Theme.
- **Alternate UI layouts** — Right Sidebar mirrors the sidebar to the right;
  Blueprint moves Alerts and Logs into top-bar popovers. Swapped at runtime
  via `MainWindow::reloadLayout` with an atomic detach/parse/re-attach.
- **Alerts Center** with info / warning / critical severities, per-alert
  dismiss, and resolved-alert history. 26 dedicated tests.
- **Products CSV export** with round-trip unit tests.
- **CMakePresets.json** for modern CMake workflow.
- **Doxyfile** for API documentation generation.
- **Sanitizers** support (AddressSanitizer, UBSanitizer).
- **Code coverage** support (gcov/lcov) and HTML reports in CI.
- **.editorconfig** for consistent coding style.

### Changed
- i18n grown to 11 languages (added es_MX, ga, pt_BR, sv).
- Test suite grown to 12 binaries / 160+ tests.
- Baseline layouts: `log_panel` `height-request` reduced from 150 to 70
  to stay within the 1200 px window budget across all palettes (the old
  value produced `Trying to measure gtkmm__GtkBox for height of 1200, but
  it needs at least N` warnings on dense palettes like Cockpit).

### Fixed
- Dracula combobox double-border (flattened the inner GTK button).
- Nord: restored Light variant stripped by an earlier unwrap script.
- Blueprint / Cockpit: removed stray light-mode CSS sections (both are
  dark-only palettes).
- Paper: ColumnView header text no longer invisible on light-on-light;
  notebook stack background forced to paper cream.
- Settings "Show logs" checkbox preserves the user's choice across palette
  transitions (Blueprint forces a log tail, but the user's preference is
  restored when leaving Blueprint).

## [1.0.0] - 2026-04-09

### Added
- **Async I/O Context** with Boost.Asio and std::jthread (C++20 RAII)
  - Non-blocking database operations
  - Single I/O thread for async work
  - Thread-safe callback marshaling via Glib::signal_idle()
  - Production-ready async pattern

- **Theme Toggle UI** in sidebar
  - Dark Mode / Light Mode radio buttons
  - Real-time theme switching
  - Integration with ThemeManager

- **vcpkg.json Manifest** for Windows dependencies
  - Modern vcpkg manifest mode
  - Automatic dependency resolution
  - Reproducible Windows builds

- **Cross-Platform Support**
  - Linux (Ubuntu 24.04+, Debian, Fedora)
  - Windows (10/11, Server 2022)
  - Platform-agnostic CMake build system
  - vcpkg integration for Windows
  - pkg-config for Linux

- **CI/CD Pipeline** (GitHub Actions)
  - Dual platform builds (Ubuntu + Windows)
  - Code quality checks (clang-tidy, cppcheck)
  - Documentation verification
  - Automated releases with artifacts

- **Adwaita Themes** with Design Tokens
  - Dark Mode (industrial dark theme)
  - Light Mode (clean light theme)
  - 30+ CSS design tokens (colors, spacing, typography)
  - Gradient sidebar backgrounds
  - Professional UI/UX

- **Complete CRUD Operations**
  - Create, Read, Update, Delete products
  - Soft delete pattern with deleted_at timestamp
  - Input validation with error dialogs
  - Async confirmation dialogs
  - Search functionality

- **Dependency Injection Pattern**
  - Refactored from Singleton anti-pattern
  - Explicit dependencies via constructor injection
  - Testable architecture with mock support
  - Production-ready pattern

- **MVP Architecture**
  - Model: DatabaseManager, SimulatedModel, ModelContext
  - View: GTK4 pages (DashboardPage, ProductsPage)
  - Presenter: DashboardPresenter, ProductsPresenter
  - Clean separation of concerns

- **SOLID Principles**
  - Interface Segregation (focused view interfaces)
  - Dependency Inversion (DI pattern)
  - Single Responsibility (layer separation)

- **Modern C++20 Features**
  - Concepts (ViewInterfaceConcept)
  - std::jthread (RAII thread management)
  - Ranges
  - constexpr
  - [[nodiscard]] attributes

- **Comprehensive Documentation**
  - README.md (project overview)
  - PORTFOLIO_SUMMARY.md (interview preparation)
  - BUILD.md (cross-platform build guide)
  - tests/README_TESTING.md (unit testing guide)

### Changed
- DatabaseManager: Added async methods alongside sync methods
- ProductsPresenter: Updated to async signatures with callbacks
- ProductsPage: Updated UI to handle async operations

### Fixed
- UI freezing during database operations (now async)
- Theme consistency across components
- Windows build with vcpkg

## [0.5.0] - 2026-04-07

### Added
- Initial MVP architecture implementation
- GTK4 user interface
- SQLite database integration
- Pharmaceutical manufacturing domain model
- Basic CRUD operations (synchronous)
- Singleton pattern (later refactored to DI)

### Known Issues
- Blocking database operations (fixed in v1.0.0)
- Singleton pattern (refactored in v1.0.0)
- Linux-only build (cross-platform in v1.0.0)

---

## Release Notes

### Version 1.0.0 Highlights

**Production-Ready Async Architecture:**
The major focus of this release is the transition from blocking to non-blocking I/O. Database operations now execute on a dedicated background thread using Boost.Asio, preventing UI freezes. Callbacks are marshaled back to the GTK main thread using Glib::signal_idle(), ensuring thread safety.

**Cross-Platform Excellence:**
Full support for both Linux and Windows with platform-specific tooling (pkg-config vs vcpkg). CMake automatically detects the platform and configures the build accordingly. CI/CD builds and tests on both Ubuntu 24.04 and Windows Server 2022.

**Modern C++20 RAII:**
Uses std::jthread instead of std::thread for automatic thread lifecycle management. No manual join/detach required - the thread joins automatically on destruction, making the code exception-safe and cleaner.

**Professional Patterns:**
Demonstrates production-grade software engineering: MVP architecture, Dependency Injection, SOLID principles, async I/O, RAII, design tokens, soft delete, and comprehensive error handling.

---

## Upgrade Notes

### From 0.5.0 to 1.0.0

**Breaking Changes:**
- `ProductsPresenter::addProduct()` now requires a callback parameter
- `ProductsPresenter::updateProduct()` now requires a callback parameter
- `ProductsPresenter::deleteProduct()` now requires a callback parameter

**Migration Example:**
```cpp
// Old (0.5.0):
bool success = presenter_->addProduct(code, name, status, stock, quality);
if (!success) {
    showError();
}

// New (1.0.0):
presenter_->addProduct(code, name, status, stock, quality, [this](bool success) {
    if (!success) {
        showError();
    }
});
```

**New Dependencies:**
- Boost.Asio (already required for signals2)
- C++20 compiler (GCC 10+, MSVC 2019+, Clang 10+)

---

[Unreleased]: https://github.com/yourusername/industrial-hmi/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/yourusername/industrial-hmi/releases/tag/v1.0.0
[0.5.0]: https://github.com/yourusername/industrial-hmi/releases/tag/v0.5.0
