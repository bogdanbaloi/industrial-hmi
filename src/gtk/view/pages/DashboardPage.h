#pragma once

#include "src/presenter/ViewObserver.h"
#include "src/presenter/DashboardPresenter.h"
#include <gtkmm.h>
#include <memory>

namespace app::view {

/// Main dashboard page - implements ViewObserver to receive Presenter updates
///
/// @design This is a pure View class in MVP architecture:
///         - NO business logic (all in Presenter)
///         - Implements ViewObserver interface to receive updates
///         - Renders ViewModels into GTK widgets
///         - Forwards user actions to Presenter
///
/// @pattern Observer Pattern:
///          DashboardPage observes DashboardPresenter via ViewObserver interface
///
/// @thread_safety All ViewObserver callbacks arrive on Presenter thread.
///                Must use Glib::signal_idle() to update GTK widgets safely.
class DashboardPage : public Gtk::Box, public ViewObserver {
public:
    DashboardPage();
    ~DashboardPage() override;

    /// Initialize the page - sets up presenter connection
    void initialize(std::shared_ptr<DashboardPresenter> presenter);

    // ViewObserver interface implementation
    void onWorkUnitChanged(const presenter::WorkUnitViewModel& vm) override;
    void onEquipmentCardChanged(const presenter::EquipmentCardViewModel& vm) override;
    void onActuatorCardChanged(const presenter::ActuatorCardViewModel& vm) override;
    void onControlPanelChanged(const presenter::ControlPanelViewModel& vm) override;
    void onStatusZoneChanged(const presenter::StatusZoneViewModel& vm) override;
    void onError(const std::string& errorMessage) override;

private:
    // GTK widgets - organized by UI sections
    
    /// Work unit information section
    struct WorkUnitWidgets {
        Gtk::Label* workUnitIdOperation{nullptr};
        Gtk::Label* productIdOperation{nullptr};
        Gtk::Label* productDescOperation{nullptr};
        Gtk::ProgressBar* progressBar{nullptr};
        Gtk::Label* statusOperation{nullptr};
    } workUnitWidgets_;

    /// Equipment status cards (e.g., equipment, stations)
    struct EquipmentCard {
        uint32_t equipmentId{0};
        Gtk::Box* cardBox{nullptr};
        Gtk::Image* statusImage{nullptr};
        Gtk::Label* statusOperation{nullptr};
        Gtk::Label* consumablesOperation{nullptr};
        Gtk::Switch* enabledSwitch{nullptr};
    };
    std::vector<EquipmentCard> equipmentCards_;

    /// Actuator status cards (e.g., actuator, automated positioning)
    struct ActuatorCard {
        uint32_t actuatorId{0};
        Gtk::Box* cardBox{nullptr};
        Gtk::Image* statusImage{nullptr};
        Gtk::Label* statusOperation{nullptr};
        Gtk::Label* alertOperation{nullptr};
        Gtk::Label* modeOperation{nullptr};  // Auto/Manual
    };
    std::vector<ActuatorCard> actuatorCards_;

    /// Control panel buttons
    struct ControlPanelWidgets {
        Gtk::Button* startButton{nullptr};
        Gtk::Button* stopButton{nullptr};
        Gtk::Button* resetButton{nullptr};
        Gtk::Button* calibrationButton{nullptr};
        Gtk::Label* activeIndicator{nullptr};  // Shows which operation is active
    } controlPanelWidgets_;

    /// Status zone (error banner at top)
    struct StatusZoneWidgets {
        Gtk::Box* bannerBox{nullptr};
        Gtk::Label* messageOperation{nullptr};
    } statusZoneWidgets_;

    /// Presenter reference
    std::shared_ptr<DashboardPresenter> presenter_;

    // UI construction methods
    void buildUI();
    void buildWorkUnitSection();
    void buildEquipmentCardsSection();
    void buildActuatorCardsSection();
    void buildControlPanelSection();
    void buildStatusZone();

    // Event handlers (user interactions → Presenter)
    void onStartButtonClicked();
    void onStopButtonClicked();
    void onResetButtonClicked();
    void onCalibrationButtonClicked();
    void onEquipmentSwitchToggled(uint32_t equipmentId, bool enabled);

    // Helper methods for updating UI safely
    void updateWorkUnitWidgets(const presenter::WorkUnitViewModel& vm);
    void updateEquipmentCard(const presenter::EquipmentCardViewModel& vm);
    void updateActuatorCard(const presenter::ActuatorCardViewModel& vm);
    void updateControlPanel(const presenter::ControlPanelViewModel& vm);
    void updateStatusZone(const presenter::StatusZoneViewModel& vm);

    // CSS styling
    void applyStyles();
    Glib::RefPtr<Gtk::CssProvider> cssProvider_;
};

}  // namespace app::view