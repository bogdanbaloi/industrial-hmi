#pragma once

#include "src/auth/Role.h"
#include "src/gtk/view/pages/Page.h"
#include "src/presenter/ViewObserver.h"
#include "src/presenter/DashboardPresenter.h"
#include "src/gtk/view/widgets/BigNumberCard.h"
#include "src/gtk/view/widgets/DonutChartWidget.h"
#include "src/gtk/view/widgets/QualityGauge.h"
#include "src/gtk/view/widgets/TrendChart.h"
#include <gtkmm.h>
#include <array>
#include <chrono>
#include <memory>
#include <optional>

class DashboardPageTest;  // forward-declare the gtest fixture

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
class DashboardPage : public Page, public ViewObserver {
    friend class ::DashboardPageTest;  // test access to private handlers
public:
    /// Constructor with dependency injection
    /// @param dialogManager Reference to DialogManager (injected)
    explicit DashboardPage(DialogManager& dialogManager);
    ~DashboardPage() override;

    /// Initialize the page - sets up presenter connection
    void initialize(std::shared_ptr<DashboardPresenter> presenter);

    /// Gate the control panel buttons by the active role.
    ///   Operator    -> Calibration + Reset disabled (read-only-ish).
    ///   Maintenance -> all sensitive, including the dangerous
    ///                  destructive ones (calibration interrupts
    ///                  production; reset wipes work unit state).
    ///   Admin       -> same as Maintenance.
    /// Called once after `initialize()` by MainWindow when auth is
    /// wired; pages built without an active session leave every
    /// button sensitive so the no-auth dev path is unchanged.
    void applyRole(app::auth::Role role);

    /// Shrink the dashboard's wide elements (QualityGauge,
    /// TrendChart) for narrow-pane contexts -- specifically when
    /// hosted inside MultiStationDashboardPage where the available
    /// width per pane is roughly half of a normal single-station
    /// terminal. Idempotent. No-op when called with false.
    void setCompact(bool compact);

    // Page overrides
    [[nodiscard]] Glib::ustring pageTitle() const override;
    void onThemeChanged() override;

    /// Redraw the dynamically-painted widgets (gauges) after a theme change,
    /// so they pick up the new track/background colors.
    void refreshThemedWidgets();

    // ViewObserver interface implementation
    void onWorkUnitChanged(const presenter::WorkUnitViewModel& vm) override;
    void onEquipmentCardChanged(const presenter::EquipmentCardViewModel& vm) override;
    void onQualityCheckpointChanged(const presenter::QualityCheckpointViewModel& vm) override;
    void onControlPanelChanged(const presenter::ControlPanelViewModel& vm) override;
    void onStatusZoneChanged(const presenter::StatusZoneViewModel& vm) override;
    void onError(const std::string& errorMessage) override;

private:
    /// Active role cache. Set by `applyRole` and consulted by
    /// `updateControlPanel` so a presenter-driven sensitivity update
    /// cannot re-enable buttons the role forbids (Operator must not
    /// be able to trigger Calibration / Reset even after the
    /// presenter pushes a fresh ControlPanelViewModel). Defaults to
    /// Admin so the no-auth dev path leaves every button live.
    app::auth::Role currentRole_{app::auth::Role::Admin};

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
        Gtk::Label* statusDot{nullptr};
        QualityGauge* gauge{nullptr};
        Gtk::Label* passRateLabel{nullptr};
        Gtk::Label* statsLabel{nullptr};
        Gtk::Label* lastDefectLabel{nullptr};
    };
    std::vector<QualityCard> qualityCards_;

    /// Sparkline trend charts (one per checkpoint, thin strip below quality)
    std::vector<TrendChart*> trendCharts_;

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

    /// Top KPI strip: three BigNumberCard widgets stretched across
    /// the top of the dashboard. OEE and Throughput are placeholder
    /// values today (model layer has no first-class fields for them
    /// -- proper wiring is Phase 8F). Pass Rate is the aggregate
    /// average across the three quality checkpoints and updates on
    /// every quality observer notification.
    struct KpiStripWidgets {
        Gtk::Box*       container{nullptr};   // .ui-managed Gtk::Box
        BigNumberCard*  oeeCard{nullptr};
        BigNumberCard*  throughputCard{nullptr};
        BigNumberCard*  passRateCard{nullptr};
    } kpiStripWidgets_;

    /// Latest pass rate per checkpoint id (index = checkpointId).
    /// Used to compute the aggregate pass rate shown in the top
    /// strip without holding a reference to the presenter's quality
    /// state.
    std::array<std::optional<float>, 3> latestPassRates_{};

    /// Recompute the aggregate pass rate from latestPassRates_ and
    /// push the new value to the OEE + Pass Rate cards. Called on
    /// every quality observer notification.
    void updateTopMetrics();

    /// Session uptime breakdown (Phase 8C). Tracks how long the
    /// system spends in each SystemState across the current
    /// session and renders the proportions as a coloured donut
    /// chart with a headline percentage in the centre.
    struct UptimeWidgets {
        Gtk::Box*         container{nullptr};
        DonutChartWidget* donut{nullptr};
    } uptimeWidgets_;

    /// Accumulated seconds per system state. Index matches the
    /// SystemState enum's int value (0 IDLE, 1 RUNNING, 2 ERROR,
    /// 3 CALIBRATION); the array sizes to one slot past the
    /// highest known value so a new state added to the enum
    /// doesn't immediately segfault here (it just gets ignored
    /// at refresh time, surfacing as a bug we can spot in CI).
    std::array<double, 4> uptimeSecondsByState_{};
    int                   currentUptimeState_{0};
    std::chrono::steady_clock::time_point uptimeSegmentStart_{
        std::chrono::steady_clock::now()};

    /// Periodic timer (5 s) that recomputes the current state's
    /// duration on the fly and refreshes the donut so the operator
    /// sees the ring tick forward while the system is sitting in
    /// one state (without it, the donut only updates on state
    /// transitions which is too sparse for an "uptime" view).
    sigc::connection uptimeRefreshConn_;

    /// Build the inline session-uptime donut inside the Work Unit
    /// card. Extracted out of buildUI so the latter stays under
    /// the readability-function-size threshold.
    void buildUptimeDonut(const Glib::RefPtr<Gtk::Builder>& builder);

    /// Called when the presenter signals a system-state change.
    /// Accumulates the current state's elapsed time before
    /// switching to the new one, then pushes a fresh segment list
    /// to the donut.
    void onSystemStateChangedForUptime(int newState);

    /// Pure-render helper: builds the segment list from
    /// uptimeSecondsByState_ + the current segment's in-flight
    /// duration, then hands the result to the donut along with
    /// the centre title (running % uptime) and subtitle.
    void refreshUptimeDonut();

    /// Presenter reference
    std::shared_ptr<DashboardPresenter> presenter_;

    // UI construction -- loads layout from assets/ui/dashboard-page.ui and
    // injects dynamic widgets (gauges, trend charts) into named containers.
    void buildUI();

    // Event handlers (user interactions -> Presenter)
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