#pragma once

namespace app::view {

/// RGB colour in the 0.0-1.0 range expected by Cairo.
/// Lives here (not nested in a widget) so gauges, charts, and any future
/// custom-painted widget can share the same palette.
struct Rgb {
    double r, g, b;
};

namespace colors {

// ----------------------------------------------------------------------------
// Status palette - used by QualityGauge arc/dot and elsewhere.
// Hex sources (Material Design 500 line):
//   Passing green  #4CAF50
//   Warning amber  #FF9800
//   Critical red   #F44336
// ----------------------------------------------------------------------------
inline constexpr Rgb kStatusPassingGreen = {0x4C / 255.0, 0xAF / 255.0, 0x50 / 255.0};
inline constexpr Rgb kStatusWarningAmber = {0xFF / 255.0, 0x98 / 255.0, 0x00 / 255.0};
inline constexpr Rgb kStatusCriticalRed  = {0xF4 / 255.0, 0x43 / 255.0, 0x36 / 255.0};

// ----------------------------------------------------------------------------
// Theme-aware track / background shades for Cairo widgets.
// Picked to sit a notch above/below the surrounding card colour.
// ----------------------------------------------------------------------------
inline constexpr Rgb kTrackDarkMode  = {0x3A / 255.0, 0x3A / 255.0, 0x3A / 255.0};
inline constexpr Rgb kTrackLightMode = {0xE0 / 255.0, 0xE0 / 255.0, 0xE0 / 255.0};

// Trend chart background
inline constexpr Rgb kChartBgDarkMode  = {0.15, 0.15, 0.17};
inline constexpr Rgb kChartBgLightMode = {0.98, 0.98, 0.98};

// Trend chart grid lines (drawn semi-transparent over background)
inline constexpr Rgb kChartGridDarkMode  = {0.35, 0.35, 0.38};
inline constexpr Rgb kChartGridLightMode = {0.70, 0.70, 0.70};

// Trend chart labels / axis text
inline constexpr Rgb kChartFgDarkMode  = {0.85, 0.85, 0.88};
inline constexpr Rgb kChartFgLightMode = {0.20, 0.20, 0.20};

// Trend line itself reuses the passing-green so it stays consistent with
// gauge semantics (quality up = green).
inline constexpr Rgb kTrendLine = kStatusPassingGreen;

}  // namespace colors
}  // namespace app::view
