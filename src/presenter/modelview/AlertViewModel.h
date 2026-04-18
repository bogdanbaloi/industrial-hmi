#pragma once

#include <string>

namespace app::presenter {

/// Severity buckets for operator-visible alerts. Drives card coloring
/// (green/amber/red) in the sidebar AlertsPanel.
enum class AlertSeverity {
    Info,
    Warning,
    Critical
};

/// ViewModel for a single alert card.
///
/// @design
/// - `key` is the dedupe identifier: multiple raise()s with the same key
///   overwrite the previous entry instead of stacking duplicates. Use a
///   stable scheme like `"quality-2-warning"` or `"equipment-0-offline"`.
/// - `timestamp` is pre-formatted ("HH:MM:SS") by the producer so the
///   View doesn't need to re-format on every redraw.
struct AlertViewModel {
    std::string   key;
    AlertSeverity severity{AlertSeverity::Info};
    std::string   title;
    std::string   message;
    std::string   timestamp;
};

}  // namespace app::presenter
