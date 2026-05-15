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

// Integration backends -- defaults match a typical local-development
// setup. Both backends default to disabled in app-config.json; these
// values fill in the blanks once the operator opts in.
constexpr int kTcpBackendPort       = 5555;
constexpr const char* kMqttBrokerHost  = "127.0.0.1";
constexpr int kMqttBrokerPort       = 1883;
constexpr const char* kMqttClientId    = "industrial-hmi";
constexpr const char* kMqttTopicPrefix = "industrial-hmi";
/// Default topic prefix for the inbound subscriber. Distinct from
/// kMqttTopicPrefix so publish vs. subscribe namespaces can be tuned
/// independently per deployment.
constexpr const char* kMqttSensorTopicPrefix = "industrial-hmi-sensors";

// OPC-UA server backend. IANA-assigned default port for OPC-UA Binary
// is 4840; URI / name are advertised in GetEndpoints replies so clients
// can identify the server in their address books.
constexpr int kOpcUaServerPort = 4840;
constexpr const char* kOpcUaApplicationUri  = "urn:industrial-hmi:server";
constexpr const char* kOpcUaApplicationName = "Industrial HMI OPC-UA Server";

/// OPC-UA *client* defaults. Loopback to the in-process server so a
/// local demo "just works"; production deployments point this at a
/// PLC's discovery URL.
constexpr const char* kOpcUaClientEndpoint        = "opc.tcp://127.0.0.1:4840";
constexpr const char* kOpcUaClientApplicationUri  = "urn:industrial-hmi:client";
constexpr const char* kOpcUaClientApplicationName = "Industrial HMI OPC-UA Client";
/// Browse-path prefix the ingest bridge monitors. `Factory` matches
/// what our own server publishes (loopback case, off by default);
/// real PLCs typically advertise a vendor-specific root.
constexpr const char* kOpcUaClientIngestPrefix    = "Factory";

// Modbus master backend. IANA-registered Modbus/TCP port is 502 but
// that's privileged on Linux; the demo simulator listens on 5020 so
// the HMI can run unprivileged against pymodbus. Real deployments
// point host/port at a PLC.
constexpr const char* kModbusHost          = "127.0.0.1";
constexpr int kModbusPort                  = 5020;
constexpr int kModbusPollIntervalMs        = 1000;
constexpr int kModbusConnectTimeoutMs      = 2000;
constexpr int kModbusRequestTimeoutMs      = 1000;
/// Analog register block defaults. The three blocks
/// (EquipmentEnabled / EquipmentSupplyLevel / QualityPassRate)
/// live at 0x00 / 0x10 / 0x20 so a default-config simulator +
/// dashboard render the analog bars alongside the boolean switches
/// without any operator tuning.
constexpr int kModbusSupplyBaseAddress     = 0x10;
constexpr float kModbusSupplyScale         = 1.0F;
constexpr int kModbusQualityBaseAddress    = 0x20;
constexpr float kModbusQualityScale        = 0.1F;

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
constexpr const char* kMainWindowRightUI     = "assets/ui/main-window-right.ui";
constexpr const char* kDashboardPageUI  = "assets/ui/dashboard-page.ui";
constexpr const char* kProductsPageUI   = "assets/ui/products-page.ui";
constexpr const char* kSettingsPageUI   = "assets/ui/settings-page.ui";
constexpr const char* kInspectionPageUI = "assets/ui/inspection-page.ui";

// CSS stylesheets
constexpr const char* kThemeCSS      = "assets/styles/adwaita-theme.css";
constexpr const char* kSidebarCSS    = "assets/styles/sidebar.css";
constexpr const char* kDashboardCSS  = "assets/styles/dashboard.css";
constexpr const char* kProductsCSS   = "assets/styles/products.css";
constexpr const char* kInspectionCSS = "assets/styles/inspection.css";

// Palette directory -- one .css file per palette, loaded on top of the
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

// Historian time-series persistence
// kHistorianDbPath: relative paths resolved against the executable's
//   own directory (same convention as the locale dir). "data/" is a
//   peer of the binary so deployments don't pollute the user's home.
// kHistorianBatchSize / kHistorianBatchAgeMs: bridge batching defaults.
//   See HistorianBridge::Config for the tuning rationale.
constexpr const char* kHistorianDbPath       = "data/historian.sqlite";
constexpr int         kHistorianBatchSize    = 32;
constexpr int         kHistorianBatchAgeMs   = 5000;
// Tiered retention sweep cadence + per-tier age cutoffs. Defaults
// match the typical industrial historian (1 h fine-grained, 24 h
// minute-aggregates, hour-aggregates kept forever).
constexpr int         kHistorianSweepIntervalMs    = 60'000;       // 1 min
constexpr int         kHistorianRawRetentionMs     = 3'600'000;    // 1 h
constexpr int         kHistorianMinuteRetentionMs  = 86'400'000;   // 24 h

}  // namespace app::config::defaults
