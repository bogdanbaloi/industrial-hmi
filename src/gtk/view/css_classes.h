#pragma once

namespace app::view::css {

// ----------------------------------------------------------------------------
// Theme modes - applied on the root window to toggle light/dark styling.
// ----------------------------------------------------------------------------
inline constexpr auto kDarkMode  = "dark-mode";
inline constexpr auto kLightMode = "light-mode";

// ----------------------------------------------------------------------------
// Control panel buttons - colour variants applied at construction.
// ----------------------------------------------------------------------------
inline constexpr auto kStartButton       = "start-button";
inline constexpr auto kStopButton        = "stop-button";
inline constexpr auto kResetButton       = "reset-button";
inline constexpr auto kCalibrationButton = "calibration-button";

// ----------------------------------------------------------------------------
// Equipment card status dot - one of these is swapped in per update.
// ----------------------------------------------------------------------------
inline constexpr auto kEquipmentOnline     = "equipment-online";
inline constexpr auto kEquipmentProcessing = "equipment-processing";
inline constexpr auto kEquipmentError      = "equipment-error";
inline constexpr auto kEquipmentOffline    = "equipment-offline";

// ----------------------------------------------------------------------------
// Quality checkpoint status dot - one of these is swapped in per update.
// ----------------------------------------------------------------------------
inline constexpr auto kQualityPassing  = "quality-passing";
inline constexpr auto kQualityWarning  = "quality-warning";
inline constexpr auto kQualityCritical = "quality-critical";

// ----------------------------------------------------------------------------
// Status zone banner severity levels.
// ----------------------------------------------------------------------------
inline constexpr auto kSeverityInfo    = "severity-info";
inline constexpr auto kSeverityWarning = "severity-warning";
inline constexpr auto kSeverityError   = "severity-error";

// ----------------------------------------------------------------------------
// Misc runtime-applied classes.
// ----------------------------------------------------------------------------
inline constexpr auto kErrorStatus         = "error-status";
inline constexpr auto kDimLabel            = "dim-label";
inline constexpr auto kDialogTitlebarDark  = "dialog-titlebar-dark";
inline constexpr auto kDialogTitlebarLight = "dialog-titlebar-light";

}  // namespace app::view::css
