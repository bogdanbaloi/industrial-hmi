#pragma once

#include <cstdint>

namespace app::presenter {

/// Active control indicator - shows which operation is currently active
///
/// @design Only one button can have the active indicator at a time.
///         This reflects the current system operational state.
///
/// @pattern This is part of a state machine implementation:
///          - Main state machine determines active operation
///          - Presenter translates to ActiveControl enum
///          - View renders green indicator light on active button
///
/// @note In GTK UI, this drives CSS class "active" on the corresponding button
enum class ActiveControl : uint8_t {
    None = 0,     ///< No active operation (system IDLE or transitioning)
    Start,        ///< Production is RUNNING
    Stop,         ///< System is STOPPED (stopped state, not the button)
    ResetRestart, ///< System is in RESET mode (requires operator intervention)
    Calibration   ///< System is in CALIBRATION/TEST mode
};

/// Display-ready state for the control panel (Start/Stop/Reset/Calibration buttons)
///
/// @design This ViewModel is computed by a state machine in the Presenter layer.
///         The View simply renders these boolean states - NO business logic in UI.
///
/// @pattern State Machine → Presenter → ViewModel → View
///          1. Main state machine determines current state (IDLE, RUNNING, ERROR, etc.)
///          2. Presenter calls computeControlPanelState() with state + flags
///          3. Presenter creates this ViewModel with button enable/disable states
///          4. View updates button sensitivity (enabled/disabled) from ViewModel
///
/// @thread_safety This struct is passed by const-ref between threads.
///                Atomic flags are read when computing this ViewModel.
///
/// @example State machine logic (simplified):
///    ```cpp
///    ControlPanelViewModel computeState() {
///        ControlPanelViewModel vm;
///        auto state = MainStateMachine::getState();
///        bool actuatorHome = checkActuatorAtHomePosition();
///        bool hasErrors = checkErrorFlags();
///        
///        switch (state) {
///            case State::IDLE:
///                vm.activeButton = ActiveControl::None;
///                vm.startEnabled = actuatorHome && !hasErrors;  // Can only start if ready
///                vm.stopEnabled = false;                      // Nothing to stop
///                vm.resetRestartEnabled = true;               // Reset always available
///                vm.calibrationEnabled = actuatorHome;          // Need actuator home
///                break;
///                
///            case State::RUNNING:
///                vm.activeButton = ActiveControl::Start;
///                vm.startEnabled = false;                     // Already running
///                vm.stopEnabled = true;                       // Can stop production
///                vm.resetRestartEnabled = false;              // Can't reset while running
///                vm.calibrationEnabled = false;               // Can't calibrate while running
///                break;
///                
///            case State::ERROR:
///                vm.activeButton = ActiveControl::None;
///                vm.startEnabled = false;                     // Can't start with errors
///                vm.stopEnabled = false;                      // Already stopped
///                vm.resetRestartEnabled = true;               // Must reset to clear error
///                vm.calibrationEnabled = false;               // Can't calibrate with errors
///                break;
///        }
///        
///        return vm;
///    }
///    ```
struct ControlPanelViewModel {
    /// Which button currently shows the active indicator light
    /// @note In GTK, this adds CSS class "active" which shows a green LED indicator
    ActiveControl activeButton{ActiveControl::None};

    /// Per-button enabled state (controls GTK widget sensitivity)
    /// @note false = button greyed out and non-clickable
    ///       true = button enabled and clickable
    
    /// Start button enabled state
    /// @logic Enabled only when:
    ///        - System is IDLE
    ///        - Actuator are in home position
    ///        - No active errors
    ///        - Auto mode is enabled
    bool startEnabled{false};

    /// Stop button enabled state
    /// @logic Enabled only when:
    ///        - System is RUNNING or CALIBRATION
    bool stopEnabled{false};

    /// Reset/Restart button enabled state
    /// @logic Enabled when:
    ///        - System is IDLE (for manual reset)
    ///        - System is ERROR (required to clear error)
    ///        - System is STOPPED
    bool resetRestartEnabled{false};

    /// Calibration button enabled state
    /// @logic Enabled only when:
    ///        - System is IDLE
    ///        - Actuator are in home position
    ///        - No active errors
    bool calibrationEnabled{false};

    /// Equality comparison for caching optimization
    /// @design Comparing ViewModels before notifying avoids redundant UI updates
    ///         when state machine runs but output state hasn't changed
    bool operator!=(const ControlPanelViewModel& other) const {
        return activeButton != other.activeButton
            || startEnabled != other.startEnabled
            || stopEnabled != other.stopEnabled
            || resetRestartEnabled != other.resetRestartEnabled
            || calibrationEnabled != other.calibrationEnabled;
    }

    bool operator==(const ControlPanelViewModel& other) const {
        return !(*this != other);
    }
};

}  // namespace app::presenter
