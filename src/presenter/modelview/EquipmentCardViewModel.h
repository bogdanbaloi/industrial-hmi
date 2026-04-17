#pragma once

#include <cstdint>
#include <string>

namespace app::presenter {

/// Equipment station status enumeration for UI display and CSS styling
///
/// @design This enum is derived from raw hardware status + error flags.
///         The Presenter layer translates complex hardware state into this
///         simplified enum that the View can easily render.
///
/// @note The enum values map directly to CSS classes in the GTK theme.
///       Example: EquipmentCardStatus::Online → CSS class "equipment-status-online"
enum class EquipmentCardStatus {
    Offline,      ///< Unreachable / not configured (safe zero-init default)
    StartingUp,   ///< Equipment is booting up
    CheckOutput,  ///< Running self-diagnostics
    Online,       ///< Idle, ready for production, no errors
    Processing,   ///< Actively processing work
    WarmingUp,    ///< System warming up
    Ready,        ///< Cycle complete, ready for next operation
    Reboot,       ///< System reboot in progress
    Error,        ///< Has active error flags (mechanical fault, supply issues, etc.)
    Disabled,     ///< Operator-disabled via UI toggle switch
    Unknown       ///< Unrecognized state from hardware
};

/// Display-ready data for a single equipment station status card
///
/// @design This is a pure Data Transfer Object (DTO) / ViewModel with NO business logic.
///         The Presenter layer translates raw hardware data into this structure.
///         The View layer simply renders these fields without interpretation.
///
/// @pattern This follows the ViewModel pattern from MVP architecture:
///          - Model (hardware): Complex state with many fields, error flags, enums
///          - Presenter: Translates Model → ViewModel (this struct)
///          - View: Renders ViewModel fields directly into widgets
///
/// @thread_safety This struct is passed by value or const-ref between threads.
///                No shared mutable state.
///
/// @example
///    EquipmentCardViewModel vm;
///    vm.equipmentId = 1;
///    vm.status = EquipmentCardStatus::Online;
///    vm.consumables = "Supply level: 85%";
///    vm.messageStatus = "Ready";
///    vm.imagePath = "assets/images/equipment/station-good.svg";
///    vm.enabled = true;
///    
///    // Pass to view via observer
///    notifyEquipmentCardChanged(vm);
struct EquipmentCardViewModel {
    /// Unique identifier for this equipment station (1-based index)
    uint32_t equipmentId{0};

    /// Current operational status - drives UI styling and status badge
    /// @note Derived from hardware status enum + error flags
    EquipmentCardStatus status{EquipmentCardStatus::Unknown};

    /// Human-readable consumables/supplies status
    /// @examples "OK", "Low supply (15%)", "Out of material", "Feed jam"
    std::string consumables;

    /// Free-text message from equipment firmware or error summary
    /// @note This is the raw status message from hardware, or a translated error
    std::string messageStatus;

    /// Filesystem path to status indicator image/icon
    /// @note Images change based on status: good.svg, moderate.svg, bad.svg, offline.svg
    /// @example "assets/images/equipment/station-good.svg"
    std::string imagePath;

    /// Whether operator has enabled this equipment via UI toggle switch
    /// @note false = operator disabled, true = operator enabled
    ///       Presenter may force-disable regardless of this flag (equipment errors)
    bool enabled{false};

    /// Equality comparison for ViewModel caching
    /// @design Only compare fields that affect UI rendering to avoid redundant updates
    bool operator!=(const EquipmentCardViewModel& other) const {
        return status != other.status
            || consumables != other.consumables
            || messageStatus != other.messageStatus
            || imagePath != other.imagePath
            || enabled != other.enabled;
    }

    bool operator==(const EquipmentCardViewModel& other) const {
        return !(*this != other);
    }
};

}  // namespace app::presenter
