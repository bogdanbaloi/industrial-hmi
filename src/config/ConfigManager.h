#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstdio>
#include "config_defaults.h"

namespace app::config {

/**
 * ConfigManager - JSON Configuration Management
 * 
 * Loads application configuration from config/app-config.json
 * Provides access to dialogs, assets, UI paths, and application settings
 * 
 * Pattern: Singleton (appropriate for global app configuration)
 * Thread-safe: Read-only after initialization
 * 
 * Usage:
 *   auto& config = ConfigManager::instance();
 *   std::string title = config.getDialogTitle("errors", "product_add_failed");
 *   std::string icon = config.getAssetPath("icons", "app_icon");
 */
class ConfigManager {
public:
    static ConfigManager& instance() {
        static ConfigManager inst;
        return inst;
    }
    
    /**
     * Initialize - Load configuration from JSON file
     * 
     * @param configPath Path to app-config.json
     * @return true if loaded successfully
     */
    [[nodiscard]] bool initialize(const std::string& configPath = defaults::kConfigPath) {
        configPath_ = configPath;
        return loadConfig();
    }
    
    // ========================================================================
    // Asset Paths
    // ========================================================================
    
    std::string getAssetPath(const std::string& category, const std::string& name) const {
        auto key = "assets." + category + "." + name;
        auto it = config_.find(key);
        return (it != config_.end()) ? it->second : "";
    }
    
    std::string getAppIcon() const {
        return getAssetPath("icons", "app_icon");
    }
    
    std::string getWindowIconName() const {
        return getAssetPath("icons", "window_icon_name");
    }
    
    std::string getUIFile(const std::string& name) const {
        return getAssetPath("ui_files", name);
    }
    
    // ========================================================================
    // Dialog Configuration
    // ========================================================================
    
    std::string getDialogTitle(const std::string& category, const std::string& name) const {
        auto key = "dialogs." + category + "." + name + ".title";
        auto it = config_.find(key);
        return (it != config_.end()) ? it->second : defaults::kDialogTitle;
    }
    
    std::string getDialogMessage(const std::string& category, const std::string& name) const {
        auto key = "dialogs." + category + "." + name + ".message";
        auto it = config_.find(key);
        if (it != config_.end()) {
            return it->second;
        }
        
        // Try message_template
        key = "dialogs." + category + "." + name + ".message_template";
        it = config_.find(key);
        return (it != config_.end()) ? it->second : "";
    }
    
    std::string getDialogIcon(const std::string& category, const std::string& name) const {
        auto key = "dialogs." + category + "." + name + ".icon";
        auto it = config_.find(key);
        return (it != config_.end()) ? it->second : defaults::kDialogIcon;
    }
    
    std::string getConfirmButton(const std::string& dialogName) const {
        auto key = "dialogs.confirmations." + dialogName + ".confirm_button";
        auto it = config_.find(key);
        return (it != config_.end()) ? it->second : defaults::kConfirmButton;
    }
    
    std::string getCancelButton(const std::string& dialogName) const {
        auto key = "dialogs.confirmations." + dialogName + ".cancel_button";
        auto it = config_.find(key);
        return (it != config_.end()) ? it->second : defaults::kCancelButton;
    }
    
    // ========================================================================
    // Application Settings
    // ========================================================================
    
    std::string getAppName() const {
        return getValue("application.name", defaults::kAppName);
    }
    
    std::string getAppVersion() const {
        return getValue("application.version", defaults::kAppVersion);
    }
    
    std::string getWindowTitle() const {
        return getValue("window.title", defaults::kWindowTitle);
    }
    
    int getWindowWidth() const {
        return getInt("window.default_width", defaults::kWindowWidth);
    }

    int getWindowHeight() const {
        return getInt("window.default_height", defaults::kWindowHeight);
    }
    
    std::string getDefaultTheme() const {
        return getValue("theme.default", defaults::kDefaultTheme);
    }

    // ========================================================================
    // i18n
    // ========================================================================

    std::string getLanguage() const {
        return getValue("i18n.language", defaults::kDefaultLanguage);
    }

    /**
     * Persist language selection to disk and update in-memory value.
     *
     * Rewrites the "i18n.language" field in the JSON config file.
     * If the i18n section is missing, inserts one.
     *
     * @param language One of "auto" or a LINGUAS code (e.g. "it", "de")
     * @return true on success, false if the config file cannot be written
     */
    [[nodiscard]] bool setLanguage(const std::string& language) {
        // Update in-memory first so subsequent getLanguage() reflects the change
        config_["i18n.language"] = language;
        return persistLanguage(language);
    }

    // ========================================================================
    // Logging Configuration
    // ========================================================================

    std::string getLogLevel() const {
        return getValue("logging.level", defaults::kLogLevel);
    }

    std::string getLogFilePath() const {
        return getValue("logging.file", defaults::kLogFile);
    }

    std::size_t getLogMaxFileSize() const {
        auto mb = getInt("logging.max_file_size_mb",
                         static_cast<int>(defaults::kLogMaxFileSize / (1024 * 1024)));
        return static_cast<std::size_t>(mb) * 1024 * 1024;
    }

    int getLogMaxFiles() const {
        return getInt("logging.max_files", defaults::kLogMaxFiles);
    }

    bool getLogConsoleEnabled() const {
        return getValue("logging.console", "true") == "true";
    }
    
    // ========================================================================
    // Template Support
    // ========================================================================
    
    /**
     * Format dialog message with template variables
     * 
     * Example:
     *   template: "Delete \"{product_name}\"?"
     *   formatMessage(template, {{"product_name", "Product A"}})
     *   → "Delete \"Product A\"?"
     */
    std::string formatMessage(const std::string& templateStr, 
                             const std::map<std::string, std::string>& vars) const {
        std::string result = templateStr;
        for (const auto& [key, value] : vars) {
            std::string placeholder = "{" + key + "}";
            size_t pos = 0;
            while ((pos = result.find(placeholder, pos)) != std::string::npos) {
                result.replace(pos, placeholder.length(), value);
                pos += value.length();
            }
        }
        return result;
    }
    
    // Non-copyable
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    
private:
    ConfigManager() = default;
    
    std::string getValue(const std::string& key, const std::string& defaultValue = "") const {
        auto it = config_.find(key);
        return (it != config_.end()) ? it->second : defaultValue;
    }

    int getInt(const std::string& key, int defaultValue) const {
        auto it = config_.find(key);
        if (it == config_.end()) return defaultValue;
        try {
            return std::stoi(it->second);
        } catch (...) {
            return defaultValue;
        }
    }
    
    bool loadConfig() {
        std::ifstream file(configPath_);
        if (!file.is_open()) {
            return false;
        }
        
        // Simple JSON parser (enough for our flat key-value needs)
        // For production, use nlohmann/json or similar
        parseJSON(file);
        file.close();
        
        // Config loaded successfully
        return true;
    }
    
    void parseJSON(std::ifstream& file) {
        std::string line;
        std::string currentPath;
        std::vector<std::string> pathStack;
        
        while (std::getline(file, line)) {
            // Remove whitespace
            line.erase(0, line.find_first_not_of(" \t"));
            line.erase(line.find_last_not_of(" \t") + 1);
            
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#' || line[0] == '/') continue;
            
            // Parse JSON structure (simplified)
            if (line.find("\"") != std::string::npos) {
                size_t keyStart = line.find("\"");
                size_t keyEnd = line.find("\"", keyStart + 1);
                if (keyStart != std::string::npos && keyEnd != std::string::npos) {
                    std::string key = line.substr(keyStart + 1, keyEnd - keyStart - 1);
                    
                    size_t colonPos = line.find(":", keyEnd);
                    if (colonPos != std::string::npos) {
                        std::string value = line.substr(colonPos + 1);
                        
                        // Check if it's an object start
                        if (value.find("{") != std::string::npos) {
                            pathStack.push_back(key);
                        } else {
                            // Extract value
                            size_t valStart = value.find("\"");
                            if (valStart != std::string::npos) {
                                size_t valEnd = value.find("\"", valStart + 1);
                                if (valEnd != std::string::npos) {
                                    std::string val = value.substr(valStart + 1, valEnd - valStart - 1);
                                    
                                    // Build full path
                                    std::string fullKey;
                                    for (const auto& p : pathStack) {
                                        fullKey += p + ".";
                                    }
                                    fullKey += key;
                                    
                                    config_[fullKey] = val;
                                }
                            } else {
                                // Number or boolean
                                value.erase(0, value.find_first_not_of(" \t"));
                                value.erase(value.find_last_not_of(" \t,") + 1);
                                
                                if (!value.empty() && value != "{" && value != "[") {
                                    std::string fullKey;
                                    for (const auto& p : pathStack) {
                                        fullKey += p + ".";
                                    }
                                    fullKey += key;
                                    config_[fullKey] = value;
                                }
                            }
                        }
                    }
                }
            } else if (line.find("}") != std::string::npos && !pathStack.empty()) {
                pathStack.pop_back();
            }
        }
    }
    
    /**
     * Targeted rewrite of the JSON config's i18n.language field.
     * Reads the whole file, replaces (or inserts) the language value,
     * and writes it back atomically-ish (via a temp file + rename).
     */
    bool persistLanguage(const std::string& language) {
        // Read entire file
        std::ifstream in(configPath_);
        if (!in.is_open()) return false;
        std::stringstream buffer;
        buffer << in.rdbuf();
        in.close();
        std::string content = buffer.str();

        // Try to replace an existing "language": "..." value within the i18n section.
        // We look for the literal `"language"` key and swap the string that follows.
        const std::string key = "\"language\"";
        size_t keyPos = content.find(key);
        bool replaced = false;
        if (keyPos != std::string::npos) {
            // Find the colon, then the opening quote of the value
            size_t colon = content.find(':', keyPos + key.size());
            if (colon != std::string::npos) {
                size_t quoteStart = content.find('"', colon + 1);
                if (quoteStart != std::string::npos) {
                    size_t quoteEnd = content.find('"', quoteStart + 1);
                    if (quoteEnd != std::string::npos) {
                        content.replace(quoteStart + 1,
                                        quoteEnd - quoteStart - 1,
                                        language);
                        replaced = true;
                    }
                }
            }
        }

        // If not found, insert a new i18n block right after the opening brace.
        if (!replaced) {
            size_t brace = content.find('{');
            if (brace == std::string::npos) return false;
            std::string block =
                "\n  \"i18n\": {\n    \"language\": \"" + language + "\"\n  },\n";
            content.insert(brace + 1, block);
        }

        // Write atomically via temp file
        std::string tmpPath = configPath_ + ".tmp";
        {
            std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) return false;
            out << content;
            if (!out.good()) return false;
        }
        std::remove(configPath_.c_str());
        return std::rename(tmpPath.c_str(), configPath_.c_str()) == 0;
    }

    std::string configPath_;
    std::map<std::string, std::string> config_;
};

} // namespace app::config

#endif // CONFIG_MANAGER_H
