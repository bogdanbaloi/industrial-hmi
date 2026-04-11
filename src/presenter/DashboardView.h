#pragma once

#include "src/presenter/modelview/WorkUnitViewModel.h"
#include "src/presenter/modelview/EquipmentCardViewModel.h"
#include "src/presenter/modelview/QualityCheckpointViewModel.h"
#include "src/presenter/modelview/ControlPanelViewModel.h"

namespace app::presenter {

/// Pure interface for Dashboard view observers
/// 
/// @design Follows Interface Segregation Principle (SOLID):
///         - Dashboard views only depend on methods they actually use
///         - Changes to Products don't force Dashboard recompilation
///         - Small, focused interface (4 methods vs 20+ in fat interface)
///
/// @pattern Observer pattern: Presenter notifies View through this interface
///
/// @lifetime Views must outlive their registration with Presenter
///           Call removeObserver() before view destruction
///
/// @threading All callbacks are invoked from Presenter thread
///            Views must handle thread-safety (e.g., Glib::Dispatcher for GTK)
class DashboardView {
public:
    /// Default constructor
    DashboardView() = default;
    
    /// Virtual destructor for polymorphism safety
    /// @design RAII: Ensures derived class destructors are called correctly
    virtual ~DashboardView() = default;
    
    // Rule of Five: Interfaces are non-copyable, non-movable
    // Views should have stable addresses for observer pattern
    DashboardView(const DashboardView&) = delete;
    DashboardView& operator=(const DashboardView&) = delete;
    DashboardView(DashboardView&&) = delete;
    DashboardView& operator=(DashboardView&&) = delete;
    
    /// Called when production work unit information changes
    /// @param viewModel Display-ready data (work unit ID, progress, description)
    /// @threading Called from Presenter thread
    virtual void onWorkUnitChanged(const WorkUnitViewModel& viewModel) = 0;
    
    /// Called when equipment station status changes
    /// @param viewModel Equipment status (ID, connectivity, supplies, operational state)
    /// @frequency Can be called up to 10Hz during active production
    /// @threading Called from Presenter thread
    virtual void onEquipmentCardChanged(const EquipmentCardViewModel& viewModel) = 0;
    
    /// Called when quality checkpoint status changes
    /// @param viewModel Quality metrics (checkpoint ID, pass rate, defects, stats)
    /// @threading Called from Presenter thread
    virtual void onQualityCheckpointChanged(const QualityCheckpointViewModel& viewModel) = 0;
    
    /// Called when control panel button states change
    /// @param viewModel Button availability (START/STOP/RESET/etc enabled/disabled)
    /// @design State machine in Presenter drives this - View just renders
    /// @threading Called from Presenter thread
    virtual void onControlPanelChanged(const ControlPanelViewModel& viewModel) = 0;
};

}  // namespace app::presenter
