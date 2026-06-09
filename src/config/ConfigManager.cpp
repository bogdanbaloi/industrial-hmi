// SPDX-License-Identifier: MIT
//
// ConfigManager - JSON Configuration Management (implementation)
//
// Heavy include surface (nlohmann/json.hpp ~25k lines) is confined to
// this translation unit. The header (ConfigManager.h) carries only
// declarations and a flat std::map<std::string,std::string> -- so the
// rest of the codebase doesn't pay the include cost just to read a
// config value.
//
// Parsing strategy:
//   1. nlohmann::json::parse(file) -> DOM
//   2. flatten() recursively walks the DOM and writes scalar leaves
//      into config_ under dot-joined paths ("dashboard.equipment_lines.0.name").
//   3. All existing getXxx() accessors hit the flat map untouched.
//
// This trade-off keeps the public API stable (no churn at call sites)
// while replacing a hand-rolled parser that had two known correctness
// bugs:
//   - bare `{` inside string values (e.g. "Delete \"{product_name}\"?")
//     pushed a bogus path frame and desynced every subsequent key
//   - the file shape depended on JSON key-order; running a formatter
//     that sorts keys (Prettier / JSON::PP) corrupted nested paths
// Both classes of bug vanish once a real RFC 8259 parser owns the
// tokeniser. See ADR-0015.

#include "ConfigManager.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ConfigValidator.h"

namespace app::config {

namespace {

using ordered_json = nlohmann::ordered_json;

/// Flatten a JSON value into a flat map of dot-joined paths to
/// string-rendered scalars. Mirrors the leaf shape the legacy
/// hand-rolled parser produced, so every getValue/getInt/getFloat call
/// site keeps working unchanged.
///
/// Implemented with an explicit std::vector frame stack rather than a
/// natural recursive DFS. The recursive form is more idiomatic for a
/// tree traversal, but our clang-tidy config gates `misc-no-recursion`
/// as an error (CI WarningsAsErrors), and a NOLINT suppression on a
/// function that calls ITSELF didn't take -- the diagnostic fires on
/// both the function and the call-site frame. Explicit stack removes
/// the warning structurally and bounds the maximum depth by std::vector
/// heap capacity (well above any realistic nesting; nlohmann already
/// rejects DOMs nested past 1000 frames at parse time).
///
/// Encoding rules per JSON scalar type, chosen to match the legacy
/// parser exactly:
///   - string : raw value (no surrounding quotes)
///   - number : default nlohmann string form (`std::to_string` for ints,
///              shortest-round-trip for doubles)
///   - bool   : "true" / "false"
///   - null   : "" (empty -- legacy parser produced the same)
///
/// Arrays use numeric indices as path segments
/// ("dashboard.equipment_lines.0.name").
///
/// Traversal order is reverse vs the recursive form (LIFO pop order
/// from the stack), but `out` is a std::map so insertion order does
/// not affect lookup -- all existing call sites are key-based.
void flatten(const ordered_json& root,
             std::map<std::string, std::string>& out) {
    struct Frame {
        const ordered_json* node;
        std::string prefix;
    };
    std::vector<Frame> stack;
    stack.push_back({&root, std::string{}});

    while (!stack.empty()) {
        Frame frame = std::move(stack.back());
        stack.pop_back();
        const ordered_json& node = *frame.node;
        const std::string& prefix = frame.prefix;

        if (node.is_object()) {
            for (auto it = node.begin(); it != node.end(); ++it) {
                std::string nextKey =
                    prefix.empty() ? it.key()
                                   : prefix + "." + it.key();
                stack.push_back({&it.value(), std::move(nextKey)});
            }
            continue;
        }
        if (node.is_array()) {
            for (std::size_t i = 0; i < node.size(); ++i) {
                std::string nextKey =
                    prefix.empty()
                        ? std::to_string(i)
                        : prefix + "." + std::to_string(i);
                stack.push_back({&node[i], std::move(nextKey)});
            }
            continue;
        }

        // Scalar leaf.
        if (prefix.empty()) {
            // Top-level scalar config (file is just `42` or `"foo"`).
            // Nothing meaningful to bind it to; skip rather than insert
            // under an empty key.
            continue;
        }
        if (node.is_string()) {
            out[prefix] = node.get<std::string>();
        } else if (node.is_boolean()) {
            out[prefix] = node.get<bool>() ? "true" : "false";
        } else if (node.is_number_integer()) {
            // Fixed-width int types (google-runtime-int): cstdint
            // typedefs are mandated by the project's clang-tidy config
            // and pin the serialised range explicitly to 64 bits
            // regardless of the platform's `long long` width.
            out[prefix] = std::to_string(node.get<std::int64_t>());
        } else if (node.is_number_unsigned()) {
            out[prefix] = std::to_string(node.get<std::uint64_t>());
        } else if (node.is_number_float()) {
            // nlohmann's dump() emits the shortest round-trip
            // representation for doubles -- exactly what std::stof /
            // std::stoi consume on the way out.
            out[prefix] = node.dump();
        } else {
            // null or unknown -- empty string matches legacy behaviour.
            out[prefix] = "";
        }
    }
}

}  // namespace

ConfigManager& ConfigManager::instance() {
    static ConfigManager inst;
    return inst;
}

ConfigManager::ConfigManager() = default;
ConfigManager::~ConfigManager() = default;

bool ConfigManager::initialize(const std::string& configPath) {
    configPath_ = configPath;
    initialized_ = loadConfig();
    return initialized_;
}

bool ConfigManager::reload() {
    // REQ-CORE-006 Phase 1: re-read + re-validate + atomic swap.
    // REQ-CORE-008 Phase 3a: internal mutex over the swap + validate.
    // REQ-CORE-009 Phase 3b: registered listeners fire AFTER lock
    //                        release, so a listener may safely call
    //                        back into ConfigManager getters from any
    //                        thread without deadlocking on a
    //                        non-recursive-aware caller.
    bool success;
    std::vector<ReloadListener> snapshot;
    {
        const std::scoped_lock<std::recursive_mutex> lock(config_mutex_);
        success = reloadLocked();
        if (success) {
            // Copy the listener list under the lock so registration
            // is race-free against dispatch; fire OUTSIDE the lock.
            snapshot = reloadListeners_;
        }
    }
    if (success) {
        for (auto& l : snapshot) {
            try {
                l();
            } catch (const std::exception& e) {
                if (logger_) {
                    logger_->warn(
                        "Config reload listener threw: {}; "
                        "continuing with remaining listeners",
                        e.what());
                }
            } catch (...) {
                if (logger_) {
                    logger_->warn(
                        "Config reload listener threw (non-std "
                        "type); continuing with remaining listeners");
                }
            }
        }
    }
    return success;
}

bool ConfigManager::reloadLocked() {
    // Body of reload() with the caller's lock already held. Order:
    //   1. Open + parse the file into a temporary DOM
    //   2. Flatten into a fresh local map (NOT into config_ yet)
    //   3. Move-swap config_ <-> next; remember previous for rollback
    //   4. Run ConfigValidator against the new state
    //   5. If validation rejects, roll back; else keep the new state
    if (configPath_.empty()) {
        // No prior initialize() means there is no path to reload from.
        // This is a programmer error, not a runtime config failure.
        return false;
    }

    std::ifstream file(configPath_);
    if (!file.is_open()) {
        if (logger_) {
            logger_->warn(
                "Config reload: cannot open '{}'; keeping previous config",
                configPath_);
        }
        return false;
    }

    ordered_json root;
    try {
        root = ordered_json::parse(
            file,
            /*cb=*/nullptr,
            /*allow_exceptions=*/true,
            /*ignore_comments=*/true);
    } catch (const nlohmann::json::parse_error&) {
        if (logger_) {
            logger_->warn(
                "Config reload: parse error in '{}'; keeping previous config",
                configPath_);
        }
        return false;
    }

    std::map<std::string, std::string> nextConfig;
    flatten(root, nextConfig);

    // Move-swap; remember the previous map so we can roll back if the
    // validator rejects the new state. We don't fall through to the
    // happy path until the validator says ok.
    std::map<std::string, std::string> prevConfig = std::move(config_);
    config_ = std::move(nextConfig);

    const auto result = ConfigValidator::validate(*this);
    if (!result.ok) {
        // Roll back. The caller sees `false`; subsequent getter calls
        // resolve against the previous (still-valid) map.
        config_ = std::move(prevConfig);
        if (logger_) {
            logger_->warn(
                "Config reload: semantic validation rejected '{}' "
                "({} violation{}); keeping previous config",
                configPath_,
                result.errors.size(),
                result.errors.size() == 1 ? "" : "s");
        }
        return false;
    }

    // The new state is live. `initialized_` was already true; nothing
    // to flip.
    if (logger_) {
        logger_->info("Config reload: applied new state from '{}'",
                      configPath_);
    }
    return true;
}

void ConfigManager::setLogger(app::core::Logger& logger) {
    logger_ = &logger;
}

void ConfigManager::addReloadListener(ReloadListener listener) {
    // Hold the lock for the push_back so registration is race-free
    // against a concurrent reload() snapshotting the vector.
    const std::scoped_lock<std::recursive_mutex> lock(config_mutex_);
    reloadListeners_.push_back(std::move(listener));
}

void ConfigManager::clearReloadListeners() {
    const std::scoped_lock<std::recursive_mutex> lock(config_mutex_);
    reloadListeners_.clear();
}

// applyI18n is defined inline in the header so the dependency on
// app::core::initI18n() stays a property of the caller's TU, not
// ConfigManager.cpp -- see the header comment for the link rationale.

void ConfigManager::clear() {
    const std::scoped_lock<std::recursive_mutex> lock(config_mutex_);
    config_.clear();
    configPath_.clear();
}

bool ConfigManager::isInitialized() const { return initialized_; }

std::string ConfigManager::rawValue(const std::string& key) const {
    return getValue(key, "");
}

// -----------------------------------------------------------------------
// Asset paths
// -----------------------------------------------------------------------

std::string ConfigManager::getAssetPath(const std::string& category,
                                        const std::string& name) const {
    return getValue("assets." + category + "." + name, "");
}
std::string ConfigManager::getAppIcon() const {
    return getAssetPath("icons", "app_icon");
}
std::string ConfigManager::getWindowIconName() const {
    return getAssetPath("icons", "window_icon_name");
}
std::string ConfigManager::getUIFile(const std::string& name) const {
    return getAssetPath("ui_files", name);
}

// -----------------------------------------------------------------------
// Dialog Configuration
// -----------------------------------------------------------------------

std::string ConfigManager::getDialogTitle(const std::string& category,
                                          const std::string& name) const {
    return getValue("dialogs." + category + "." + name + ".title",
                    defaults::kDialogTitle);
}

std::string ConfigManager::getDialogMessage(const std::string& category,
                                            const std::string& name) const {
    const std::scoped_lock<std::recursive_mutex> lock(config_mutex_);
    const auto base = "dialogs." + category + "." + name;
    auto it = config_.find(base + ".message");
    if (it != config_.end()) return it->second;
    it = config_.find(base + ".message_template");
    return (it != config_.end()) ? it->second : "";
}

std::string ConfigManager::getDialogIcon(const std::string& category,
                                         const std::string& name) const {
    return getValue("dialogs." + category + "." + name + ".icon",
                    defaults::kDialogIcon);
}

std::string ConfigManager::getConfirmButton(
    const std::string& dialogName) const {
    return getValue(
        "dialogs.confirmations." + dialogName + ".confirm_button",
        defaults::kConfirmButton);
}

std::string ConfigManager::getCancelButton(
    const std::string& dialogName) const {
    return getValue(
        "dialogs.confirmations." + dialogName + ".cancel_button",
        defaults::kCancelButton);
}

// -----------------------------------------------------------------------
// Application Settings
// -----------------------------------------------------------------------

std::string ConfigManager::getAppName() const {
    return getValue("application.name", defaults::kAppName);
}
std::string ConfigManager::getAppVersion() const {
    return getValue("application.version", defaults::kAppVersion);
}
std::string ConfigManager::getWindowTitle() const {
    return getValue("window.title", defaults::kWindowTitle);
}
int ConfigManager::getWindowWidth() const {
    return getInt("window.default_width", defaults::kWindowWidth);
}
int ConfigManager::getWindowHeight() const {
    return getInt("window.default_height", defaults::kWindowHeight);
}
std::string ConfigManager::getDefaultTheme() const {
    return getValue("theme.default", defaults::kDefaultTheme);
}

// -----------------------------------------------------------------------
// i18n + palette
// -----------------------------------------------------------------------

std::string ConfigManager::getLanguage() const {
    return getValue("i18n.language", defaults::kDefaultLanguage);
}

bool ConfigManager::setLanguage(const std::string& language) {
    {
        // Lock only the in-memory write. persistLanguage performs file
        // I/O which we deliberately keep OUTSIDE the lock: holding a
        // mutex across disk writes would stall every getter for the
        // duration of fsync, which is unbounded on a busy host.
        const std::scoped_lock<std::recursive_mutex> lock(config_mutex_);
        config_["i18n.language"] = language;
    }
    return persistLanguage(language);
}

std::string ConfigManager::getPalette() const {
    return getValue("ui.palette", "");
}

bool ConfigManager::setPalette(const std::string& palette) {
    {
        // Same pattern as setLanguage: lock the in-memory write only,
        // do disk I/O outside the lock.
        const std::scoped_lock<std::recursive_mutex> lock(config_mutex_);
        config_["ui.palette"] = palette;
    }
    return persistPalette(palette);
}

// -----------------------------------------------------------------------
// Logging
// -----------------------------------------------------------------------

std::string ConfigManager::getLogLevel() const {
    return getValue("logging.level", defaults::kLogLevel);
}
std::string ConfigManager::getLogFilePath() const {
    return getValue("logging.file", defaults::kLogFile);
}
std::size_t ConfigManager::getLogMaxFileSize() const {
    auto mb = getInt("logging.max_file_size_mb",
                     static_cast<int>(defaults::kLogMaxFileSizeMB));
    return static_cast<std::size_t>(mb) * defaults::kBytesPerMegabyte;
}
int ConfigManager::getLogMaxFiles() const {
    return getInt("logging.max_files", defaults::kLogMaxFiles);
}
bool ConfigManager::getLogConsoleEnabled() const {
    return getValue("logging.console", "true") == "true";
}

// -----------------------------------------------------------------------
// Integration backends - TCP / Auth / Multi-station / Historian
// -----------------------------------------------------------------------

bool ConfigManager::isTcpBackendEnabled() const {
    return getValue("network.tcp.enabled", "false") == "true";
}
int ConfigManager::getTcpBackendPort() const {
    return getInt("network.tcp.port", defaults::kTcpBackendPort);
}

bool ConfigManager::isAuthEnabled() const {
    return getValue("auth.enabled", "false") == "true";
}
std::string ConfigManager::getAuthDbPath() const {
    return getValue("auth.db_path", defaults::kAuthDbPath);
}

bool ConfigManager::isMultiStationEnabled() const {
    return getValue("ui.multistation_enabled", "false") == "true";
}

bool ConfigManager::isHistorianEnabled() const {
    return getValue("historian.enabled", "false") == "true";
}
std::string ConfigManager::getHistorianDbPath() const {
    return getValue("historian.db_path", defaults::kHistorianDbPath);
}
int ConfigManager::getHistorianBatchSize() const {
    return getInt("historian.batch_size", defaults::kHistorianBatchSize);
}
int ConfigManager::getHistorianBatchAgeMs() const {
    return getInt("historian.batch_age_ms",
                  defaults::kHistorianBatchAgeMs);
}
int ConfigManager::getHistorianSweepIntervalMs() const {
    return getInt("historian.sweep_interval_ms",
                  defaults::kHistorianSweepIntervalMs);
}
int ConfigManager::getHistorianRawRetentionMs() const {
    return getInt("historian.raw_retention_ms",
                  defaults::kHistorianRawRetentionMs);
}
int ConfigManager::getHistorianMinuteRetentionMs() const {
    return getInt("historian.minute_retention_ms",
                  defaults::kHistorianMinuteRetentionMs);
}

// -----------------------------------------------------------------------
// MQTT
// -----------------------------------------------------------------------

bool ConfigManager::isMqttBackendEnabled() const {
    return getValue("network.mqtt.enabled", "false") == "true";
}
std::string ConfigManager::getMqttBrokerHost() const {
    return getValue("network.mqtt.broker_host", defaults::kMqttBrokerHost);
}
int ConfigManager::getMqttBrokerPort() const {
    return getInt("network.mqtt.broker_port", defaults::kMqttBrokerPort);
}
std::string ConfigManager::getMqttClientId() const {
    return getValue("network.mqtt.client_id", defaults::kMqttClientId);
}
std::string ConfigManager::getMqttTopicPrefix() const {
    return getValue("network.mqtt.topic_prefix",
                    defaults::kMqttTopicPrefix);
}
bool ConfigManager::isMqttEmitPlainText() const {
    return getValue("network.mqtt.emit_plain_text", "true") == "true";
}
bool ConfigManager::isMqttEmitJson() const {
    return getValue("network.mqtt.emit_json", "false") == "true";
}
bool ConfigManager::isMqttSubscriberEnabled() const {
    return getValue("network.mqtt.subscriber.enabled", "false") == "true";
}
std::string ConfigManager::getMqttSensorTopicPrefix() const {
    return getValue("network.mqtt.subscriber.topic_prefix",
                    defaults::kMqttSensorTopicPrefix);
}

// -----------------------------------------------------------------------
// Modbus
// -----------------------------------------------------------------------

bool ConfigManager::isModbusBackendEnabled() const {
    return getValue("network.modbus.enabled", "false") == "true";
}
std::string ConfigManager::getModbusHost() const {
    return getValue("network.modbus.host", defaults::kModbusHost);
}
int ConfigManager::getModbusPort() const {
    return getInt("network.modbus.port", defaults::kModbusPort);
}
int ConfigManager::getModbusPollIntervalMs() const {
    return getInt("network.modbus.poll_interval_ms",
                  defaults::kModbusPollIntervalMs);
}
int ConfigManager::getModbusConnectTimeoutMs() const {
    return getInt("network.modbus.connect_timeout_ms",
                  defaults::kModbusConnectTimeoutMs);
}
int ConfigManager::getModbusRequestTimeoutMs() const {
    return getInt("network.modbus.request_timeout_ms",
                  defaults::kModbusRequestTimeoutMs);
}
int ConfigManager::getModbusSlaveId() const {
    return getInt("network.modbus.slave_id", 1);
}
int ConfigManager::getModbusEquipmentBaseAddress() const {
    return getInt("network.modbus.equipment_base_address", 0);
}
int ConfigManager::getModbusEquipmentCount() const {
    return getInt("network.modbus.equipment_count", 3);
}
int ConfigManager::getModbusSupplyBaseAddress() const {
    return getInt("network.modbus.supply_base_address",
                  defaults::kModbusSupplyBaseAddress);
}
float ConfigManager::getModbusSupplyScale() const {
    return getFloat("network.modbus.supply_scale",
                    defaults::kModbusSupplyScale);
}
int ConfigManager::getModbusQualityBaseAddress() const {
    return getInt("network.modbus.quality_base_address",
                  defaults::kModbusQualityBaseAddress);
}
float ConfigManager::getModbusQualityScale() const {
    return getFloat("network.modbus.quality_scale",
                    defaults::kModbusQualityScale);
}
int ConfigManager::getModbusQualityCount() const {
    return getInt("network.modbus.quality_count", 3);
}

// -----------------------------------------------------------------------
// OPC-UA
// -----------------------------------------------------------------------

bool ConfigManager::isOpcUaBackendEnabled() const {
    return getValue("network.opcua.enabled", "false") == "true";
}
int ConfigManager::getOpcUaServerPort() const {
    return getInt("network.opcua.port", defaults::kOpcUaServerPort);
}
std::string ConfigManager::getOpcUaApplicationUri() const {
    return getValue("network.opcua.application_uri",
                    defaults::kOpcUaApplicationUri);
}
std::string ConfigManager::getOpcUaApplicationName() const {
    return getValue("network.opcua.application_name",
                    defaults::kOpcUaApplicationName);
}
bool ConfigManager::isOpcUaServerCommandsEnabled() const {
    return getValue("network.opcua.server.commands_enabled", "true") ==
           "true";
}
bool ConfigManager::isOpcUaClientEnabled() const {
    return getValue("network.opcua.client.enabled", "false") == "true";
}
std::string ConfigManager::getOpcUaClientEndpoint() const {
    return getValue("network.opcua.client.endpoint",
                    defaults::kOpcUaClientEndpoint);
}
std::string ConfigManager::getOpcUaClientApplicationUri() const {
    return getValue("network.opcua.client.application_uri",
                    defaults::kOpcUaClientApplicationUri);
}
std::string ConfigManager::getOpcUaClientApplicationName() const {
    return getValue("network.opcua.client.application_name",
                    defaults::kOpcUaClientApplicationName);
}
bool ConfigManager::isOpcUaIngestBridgeEnabled() const {
    return getValue("network.opcua.client.ingest_bridge.enabled",
                    "false") == "true";
}
std::string ConfigManager::getOpcUaIngestBridgeTopicPrefix() const {
    return getValue("network.opcua.client.ingest_bridge.topic_prefix",
                    defaults::kOpcUaClientIngestPrefix);
}

// -----------------------------------------------------------------------
// Template support
// -----------------------------------------------------------------------

std::string ConfigManager::formatMessage(
    const std::string& templateStr,
    const std::map<std::string, std::string>& vars) const {
    std::string result = templateStr;
    for (const auto& [key, value] : vars) {
        const std::string placeholder = "{" + key + "}";
        std::size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), value);
            pos += value.length();
        }
    }
    return result;
}

// -----------------------------------------------------------------------
// Private helpers
// -----------------------------------------------------------------------

std::string ConfigManager::getValue(const std::string& key,
                                    const std::string& defaultValue) const {
    // REQ-CORE-008: every read of `config_` is mutex-protected so it is
    // race-free against a concurrent `reload()` from ConfigFileWatcher.
    // Recursive mutex so `reload()` can hold the lock during validation
    // and have validate() recurse back through this method on the same
    // thread.
    const std::scoped_lock<std::recursive_mutex> lock(config_mutex_);
    auto it = config_.find(key);
    return (it != config_.end()) ? it->second : defaultValue;
}

int ConfigManager::getInt(const std::string& key, int defaultValue) const {
    const std::scoped_lock<std::recursive_mutex> lock(config_mutex_);
    auto it = config_.find(key);
    if (it == config_.end()) return defaultValue;
    try {
        return std::stoi(it->second);
    } catch (...) {
        return defaultValue;
    }
}

float ConfigManager::getFloat(const std::string& key,
                              float defaultValue) const {
    const std::scoped_lock<std::recursive_mutex> lock(config_mutex_);
    auto it = config_.find(key);
    if (it == config_.end()) return defaultValue;
    try {
        return std::stof(it->second);
    } catch (...) {
        return defaultValue;
    }
}

bool ConfigManager::loadConfig() {
    std::ifstream file(configPath_);
    if (!file.is_open()) return false;

    // ordered_json preserves the original file's key order. Strictly,
    // the flat map we build doesn't need that (lookups are by key), but
    // ordered traversal makes the in-memory shape match the file shape
    // for tests that compare flatten() output for debugging.
    ordered_json root;
    try {
        // allow_comments = true mirrors the legacy parser's tolerance of
        // `// ...` and `#` lines, since several existing config samples
        // use them as section banners. strict = false keeps trailing
        // commas working too. exceptions = true so we catch and convert
        // them into a clean false return.
        root = ordered_json::parse(
            file,
            /*cb=*/nullptr,
            /*allow_exceptions=*/true,
            /*ignore_comments=*/true);
    } catch (const nlohmann::json::parse_error&) {
        // Parse error -- leave config_ empty, initialized_ stays false.
        // Bootstrap's outer guard surfaces this as ConfigCorrupt.
        return false;
    }

    // REQ-CORE-008: even though loadConfig() is typically called only
    // from initialize() (single-threaded by convention before any
    // watcher exists), lock the write so the invariant "every mutation
    // of config_ holds config_mutex_" is enforced uniformly. Cheap on
    // the cold path; eliminates a foot-gun for any future re-entry.
    const std::scoped_lock<std::recursive_mutex> lock(config_mutex_);
    config_.clear();
    flatten(root, config_);
    return true;
}

bool ConfigManager::persistLanguage(const std::string& language) {
    const std::string insertion =
        "\n  \"i18n\": {\n    \"language\": \"" + language + "\"\n  },\n";
    return persistStringField("language", language, insertion);
}

bool ConfigManager::persistPalette(const std::string& palette) {
    const std::string insertion =
        "\n  \"ui\": {\n    \"palette\": \"" + palette + "\"\n  },\n";
    return persistStringField("palette", palette, insertion);
}

bool ConfigManager::persistStringField(
    const std::string& fieldName,
    const std::string& value,
    const std::string& insertionIfMissing) {
    std::ifstream in(configPath_);
    if (!in.is_open()) return false;
    std::stringstream buffer;
    buffer << in.rdbuf();
    in.close();
    std::string content = buffer.str();

    const std::string key = "\"" + fieldName + "\"";
    const std::size_t keyPos = content.find(key);
    bool replaced = false;
    if (keyPos != std::string::npos) {
        const std::size_t colon = content.find(':', keyPos + key.size());
        if (colon != std::string::npos) {
            const std::size_t quoteStart = content.find('"', colon + 1);
            if (quoteStart != std::string::npos) {
                const std::size_t quoteEnd =
                    content.find('"', quoteStart + 1);
                if (quoteEnd != std::string::npos) {
                    content.replace(quoteStart + 1,
                                    quoteEnd - quoteStart - 1,
                                    value);
                    replaced = true;
                }
            }
        }
    }

    if (!replaced) {
        const std::size_t brace = content.find('{');
        if (brace == std::string::npos) return false;
        content.insert(brace + 1, insertionIfMissing);
    }

    const std::string tmpPath = configPath_ + ".tmp";
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out << content;
        if (!out.good()) return false;
    }
    std::remove(configPath_.c_str());
    return std::rename(tmpPath.c_str(), configPath_.c_str()) == 0;
}

}  // namespace app::config
