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

// i18n
// "auto" = respect LANGUAGE/LANG from environment (OS locale)
// otherwise: one of the LINGUAS codes (en, de, es, fi, fr, ga, it, pt, sv)
constexpr const char* kDefaultLanguage = "auto";

// Directory (relative to binary cwd) containing compiled gettext catalogs:
// <kLocaleDir>/<lang>/LC_MESSAGES/industrial-hmi.mo. Used by initI18n at
// startup and again by MainWindow::rebuildPages during live reload.
constexpr const char* kLocaleDir = "locale";

// Dialogs
constexpr const char* kDialogTitle = "Dialog";
constexpr const char* kDialogIcon = "dialog-information";
constexpr const char* kConfirmButton = "OK";
constexpr const char* kCancelButton = "Cancel";

// Logging
constexpr const char* kLogFile = "logs/app.log";
constexpr const char* kLogLevel = "TRACE";
constexpr std::size_t kBytesPerKilobyte = 1024;
constexpr std::size_t kBytesPerMegabyte = kBytesPerKilobyte * kBytesPerKilobyte;
constexpr std::size_t kLogMaxFileSizeMB = 5;
constexpr std::size_t kLogMaxFileSize = kLogMaxFileSizeMB * kBytesPerMegabyte;
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

// UI layout files (GtkBuilder XML)
constexpr const char* kMainWindowUI    = "assets/ui/main-window.ui";
// Alternative MainWindow layouts picked by palette at startup. Each
// must expose the same set of widget IDs as main-window.ui so
// MainWindow.cpp stays agnostic about which file was loaded.
constexpr const char* kMainWindowBlueprintUI = "assets/ui/main-window-blueprint.ui";
constexpr const char* kDashboardPageUI = "assets/ui/dashboard-page.ui";
constexpr const char* kProductsPageUI  = "assets/ui/products-page.ui";
constexpr const char* kSettingsPageUI  = "assets/ui/settings-page.ui";

// CSS stylesheets
constexpr const char* kThemeCSS     = "assets/styles/adwaita-theme.css";
constexpr const char* kSidebarCSS   = "assets/styles/sidebar.css";
constexpr const char* kDashboardCSS = "assets/styles/dashboard.css";
constexpr const char* kProductsCSS  = "assets/styles/products.css";

// Palette directory — one .css file per palette, loaded on top of the
// base stylesheets by ThemeManager. Filename = palette id (e.g.
// "nord.css"). A palette of "industrial" or "" is a no-op (no
// extra provider), falling back to the base dark/light look.
constexpr const char* kPaletteDir   = "assets/styles/themes";

// Timers (milliseconds)
constexpr int kAutoRefreshIntervalMs = 2000;
constexpr int kAutoRefreshStartDelayMs = 1000;
constexpr int kLogPanelRefreshMs = 500;

// Quality thresholds
constexpr float kQualityPassThreshold = 95.0f;
constexpr float kQualityWarningThreshold = 90.0f;

}  // namespace app::config::defaults
