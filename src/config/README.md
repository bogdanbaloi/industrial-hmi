# `src/config/` -- Configuration Policy + Defaults

JSON-driven configuration surface for the entire application. One
file an operator edits (`config/app-config.json`) tunes auth, MQTT,
Modbus, OPC-UA, historian, UI palette, log level, and every other
knob the binary exposes. Compiled defaults in `config_defaults.h`
keep the binary runnable even when the JSON is missing or corrupt.

---

## Why this module exists separately

An industrial HMI deployed across N customer sites needs to be
re-tuned for each one: the broker hostname differs, the Modbus
unit id changes, the language is German instead of Romanian, the
audit retention is 6 months instead of 30 days. None of those are
worth a rebuild.

`src/config/` is the **single source of truth** for which knobs
exist and what their defaults are:

- **`config_defaults.h`** -- header-only constants. Compiled into
  the binary so a missing config file still produces a working
  application (degraded but functional).
- **`ConfigManager`** -- singleton that loads
  `config/app-config.json` over the defaults, exposes typed
  getters per concern, and owns the **policy** of what to do
  when a section is missing or malformed (warn + fall back, not
  crash). Also owns the shipped hot-reload path (`reload()` +
  `ReloadListener`s).
- **`ConfigValidator`** -- semantic / JSON-schema validation
  against `schemas/app-config.schema.json` (ADR-0015). A reload
  that parses but fails validation is rejected so a typo never
  half-applies.
- **`ConfigFileWatcher`** -- a `std::jthread` that watches the
  config file's mtime and calls `reload()` when it changes.

Keeping this in a self-contained module means every other layer
depends on a stable interface (`ConfigManager::getMqttBrokerHost()`)
rather than passing strings around, and adding a new knob is one
new default + one new getter -- no scatter across files.

---

## File map

```
src/config/
├── ConfigManager.{h,cpp}     Singleton config façade + per-section getters
│                             (nlohmann/json behind a PIMPL boundary, ADR-0015)
├── config_defaults.h         Header-only constants (paths, hostnames, sizes)
├── ConfigValidator.{h,cpp}   Semantic / JSON-schema validation (ADR-0015)
└── ConfigFileWatcher.{h,cpp}  std::jthread mtime watcher -> reload()
```

Four pairs, still a deliberately small policy surface. The
runtime cost stays near zero (`ConfigManager::instance()` is a
Meyers' singleton constructed on first use); the JSON parser is
kept behind a PIMPL boundary so its single-header bulk never
leaks into the 100+ call sites that touch an accessor.

---

## Architecture (SOLID at a glance)

```
                       config/app-config.json
                                │
                                ▼
                       ┌──────────────────┐
                       │  ConfigManager   │  loads JSON over defaults
                       │  (singleton)     │  logs warnings on missing keys
                       └────────┬─────────┘
                                │ getX() per concern
                                ▼
              ┌───────────────────────────────────┐
              │  Bootstrap, AuthService, MQTT,    │
              │  Modbus, OpcUa, Historian, Theme, │
              │  IntegrationManager, ...          │
              └───────────────────────────────────┘
```

**SOLID applied:**

- **S** -- ConfigManager has one job: load + expose typed knobs.
  No business logic, no I/O beyond reading the JSON file.
- **O** -- Adding a new knob is one new default + one new
  getter. Existing call sites don't recompile.
- **L** -- Tests inject a different config path (or pre-loaded
  in-memory map) without behavioural divergence.
- **I** -- Per-section getters (
  `getMqttBrokerHost`, `getModbusSlaveId`, `getAuthDbPath`, ...)
  so callers only need what they use. No "get the whole config
  blob" escape hatch.
- **D** -- Every layer takes the `ConfigManager` it needs by
  reference (or via `instance()`). The auth core, integration
  core, historian -- none depend on each other's config.

---

## API surface

### `config_defaults.h`

Compiled-in constants. Sample:

```cpp
namespace app::config::defaults {

// Files + folders
constexpr const char* kConfigPath        = "config/app-config.json";
constexpr const char* kLocaleDir         = "share/locale";
constexpr const char* kGtkAppId          = "com.example.industrial-hmi";

// Auth + historian DB paths
constexpr const char* kAuthDbPath        = "data/auth.sqlite";
constexpr const char* kHistorianDbPath   = "data/historian.sqlite";
constexpr const char* kProductsDbPath    = "data/products.sqlite";
constexpr const char* kAuditDbPath       = "data/audit.sqlite";

// MQTT defaults
constexpr const char* kMqttBrokerHost    = "127.0.0.1";
constexpr int         kMqttBrokerPort    = 1883;

// Modbus defaults
constexpr const char* kModbusHost        = "127.0.0.1";
constexpr int         kModbusPort        = 5020;
// (slave id default is inline in getModbusSlaveId(): "network.modbus.slave_id", 1)

// UI defaults
constexpr const char* kDefaultLanguage   = "auto";
constexpr const char* kDefaultPalette    = "Adwaita Dark";
constexpr const char* kDefaultLayout     = "Default";
constexpr unsigned    kAutoRefreshMs     = 2'000;

}  // namespace app::config::defaults
```

Every default is `constexpr` (or `inline constexpr` for strings)
so they vanish into the binary's read-only segment at compile
time.

### `ConfigManager`

```cpp
class ConfigManager {
public:
    static ConfigManager& instance();

    bool initialize(const std::string& path = defaults::kConfigPath);
    bool isInitialized() const;
    void setLogger(Logger& logger);

    // Section getters (typed):
    bool        isAuthEnabled() const;
    std::string getAuthDbPath() const;

    bool        isMqttEnabled() const;
    std::string getMqttBrokerHost() const;
    int         getMqttBrokerPort() const;

    bool        isModbusBackendEnabled() const;
    std::string getModbusHost() const;
    int         getModbusPort() const;
    int         getModbusSlaveId() const;

    bool        isHistorianEnabled() const;
    std::string getHistorianDbPath() const;
    int         getHistorianBatchSize() const;
    int         getHistorianBatchAgeMs() const;
    int         getHistorianSweepIntervalMs() const;      // ADR-0007
    int         getHistorianRawRetentionMs() const;       // ADR-0007
    int         getHistorianMinuteRetentionMs() const;    // ADR-0007

    std::string getLanguage() const;
    std::string getPalette() const;
    std::string getLayout() const;
    int         getLogLevel() const;
    bool        isConsoleLoggingEnabled() const;
    std::string getLogFilePath() const;
    // ... ~30 more
};
```

Pattern per getter:

```cpp
std::string ConfigManager::getMqttBrokerHost() const {
    return getString("network.mqtt.broker_host",
                     defaults::kMqttBrokerHost);
}
```

The fallback to `defaults::*` means a missing JSON key never
returns an empty string or a sentinel; callers get a working
value or the documented default.

### Policy: `applyI18n` (lives here, mechanism in `core/`)

```cpp
void ConfigManager::applyI18n() {
    const auto language = isInitialized() ? getLanguage() : "auto";
    app::core::initI18n(defaults::kLocaleDir, language.c_str());
}
```

Pattern repeated for future `applyTheme`, `applyPalette`,
`applyLogging` -- ConfigManager decides what to do, the target
subsystem decides how. Keeps each side single-responsibility.

---

## Embedding in another C++ project

Minimum dependencies: C++20 compiler. Header-only JSON parser
already vendored (`nlohmann/json` via `single_include`) -- no
external dependency to wire.

### Bootstrap

```cpp
#include "config/ConfigManager.h"

auto& cfg = app::config::ConfigManager::instance();
cfg.setLogger(logger);   // optional; warnings go to stderr otherwise
const bool ok = cfg.initialize("config/app-config.json");
if (!ok) {
    logger.warn("Config load failed; falling back to compiled defaults");
}
```

`initialize()` returns false on file-not-found / parse error;
the manager still works (every getter returns its compiled
default). Useful for unit tests that don't ship a JSON.

### Reading a knob

```cpp
if (cfg.isMqttEnabled()) {
    auto host = cfg.getMqttBrokerHost();
    auto port = cfg.getMqttBrokerPort();
    // ... wire MqttClient
}
```

### Adding a new knob

1. Add the default in `config_defaults.h`:
   ```cpp
   constexpr int kAlertRetentionDays = 7;
   ```
2. Add the getter in `ConfigManager.h`:
   ```cpp
   int getAlertRetentionDays() const {
       return getInt("alerts.retention_days",
                     defaults::kAlertRetentionDays);
   }
   ```
3. Document the JSON path in `config/app-config.json`:
   ```json
   "alerts": { "retention_days": 7 }
   ```

Done. The change is local; no scatter.

---

## Threading model

- **`initialize()` is called once** at startup, before any
  worker thread spawns. Concurrent reads from any thread are
  safe (string copies returned by value).
- **Guarded by a mutex** (REQ-CORE-008): every read/write of the
  flat-key map goes through a `mutable std::recursive_mutex
  config_mutex_`. Recursive because `reload()` holds the lock
  across the `ConfigValidator::validate(*this)` call, which
  recurses back through our own public getters on the same
  thread; a plain `std::mutex` would self-deadlock. The
  read-heavy workload tolerates the slightly costlier primitive
  (config access is sub-microsecond and rare versus the alarm /
  dashboard hot paths).
- **Hot-reload is shipped** (REQ-CORE-006/008/009):
  `reload()` re-reads the configured path and swaps the in-memory
  map atomically -- on parse failure or validator rejection the
  previous config is preserved (an operator typo never
  half-applies). `ConfigFileWatcher` is a `std::jthread` mtime
  watcher that calls `reload()` when the file changes;
  `addReloadListener()` / `ReloadListener` fire after a successful
  swap so consumers (e.g. Bootstrap re-applying `applyI18n()`)
  re-bind to the new values. The Settings page also exposes a
  subset (palette, language, log level) for direct runtime change
  through `setLanguage` / `setPalette` persisting setters.

---

## Testing

`tests/ConfigManagerTest.cpp` -- happy path (well-formed JSON
loaded into expected values), malformed JSON (fallback to
defaults), missing keys per section (fallback to defaults),
runtime override path.

The module is also smoke-tested by every binary launch in CI
(missing config -> compiled defaults -> binary still boots and
runs unit-tests).

Run isolated:

```bash
cd build/debug
ctest -R ConfigManager --output-on-failure
```

---

## The JSON config in practice

The shipped `config/app-config.json` is the canonical example.
Sections (truncated):

```json
{
  "network": {
    "tcp": { "enabled": true, "port": 5555 },
    "mqtt": {
      "enabled": true,
      "broker_host": "mosquitto",
      "broker_port": 1883
    },
    "modbus": {
      "enabled": true,
      "host": "modbus-slave",
      "port": 5020,
      "unit_id": 1
    }
  },
  "historian": {
    "enabled": false,
    "db_path": "data/historian.sqlite",
    "batch_size": 32,
    "batch_age_ms": 5000
  },
  "auth": {
    "enabled": false,
    "db_path": "data/auth.sqlite"
  },
  "ui": {
    "language": "auto",
    "palette": "Adwaita Dark",
    "layout": "Default"
  },
  "logging": {
    "level": "info",
    "console": true,
    "file_path": "logs/industrial-hmi.log"
  }
}
```

Docker compose mounts a different file
(`docker/app-config.docker.json`) over this default so service
hostnames resolve via Docker DNS (`mosquitto`, `modbus-slave`)
rather than IPs.

---

## Out of scope (intentional)

- **Encrypted secrets** -- production deployments inject secrets
  via environment variables read at startup, not via JSON. The
  config layer doesn't try to be a secrets manager.
- **Multi-file include / overlay** -- one JSON file. Adding
  include semantics (`"include": "site-overrides.json"`) would
  be useful but no operator has asked.
