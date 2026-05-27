#include "DashboardPage.h"
#include "src/gtk/view/DialogManager.h"
#include "src/gtk/view/css_classes.h"
#include "src/gtk/view/ui_sizes.h"
#include "src/config/config_defaults.h"
#include "src/core/i18n.h"
#include "src/core/Application.h"

#include <format>

namespace app::view {

namespace {
inline app::core::Logger& log() {
    return app::core::Application::instance().logger();
}

// KPI top strip targets + placeholder values + status tier
// thresholds. Kept in one block so a future "make these configurable"
// change has a single seam, and clang-tidy's magic-number lint stays
// quiet.
//
// Targets are world-class industrial benchmarks (see Vorne / OEE
// Industry Standards 2024):
//   * OEE target 85%  -- "world class" threshold
//   * Throughput placeholder 100 u/h -- demo-line nominal
//   * Pass rate target 98% -- typical pharma / regulated industry
constexpr double kOeeTargetPct           = 85.0;
constexpr double kOeeInitialPct          = 85.0;  // shown before first sample
constexpr double kThroughputPlaceholder  = 127.0; // demo value -- TODO Phase 8F real wiring
constexpr double kThroughputTargetUph    = 100.0;
constexpr double kPassRateTargetPct      = 98.0;

// Pass-rate aggregate tier thresholds. Same brackets QualityCheckpoint-
// Status uses per-checkpoint so the KPI strip's status colour matches
// what the operator already learnt from the individual gauges:
//   >= 98 OK   |   90 - 98 Warning   |   < 90 Critical
constexpr double kPassRateOkThresholdPct      = 98.0;
constexpr double kPassRateWarningThresholdPct = 90.0;

// OEE-derived tier thresholds. Looser than pass-rate because OEE
// compounds Availability x Performance x Quality and world-class
// industrial sits at 85% (Vorne):
//   >= 85 OK   |   80 - 85 Warning   |   < 80 Critical
constexpr double kOeeOkThresholdPct      = 85.0;
constexpr double kOeeWarningThresholdPct = 80.0;

// Placeholder OEE formula derives a movement-with-quality value
// from the aggregate pass rate; replaced by a real OEE model in
// Phase 8F. Formula: oee = kOeeBaseline + (passRate - 90) * kOeeQualityWeight
constexpr double kOeeFormulaBaseline      = 80.0;
constexpr double kOeeFormulaPivotPct      = 90.0;
constexpr double kOeeFormulaQualityWeight = 0.7;

// Session-uptime donut palette (Phase 8C). Idle gets a calmer
// neutral blue since it is neither good nor bad on its own; the
// other three states reuse the QualityCheckpointStatus colour
// vocabulary so the dashboard reads as one visual language.
constexpr Rgb kUptimeIdleColor = {0.42, 0.55, 0.78};

// Percent scale -- used wherever a 0..1 share is converted to a
// 0..100 percentage for display.
constexpr double kPercentScale = 100.0;
}  // namespace

DashboardPage::DashboardPage(DialogManager& dialogManager)
    : Page(dialogManager) {
    log().debug("DashboardPage: constructing");
    buildUI();
    applyStyles();
}

DashboardPage::~DashboardPage() {
    // Unregister from presenter before destruction
    if (presenter_) {
        presenter_->removeObserver(this);
    }
    // Stop the uptime refresh timer so its lambda doesn't fire
    // against a half-destructed page on the next Glib tick.
    if (uptimeRefreshConn_.connected()) {
        uptimeRefreshConn_.disconnect();
    }
}

void DashboardPage::initialize(std::shared_ptr<DashboardPresenter> presenter) {
    log().info("DashboardPage: initialized, registering with presenter");
    presenter_ = presenter;

    // Register as observer - will receive all state updates
    presenter_->addObserver(this);

    // Hook the presenter's system-state signal to drive the
    // session-uptime donut (Phase 8C). The signal fires on every
    // state transition; we accumulate the previous state's
    // elapsed time and push a fresh segment list to the donut.
    presenter_->signalSystemStateChanged().connect(
        sigc::mem_fun(*this, &DashboardPage::onSystemStateChangedForUptime));

    // Periodic refresh -- 5 s strikes a balance between visible
    // motion (the operator sees the donut tick forward while the
    // system holds in one state) and CPU cost (a static donut
    // re-render is cheap but not free).
    constexpr unsigned kUptimeRefreshIntervalMs = 5'000;
    uptimeRefreshConn_ = Glib::signal_timeout().connect(
        [this]() {
            refreshUptimeDonut();
            return true;  // keep firing
        },
        kUptimeRefreshIntervalMs);

    // Anchor the first segment at "now" so the donut starts
    // accumulating immediately, even before the first explicit
    // state-change signal arrives.
    uptimeSegmentStart_ = std::chrono::steady_clock::now();

    // Paint the donut once up front so it shows the empty ring +
    // "--%" placeholder immediately. Without this the widget stays
    // visually empty until the first state-change signal arrives
    // (primary: ~instant via SimulatedModel) or the 5 s timer fires
    // (secondary: MirrorModel is passive, state isn't bridged, so
    // the user would otherwise see ~5 s of blank space where the
    // donut belongs).
    refreshUptimeDonut();

    // Presenter will send initial state after registration
}

Glib::ustring DashboardPage::pageTitle() const {
    return _("Dashboard");
}

void DashboardPage::applyRole(app::auth::Role role) {
    // Calibration interrupts production and Reset wipes the work
    // unit -- both fall under canResetSystem / canCalibrate which
    // require Maintenance or above. Operators see the buttons in
    // the layout (so they know what's possible) but cannot trigger
    // them; the tooltip surfaces the reason.
    //
    // Cache the role so updateControlPanel (called every time the
    // presenter pushes a ControlPanelViewModel) gates the
    // sensitivity update too -- otherwise a presenter update would
    // silently re-enable buttons the role forbids, which is exactly
    // the bug we hit during B2.5 docker testing.
    currentRole_ = role;

    const bool calibrateAllowed = app::auth::canCalibrate(role);
    const bool resetAllowed     = app::auth::canResetSystem(role);

    if (controlPanelWidgets_.calibrationButton != nullptr) {
        controlPanelWidgets_.calibrationButton->set_sensitive(
            calibrateAllowed);
        if (!calibrateAllowed) {
            controlPanelWidgets_.calibrationButton->set_tooltip_text(
                _("Requires Maintenance role"));
        }
    }
    if (controlPanelWidgets_.resetButton != nullptr) {
        controlPanelWidgets_.resetButton->set_sensitive(resetAllowed);
        if (!resetAllowed) {
            controlPanelWidgets_.resetButton->set_tooltip_text(
                _("Requires Maintenance role"));
        }
    }
}

void DashboardPage::onThemeChanged() {
    refreshThemedWidgets();
}

void DashboardPage::setCompact(bool compact) {
    // Compact mode -- driven by MultiStationDashboardPage when the
    // dashboard is hosted in a narrow pane. Multi-station with the
    // full 200px sidebar leaves ~760px per pane on a 1920x1080
    // terminal -- below the natural width of the standard cards.
    // Aggressive measures:
    //   * Quality gauges shrink to 50x50 (from 100x100).
    //   * Trend charts are hidden entirely -- the gauges + numeric
    //     pass-rate are sufficient for at-a-glance monitoring;
    //     sparkline detail belongs in the dedicated History tab.
    //   * The .dashboard-compact CSS class on the page also shrinks
    //     paddings and font sizes (see adwaita-theme.css).
    constexpr int kCompactGaugeSize     = 40;   // 100 -> 40

    for (auto& card : qualityCards_) {
        if (card.gauge != nullptr) {
            if (compact) {
                card.gauge->set_content_width(kCompactGaugeSize);
                card.gauge->set_content_height(kCompactGaugeSize);
                card.gauge->set_size_request(kCompactGaugeSize, kCompactGaugeSize);
            }
            card.gauge->queue_draw();
        }
    }
    for (auto* chart : trendCharts_) {
        if (chart != nullptr) {
            // Hide entirely in compact mode -- they're the widest
            // element per quality card and provide detail the
            // operator can reach in the History tab anyway.
            if (compact) {
                chart->set_visible(false);
            }
            chart->queue_draw();
        }
    }

    // Hide the KPI strip in compact mode -- two side-by-side
    // dashboards each with their own KPI cards becomes visual noise
    // without adding information. Single-station keeps the strip.
    if (kpiStripWidgets_.container != nullptr) {
        kpiStripWidgets_.container->set_visible(!compact);
    }
}

void DashboardPage::refreshThemedWidgets() {
    for (auto& card : qualityCards_) {
        if (card.gauge) card.gauge->queue_draw();
    }
    for (auto* chart : trendCharts_) {
        if (chart) chart->queue_draw();
    }
}

// ViewObserver Interface Implementation

/// @note All these methods are called from Presenter thread!
///       Must use Glib::signal_idle() to update GTK widgets safely.

void DashboardPage::onWorkUnitChanged(const presenter::WorkUnitViewModel& vm) {
    log().trace("View received WorkUnitVM: id={} progress={}/{}",
                vm.workUnitId, vm.completedOperations, vm.totalOperations);
    Glib::signal_idle().connect_once([this, vm]() { updateWorkUnitWidgets(vm); });
}

void DashboardPage::onEquipmentCardChanged(const presenter::EquipmentCardViewModel& vm) {
    // status enum -> short label for the log
    const char* s = [&] {
        using S = presenter::EquipmentCardStatus;
        switch (vm.status) {
            case S::Offline:     return "Offline";
            case S::StartingUp:  return "StartingUp";
            case S::CheckOutput: return "CheckOutput";
            case S::Online:      return "Online";
            case S::Processing:  return "Processing";
            case S::WarmingUp:   return "WarmingUp";
            case S::Ready:       return "Ready";
            case S::Reboot:      return "Reboot";
            case S::Error:       return "Error";
            case S::Disabled:    return "Disabled";
            default:             return "Unknown";
        }
    }();
    log().trace("View received EquipmentCardVM: id={} status={}",
                vm.equipmentId, s);
    Glib::signal_idle().connect_once([this, vm]() { updateEquipmentCard(vm); });
}

void DashboardPage::onQualityCheckpointChanged(const presenter::QualityCheckpointViewModel& vm) {
    const char* s = [&] {
        using S = presenter::QualityCheckpointStatus;
        switch (vm.status) {
            case S::Passing:  return "Passing";
            case S::Warning:  return "Warning";
            case S::Critical: return "Critical";
            default:          return "Unknown";
        }
    }();
    log().trace("View received QualityCheckpointVM: id={} status={} pass={:.1f}%",
                vm.checkpointId, s, vm.passRate);
    Glib::signal_idle().connect_once([this, vm]() { updateQualityCard(vm); });
}

void DashboardPage::onControlPanelChanged(const presenter::ControlPanelViewModel& vm) {
    log().trace("View received ControlPanelVM: start={} stop={} reset={} calib={}",
                vm.startEnabled, vm.stopEnabled,
                vm.resetRestartEnabled, vm.calibrationEnabled);
    Glib::signal_idle().connect_once([this, vm]() { updateControlPanel(vm); });
}

void DashboardPage::onStatusZoneChanged(const presenter::StatusZoneViewModel& vm) {
    log().trace("View received StatusZoneVM");
    Glib::signal_idle().connect_once([this, vm]() { updateStatusZone(vm); });
}

void DashboardPage::onError(const std::string& errorMessage) {
    log().error("DashboardPage: onError from presenter: {}", errorMessage);
    // Show error dialog (marshaled to GTK thread)
    Glib::signal_idle().connect_once([errorMessage]() {
        auto dialog = Gtk::MessageDialog(errorMessage, false, 
                                         Gtk::MessageType::ERROR, 
                                         Gtk::ButtonsType::OK, true);
        dialog.set_title(_("Error"));
        dialog.set_modal(true);
        dialog.present();
    });
}

// UI Construction

void DashboardPage::buildUI() {
    // Load the entire static layout from XML -- all widget creation,
    // spacing, CSS classes, and translatable labels live in the .ui file.
    auto builder = Gtk::Builder::create_from_file(
        app::config::defaults::kDashboardPageUI);

    auto* root = builder->get_widget<Gtk::Box>("dashboard_root");
    if (root) {
        append(*root);
    }

    // Status zone
    statusZoneWidgets_.bannerBox = builder->get_widget<Gtk::Box>("status_banner");
    statusZoneWidgets_.messageLabel = builder->get_widget<Gtk::Label>("status_message");

    // KPI top strip -- three BigNumberCard widgets injected into the
    // empty container defined in dashboard-page.ui. Initial values
    // give the operator something to read before the first presenter
    // notification arrives; targets are static (set here, not from
    // the model, until Phase 8F adds first-class fields).
    kpiStripWidgets_.container = builder->get_widget<Gtk::Box>("kpi_strip");
    if (kpiStripWidgets_.container) {
        kpiStripWidgets_.oeeCard = Gtk::make_managed<BigNumberCard>();
        kpiStripWidgets_.oeeCard->setLabel(_("OEE"));
        kpiStripWidgets_.oeeCard->setUnit("%");
        kpiStripWidgets_.oeeCard->setValue(kOeeInitialPct, 1);
        kpiStripWidgets_.oeeCard->setTarget(kOeeTargetPct);
        kpiStripWidgets_.oeeCard->setStatus(BigNumberCard::Status::Ok);
        kpiStripWidgets_.container->append(*kpiStripWidgets_.oeeCard);

        kpiStripWidgets_.throughputCard = Gtk::make_managed<BigNumberCard>();
        kpiStripWidgets_.throughputCard->setLabel(_("THROUGHPUT"));
        kpiStripWidgets_.throughputCard->setUnit(_("u/h"));
        // Placeholder demo value until the model layer surfaces a
        // real throughput counter (work units per hour). Tracked in
        // the action queue as Phase 8F.
        kpiStripWidgets_.throughputCard->setValue(kThroughputPlaceholder, 0);
        kpiStripWidgets_.throughputCard->setTarget(kThroughputTargetUph);
        kpiStripWidgets_.throughputCard->setStatus(BigNumberCard::Status::Ok);
        kpiStripWidgets_.container->append(*kpiStripWidgets_.throughputCard);

        kpiStripWidgets_.passRateCard = Gtk::make_managed<BigNumberCard>();
        kpiStripWidgets_.passRateCard->setLabel(_("PASS RATE"));
        kpiStripWidgets_.passRateCard->setUnit("%");
        kpiStripWidgets_.passRateCard->setValue(0.0, 1);
        kpiStripWidgets_.passRateCard->setTarget(kPassRateTargetPct);
        kpiStripWidgets_.passRateCard->setStatus(BigNumberCard::Status::Ok);
        kpiStripWidgets_.container->append(*kpiStripWidgets_.passRateCard);
    }

    // Work unit
    workUnitWidgets_.workUnitIdLabel = builder->get_widget<Gtk::Label>("wu_id_label");
    workUnitWidgets_.productIdLabel = builder->get_widget<Gtk::Label>("wu_product_label");
    workUnitWidgets_.productDescLabel = builder->get_widget<Gtk::Label>("wu_desc_label");
    workUnitWidgets_.progressBar = builder->get_widget<Gtk::ProgressBar>("wu_progress");
    workUnitWidgets_.statusLabel = builder->get_widget<Gtk::Label>("wu_status");

    // Equipment cards
    // Card IDs match the SimulatedModel demo data (equipmentId 0, 1, 2)
    // so the presenter's VMs route to the correct card.
    for (uint32_t i = 0; i < 3; ++i) {
        EquipmentCard card;
        card.equipmentId = i;
        auto id = std::to_string(i);
        card.cardBox = builder->get_widget<Gtk::Box>("eq_card_" + id);
        card.statusDot = builder->get_widget<Gtk::Label>("eq_dot_" + id);
        card.statusLabel = builder->get_widget<Gtk::Label>("eq_status_" + id);
        card.consumablesLabel = builder->get_widget<Gtk::Label>("eq_info_" + id);
        card.enabledSwitch = builder->get_widget<Gtk::Switch>("eq_switch_" + id);

        card.enabledSwitch->signal_state_set().connect(
            [this, equipmentId = card.equipmentId](bool state) {
                onEquipmentSwitchToggled(equipmentId, state);
                return false;
            }, false);

        equipmentCards_.push_back(card);
    }

    // Quality cards (gauge + trend chart injected dynamically)
    for (uint32_t i = 0; i < 3; ++i) {
        QualityCard card;
        card.checkpointId = i;
        auto id = std::to_string(i);
        card.cardBox = builder->get_widget<Gtk::Box>("qc_card_" + id);
        card.nameLabel = builder->get_widget<Gtk::Label>("qc_name_" + id);
        card.statusDot = builder->get_widget<Gtk::Label>("qc_dot_" + id);
        card.passRateLabel = builder->get_widget<Gtk::Label>("qc_rate_" + id);
        card.statsLabel = builder->get_widget<Gtk::Label>("qc_stats_" + id);
        card.lastDefectLabel = builder->get_widget<Gtk::Label>("qc_defect_" + id);

        // Inject dynamic Cairo gauge into the container defined in XML
        auto* gaugeContainer = builder->get_widget<Gtk::Box>("qc_gauge_container_" + id);
        card.gauge = Gtk::make_managed<QualityGauge>();
        card.gauge->set_margin_top(sizes::kSpacingSmall);
        card.gauge->set_margin_bottom(sizes::kSpacingSmall);
        gaugeContainer->append(*card.gauge);

        // Inject dynamic trend chart into the container defined in XML.
        // Hidden by default since Phase 8E -- the KPI top strip's
        // aggregate Pass Rate plus the per-checkpoint gauge already
        // cover at-a-glance quality monitoring. Sparkline detail
        // belongs in the History tab; keeping the widget constructed
        // (just invisible) so a future operator-toggle or compact-
        // mode flag can flip it back on without re-plumbing.
        auto* trendContainer = builder->get_widget<Gtk::Box>("qc_trend_container_" + id);
        auto* chart = Gtk::make_managed<TrendChart>(
            "", sizes::kTrendChartMinY, sizes::kTrendChartMaxY,
            sizes::kTrendChartCapacity);
        chart->set_content_height(sizes::kTrendChartInlineHeight);
        chart->set_vexpand(true);
        chart->set_hexpand(true);
        trendContainer->append(*chart);
        trendContainer->set_visible(false);
        trendCharts_.push_back(chart);

        qualityCards_.push_back(card);
    }

    // Session uptime donut (Phase 8C). Extracted to keep buildUI()
    // below the readability-function-size threshold.
    buildUptimeDonut(builder);

    // Control panel
    controlPanelWidgets_.activeIndicator = builder->get_widget<Gtk::Label>("cp_indicator");
    controlPanelWidgets_.startButton = builder->get_widget<Gtk::Button>("cp_start");
    controlPanelWidgets_.stopButton = builder->get_widget<Gtk::Button>("cp_stop");
    controlPanelWidgets_.resetButton = builder->get_widget<Gtk::Button>("cp_reset");
    controlPanelWidgets_.calibrationButton = builder->get_widget<Gtk::Button>("cp_calibration");

    // GTK4 Builder parses css-classes with spaces inconsistently across
    // versions, so we add the per-button color classes from code.
    controlPanelWidgets_.startButton->add_css_class(css::kStartButton);
    controlPanelWidgets_.stopButton->add_css_class(css::kStopButton);
    controlPanelWidgets_.resetButton->add_css_class(css::kResetButton);
    controlPanelWidgets_.calibrationButton->add_css_class(css::kCalibrationButton);

    controlPanelWidgets_.startButton->signal_clicked().connect(
        sigc::mem_fun(*this, &DashboardPage::onStartButtonClicked));
    controlPanelWidgets_.stopButton->signal_clicked().connect(
        sigc::mem_fun(*this, &DashboardPage::onStopButtonClicked));
    controlPanelWidgets_.resetButton->signal_clicked().connect(
        sigc::mem_fun(*this, &DashboardPage::onResetButtonClicked));
    controlPanelWidgets_.calibrationButton->signal_clicked().connect(
        sigc::mem_fun(*this, &DashboardPage::onCalibrationButtonClicked));
}

// Event Handlers (User Actions -> Presenter)

void DashboardPage::onStartButtonClicked() {
    log().debug("DashboardPage: Start button clicked");
    if (presenter_) {
        presenter_->onStartClicked();
    }
}

void DashboardPage::onStopButtonClicked() {
    log().debug("DashboardPage: Stop button clicked");
    if (presenter_) {
        presenter_->onStopClicked();
    }
}

void DashboardPage::onResetButtonClicked() {
    log().debug("DashboardPage: Reset button clicked (opening confirmation)");
    auto* parent = dynamic_cast<Gtk::Window*>(get_root());

    dialogManager_.showConfirmAsync(
        _("Confirm Reset"),
        _("Reset the system?\n\n"
          "This will stop all operations and return equipment to initial state."),
        [this](bool confirmed) {
            log().debug("DashboardPage: Reset confirmation -> {}",
                        confirmed ? "confirmed" : "cancelled");
            if (confirmed && presenter_) {
                presenter_->onResetRestartClicked();
            }
        },
        parent
    );
}

void DashboardPage::onCalibrationButtonClicked() {
    log().debug("DashboardPage: Calibration button clicked (opening confirmation)");
    auto* parent = dynamic_cast<Gtk::Window*>(get_root());

    dialogManager_.showConfirmAsync(
        _("Confirm Calibration"),
        _("Start calibration procedure?\n\n"
          "All equipment will be moved to home position.\n"
          "This may take several minutes."),
        [this](bool confirmed) {
            log().debug("DashboardPage: Calibration confirmation -> {}",
                        confirmed ? "confirmed" : "cancelled");
            if (confirmed && presenter_) {
                presenter_->onCalibrationClicked();
            }
        },
        parent
    );
}

void DashboardPage::onEquipmentSwitchToggled(uint32_t equipmentId, bool enabled) {
    log().debug("DashboardPage: equipment switch {} -> {}",
                equipmentId, enabled ? "ON" : "OFF");
    if (presenter_) {
        presenter_->onEquipmentToggled(equipmentId, enabled);
    }
}

// UI Update Helpers (Render ViewModels)

void DashboardPage::updateWorkUnitWidgets(const presenter::WorkUnitViewModel& vm) {
    workUnitWidgets_.workUnitIdLabel->set_text(vm.workUnitId);
    workUnitWidgets_.productIdLabel->set_text(vm.productId);
    workUnitWidgets_.productDescLabel->set_text(vm.productDescription);
    
    workUnitWidgets_.progressBar->set_fraction(vm.progress);
    workUnitWidgets_.progressBar->set_text(
        std::to_string(vm.completedOperations) + " / " + 
        std::to_string(vm.totalOperations) + " operations"
    );
    
    workUnitWidgets_.statusLabel->set_text(vm.statusMessage);
    
    // Update styling based on error state
    if (vm.hasErrors) {
        workUnitWidgets_.statusLabel->add_css_class(css::kErrorStatus);
    } else {
        workUnitWidgets_.statusLabel->remove_css_class(css::kErrorStatus);
    }
}

void DashboardPage::updateEquipmentCard(const presenter::EquipmentCardViewModel& vm) {
    // Find card by equipment ID
    auto it = std::find_if(equipmentCards_.begin(), equipmentCards_.end(),
        [&vm](const EquipmentCard& card) { return card.equipmentId == vm.equipmentId; }
    );
    
    if (it == equipmentCards_.end()) return;
    
    auto& card = *it;
    
    // Update status dot color based on status
    const char* dotColor = css::kEquipmentOffline;
    switch (vm.status) {
        case presenter::EquipmentCardStatus::Online:     dotColor = css::kEquipmentOnline; break;
        case presenter::EquipmentCardStatus::Processing: dotColor = css::kEquipmentProcessing; break;
        case presenter::EquipmentCardStatus::Error:      dotColor = css::kEquipmentError; break;
        case presenter::EquipmentCardStatus::Offline:    dotColor = css::kEquipmentOffline; break;
        default: break;
    }

    // Remove old status classes and add new one
    card.statusDot->remove_css_class(css::kEquipmentOnline);
    card.statusDot->remove_css_class(css::kEquipmentProcessing);
    card.statusDot->remove_css_class(css::kEquipmentError);
    card.statusDot->remove_css_class(css::kEquipmentOffline);
    card.statusDot->add_css_class(dotColor);
    
    // Update status text
    Glib::ustring statusText;
    switch (vm.status) {
        case presenter::EquipmentCardStatus::Online: statusText = _("ONLINE"); break;
        case presenter::EquipmentCardStatus::Processing: statusText = _("PROCESSING"); break;
        case presenter::EquipmentCardStatus::Error: statusText = _("ERROR"); break;
        case presenter::EquipmentCardStatus::Offline: statusText = _("OFFLINE"); break;
        default: statusText = _("UNKNOWN"); break;
    }
    card.statusLabel->set_text(statusText);
    
    // Update consumables
    card.consumablesLabel->set_text(vm.consumables);
    
    // Update switch
    card.enabledSwitch->set_active(vm.enabled);
}

void DashboardPage::updateQualityCard(const presenter::QualityCheckpointViewModel& vm) {
    if (vm.checkpointId >= qualityCards_.size()) return;
    
    auto& card = qualityCards_[vm.checkpointId];
    
    // Update checkpoint name
    card.nameLabel->set_text(vm.checkpointName);
    
    // Update status dot color based on status
    const char* dotColor = css::kQualityPassing;
    switch (vm.status) {
        case presenter::QualityCheckpointStatus::Passing:  dotColor = css::kQualityPassing; break;
        case presenter::QualityCheckpointStatus::Warning:  dotColor = css::kQualityWarning; break;
        case presenter::QualityCheckpointStatus::Critical: dotColor = css::kQualityCritical; break;
    }

    // Remove old status classes and add new one
    card.statusDot->remove_css_class(css::kQualityPassing);
    card.statusDot->remove_css_class(css::kQualityWarning);
    card.statusDot->remove_css_class(css::kQualityCritical);
    card.statusDot->add_css_class(dotColor);
    
    // Update gauge: arc length reflects real pass rate, color follows status
    card.gauge->setValue(vm.passRate, vm.status);

    // Feed the trend chart for this checkpoint (if present)
    if (vm.checkpointId < trendCharts_.size() && trendCharts_[vm.checkpointId]) {
        trendCharts_[vm.checkpointId]->addPoint(vm.passRate);
    }

    // Remember the latest reading so the KPI strip can recompute
    // the aggregate. Bounds-checked because the model could in
    // principle ship more checkpoints than we have slots for.
    if (vm.checkpointId < latestPassRates_.size()) {
        latestPassRates_[vm.checkpointId] = vm.passRate;
        updateTopMetrics();
    }
    
    // Update pass rate - large and prominent
    card.passRateLabel->set_text(
        std::vformat("{:.1f}%", std::make_format_args(vm.passRate)));
    
    // Update stats - inspected • defects
    card.statsLabel->set_text(
        Glib::ustring::compose(_("%1 inspected • %2 defects"),
                               vm.unitsInspected, vm.defectsFound));

    // Update last defect info
    if (!vm.lastDefect.empty()) {
        card.lastDefectLabel->set_text(
            Glib::ustring::compose(_("Last: %1"), vm.lastDefect));
    } else {
        card.lastDefectLabel->set_text(_("No defects detected"));
    }
}

void DashboardPage::updateControlPanel(const presenter::ControlPanelViewModel& vm) {
    // Combine the presenter's VM-level enable flag with the role
    // gate. The presenter doesn't know about user roles -- it tells
    // us "this button would be legal in the current production
    // state". We AND it with canCalibrate / canResetSystem so an
    // Operator never sees Calibration / Reset enabled even when the
    // production state would otherwise allow it.
    const bool calibrateAllowed = app::auth::canCalibrate(currentRole_);
    const bool resetAllowed     = app::auth::canResetSystem(currentRole_);

    controlPanelWidgets_.startButton->set_sensitive(vm.startEnabled);
    controlPanelWidgets_.stopButton->set_sensitive(vm.stopEnabled);
    controlPanelWidgets_.resetButton->set_sensitive(
        vm.resetRestartEnabled && resetAllowed);
    controlPanelWidgets_.calibrationButton->set_sensitive(
        vm.calibrationEnabled && calibrateAllowed);
    
    // Update active indicator
    Glib::ustring indicatorText;
    switch (vm.activeButton) {
        case presenter::ActiveControl::Start:
            indicatorText = _("RUNNING");
            break;
        case presenter::ActiveControl::Stop:
            indicatorText = _("STOPPED");
            break;
        case presenter::ActiveControl::Calibration:
            indicatorText = _("CALIBRATION");
            break;
        default:
            indicatorText = _("IDLE");
            break;
    }
    controlPanelWidgets_.activeIndicator->set_text(indicatorText);
}

void DashboardPage::updateStatusZone(const presenter::StatusZoneViewModel& vm) {
    using Severity = presenter::StatusZoneViewModel::Severity;
    
    if (vm.severity == Severity::NONE) {
        statusZoneWidgets_.bannerBox->set_visible(false);
        return;
    }
    
    statusZoneWidgets_.bannerBox->set_visible(true);
    statusZoneWidgets_.messageLabel->set_text(vm.message);
    
    // Apply CSS class based on severity
    statusZoneWidgets_.bannerBox->remove_css_class(css::kSeverityInfo);
    statusZoneWidgets_.bannerBox->remove_css_class(css::kSeverityWarning);
    statusZoneWidgets_.bannerBox->remove_css_class(css::kSeverityError);

    switch (vm.severity) {
        case Severity::INFO:    statusZoneWidgets_.bannerBox->add_css_class(css::kSeverityInfo); break;
        case Severity::WARNING: statusZoneWidgets_.bannerBox->add_css_class(css::kSeverityWarning); break;
        case Severity::ERROR:   statusZoneWidgets_.bannerBox->add_css_class(css::kSeverityError); break;
        default: break;
    }
}

// KPI strip aggregate computation

void DashboardPage::updateTopMetrics() {
    // Aggregate pass rate = arithmetic mean of every checkpoint
    // that has reported at least once. Checkpoints we haven't seen
    // yet are skipped so a partially-populated dashboard still
    // shows a meaningful Pass Rate without dropping toward zero.
    double sum   = 0.0;
    int    count = 0;
    for (const auto& opt : latestPassRates_) {
        if (opt.has_value()) {
            sum += static_cast<double>(*opt);
            ++count;
        }
    }
    if (count == 0) return;
    const double avgPassRate = sum / count;

    // Status thresholds match the QualityCheckpointStatus tiers
    // operators are already used to from the per-checkpoint cards
    // (constants live in the anonymous-namespace block at the top
    // of this file alongside the KPI targets).
    auto tierForPassRate = [](double v) {
        if (v >= kPassRateOkThresholdPct)      return BigNumberCard::Status::Ok;
        if (v >= kPassRateWarningThresholdPct) return BigNumberCard::Status::Warning;
        return BigNumberCard::Status::Critical;
    };

    if (kpiStripWidgets_.passRateCard != nullptr) {
        kpiStripWidgets_.passRateCard->setValue(avgPassRate, 1);
        kpiStripWidgets_.passRateCard->setStatus(tierForPassRate(avgPassRate));
    }

    // OEE -- placeholder formula until Phase 8F surfaces a real
    // Availability * Performance * Quality breakdown. The current
    // approximation maps the aggregate pass rate to an 80-95 OEE
    // band so the card visibly moves with quality without claiming
    // a metric we don't actually compute. The target of 85% gives
    // the delta arrow something to flip against.
    auto tierForOee = [](double v) {
        if (v >= kOeeOkThresholdPct)      return BigNumberCard::Status::Ok;
        if (v >= kOeeWarningThresholdPct) return BigNumberCard::Status::Warning;
        return BigNumberCard::Status::Critical;
    };

    if (kpiStripWidgets_.oeeCard != nullptr) {
        const double oee =
            kOeeFormulaBaseline +
            (avgPassRate - kOeeFormulaPivotPct) * kOeeFormulaQualityWeight;
        kpiStripWidgets_.oeeCard->setValue(oee, 1);
        kpiStripWidgets_.oeeCard->setStatus(tierForOee(oee));
    }
    // Throughput card is intentionally NOT updated here -- it stays
    // at the static placeholder set in buildUI until Phase 8F.
}

// Session-uptime tracking (Phase 8C donut)

void DashboardPage::buildUptimeDonut(const Glib::RefPtr<Gtk::Builder>& builder) {
    // Container lives inside the Work Unit card -- inline placement
    // keeps the "what we're producing" + "how the session is going"
    // information in one visual block instead of burning another
    // vertical section. Compact size (110x110) fits beside the
    // work-unit text without pushing the rest of the dashboard past
    // the 1200 px height budget on a standard 1920x1080 industrial
    // terminal.
    uptimeWidgets_.container = builder->get_widget<Gtk::Box>("uptime_donut_container");
    if (uptimeWidgets_.container == nullptr) return;

    constexpr int kInlineDonutSize = 110;
    uptimeWidgets_.donut = Gtk::make_managed<DonutChartWidget>();
    uptimeWidgets_.donut->set_content_width(kInlineDonutSize);
    uptimeWidgets_.donut->set_content_height(kInlineDonutSize);
    uptimeWidgets_.donut->set_size_request(kInlineDonutSize, kInlineDonutSize);
    uptimeWidgets_.donut->setCenterTitle("--");
    uptimeWidgets_.donut->setCenterSubtitle(_("uptime"));
    uptimeWidgets_.container->append(*uptimeWidgets_.donut);
}

void DashboardPage::onSystemStateChangedForUptime(int newState) {
    // Accumulate the time spent in the *previous* state before
    // switching the active slot. Bounds-checked so a new state
    // beyond the array doesn't smash a neighbour.
    const auto now = std::chrono::steady_clock::now();
    const double elapsed =
        std::chrono::duration<double>(now - uptimeSegmentStart_).count();
    if (currentUptimeState_ >= 0 &&
        static_cast<size_t>(currentUptimeState_) < uptimeSecondsByState_.size()) {
        uptimeSecondsByState_[currentUptimeState_] += elapsed;
    }

    currentUptimeState_  = newState;
    uptimeSegmentStart_  = now;

    // Push immediately so the donut reflects the transition without
    // waiting for the next periodic refresh.
    refreshUptimeDonut();
}

void DashboardPage::refreshUptimeDonut() {
    if (uptimeWidgets_.donut == nullptr) return;

    // Per-state colours -- aligned with the QualityCheckpointStatus
    // colour vocabulary the rest of the dashboard already uses
    // (passing-green for "good" Running time, amber for Calibration
    // which is a paused-but-not-broken state, red for Error). Idle
    // gets a calmer neutral blue since it is neither good nor bad
    // on its own.
    using Seg = DonutChartWidget::Segment;
    // SystemState int mapping: 0 IDLE, 1 RUNNING, 2 ERROR, 3 CALIBRATION
    static constexpr Rgb kIdleColor        = kUptimeIdleColor;
    static constexpr Rgb kCalibrationColor = colors::kStatusWarningAmber;

    // Snapshot the accumulator with the in-flight current segment
    // folded in so the donut shows a continuously-advancing ring
    // even while the system holds in one state.
    auto live = uptimeSecondsByState_;
    if (currentUptimeState_ >= 0 &&
        static_cast<size_t>(currentUptimeState_) < live.size()) {
        const auto now = std::chrono::steady_clock::now();
        const double inFlight =
            std::chrono::duration<double>(now - uptimeSegmentStart_).count();
        live[currentUptimeState_] += inFlight;
    }

    std::vector<Seg> segments;
    segments.reserve(live.size());
    segments.push_back({_("Idle"),        live[0], kIdleColor});
    segments.push_back({_("Running"),     live[1], colors::kStatusPassingGreen});
    segments.push_back({_("Error"),       live[2], colors::kStatusCriticalRed});
    segments.push_back({_("Calibration"), live[3], kCalibrationColor});
    uptimeWidgets_.donut->setSegments(std::move(segments));

    // Headline: percentage of session time the system was Running.
    // Reads as "production uptime" -- the operator's primary metric.
    const double total = live[0] + live[1] + live[2] + live[3];
    if (total <= 0.0) {
        uptimeWidgets_.donut->setCenterTitle("--");
        uptimeWidgets_.donut->setCenterSubtitle(_("uptime"));
        return;
    }
    const double runningShare = (live[1] / total) * kPercentScale;
    uptimeWidgets_.donut->setCenterTitle(
        std::vformat("{:.0f}%", std::make_format_args(runningShare)));
    uptimeWidgets_.donut->setCenterSubtitle(_("uptime"));
}

// CSS Styling

void DashboardPage::applyStyles() {
    cssProvider_ = Gtk::CssProvider::create();
    cssProvider_->load_from_path(app::config::defaults::kDashboardCSS);

    Gtk::StyleContext::add_provider_for_display(
        Gdk::Display::get_default(),
        cssProvider_,
        GTK_STYLE_PROVIDER_PRIORITY_USER
    );
}

}  // namespace app::view