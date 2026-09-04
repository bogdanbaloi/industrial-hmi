#pragma once

#include <QString>

// Central place for the Qt frontend's colours and reusable stylesheet
// fragments, so no widget hardcodes a hex string. The analog of the GTK
// theme CSS: one source of truth for status colours.
namespace app::view::theme {

inline constexpr const char* kColorOk       = "#2e7d32";  // green
inline constexpr const char* kColorWarning  = "#f9a825";  // amber
inline constexpr const char* kColorInfo     = "#1565c0";  // blue
inline constexpr const char* kColorAlarm    = "#c62828";  // red
inline constexpr const char* kColorNeutral  = "#616161";  // grey

/// Full-width status banner: white bold text on a coloured ground.
inline QString bannerStyle(const char* background) {
    return QString("padding: 8px; font-weight: bold; color: white; "
                   "background: %1;")
        .arg(background);
}

/// Coloured bold text (status labels on the cards).
inline QString coloredBold(const char* color) {
    return QString("color: %1; font-weight: bold;").arg(color);
}

}  // namespace app::view::theme
