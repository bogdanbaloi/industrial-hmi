#pragma once

#include "BasePresenter.h"
#include "src/presenter/modelview/WorkUnitViewModel.h"
#include "src/presenter/modelview/EquipmentCardViewModel.h"
#include "src/presenter/modelview/ActuatorCardViewModel.h"
#include "src/presenter/modelview/QualityCheckpointViewModel.h"
#include "src/presenter/modelview/ControlPanelViewModel.h"
#include "src/presenter/modelview/PlaceholderViewModels.h"

#include <memory>
#include <mutex>
#include <atomic>

namespace app {

/// Presenter for Dashboard page - orchestrates data flow between Model and View
///
/// @design This is the core of MVP architecture:
///         - Subscribes to Model signals (state machines, hardware updates, DB changes)
///         - Transforms raw Model data into ViewModels
///         - Notifies Views via Observer pattern
///         - Forwards user commands to Model
///
/// @thread_safety Methods can be called from multiple threads:
///                - Model signals arrive on hardware/app threads
///                - User actions arrive on UI thread
///                All shared state is protected by mutex or atomics
class DashboardPresenter : public BasePresenter {
public:
    DashboardPresenter();
    ~DashboardPresenter() override = default;

    // Disable copy/move
    DashboardPresenter(const DashboardPresenter&) = delete;
    DashboardPresenter& operator=(const DashboardPresenter&) = delete;
    DashboardPresenter(DashboardPresenter&&) = delete;
    DashboardPresenter& operator=(DashboardPresenter&&) = delete;

    /// Initialize presenter - subscribe to Model signals
    void initialize() override;

    // User action handlers (called from View/UI thread)
    
    /// Called when user clicks Start button
    void onStartClicked();
    
    /// Called when user clicks Stop button
    void onStopClicked();
    
    /// Called when user clicks Reset/Restart button
    void onResetRestartClicked();
    
    /// Called when user clicks Calibration button
    void onCalibrationClicked();
    
    /// Called when user toggles equipment enable switch
    /// @param equipmentId Equipment identifier
    /// @param enabled New enabled state
    void onEquipmentToggled(uint32_t equipmentId, bool enabled);

private:
    // Model signal handlers (called from Model/background threads)
    
    /// Called when new work unit enters the system
    /// @param workUnitId Work unit identifier from PLC
    void handleNewWorkUnit(const std::string& workUnitId);
    
    /// Called when work unit processing completes
    /// @param workUnitId Work unit identifier
    void handleWorkUnitComplete(const std::string& workUnitId);
    
    /// Called when equipment status changes
    /// @param equipmentId Equipment identifier
    /// @param status Raw hardware status data
    void handleEquipmentStatusUpdate(uint32_t equipmentId, /* raw status type */ int status);
    
    /// Called when actuator status changes
    /// @param actuatorId Actuator identifier
    /// @param status Raw hardware status data
    void handleActuatorStatusUpdate(uint32_t actuatorId, /* raw status type */ int status);
    
    /// Called when quality checkpoint status changes
    /// @param checkpointId Quality checkpoint identifier
    /// @param status Raw quality status data
    void handleQualityCheckpointUpdate(uint32_t checkpointId, int status);
    
    /// Called when system state changes
    /// @param newState New state from state machine
    void handleSystemStateChanged(/* State enum */ int newState);
    
    /// Called when error conditions change
    /// @param errorMask Bitfield of active errors
    void handleErrorUpdate(uint32_t errorMask);

    // ViewModel builders (transform Model data → ViewModels)
    
    /// Build work unit ViewModel from database query
    /// @param workUnitId Work unit identifier
    /// @return Display-ready ViewModel
    presenter::WorkUnitViewModel buildWorkUnitVM(const std::string& workUnitId);
    
    /// Build equipment ViewModel from hardware status
    /// @param equipmentId Equipment identifier
    /// @param status Raw hardware status
    /// @return Display-ready ViewModel
    presenter::EquipmentCardViewModel buildEquipmentVM(uint32_t equipmentId, int status);
    
    /// Build actuator ViewModel from hardware status
    /// @param actuatorId Actuator identifier
    /// @param status Raw hardware status
    /// @return Display-ready ViewModel
    presenter::ActuatorCardViewModel buildActuatorVM(uint32_t actuatorId, int status);
    
    /// Build quality checkpoint ViewModel from quality data
    /// @param checkpointId Quality checkpoint identifier
    /// @param status Quality status
    /// @return Display-ready ViewModel
    presenter::QualityCheckpointViewModel buildQualityCheckpointVM(uint32_t checkpointId, int status);
    
    /// Build control panel ViewModel from current system state
    /// @return Display-ready ViewModel with button states
    presenter::ControlPanelViewModel buildControlPanelVM();
    
    /// Build status zone ViewModel from error mask
    /// @param errorMask Bitfield of active errors
    /// @return Display-ready ViewModel
    presenter::StatusZoneViewModel buildStatusZoneVM(uint32_t errorMask);

    // Notification helpers
    
    /// Notify observers of work unit change
    void notifyWorkUnitChanged(const presenter::WorkUnitViewModel& vm);
    
    /// Notify observers of equipment status change
    void notifyEquipmentCardChanged(const presenter::EquipmentCardViewModel& vm);
    
    /// Notify observers of actuator status change
    void notifyActuatorCardChanged(const presenter::ActuatorCardViewModel& vm);
    
    /// Notify observers of quality checkpoint change
    void notifyQualityCheckpointChanged(const presenter::QualityCheckpointViewModel& vm);
    
    /// Notify observers of control panel state change
    void notifyControlPanelChanged(const presenter::ControlPanelViewModel& vm);
    
    /// Notify observers of status zone change
    void notifyStatusZoneChanged(const presenter::StatusZoneViewModel& vm);
    
    /// Notify observers of error
    void notifyError(const std::string& errorMessage);

    // State caching (for change detection and optimization)
    
    /// Cached ViewModels - only notify on actual changes
    presenter::WorkUnitViewModel lastWorkUnitVM_;
    std::vector<presenter::EquipmentCardViewModel> lastEquipmentVMs_;
    std::vector<presenter::ActuatorCardViewModel> lastActuatorVMs_;
    presenter::ControlPanelViewModel lastControlPanelVM_;
    presenter::StatusZoneViewModel lastStatusZoneVM_;
    
    /// Mutex protecting cached state
    std::mutex cacheMutex_;
    
    /// Atomic flags for simple state
    std::atomic<bool> waitingForEquipmentHome_{false};
    std::atomic<uint32_t> currentErrorMask_{0};
};

}  // namespace app