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
    if (cfg.isHttpBackendEnabled() && !isValidPort(cfg.getHttpBackendPort())) {
        errors.emplace_back("network.http.port: out of range (1..65535)");
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

/// TLS for the HTTP/REST backend (REQ-INTEGRATION-010). SHAPE ONLY: this
/// asks whether the operator supplied the paths their own settings require,
/// not whether the files exist or parse. ConfigValidator is linked into
/// every build, including ones without the HTTP backend, so it must stay
/// free of OpenSSL and of the filesystem, the same posture as checkModbus.
/// Proving the material is HttpTlsMaterial's job, at backend construction.
void checkHttpTls(const ConfigManager& cfg,
                  std::vector<std::string>& errors) {
    if (!cfg.isHttpBackendEnabled() || !cfg.isHttpTlsEnabled()) return;
    if (cfg.getHttpTlsCertPath().empty()) {
        errors.emplace_back(
            "network.http.tls.cert_path: required when "
            "network.http.tls.enabled is true");
    }
    if (cfg.getHttpTlsKeyPath().empty()) {
        errors.emplace_back(
            "network.http.tls.key_path: required when "
            "network.http.tls.enabled is true");
    }
    if (cfg.isHttpTlsVerifyPeer() && cfg.getHttpTlsClientCaPath().empty()) {
        errors.emplace_back(
            "network.http.tls.client_ca_path: required when "
            "network.http.tls.verify_peer is true");
    }
}

// Sign&Encrypt for the OPC-UA endpoints (REQ-INTEGRATION-011, ADR-0031).
//
// SHAPE ONLY, for the same reason checkHttpTls is shape only: this validator
// is compiled into EVERY build, including ones without the OPC-UA backend and
// without open62541. It asks whether the operator supplied the paths their own
// settings require. Whether those paths hold readable DER is
// `Open62541SecurityMaterial`'s question, answered at construction, where a
// failure can name the file and the reason.
//
// The two endpoints are checked through one helper rather than two copies:
// the shape is identical and only the key prefix differs, so a future field
// cannot be added to one half and forgotten in the other.
void checkOpcUaSecurityEndpoint(bool enabled,
                                const std::string& keyPrefix,
                                const std::string& certPath,
                                const std::string& privateKeyPath,
                                const std::string& trustListDir,
                                std::vector<std::string>& errors) {
    if (!enabled) return;
    if (certPath.empty()) {
        errors.emplace_back(keyPrefix + ".cert_path: required when " +
                            keyPrefix + ".enabled is true");
    }
    if (privateKeyPath.empty()) {
        errors.emplace_back(keyPrefix + ".private_key_path: required when " +
                            keyPrefix + ".enabled is true");
    }
    if (trustListDir.empty()) {
        errors.emplace_back(keyPrefix + ".trust_list_dir: required when " +
                            keyPrefix + ".enabled is true");
    }
}

void checkOpcUaSecurity(const ConfigManager& cfg,
                        std::vector<std::string>& errors) {
    if (!cfg.isOpcUaBackendEnabled()) return;

    checkOpcUaSecurityEndpoint(
        cfg.isOpcUaServerSecurityEnabled(),
        "network.opcua.server.security",
        cfg.getOpcUaServerSecurityCertPath(),
        cfg.getOpcUaServerSecurityPrivateKeyPath(),
        cfg.getOpcUaServerSecurityTrustListDir(),
        errors);

    // Gated on the client being enabled as well: security settings under a
    // client that is switched off describe an endpoint that never dials, and
    // refusing to start over them would be noise.
    if (!cfg.isOpcUaClientEnabled()) return;
    checkOpcUaSecurityEndpoint(
        cfg.isOpcUaClientSecurityEnabled(),
        "network.opcua.client.security",
        cfg.getOpcUaClientSecurityCertPath(),
        cfg.getOpcUaClientSecurityPrivateKeyPath(),
        cfg.getOpcUaClientSecurityTrustListDir(),
        errors);
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
    checkHttpTls(cfg, r.errors);
    checkOpcUaSecurity(cfg, r.errors);
    checkHistorian(cfg, r.errors);
    checkModbus(cfg, r.errors);
    r.ok = r.errors.empty();
    return r;
}

}  // namespace app::config
