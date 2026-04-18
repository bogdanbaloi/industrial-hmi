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
}

void DashboardPage::initialize(std::shared_ptr<DashboardPresenter> presenter) {
    log().info("DashboardPage: initialized, registering with presenter");
    presenter_ = presenter;

    // Register as observer - will receive all state updates
    presenter_->addObserver(this);

    // Presenter will send initial state after registration
}

Glib::ustring DashboardPage::pageTitle() const {
    return _("Dashboard");
}

void DashboardPage::onThemeChanged() {
    refreshThemedWidgets();
}

void DashboardPage::refreshThemedWidgets() {
    for (auto& card : qualityCards_) {
        if (card.gauge) card.gauge->queue_draw();
    }
    for (auto* chart : trendCharts_) {
        if (chart) chart->queue_draw();
    }
}

// ============================================================================
// ViewObserver Interface Implementation
// ============================================================================

/// @note All these methods are called from Presenter thread!
///       Must use Glib::signal_idle() to update GTK widgets safely.

void DashboardPage::onWorkUnitChanged(const presenter::WorkUnitViewModel& vm) {
    log().trace("DashboardPage: WorkUnitViewModel received (id={})", vm.workUnitId);
    Glib::signal_idle().connect_once([this, vm]() { updateWorkUnitWidgets(vm); });
}

void DashboardPage::onEquipmentCardChanged(const presenter::EquipmentCardViewModel& vm) {
    log().trace("DashboardPage: EquipmentCardViewModel received (id={})", vm.equipmentId);
    Glib::signal_idle().connect_once([this, vm]() { updateEquipmentCard(vm); });
}

void DashboardPage::onQualityCheckpointChanged(const presenter::QualityCheckpointViewModel& vm) {
    log().trace("DashboardPage: QualityCheckpointViewModel received (id={}, pass={:.1f}%)",
                vm.checkpointId, vm.passRate);
    Glib::signal_idle().connect_once([this, vm]() { updateQualityCard(vm); });
}

void DashboardPage::onControlPanelChanged(const presenter::ControlPanelViewModel& vm) {
    log().trace("DashboardPage: ControlPanelViewModel received");
    Glib::signal_idle().connect_once([this, vm]() { updateControlPanel(vm); });
}

void DashboardPage::onStatusZoneChanged(const presenter::StatusZoneViewModel& vm) {
    log().trace("DashboardPage: StatusZoneViewModel received");
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

// ============================================================================
// UI Construction
// ============================================================================

void DashboardPage::buildUI() {
    // Load the entire static layout from XML — all widget creation,
    // spacing, CSS classes, and translatable labels live in the .ui file.
    auto builder = Gtk::Builder::create_from_file(
        app::config::defaults::kDashboardPageUI);

    auto* root = builder->get_widget<Gtk::Box>("dashboard_root");
    if (root) {
        append(*root);
    }

    // ---- Status zone ----
    statusZoneWidgets_.bannerBox = builder->get_widget<Gtk::Box>("status_banner");
    statusZoneWidgets_.messageLabel = builder->get_widget<Gtk::Label>("status_message");

    // ---- Work unit ----
    workUnitWidgets_.workUnitIdLabel = builder->get_widget<Gtk::Label>("wu_id_label");
    workUnitWidgets_.productIdLabel = builder->get_widget<Gtk::Label>("wu_product_label");
    workUnitWidgets_.productDescLabel = builder->get_widget<Gtk::Label>("wu_desc_label");
    workUnitWidgets_.progressBar = builder->get_widget<Gtk::ProgressBar>("wu_progress");
    workUnitWidgets_.statusLabel = builder->get_widget<Gtk::Label>("wu_status");

    // ---- Equipment cards ----
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

    // ---- Quality cards (gauge + trend chart injected dynamically) ----
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

        // Inject dynamic trend chart into the container defined in XML
        auto* trendContainer = builder->get_widget<Gtk::Box>("qc_trend_container_" + id);
        auto* chart = Gtk::make_managed<TrendChart>(
            "", sizes::kTrendChartMinY, sizes::kTrendChartMaxY,
            sizes::kTrendChartCapacity);
        chart->set_content_height(sizes::kTrendChartInlineHeight);
        chart->set_vexpand(true);
        chart->set_hexpand(true);
        trendContainer->append(*chart);
        trendCharts_.push_back(chart);

        qualityCards_.push_back(card);
    }

    // ---- Control panel ----
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

// ============================================================================
// Event Handlers (User Actions → Presenter)
// ============================================================================

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

// ============================================================================
// UI Update Helpers (Render ViewModels)
// ============================================================================

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
    // Update button sensitivity (enabled/disabled)
    controlPanelWidgets_.startButton->set_sensitive(vm.startEnabled);
    controlPanelWidgets_.stopButton->set_sensitive(vm.stopEnabled);
    controlPanelWidgets_.resetButton->set_sensitive(vm.resetRestartEnabled);
    controlPanelWidgets_.calibrationButton->set_sensitive(vm.calibrationEnabled);
    
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

// ============================================================================
// CSS Styling
// ============================================================================

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