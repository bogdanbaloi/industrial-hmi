#pragma once

#include <functional>
#include <string>

namespace app::presenter {

/// Severity buckets for operator-visible alerts. Drives card coloring
/// (green/amber/red) in the sidebar AlertsPanel.
enum class AlertSeverity {
    Info,
    Warning,
    Critical
};

/// ISA-18.2 alarm lifecycle state (Phase 1 subset). The producer reports
/// the underlying condition via AlertCenter::raise / clear; the operator
/// advances the state via AlertCenter::acknowledge. The defining
/// behaviour: a condition that returns to normal while still
/// UNACKNOWLEDGED does NOT silently disappear -- it becomes RtnUnack and
/// stays visible until the operator acknowledges it, so a transient fault
/// can't be missed. (Shelved / Suppressed / OutOfService arrive later.)
enum class AlarmState {
    UnackActive,  ///< condition active, not yet acknowledged
    AckActive,    ///< condition active, acknowledged by the operator
    RtnUnack      ///< condition cleared, awaiting acknowledgement
};

/// ViewModel for a single alert card.
///
/// @design
/// - `key` is the dedupe identifier: multiple raise()s with the same key
///   overwrite the previous entry instead of stacking duplicates. Use a
///   stable scheme like `"quality-2-warning"` or `"equipment-0-offline"`.
/// - `timestamp` is pre-formatted ("HH:MM:SS") by the producer so the
///   View doesn't need to re-format on every redraw.
/// - `retranslate` is an optional callback the producer sets so that
///   `title` / `message` can be regenerated in the currently active
///   gettext catalog. AlertCenter invokes it on every entry (active +
///   history) when MainWindow signals a language switch, which lets
///   old history rows re-render in the new language without losing
///   their numeric context (equipment id, pass rate, etc).
struct AlertViewModel {
    std::string   key;
    AlertSeverity severity{AlertSeverity::Info};
    std::string   title;
    std::string   message;
    std::string   timestamp;

    /// Lifecycle state, set by AlertCenter (the producer leaves it at the
    /// default; AlertCenter assigns UnackActive on first raise and
    /// advances it). Drives the state badge + the Acknowledge affordance
    /// in the AlertsPanel.
    AlarmState    state{AlarmState::UnackActive};

    /// Optional. When set, AlertCenter::retranslate() calls this on the
    /// alert to refresh `title`/`message` from the current locale. The
    /// callback must capture the raw format args (ids, names, numbers)
    /// and call gettext/std::vformat internally.
    std::function<void(AlertViewModel&)> retranslate;
};

}  // namespace app::presenter
