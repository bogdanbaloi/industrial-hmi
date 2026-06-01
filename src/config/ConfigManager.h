#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <cstddef>
#include <cstdio>
#include <map>
#include <string>

#include "config_defaults.h"
#include "src/core/LoggerBase.h"
#include "src/core/i18n.h"

namespace app::config {

/**
 * ConfigManager - JSON Configuration Management
 *
 * Loads application configuration from config/app-config.json
 * Provides access to dialogs, assets, UI paths, and application settings.
 *
 * Pattern: Singleton (appropriate for global app configuration).
 * Thread-safe: Read-only after initialization.
 *
 * Architecture (since v0.13):
 *   - Hand-rolled flat-key JSON parser replaced with nlohmann/json
 *     (vendored via FetchContent; see ADR-0015).
 *   - PIMPL boundary: nlohmann/json.hpp is included ONLY from
 *     ConfigManager.cpp so its ~25k-line single header doesn't leak
 *     into every TU touching a config accessor.
 *   - Public API still surfaces a flat dot-joined key -> string map,
 *     so the 100+ call sites in the codebase didn't need to change.
 */
class ConfigManager {
public:
    static ConfigManager& instance();

    /**
     * Initialize - Load configuration from JSON file.
     * @return true if the file was opened and parsed successfully.
     */
    [[nodiscard]] bool initialize(
        const std::string& configPath = defaults::kConfigPath);

    /**
     * Inject a logger so degraded-config policy methods (applyI18n, future
     * applyTheme) can report through the normal log pipeline. Without a
     * logger they fall back to stderr.
     */
    void setLogger(app::core::Logger& logger);

    /**
     * Apply the configured language to the i18n subsystem.
     *
     *   - Config loaded OK   -> use getLanguage() (explicit code or "auto")
     *   - Config unavailable -> "auto" + warning
     *
     * Always succeeds: worst case gettext falls back to source strings.
     *
     * Defined inline so the call to app::core::initI18n() lives in
     * whichever TU invokes applyI18n() (Bootstrap, etc.) rather than in
     * ConfigManager.cpp. This keeps the objectsConfig static archive
     * free of an external initI18n reference, so test executables that
     * link objectsConfig without objectsCore (e.g. test_argon2_password_hasher)
     * still resolve at link time.
     */
    void applyI18n() {
        std::string language;
        if (initialized_) {
            language = getLanguage();
        } else {
            language = "auto";
            if (logger_) {
                logger_->warn(
                    "Config unavailable - falling back to OS locale for i18n");
            } else {
                std::fprintf(
                    stderr,
                    "[warn] Config unavailable - falling back to OS locale\n");
            }
        }
        app::core::initI18n(defaults::kLocaleDir, language.c_str());
    }

    /**
     * Reset in-memory configuration. Tests call this between runs to get
     * a clean baseline; production code shouldn't need it.
     */
    void clear();

    // Asset Paths

    std::string getAssetPath(const std::string& category,
                             const std::string& name) const;
    std::string getAppIcon() const;
    std::string getWindowIconName() const;
    std::string getUIFile(const std::string& name) const;

    // Dialog Configuration

    std::string getDialogTitle(const std::string& category,
                               const std::string& name) const;
    std::string getDialogMessage(const std::string& category,
                                 const std::string& name) const;
    std::string getDialogIcon(const std::string& category,
                              const std::string& name) const;
    std::string getConfirmButton(const std::string& dialogName) const;
    std::string getCancelButton(const std::string& dialogName) const;

    // Application Settings

    std::string getAppName() const;
    std::string getAppVersion() const;
    std::string getWindowTitle() const;
    int getWindowWidth() const;
    int getWindowHeight() const;
    std::string getDefaultTheme() const;

    // i18n

    std::string getLanguage() const;
    /**
     * Persist language selection to disk and update in-memory value.
     * @param language One of "auto" or a LINGUAS code (e.g. "it", "de").
     */
    [[nodiscard]] bool setLanguage(const std::string& language);

    // UI palette

    std::string getPalette() const;
    [[nodiscard]] bool setPalette(const std::string& palette);

    // Logging

    std::string getLogLevel() const;
    std::string getLogFilePath() const;
    std::size_t getLogMaxFileSize() const;
    int getLogMaxFiles() const;
    bool getLogConsoleEnabled() const;

    // Integration backends - TCP

    [[nodiscard]] bool isTcpBackendEnabled() const;
    [[nodiscard]] int getTcpBackendPort() const;

    // Auth

    [[nodiscard]] bool isAuthEnabled() const;
    [[nodiscard]] std::string getAuthDbPath() const;

    // Multi-station

    [[nodiscard]] bool isMultiStationEnabled() const;

    // Historian

    [[nodiscard]] bool isHistorianEnabled() const;
    [[nodiscard]] std::string getHistorianDbPath() const;
    [[nodiscard]] int getHistorianBatchSize() const;
    [[nodiscard]] int getHistorianBatchAgeMs() const;
    [[nodiscard]] int getHistorianSweepIntervalMs() const;
    [[nodiscard]] int getHistorianRawRetentionMs() const;
    [[nodiscard]] int getHistorianMinuteRetentionMs() const;

    // MQTT

    [[nodiscard]] bool isMqttBackendEnabled() const;
    [[nodiscard]] std::string getMqttBrokerHost() const;
    [[nodiscard]] int getMqttBrokerPort() const;
    [[nodiscard]] std::string getMqttClientId() const;
    [[nodiscard]] std::string getMqttTopicPrefix() const;
    [[nodiscard]] bool isMqttEmitPlainText() const;
    [[nodiscard]] bool isMqttEmitJson() const;
    [[nodiscard]] bool isMqttSubscriberEnabled() const;
    [[nodiscard]] std::string getMqttSensorTopicPrefix() const;

    // Modbus

    [[nodiscard]] bool isModbusBackendEnabled() const;
    [[nodiscard]] std::string getModbusHost() const;
    [[nodiscard]] int getModbusPort() const;
    [[nodiscard]] int getModbusPollIntervalMs() const;
    [[nodiscard]] int getModbusConnectTimeoutMs() const;
    [[nodiscard]] int getModbusRequestTimeoutMs() const;
    [[nodiscard]] int getModbusSlaveId() const;
    [[nodiscard]] int getModbusEquipmentBaseAddress() const;
    [[nodiscard]] int getModbusEquipmentCount() const;
    [[nodiscard]] int getModbusSupplyBaseAddress() const;
    [[nodiscard]] float getModbusSupplyScale() const;
    [[nodiscard]] int getModbusQualityBaseAddress() const;
    [[nodiscard]] float getModbusQualityScale() const;
    [[nodiscard]] int getModbusQualityCount() const;

    // OPC-UA

    [[nodiscard]] bool isOpcUaBackendEnabled() const;
    [[nodiscard]] int getOpcUaServerPort() const;
    [[nodiscard]] std::string getOpcUaApplicationUri() const;
    [[nodiscard]] std::string getOpcUaApplicationName() const;
    [[nodiscard]] bool isOpcUaServerCommandsEnabled() const;
    [[nodiscard]] bool isOpcUaClientEnabled() const;
    [[nodiscard]] std::string getOpcUaClientEndpoint() const;
    [[nodiscard]] std::string getOpcUaClientApplicationUri() const;
    [[nodiscard]] std::string getOpcUaClientApplicationName() const;
    [[nodiscard]] bool isOpcUaIngestBridgeEnabled() const;
    [[nodiscard]] std::string getOpcUaIngestBridgeTopicPrefix() const;

    // Template Support

    /**
     * Format dialog message with template variables.
     *
     *   template: "Delete \"{product_name}\"?"
     *   formatMessage(template, {{"product_name", "Product A"}})
     *     -> "Delete \"Product A\"?"
     */
    std::string formatMessage(
        const std::string& templateStr,
        const std::map<std::string, std::string>& vars) const;

    /**
     * Direct flat-key access. Exposed for ConfigValidator (and tests).
     * Returns empty string when the key is absent.
     */
    [[nodiscard]] std::string rawValue(const std::string& key) const;

    /**
     * Whether the JSON file was successfully read and parsed during the
     * most recent initialize() call. Used by the validator + bootstrap
     * to skip semantic checks when no config was loaded.
     */
    [[nodiscard]] bool isInitialized() const;

    // Non-copyable, non-movable singleton
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    ConfigManager(ConfigManager&&) = delete;
    ConfigManager& operator=(ConfigManager&&) = delete;

private:
    ConfigManager();
    ~ConfigManager();

    std::string getValue(const std::string& key,
                         const std::string& defaultValue = "") const;
    int getInt(const std::string& key, int defaultValue) const;
    float getFloat(const std::string& key, float defaultValue) const;

    bool loadConfig();
    bool persistLanguage(const std::string& language);
    bool persistPalette(const std::string& palette);
    bool persistStringField(const std::string& fieldName,
                            const std::string& value,
                            const std::string& insertionIfMissing);

    std::string configPath_;
    std::map<std::string, std::string> config_;
    app::core::Logger* logger_ = nullptr;
    bool initialized_ = false;
};

}  // namespace app::config

#endif  // CONFIG_MANAGER_H
