#pragma once

#include "src/presenter/ViewObserver.h"
#include "src/presenter/DashboardPresenter.h"
#include <gtkmm.h>
#include <memory>

namespace app::view {

// Forward declaration
class DialogManager;

/// Main dashboard page - implements ViewObserver to receive Presenter updates
///
/// @design This is a pure View class in MVP architecture:
///         - NO business logic (all in Presenter)
///         - Implements ViewObserver interface to receive updates
///         - Renders ViewModels into GTK widgets
///         - Forwards user actions to Presenter
///         - Uses DialogManager for error/confirmation dialogs (DI)
///
/// @pattern Observer Pattern + Dependency Injection:
///          - DashboardPage observes DashboardPresenter via ViewObserver
///          - DialogManager injected via constructor
///
/// @thread_safety All ViewObserver callbacks arrive on Presenter thread.
///                Must use Glib::signal_idle() to update GTK widgets safely.
class DashboardPage : public Gtk::Box, public ViewObserver {
public:
    /// Constructor with dependency injection
    /// @param dialogManager Reference to DialogManager (injected)
    explicit DashboardPage(DialogManager& dialogManager);
    ~DashboardPage() override;

    /// Initialize the page - sets up presenter connection
    void initialize(std::shared_ptr<DashboardPresenter> presenter);

    // ViewObserver interface implementation
    void onWorkUnitChanged(const presenter::WorkUnitViewModel& vm) override;
    void onEquipmentCardChanged(const presenter::EquipmentCardViewModel& vm) override;
    void onQualityCheckpointChanged(const presenter::QualityCheckpointViewModel& vm) override;
    void onControlPanelChanged(const presenter::ControlPanelViewModel& vm) override;
    void onStatusZoneChanged(const presenter::StatusZoneViewModel& vm) override;
    void onError(const std::string& errorMessage) override;

private:
    // GTK widgets - organized by UI sections
    
    /// Work unit information section
    struct WorkUnitWidgets {
        Gtk::Label* workUnitIdLabel{nullptr};
        Gtk::Label* productIdLabel{nullptr};
        Gtk::Label* productDescLabel{nullptr};
        Gtk::ProgressBar* progressBar{nullptr};
        Gtk::Label* statusLabel{nullptr};
    } workUnitWidgets_;

    /// Equipment status cards
    struct EquipmentCard {
        uint32_t equipmentId{0};
        Gtk::Box* cardBox{nullptr};
        Gtk::Label* statusDot{nullptr};  // Status dot (colored ●)
        Gtk::Label* statusLabel{nullptr};
        Gtk::Label* consumablesLabel{nullptr};
        Gtk::Switch* enabledSwitch{nullptr};
    };
    std::vector<EquipmentCard> equipmentCards_;

    /// Quality checkpoint cards
    struct QualityCard {
        uint32_t checkpointId{0};
        Gtk::Box* cardBox{nullptr};
        Gtk::Label* nameLabel{nullptr};
        Gtk::Label* statusDot{nullptr};  // Colored ● for pass/warning/critical
        Gtk::Picture* gaugeImage{nullptr}; // Visual gauge indicator (SVG support)
        Gtk::Label* passRateLabel{nullptr};
        Gtk::Label* statsLabel{nullptr};
        Gtk::Label* lastDefectLabel{nullptr};
    };
    std::vector<QualityCard> qualityCards_;

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
        Gtk::Label* messageLabel{nullptr};
    } statusZoneWidgets_;

    /// Injected dependencies
    DialogManager& dialogManager_;
    
    /// Presenter reference
    std::shared_ptr<DashboardPresenter> presenter_;

    // UI construction methods
    void buildUI();
    void buildWorkUnitSection();
    void buildEquipmentSection();
    void buildQualitySection();
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
    void updateQualityCard(const presenter::QualityCheckpointViewModel& vm);
    void updateControlPanel(const presenter::ControlPanelViewModel& vm);
    void updateStatusZone(const presenter::StatusZoneViewModel& vm);

    // CSS styling
    void applyStyles();
    Glib::RefPtr<Gtk::CssProvider> cssProvider_;
};

}  // namespace app::view