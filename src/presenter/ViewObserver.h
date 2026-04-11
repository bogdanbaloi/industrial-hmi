#pragma once

#include "src/presenter/modelview/ActuatorCardViewModel.h"
#include "src/presenter/modelview/ControlPanelViewModel.h"
#include "src/presenter/modelview/WorkUnitViewModel.h"
#include "src/presenter/modelview/EquipmentCardViewModel.h"
#include "src/presenter/modelview/PlaceholderViewModels.h"
#include <string>

namespace app {

/// Interface for Views that observe Presenter state changes
///
/// Views implement this interface to receive notifications from Presenter.
/// This is the core of the Observer pattern implementation in our MVP architecture.
///
/// @note All methods are called from Presenter thread!
///       Views must handle thread-safety (e.g., using Glib::Dispatcher for GTK).
///
/// @design The interface uses empty default implementations instead of pure virtual
///         to allow views to only override methods they need. This follows the
///         Interface Segregation Principle.
class ViewObserver {
public:
    ViewObserver() = default;
    virtual ~ViewObserver() = default;

    // Delete copy/move operations - observers should have stable addresses
    ViewObserver(const ViewObserver&) = delete;
    ViewObserver& operator=(const ViewObserver&) = delete;
    ViewObserver(ViewObserver&&) = delete;
    ViewObserver& operator=(ViewObserver&&) = delete;

    /// Called when production work unit information changes (new work, progress update)
    /// @param viewModel Display-ready data for work unit info widget
    virtual void onWorkUnitChanged(const presenter::WorkUnitViewModel& /*viewModel*/) {}

    /// Called when a critical error occurs that should be shown to the user
    /// @param errorMessage Human-readable error description
    virtual void onError(const std::string& /*errorMessage*/) {}

    /// Called when process configuration dialog should be shown
    /// @param viewModel Data for the configuration confirmation dialog
    /// @note Default: empty (for views that don't support configuration dialogs)
    virtual void onActionRequired(const presenter::ProcessConfigDialogViewModel& /*viewModel*/) {}

    /// Called when configuration dialog settings were saved successfully
    /// View should close the dialog and show success feedback
    virtual void onActionCompleted() {}

    /// Called when configuration dialog settings failed to save
    /// @param errorMessage Reason for failure
    virtual void onActionFailed(const std::string& /*errorMessage*/) {}

    /// Called when test run completes successfully
    /// View should show test completion dialog for operator approval
    virtual void onTestCompleted() {}

    /// Called when products list has been loaded from database
    /// @param viewModel Contains array of products with metadata
    virtual void onProductsLoaded(const presenter::ProductsViewModel& /*viewModel*/) {}

    /// Called when product data is ready for viewing (ViewProductDialog should be shown)
    /// @param viewModel Complete product details for read-only display
    virtual void onViewProductReady(const presenter::ViewProductDialogViewModel& /*viewModel*/) {}

    /// Called when reset confirmation is requested (ResetProductDialog should be shown)
    /// @param viewModel The reset dialog view model with product info and reset options
    virtual void
    onResetProductRequested(const presenter::ResetProductDialogViewModel& /*viewModel*/) {}

    /// Called when product reset completes
    /// @param success True if resetting was successful
    /// @param message Success or error message to display
    virtual void onProductReset(bool /*success*/, const std::string& /*message*/) {}

    /// Called when equipment station status changes
    /// @param viewModel Equipment status data (supplies, connectivity, operational state)
    /// @note Can be called up to 10 times per second during active production
    virtual void onEquipmentCardChanged(const presenter::EquipmentCardViewModel& /*viewModel*/) {}

    /// Called when actuator equipment status changes
    /// @param viewModel Actuator status data (position, mode, alerts, auto mode)
    /// @note Can be called up to 10 times per second during active production
    virtual void onActuatorCardChanged(const presenter::ActuatorCardViewModel& /*viewModel*/) {}

    /// Called when header status bar should update (connectivity indicators)
    /// @param viewModel Network status, DB connectivity, system health
    virtual void onHeaderStatusChanged(const presenter::HeaderStatusViewModel& /*viewModel*/) {}

    /// Called when control panel button states change (enabled/disabled)
    /// @param viewModel Button availability based on current system state
    /// @design This is driven by a state machine in the presenter - view just renders
    virtual void onControlPanelChanged(const presenter::ControlPanelViewModel& /*viewModel*/) {}

    /// Called when a process step completes or fails
    /// @param viewModel Step identifier, completion status, coordinates
    /// @note View should update the process visualization
    virtual void onProcessStepChanged(const presenter::ProcessStepViewModel& /*viewModel*/) {}

    /// Called when system error status changes
    /// @param viewModel Aggregated error state translated to display-ready format
    /// @note View should update the status zone banner (background color + message)
    virtual void onStatusZoneChanged(const presenter::StatusZoneViewModel& /*viewModel*/) {}

    // Reset Process Panel state management
    /// Called to show the reset panel in "choosing action" state
    /// Panel shows: title + "Choose an action:" + both buttons enabled
    virtual void onResetProcessShowChoosing() {}

    /// Called to show the reset panel in "waiting for equipment" state
    /// Panel shows: title + "Please bring equipment to home position" + one button highlighted
    /// @param restartSelected true if "Restart process" was selected, false if "Release work unit"
    virtual void onResetProcessShowWaitingForEquipment(bool /*restartSelected*/) {}

    /// Called to hide the reset panel completely
    virtual void onResetProcessHide() {}
};

}  // namespace app
