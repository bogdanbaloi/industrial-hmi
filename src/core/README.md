# `src/core/` -- Shared Utilities & Bootstrap

The thin foundation every other layer rests on: staged-startup
orchestrator, two-phase logger, Rust-inspired `Result<T, E>` type,
typed exception handling, locale-binding mechanism for gettext,
ISO 8601 time formatting, and a fatal-error reporter that survives
the GTK toolkit not being initialised yet. GTK-free by construction
so both front-ends + every test binary can link it without dragging
in a GUI toolkit.

---

## Why this module exists separately

Boilerplate that every C++ application accumulates -- logger, error
handling, startup sequencing, i18n binding, time formatting -- usually
ends up scattered across `main.cpp` and a few helper files, with
order-of-initialisation bugs nobody notices until a config error needs
to be logged before the logger exists.

Pulling all of it into `src/core/` does three things:

- **Single source of truth** for the staged startup contract
  (`Bootstrap`). Every entry point follows the same lifecycle:
  logger -> config -> i18n -> database -> UI / event loop.
- **GTK-free dependency** -- the console binary links `core/`
  without touching gtkmm. So do unit tests.
- **One place to extend the foundation** -- adding a metrics
  client, telemetry sink, or graceful-shutdown handler is a new
  file here, wired once from Bootstrap.

---

## File map

```
src/core/
├── Bootstrap.{h,cpp}       Staged startup orchestrator (logger -> config -> i18n)
├── LoggerBase.h            Logger interface + level enum + std::format helpers
├── LoggerImpl.h            File + console concrete loggers (header-only)
├── ExceptionHandler.h      Top-level translator: typed exception -> exit code + log
├── ErrorHandling.h         Strong typedefs for common error categories
├── Result.h                Rust-inspired Result<T, E> via std::variant + concepts
├── StartupErrors.h         Typed exceptions thrown from Bootstrap / Application init
├── StartupDialog.{h,cpp}   Platform-native fatal-error reporter (no GTK needed)
├── TimeFormat.{h,cpp}      ISO 8601 UTC -> local time string formatter
├── i18n.h                  gettext macros (_, N_) + initI18n signature
├── i18n.cpp                Binds gettext catalogs to the runtime
├── Application.{h,cpp}     GTK-bound application lifecycle (only used by GTK exe)
```

Two things to call out:

1. `Application.{h,cpp}` is the only GTK-dependent file in this
   folder. It belongs to the GTK front-end's lifecycle; the
   console binary never compiles it. We keep it here because it's
   the *companion* to `Bootstrap` -- one ends, the other begins.
2. `LoggerImpl.h` is intentionally header-only. The implementation
   is short (rotating file + console writer ~150 LOC) and being
   header-only avoids an extra compile unit per binary.

---

## Architecture (SOLID at a glance)

```
                ┌──────────────────────┐
                │     Bootstrap         │   staged startup; owns Logger + Config
                │                       │   instances by the time it returns
                └──────────┬────────────┘
                           │
       ┌───────────────────┼───────────────────────┐
       │                   │                       │
       ▼                   ▼                       ▼
   Logger              Config                  i18n
   (LoggerBase) ── (ConfigManager) ─── (initI18n)
   two-phase           policy owner            mechanism

   Result<T, E> ── ExceptionHandler ── StartupDialog
   (return-based)   (catch-translate)   (native fatal report)
```

**SOLID applied:**

- **S** -- One purpose per file. `Result<T,E>` only models a
  return value. `LoggerBase` only describes the log surface.
  `Bootstrap` only sequences startup. Each rotation independent.
- **O** -- Adding a new logger backend (syslog, journald,
  network) is a new `LoggerBase` impl + one line in Bootstrap.
  Adding a new typed exception is a new struct in
  `StartupErrors.h` + one case in `ExceptionHandler::translate`.
- **L** -- Every concrete logger satisfies the same `LoggerBase`
  contract (level, location, formatted message). The bootstrap
  swap (`ConsoleLogger` -> `FileLogger`) is invisible to call
  sites; the next `logger.info(...)` works on either.
- **I** -- `LoggerBase` is six methods (one per level + `name`);
  callers using only `info` don't pay for `trace`. `Result<T,E>`
  exposes only what callers need (`isOk`, `value`, `error`); no
  `unsafe_unwrap_unchecked` escape hatches.
- **D** -- Every layer takes a `Logger&` by reference. No
  globals, no singletons in this directory (except
  `ConfigManager`, which is the application's tuning surface
  and singleton-by-design).

---

## Key surfaces

### `Bootstrap` -- two-phase logger pattern

```cpp
app::core::Bootstrap bootstrap;
if (!bootstrap.run()) {
    return 1;   // unrecoverable startup failure
}
auto& logger = bootstrap.logger();
auto& config = bootstrap.config();
```

The body of `Bootstrap::run()` walks:

1. **Phase 1 logger** -- minimal stderr `ConsoleLogger` at INFO.
   Always works. Used to log warnings from steps 2-3.
2. `ConfigManager::initialize()` -- loads `app-config.json`,
   falls back to compiled defaults on failure. Logs warnings via
   the bootstrap logger.
3. **Phase 2 logger** -- swap in the configured `FileLogger`
   (rotation, path, level from config). If the swap fails (bad
   path, permission denied), keep the bootstrap logger and
   record the failure as a startup warning.
4. `ConfigManager::applyI18n()` -- policy decides the language,
   calls `core::initI18n` (mechanism). Degraded states logged
   via the now-final logger.

Solves the classic "config needs a logger, logger needs config"
chicken-and-egg by accepting that the first ~3 log lines might
go to stderr while config is being read. Acceptable trade-off:
the operator sees them on the terminal during demo runs, and a
production deployment with a service supervisor (systemd, Docker)
captures stderr anyway.

### `Result<T, E>` -- type-safe error handling

```cpp
Result<Product, DatabaseError> findProduct(int id) {
    if (!connected) return Err(DatabaseError::ConnectionFailed);
    return Ok(product);
}

auto r = findProduct(42);
if (r.isOk()) {
    use(r.value());
} else {
    logger.error("findProduct failed: {}", to_string(r.error()));
}
```

Backed by `std::variant<T, E>` with C++20 concepts gating the
type constraints. Used by paths where throwing an exception is
overkill (database lookups, config parses, parser steps) but
returning `std::optional<T>` would erase the failure reason.

### `ExceptionHandler` + typed startup errors

```cpp
try {
    app.initialize(bootstrap, argc, argv);
} catch (const StartupError& e) {
    return app::core::ExceptionHandler::handleFatal(e);
}
```

`StartupError` is the base of a small hierarchy
(`DatabaseInitError`, `LayoutLoadError`, `AuthBootstrapError`,
...). `ExceptionHandler::handleFatal` translates the exception
type into an exit code + a user-facing message rendered via
`StartupDialog`. Avoids the "catch (std::exception&)" anti-pattern
where every failure looks the same to the operator.

### `StartupDialog` -- fatal reporter without GTK

GTK might be the very thing that failed (broken theme, missing
display server). So the fatal reporter must work without it:

- On Windows: `MessageBoxW` via the Win32 API.
- On Linux: stderr message + return code; service supervisors
  display it via journalctl.
- On macOS: NSAlert via Cocoa (when built for Mac).

A second tier of robustness: if the GTK process can't even reach
`main()`, the harness shows `StartupDialog` with the typed
exception's message and exits cleanly.

### `LoggerBase` + `LoggerImpl`

```cpp
class Logger {
    virtual void log(Level, std::string_view fileLoc, std::string msg) = 0;

    // Level helpers + std::format passthrough.
    template <typename... Args>
    void info(std::string_view fmt, Args&&... args);
    // ...
};
```

Concrete loggers (`ConsoleLogger`, `FileLogger`) live in
`LoggerImpl.h` (header-only). `FileLogger` does size-based
rotation; the rotation policy is read from config so an operator
can dial it without recompile.

Macros `LOG_INFO(logger, fmt, ...)` etc. wrap `std::format` +
`std::source_location` so every log line carries `file:line` for
free.

### `i18n` -- pure mechanism

```cpp
namespace app::core {
void initI18n(const char* localeDir, const char* language = "auto");
}
```

Binds gettext catalogs to runtime. No policy, no fallback
decisions beyond what gettext itself does (unknown language ->
source strings). Policy (which language? which catalog dir?)
lives in `ConfigManager::applyI18n` -- ConfigManager owns
*what*, `core/i18n` owns *how*.

`_()` macro is defined locally (not via `<glibmm/i18n.h>`) so
this module stays gtkmm-free.

### `TimeFormat` -- shared formatter

```cpp
std::string formatIso8601Local(std::string_view iso8601Utc);
// "2026-05-17T20:02:51Z" -> "2026-05-17 23:02:51" (Europe/Bucharest)
```

Used by AuditLogPage + UsersPage + future audit-CSV exports.
Extracted so the parsing dance (`std::from_chars` per field,
`timegm` / `_mkgmtime` for cross-platform UTC->epoch) lives in
one place.

---

## Embedding in another C++ project

Minimum dependencies: C++20 compiler. **No GTK, no Boost, no
SQLite.** Optional: libintl for `i18n.cpp` (every Linux already
ships it; Windows ships via gettext-runtime).

### Bootstrap pattern

```cpp
#include "core/Bootstrap.h"

int main(int argc, char* argv[]) {
    app::core::Bootstrap bootstrap;
    if (!bootstrap.run()) {
        return 1;
    }

    auto& logger = bootstrap.logger();
    auto& config = bootstrap.config();

    // ... your application body here, using logger + config
    return 0;
}
```

### Result<T, E> in your own code

```cpp
#include "core/Result.h"

using app::core::Result;
using app::core::Ok;
using app::core::Err;

enum class ParseError { Empty, Malformed, Overflow };

Result<int, ParseError> parsePositiveInt(std::string_view s) {
    if (s.empty()) return Err(ParseError::Empty);
    int value;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{}) return Err(ParseError::Malformed);
    return Ok(value);
}
```

### Custom logger backend

```cpp
class JournaldLogger : public app::core::Logger {
    void log(Level lvl, std::string_view loc, std::string msg) override {
        sd_journal_send("MESSAGE=%s", msg.c_str(),
                        "PRIORITY=%d", priorityOf(lvl),
                        "CODE_FILE=%s", loc.data(),
                        NULL);
    }
};

// Swap from Bootstrap's default by re-assigning after run().
```

---

## Threading model

- **Logger writes** are mutex-guarded per-instance (one mutex
  inside `FileLogger`). Multiple loggers are independent.
- **`Result<T, E>` is value-like**, no synchronisation needed.
  `std::variant` storage is exclusive.
- **`Bootstrap::run()` is single-threaded** -- runs from main
  before any worker starts.
- **`i18n` rebind** is meant to be called from the GTK main
  thread (gettext globals are not thread-safe across rebinds).
  The console binary calls it once at startup; the GTK binary
  calls it again on each language switch.
- **`ConfigManager`** is read-only after `initialize()` returns;
  subsequent reads are safe from any thread.

---

## Testing

`tests/ResultTest.cpp` -- value vs error storage, copy / move,
chaining helpers, exception-safety guards.

`tests/ConfigManagerTest.cpp` -- JSON load happy path,
malformed-JSON fallback to defaults, missing-file fallback,
runtime override of individual keys.

`tests/LoggerImplTest.cpp` (implicit via other suites) -- file
rotation, level filtering, format passthrough.

The bootstrap itself is exercised end-to-end by every binary
build (smoke fact: `industrial-hmi --version` runs through
the full bootstrap and exits clean).

Run isolated:

```bash
cd build/debug
ctest -R '(Result|ConfigManager)' --output-on-failure
```

---

## Out of scope (intentional)

- **Metrics / observability** -- adding Prometheus / OpenTelemetry
  is a new `core/` file (a `MetricsClient` interface + a concrete
  per backend). Hasn't been needed yet; HMI runs with file logs.
- **Hot-reload of config** -- ConfigManager is read-once.
  Operator changes go through Settings UI + a runtime apply path
  in `ThemeManager` for theme bits; full hot-reload would
  require every layer to re-read its config, which is bigger
  than the current operator workflow needs.
- **C++ exceptions across DLL boundary** -- the ONNX plugin
  uses a C ABI specifically to avoid this; we never throw
  across module boundaries.
- **Pluggable allocator / memory tracking** -- not currently
  needed; the project runs comfortably with the system allocator.
