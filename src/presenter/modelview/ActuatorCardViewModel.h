#pragma once

#include <cstdint>
#include <string>

namespace app::presenter {

/// Automated actuator equipment status for UI display
///
/// @design Generic status for actuatoric or automated positioning equipment
enum class ActuatorCardStatus {
    Offline,      ///< Not connected / not responding
    Idle,         ///< Connected, waiting for work
    Working,      ///< Actively performing operations
    Error,        ///< Has error condition (position error, mechanism failure, etc.)
    Homing,       ///< Moving to home/reference position
    Calibrating,  ///< In calibration mode
    Unknown       ///< Unrecognized state
};

/// Display-ready data for automated actuator equipment status card
///
/// @design This ViewModel represents automated positioning/manipulation equipment.
///         The Presenter subscribes to PLC signals and translates
///         raw actuator status into this display-ready format.
///
/// @thread_safety Actuator data arrives on hardware thread at ~10Hz.
///                Presenter caches this ViewModel and only notifies on changes.
struct ActuatorCardViewModel {
    /// Actuator identifier (0-based or 1-based, depending on system)
    uint32_t actuatorId{0};

    /// Current operational status
    ActuatorCardStatus status{ActuatorCardStatus::Unknown};

    /// Human-readable status message
    /// @examples "Ready", "Processing", "Error: Position timeout", "Moving to home"
    std::string statusMessage;

    /// Filesystem path to actuator status image
    /// @note Images: actuator-left-good.png, actuator-right-good.png, actuator-*-moderate.png, actuator-*-bad.png
    /// @example "assets/images/actuators/actuator-left-good.png"
    std::string imagePath;

    /// Whether actuator is in automatic mode (vs manual/jog mode)
    /// @note Only actuators in auto mode participate in production
    bool autoMode{false};

    /// Whether actuator has an active alert condition
    /// @note Alerts are non-critical warnings (e.g., slow cycle time, near limit)
    bool hasAlert{false};

    /// Whether actuator is at home/reference position
    /// @note System may require all actuators at home before starting production
    bool atHomePosition{false};

    /// Alert message if hasAlert == true
    std::string alertMessage;

    /// Equality for caching
    bool operator!=(const ActuatorCardViewModel& other) const {
        return status != other.status
            || statusMessage != other.statusMessage
            || imagePath != other.imagePath
            || autoMode != other.autoMode
            || hasAlert != other.hasAlert
            || atHomePosition != other.atHomePosition;
    }
};

}  // namespace app::presenter
