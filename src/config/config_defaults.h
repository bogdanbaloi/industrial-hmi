#pragma once

#include <cstddef>

namespace app::config::defaults {

// Application
constexpr const char* kAppName = "Industrial HMI";
constexpr const char* kAppVersion = "1.0.0";

// Window
constexpr const char* kWindowTitle = "Industrial HMI";
constexpr int kWindowWidth = 1920;
constexpr int kWindowHeight = 1080;

// Theme
constexpr const char* kDefaultTheme = "dark";

// Dialogs
constexpr const char* kDialogTitle = "Dialog";
constexpr const char* kDialogIcon = "dialog-information";
constexpr const char* kConfirmButton = "OK";
constexpr const char* kCancelButton = "Cancel";

// Logging
constexpr const char* kLogFile = "logs/app.log";
constexpr const char* kLogLevel = "INFO";
constexpr std::size_t kLogMaxFileSize = 5 * 1024 * 1024;  // 5 MB
constexpr int kLogMaxFiles = 3;
constexpr bool kLogConsoleEnabled = true;

// Database
constexpr const char* kDatabasePath = ":memory:";

// Product status values (used in DB, UI, and presenter layers)
constexpr const char* kStatusActive = "Active";
constexpr const char* kStatusInactive = "Inactive";
constexpr const char* kStatusLowStock = "Low Stock";

// Sentinel value for "not found" queries
constexpr int kInvalidProductId = -1;

// Config
constexpr const char* kConfigPath = "config/app-config.json";

// GTK Application
constexpr const char* kGtkAppId = "com.portfolio.industrial-hmi";

// UI files
constexpr const char* kMainWindowUI = "ui/main-window.ui";
constexpr const char* kSidebarCSS = "ui/sidebar.css";
constexpr const char* kThemeCSS = "ui/adwaita-theme.css";

}  // namespace app::config::defaults
