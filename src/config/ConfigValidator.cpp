// SPDX-License-Identifier: MIT
//
// ConfigValidator -- semantic validation rules for app-config.json.
//
// Rule set chosen by examining every ConfigManager accessor in production
// code and asking "which inputs would cause silent malfunction?":
//   - integer ports (1..65535)
//   - bounded enums (log level, language code)
//   - positive-only durations / counts / sizes
//   - presence of required string fields when their owning feature is on
//
// Rules that are LENIENT on purpose (do NOT validate):
//   - Optional string fields that have sensible defaults already
//     baked into ConfigManager::getValue (paths, broker hosts, topic
//     prefixes). The accessor returns the default when the key is
//     absent, so refusing to start because an OPTIONAL field is empty
//     would be a regression versus the legacy hand-rolled parser.
//   - The `dialogs.*` subtree (purely UI strings, no failure mode
//     beyond "label says 'OK' in English").
//
// Whenever you add a new feature flag + port pair to ConfigManager,
// also add the matching range rule here and update
// schemas/app-config.schema.json with the same constraint. The two
// files together form the contract -- the schema is documentation
// for tooling, this .cpp is the runtime enforcement (ADR-0015).

#include "ConfigValidator.h"

#include "ConfigManager.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace app::config {

namespace {

constexpr int kMinPort = 1;
constexpr int kMaxPort = 65535;

bool isValidPort(int p) { return p >= kMinPort && p <= kMaxPort; }

constexpr std::array<std::string_view, 5> kLogLevels{
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR"};

constexpr std::array<std::string_view, 12> kLanguageCodes{
    "auto",  "en",    "de",    "es",    "es_MX", "fi",
    "fr",    "ga",    "it",    "pt",    "pt_BR", "sv"};

template <std::size_t N>
bool oneOf(const std::array<std::string_view, N>& choices,
           const std::string& v) {
    return std::find(choices.begin(), choices.end(), v) != choices.end();
}

void checkLogging(const ConfigManager& cfg,
                  std::vector<std::string>& errors) {
    const auto level = cfg.getLogLevel();
    if (!oneOf(kLogLevels, level)) {
        errors.emplace_back("logging.level: '" + level +
                            "' is not one of TRACE|DEBUG|INFO|WARN|ERROR");
    }
    if (cfg.getLogMaxFiles() < 1) {
        errors.emplace_back("logging.max_files: must be >= 1");
    }
    // max_file_size is a std::size_t -- already non-negative by type.
    // The accessor multiplies by 1MB, so a configured 0 yields a 0-byte
    // ceiling which would degrade logging into "never write". Reject.
    if (cfg.getLogMaxFileSize() == 0) {
        errors.emplace_back("logging.max_file_size_mb: must be >= 1");
    }
}

void checkI18n(const ConfigManager& cfg,
               std::vector<std::string>& errors) {
    const auto lang = cfg.getLanguage();
    if (!oneOf(kLanguageCodes, lang)) {
        errors.emplace_back(
            "i18n.language: '" + lang +
            "' is not a recognised LINGUAS code (expected 'auto' or one of "
            "en|de|es|es_MX|fi|fr|ga|it|pt|pt_BR|sv)");
    }
}

void checkWindow(const ConfigManager& cfg,
                 std::vector<std::string>& errors) {
    if (cfg.getWindowWidth() <= 0) {
        errors.emplace_back("window.default_width: must be > 0");
    }
    if (cfg.getWindowHeight() <= 0) {
        errors.emplace_back("window.default_height: must be > 0");
    }
}

void checkBackends(const ConfigManager& cfg,
                   std::vector<std::string>& errors) {
    // Only validate port ranges when the matching backend is ENABLED.
    // A disabled backend with a placeholder port should not block startup.
    if (cfg.isTcpBackendEnabled() && !isValidPort(cfg.getTcpBackendPort())) {
        errors.emplace_back("network.tcp.port: out of range (1..65535)");
    }
    if (cfg.isMqttBackendEnabled() &&
        !isValidPort(cfg.getMqttBrokerPort())) {
        errors.emplace_back(
            "network.mqtt.broker_port: out of range (1..65535)");
    }
    if (cfg.isModbusBackendEnabled() &&
        !isValidPort(cfg.getModbusPort())) {
        errors.emplace_back(
            "network.modbus.port: out of range (1..65535)");
    }
    if (cfg.isOpcUaBackendEnabled() &&
        !isValidPort(cfg.getOpcUaServerPort())) {
        errors.emplace_back(
            "network.opcua.port: out of range (1..65535)");
    }
}

void checkHistorian(const ConfigManager& cfg,
                    std::vector<std::string>& errors) {
    if (!cfg.isHistorianEnabled()) return;
    if (cfg.getHistorianBatchSize() <= 0) {
        errors.emplace_back("historian.batch_size: must be > 0 when enabled");
    }
    if (cfg.getHistorianBatchAgeMs() <= 0) {
        errors.emplace_back(
            "historian.batch_age_ms: must be > 0 when enabled");
    }
    if (cfg.getHistorianSweepIntervalMs() <= 0) {
        errors.emplace_back(
            "historian.sweep_interval_ms: must be > 0 when enabled");
    }
}

void checkModbus(const ConfigManager& cfg,
                 std::vector<std::string>& errors) {
    if (!cfg.isModbusBackendEnabled()) return;
    if (cfg.getModbusEquipmentCount() <= 0) {
        errors.emplace_back(
            "network.modbus.equipment_count: must be > 0 when enabled");
    }
    if (cfg.getModbusPollIntervalMs() <= 0) {
        errors.emplace_back(
            "network.modbus.poll_interval_ms: must be > 0 when enabled");
    }
    if (cfg.getModbusSupplyScale() <= 0.0f) {
        errors.emplace_back(
            "network.modbus.supply_scale: must be > 0 when enabled");
    }
    if (cfg.getModbusQualityScale() <= 0.0f) {
        errors.emplace_back(
            "network.modbus.quality_scale: must be > 0 when enabled");
    }
}

}  // namespace

ConfigValidator::Result ConfigValidator::validate(const ConfigManager& cfg) {
    Result r;
    checkLogging(cfg, r.errors);
    checkI18n(cfg, r.errors);
    checkWindow(cfg, r.errors);
    checkBackends(cfg, r.errors);
    checkHistorian(cfg, r.errors);
    checkModbus(cfg, r.errors);
    r.ok = r.errors.empty();
    return r;
}

}  // namespace app::config
