#include "DashboardPage.h"
#include "src/gtk/view/DialogManager.h"

namespace app::view {

DashboardPage::DashboardPage(DialogManager& dialogManager)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , dialogManager_(dialogManager) {
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
    presenter_ = presenter;
    
    // Register as observer - will receive all state updates
    presenter_->addObserver(this);
    
    // Presenter will send initial state after registration
}

// ============================================================================
// ViewObserver Interface Implementation
// ============================================================================

/// @note All these methods are called from Presenter thread!
///       Must use Glib::signal_idle() to update GTK widgets safely.

void DashboardPage::onWorkUnitChanged(const presenter::WorkUnitViewModel& vm) {
    // Marshal to GTK main thread
    Glib::signal_idle().connect_once([this, vm]() {
        updateWorkUnitWidgets(vm);
    });
}

void DashboardPage::onEquipmentCardChanged(const presenter::EquipmentCardViewModel& vm) {
    // Marshal to GTK main thread
    Glib::signal_idle().connect_once([this, vm]() {
        updateEquipmentCard(vm);
    });
}

void DashboardPage::onQualityCheckpointChanged(const presenter::QualityCheckpointViewModel& vm) {
    // Marshal to GTK main thread
    Glib::signal_idle().connect_once([this, vm]() {
        updateQualityCard(vm);
    });
}

void DashboardPage::onControlPanelChanged(const presenter::ControlPanelViewModel& vm) {
    // Marshal to GTK main thread
    Glib::signal_idle().connect_once([this, vm]() {
        updateControlPanel(vm);
    });
}

void DashboardPage::onStatusZoneChanged(const presenter::StatusZoneViewModel& vm) {
    // Marshal to GTK main thread
    Glib::signal_idle().connect_once([this, vm]() {
        updateStatusZone(vm);
    });
}

void DashboardPage::onError(const std::string& errorMessage) {
    // Show error dialog (marshaled to GTK thread)
    Glib::signal_idle().connect_once([errorMessage]() {
        auto dialog = Gtk::MessageDialog(errorMessage, false, 
                                         Gtk::MessageType::ERROR, 
                                         Gtk::ButtonsType::OK, true);
        dialog.set_title("Error");
        dialog.set_modal(true);
        dialog.present();
    });
}

// ============================================================================
// UI Construction
// ============================================================================

void DashboardPage::buildUI() {
    // Modern Industrial Clean design for 1920x1080
    set_spacing(40);  // Airy vertical spacing between major sections
    set_margin_start(60);
    set_margin_end(60);
    set_margin_top(40);
    set_margin_bottom(40);
    
    // Section order: Status → Work Unit → Equipment → Quality → Controls
    buildStatusZone();
    buildWorkUnitSection();
    buildEquipmentSection();
    buildQualitySection();
    buildControlPanelSection();
}

void DashboardPage::buildWorkUnitSection() {
    // Section header
    auto* header = Gtk::make_managed<Gtk::Label>("WORK UNIT");
    header->set_xalign(0.0);
    header->add_css_class("section-header");
    append(*header);
    
    // Main card frame
    auto* frame = Gtk::make_managed<Gtk::Frame>();
    frame->set_margin_top(15);
    frame->set_margin_bottom(30);
    frame->add_css_class("work-unit-card");
    
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 15);
    box->set_margin(25);
    
    // First row: Work Unit ID • Product ID
    auto* idsRow = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 30);
    
    // Work Unit ID
    auto* wuBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    auto* wuLabel = Gtk::make_managed<Gtk::Label>("Work Unit:");
    wuLabel->add_css_class("field-label");
    workUnitWidgets_.workUnitIdLabel = Gtk::make_managed<Gtk::Label>("-");
    workUnitWidgets_.workUnitIdLabel->add_css_class("field-value-large");
    workUnitWidgets_.workUnitIdLabel->set_selectable(true);
    wuBox->append(*wuLabel);
    wuBox->append(*workUnitWidgets_.workUnitIdLabel);
    idsRow->append(*wuBox);
    
    // Separator
    auto* sep = Gtk::make_managed<Gtk::Label>("•");
    sep->add_css_class("separator-dot");
    idsRow->append(*sep);
    
    // Product ID
    auto* prodBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    auto* prodLabel = Gtk::make_managed<Gtk::Label>("Product:");
    prodLabel->add_css_class("field-label");
    workUnitWidgets_.productIdLabel = Gtk::make_managed<Gtk::Label>("-");
    workUnitWidgets_.productIdLabel->add_css_class("field-value");
    prodBox->append(*prodLabel);
    prodBox->append(*workUnitWidgets_.productIdLabel);
    idsRow->append(*prodBox);
    
    box->append(*idsRow);
    
    // Product description
    workUnitWidgets_.productDescLabel = Gtk::make_managed<Gtk::Label>("");
    workUnitWidgets_.productDescLabel->set_wrap(true);
    workUnitWidgets_.productDescLabel->set_xalign(0.0);
    workUnitWidgets_.productDescLabel->add_css_class("description-text");
    box->append(*workUnitWidgets_.productDescLabel);
    
    // Progress section
    auto* progressBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    progressBox->set_margin_top(15);
    
    workUnitWidgets_.progressBar = Gtk::make_managed<Gtk::ProgressBar>();
    workUnitWidgets_.progressBar->set_show_text(true);
    workUnitWidgets_.progressBar->set_size_request(-1, 28);
    workUnitWidgets_.progressBar->add_css_class("progress-bar-large");
    progressBox->append(*workUnitWidgets_.progressBar);
    
    workUnitWidgets_.statusLabel = Gtk::make_managed<Gtk::Label>("Ready");
    workUnitWidgets_.statusLabel->add_css_class("status-message");
    workUnitWidgets_.statusLabel->set_xalign(0.0);
    progressBox->append(*workUnitWidgets_.statusLabel);
    
    box->append(*progressBox);
    
    frame->set_child(*box);
    append(*frame);
}

void DashboardPage::buildEquipmentSection() {
    // Section header
    auto* header = Gtk::make_managed<Gtk::Label>("EQUIPMENT STATIONS");
    header->set_xalign(0.0);
    header->add_css_class("section-header");
    append(*header);
    
    // Cards container
    auto* grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_margin_top(15);
    grid->set_margin_bottom(30);
    grid->set_row_spacing(20);
    grid->set_column_spacing(20);
    
    // Create 3 equipment cards (A-LINE, B-LINE, C-LINE)
    for (uint32_t i = 0; i < 3; ++i) {
        EquipmentCard card;
        card.equipmentId = i + 1;
        
        // Card container
        card.cardBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
        card.cardBox->set_size_request(280, 200);
        card.cardBox->add_css_class("equipment-card");
        
        // Header: A-LINE, B-LINE, C-LINE with status dot
        const std::array<std::string, 4> lineNames = {"A-LINE", "B-LINE", "C-LINE", "D-LINE"};
        auto* headerBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto* stationLabel = Gtk::make_managed<Gtk::Label>(lineNames[i]);
        stationLabel->add_css_class("card-title");
        headerBox->append(*stationLabel);
        
        // Status dot (colored circle)
        card.statusDot = Gtk::make_managed<Gtk::Label>("●");
        card.statusDot->add_css_class("status-dot");
        headerBox->append(*card.statusDot);
        
        card.cardBox->append(*headerBox);
        
        // Status text (ONLINE/OFFLINE/ERROR/PROCESSING)
        card.statusLabel = Gtk::make_managed<Gtk::Label>("OFFLINE");
        card.statusLabel->add_css_class("equipment-status");
        card.cardBox->append(*card.statusLabel);
        
        // Consumables/supply info
        card.consumablesLabel = Gtk::make_managed<Gtk::Label>("-");
        card.consumablesLabel->set_wrap(true);
        card.consumablesLabel->add_css_class("equipment-info");
        card.cardBox->append(*card.consumablesLabel);
        
        // Enable switch
        card.enabledSwitch = Gtk::make_managed<Gtk::Switch>();
        card.enabledSwitch->set_halign(Gtk::Align::CENTER);
        card.enabledSwitch->set_margin_top(10);
        card.enabledSwitch->signal_state_set().connect([this, equipmentId = card.equipmentId](bool state) {
            onEquipmentSwitchToggled(equipmentId, state);
            return false;
        }, false);
        card.cardBox->append(*card.enabledSwitch);
        
        // Add to grid (2 columns)
        grid->attach(*card.cardBox, i % 2, i / 2);
        
        equipmentCards_.push_back(card);
    }
    
    append(*grid);
}

void DashboardPage::buildQualitySection() {
    // Section header
    auto* header = Gtk::make_managed<Gtk::Label>("QUALITY CHECKPOINTS");
    header->set_xalign(0.0);
    header->add_css_class("section-header");
    append(*header);
    
    // Cards container - 3 quality checkpoints in a row
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 20);
    box->set_margin_top(15);
    box->set_margin_bottom(30);
    box->set_homogeneous(true);
    
    // Create 3 quality checkpoint cards
    for (uint32_t i = 0; i < 3; ++i) {
        QualityCard card;
        card.checkpointId = i;
        
        // Card container
        card.cardBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
        card.cardBox->set_size_request(350, 180);
        card.cardBox->add_css_class("quality-card");
        
        // Checkpoint name with status dot
        auto* headerBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        card.nameLabel = Gtk::make_managed<Gtk::Label>("Checkpoint " + std::to_string(i + 1));
        card.nameLabel->add_css_class("card-title");
        headerBox->append(*card.nameLabel);
        
        card.statusDot = Gtk::make_managed<Gtk::Label>("●");
        card.statusDot->add_css_class("status-dot");
        headerBox->append(*card.statusDot);
        
        card.cardBox->append(*headerBox);
        
        // Quality gauge visual indicator
        card.gaugeImage = Gtk::make_managed<Gtk::Picture>();
        card.gaugeImage->set_size_request(60, 60);
        card.gaugeImage->set_margin_top(8);
        card.gaugeImage->set_margin_bottom(8);
        card.cardBox->append(*card.gaugeImage);
        
        // Pass rate - large and prominent
        card.passRateLabel = Gtk::make_managed<Gtk::Label>("---%");
        card.passRateLabel->add_css_class("pass-rate-large");
        card.cardBox->append(*card.passRateLabel);
        
        // Stats: Inspected / Defects
        card.statsLabel = Gtk::make_managed<Gtk::Label>("0 inspected • 0 defects");
        card.statsLabel->add_css_class("quality-stats");
        card.cardBox->append(*card.statsLabel);
        
        // Last defect info
        card.lastDefectLabel = Gtk::make_managed<Gtk::Label>("-");
        card.lastDefectLabel->set_wrap(true);
        card.lastDefectLabel->add_css_class("defect-info");
        card.cardBox->append(*card.lastDefectLabel);
        
        box->append(*card.cardBox);
        qualityCards_.push_back(card);
    }
    
    append(*box);
}

void DashboardPage::buildControlPanelSection() {
    auto* frame = Gtk::make_managed<Gtk::Frame>("Control Panel");
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 15);
    box->set_margin(15);
    box->set_halign(Gtk::Align::CENTER);
    
    // Active indicator
    controlPanelWidgets_.activeIndicator = Gtk::make_managed<Gtk::Label>("System Idle");
    controlPanelWidgets_.activeIndicator->add_css_class("active-indicator");
    box->append(*controlPanelWidgets_.activeIndicator);
    
    // Start button
    controlPanelWidgets_.startButton = Gtk::make_managed<Gtk::Button>("START");
    controlPanelWidgets_.startButton->add_css_class("control-button");
    controlPanelWidgets_.startButton->add_css_class("start-button");
    controlPanelWidgets_.startButton->set_size_request(120, 60);
    controlPanelWidgets_.startButton->signal_clicked().connect(
        sigc::mem_fun(*this, &DashboardPage::onStartButtonClicked)
    );
    box->append(*controlPanelWidgets_.startButton);
    
    // Stop button
    controlPanelWidgets_.stopButton = Gtk::make_managed<Gtk::Button>("STOP");
    controlPanelWidgets_.stopButton->add_css_class("control-button");
    controlPanelWidgets_.stopButton->add_css_class("stop-button");
    controlPanelWidgets_.stopButton->set_size_request(120, 60);
    controlPanelWidgets_.stopButton->signal_clicked().connect(
        sigc::mem_fun(*this, &DashboardPage::onStopButtonClicked)
    );
    box->append(*controlPanelWidgets_.stopButton);
    
    // Reset button
    controlPanelWidgets_.resetButton = Gtk::make_managed<Gtk::Button>("RESET");
    controlPanelWidgets_.resetButton->add_css_class("control-button");
    controlPanelWidgets_.resetButton->set_size_request(120, 60);
    controlPanelWidgets_.resetButton->signal_clicked().connect(
        sigc::mem_fun(*this, &DashboardPage::onResetButtonClicked)
    );
    box->append(*controlPanelWidgets_.resetButton);
    
    // Calibration button
    controlPanelWidgets_.calibrationButton = Gtk::make_managed<Gtk::Button>("CALIBRATION");
    controlPanelWidgets_.calibrationButton->add_css_class("control-button");
    controlPanelWidgets_.calibrationButton->set_size_request(120, 60);
    controlPanelWidgets_.calibrationButton->signal_clicked().connect(
        sigc::mem_fun(*this, &DashboardPage::onCalibrationButtonClicked)
    );
    box->append(*controlPanelWidgets_.calibrationButton);
    
    frame->set_child(*box);
    append(*frame);
}

void DashboardPage::buildStatusZone() {
    statusZoneWidgets_.bannerBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    statusZoneWidgets_.bannerBox->set_margin(10);
    statusZoneWidgets_.bannerBox->add_css_class("status-banner");
    statusZoneWidgets_.bannerBox->set_visible(false);  // Hidden by default
    
    statusZoneWidgets_.messageLabel = Gtk::make_managed<Gtk::Label>("");
    statusZoneWidgets_.messageLabel->set_wrap(true);
    statusZoneWidgets_.messageLabel->set_xalign(0.0);
    statusZoneWidgets_.bannerBox->append(*statusZoneWidgets_.messageLabel);
    
    append(*statusZoneWidgets_.bannerBox);
}

// ============================================================================
// Event Handlers (User Actions → Presenter)
// ============================================================================

void DashboardPage::onStartButtonClicked() {
    if (presenter_) {
        presenter_->onStartClicked();
    }
}

void DashboardPage::onStopButtonClicked() {
    if (presenter_) {
        presenter_->onStopClicked();
    }
}

void DashboardPage::onResetButtonClicked() {
    auto* parent = dynamic_cast<Gtk::Window*>(get_root());
    
    dialogManager_.showConfirmAsync(
        "Confirm Reset",
        "Reset the system?\n\n"
        "This will stop all operations and return equipment to initial state.",
        [this](bool confirmed) {
            if (confirmed && presenter_) {
                presenter_->onResetRestartClicked();
            }
        },
        parent
    );
}

void DashboardPage::onCalibrationButtonClicked() {
    auto* parent = dynamic_cast<Gtk::Window*>(get_root());
    
    dialogManager_.showConfirmAsync(
        "Confirm Calibration",
        "Start calibration procedure?\n\n"
        "All equipment will be moved to home position.\n"
        "This may take several minutes.",
        [this](bool confirmed) {
            if (confirmed && presenter_) {
                presenter_->onCalibrationClicked();
            }
        },
        parent
    );
}

void DashboardPage::onEquipmentSwitchToggled(uint32_t equipmentId, bool enabled) {
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
        workUnitWidgets_.statusLabel->add_css_class("error-status");
    } else {
        workUnitWidgets_.statusLabel->remove_css_class("error-status");
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
    std::string dotColor;
    switch (vm.status) {
        case presenter::EquipmentCardStatus::Online:
            dotColor = "equipment-online";
            break;
        case presenter::EquipmentCardStatus::Processing:
            dotColor = "equipment-processing";
            break;
        case presenter::EquipmentCardStatus::Error:
            dotColor = "equipment-error";
            break;
        case presenter::EquipmentCardStatus::Offline:
            dotColor = "equipment-offline";
            break;
        default:
            dotColor = "equipment-offline";
            break;
    }
    
    // Remove old status classes and add new one
    card.statusDot->remove_css_class("equipment-online");
    card.statusDot->remove_css_class("equipment-processing");
    card.statusDot->remove_css_class("equipment-error");
    card.statusDot->remove_css_class("equipment-offline");
    card.statusDot->add_css_class(dotColor);
    
    // Update status text
    std::string statusText;
    switch (vm.status) {
        case presenter::EquipmentCardStatus::Online: statusText = "ONLINE"; break;
        case presenter::EquipmentCardStatus::Processing: statusText = "PROCESSING"; break;
        case presenter::EquipmentCardStatus::Error: statusText = "ERROR"; break;
        case presenter::EquipmentCardStatus::Offline: statusText = "OFFLINE"; break;
        default: statusText = "UNKNOWN"; break;
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
    std::string dotColor;
    switch (vm.status) {
        case presenter::QualityCheckpointStatus::Passing:
            dotColor = "quality-passing";
            break;
        case presenter::QualityCheckpointStatus::Warning:
            dotColor = "quality-warning";
            break;
        case presenter::QualityCheckpointStatus::Critical:
            dotColor = "quality-critical";
            break;
    }
    
    // Remove old status classes and add new one
    card.statusDot->remove_css_class("quality-passing");
    card.statusDot->remove_css_class("quality-warning");
    card.statusDot->remove_css_class("quality-critical");
    card.statusDot->add_css_class(dotColor);
    
    // Update gauge image based on status
    std::string gaugePath;
    switch (vm.status) {
        case presenter::QualityCheckpointStatus::Passing:
            gaugePath = "assets/img/quality-gauge-pass.svg";
            break;
        case presenter::QualityCheckpointStatus::Warning:
            gaugePath = "assets/img/quality-gauge-warning.svg";
            break;
        case presenter::QualityCheckpointStatus::Critical:
            gaugePath = "assets/img/quality-gauge-critical.svg";
            break;
    }
    
    // Load SVG using Gtk::Picture (GTK4 native SVG support)
    card.gaugeImage->set_filename(gaugePath);
    
    // Update pass rate - large and prominent
    char passRateText[32];
    snprintf(passRateText, sizeof(passRateText), "%.1f%%", vm.passRate);
    card.passRateLabel->set_text(passRateText);
    
    // Update stats - inspected • defects
    char statsText[128];
    snprintf(statsText, sizeof(statsText), "%d inspected • %d defects", 
             vm.unitsInspected, vm.defectsFound);
    card.statsLabel->set_text(statsText);
    
    // Update last defect info
    if (!vm.lastDefect.empty()) {
        card.lastDefectLabel->set_text("Last: " + vm.lastDefect);
    } else {
        card.lastDefectLabel->set_text("No defects detected");
    }
}

void DashboardPage::updateControlPanel(const presenter::ControlPanelViewModel& vm) {
    // Update button sensitivity (enabled/disabled)
    controlPanelWidgets_.startButton->set_sensitive(vm.startEnabled);
    controlPanelWidgets_.stopButton->set_sensitive(vm.stopEnabled);
    controlPanelWidgets_.resetButton->set_sensitive(vm.resetRestartEnabled);
    controlPanelWidgets_.calibrationButton->set_sensitive(vm.calibrationEnabled);
    
    // Update active indicator
    std::string indicatorText;
    switch (vm.activeButton) {
        case presenter::ActiveControl::Start:
            indicatorText = "🟢 RUNNING";
            break;
        case presenter::ActiveControl::Stop:
            indicatorText = "🔴 STOPPED";
            break;
        case presenter::ActiveControl::Calibration:
            indicatorText = "🟡 CALIBRATION";
            break;
        default:
            indicatorText = "⚪ IDLE";
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
    statusZoneWidgets_.bannerBox->remove_css_class("severity-info");
    statusZoneWidgets_.bannerBox->remove_css_class("severity-warning");
    statusZoneWidgets_.bannerBox->remove_css_class("severity-error");
    
    switch (vm.severity) {
        case Severity::INFO:
            statusZoneWidgets_.bannerBox->add_css_class("severity-info");
            break;
        case Severity::WARNING:
            statusZoneWidgets_.bannerBox->add_css_class("severity-warning");
            break;
        case Severity::ERROR:
            statusZoneWidgets_.bannerBox->add_css_class("severity-error");
            break;
        default:
            break;
    }
}

// ============================================================================
// CSS Styling
// ============================================================================

void DashboardPage::applyStyles() {
    cssProvider_ = Gtk::CssProvider::create();
    
    // Modern Industrial Clean design - optimized for 1920x1080
    cssProvider_->load_from_data(R"(
        /* ===== SECTION HEADERS ===== */
        .section-header {
            font-size: 13px;
            font-weight: 700;
            color: #90A4AE;
            letter-spacing: 1px;
            margin-bottom: 15px;
        }
        
        /* ===== WORK UNIT CARD ===== */
        .work-unit-card {
            border: 2px solid #E0E0E0;
            border-radius: 12px;
            background: white;
            box-shadow: 0 2px 8px rgba(0,0,0,0.04);
        }
        
        .field-label {
            font-size: 13px;
            font-weight: 500;
            color: #78909C;
        }
        
        .field-value {
            font-size: 15px;
            font-weight: 600;
            color: #37474F;
        }
        
        .field-value-large {
            font-size: 17px;
            font-weight: 700;
            color: #263238;
        }
        
        .separator-dot {
            font-size: 18px;
            color: #CFD8DC;
            font-weight: 400;
        }
        
        .description-text {
            font-size: 14px;
            color: #607D8B;
            line-height: 1.5;
        }
        
        .progress-bar-large progressbar {
            min-height: 28px;
            border-radius: 6px;
            background: #ECEFF1;
        }
        
        .progress-bar-large progressbar progress {
            background: linear-gradient(90deg, #42A5F5, #1E88E5);
            border-radius: 6px;
        }
        
        .status-message {
            font-size: 14px;
            color: #546E7A;
            font-weight: 500;
        }
        
        /* ===== EQUIPMENT CARDS ===== */
        .equipment-card {
            border: 2px solid #ECEFF1;
            border-radius: 12px;
            padding: 20px;
            background: white;
            transition: all 0.2s;
        }
        
        .equipment-card:hover {
            border-color: #90CAF9;
            box-shadow: 0 4px 12px rgba(33, 150, 243, 0.1);
        }
        
        .card-title {
            font-size: 14px;
            font-weight: 600;
            color: #455A64;
        }
        
        .status-dot {
            font-size: 16px;
            margin-left: auto;
        }
        
        .equipment-status {
            font-size: 13px;
            font-weight: 700;
            letter-spacing: 0.5px;
            margin-top: 12px;
        }
        
        .equipment-info {
            font-size: 13px;
            color: #78909C;
            margin-top: 8px;
        }
        
        /* ===== QUALITY CARDS ===== */
        .quality-card {
            border: 2px solid #E8F5E9;
            border-radius: 12px;
            padding: 20px;
            background: #FAFAFA;
            transition: all 0.2s;
        }
        
        .quality-card:hover {
            border-color: #81C784;
            box-shadow: 0 4px 12px rgba(76, 175, 80, 0.15);
        }
        
        .pass-rate-large {
            font-size: 36px;
            font-weight: 700;
            color: #2E7D32;
            margin: 12px 0;
        }
        
        .quality-stats {
            font-size: 13px;
            color: #546E7A;
            font-weight: 500;
        }
        
        .defect-info {
            font-size: 12px;
            color: #78909C;
            margin-top: 8px;
        }
        
        /* Quality status dots colors */
        .quality-passing {
            color: #4CAF50;
        }
        
        .quality-warning {
            color: #FF9800;
        }
        
        .quality-critical {
            color: #F44336;
        }
        
        /* Equipment status dots colors */
        .equipment-online {
            color: #4CAF50;  /* Green */
        }
        
        .equipment-processing {
            color: #2196F3;  /* Blue */
        }
        
        .equipment-error {
            color: #F44336;  /* Red */
        }
        
        .equipment-offline {
            color: #9E9E9E;  /* Gray */
        }
        
        /* ===== CONTROL BUTTONS ===== */
        .control-button {
            font-weight: 600;
            font-size: 15px;
            min-width: 140px;
            min-height: 56px;
            border-radius: 8px;
            margin: 0 6px;
            transition: all 0.2s;
        }
        
        .start-button {
            background: #4CAF50;
            color: white;
            border: none;
        }
        
        .start-button:hover {
            background: #66BB6A;
            box-shadow: 0 4px 8px rgba(76, 175, 80, 0.3);
        }
        
        .stop-button {
            background: #F44336;
            color: white;
            border: none;
        }
        
        .stop-button:hover {
            background: #EF5350;
            box-shadow: 0 4px 8px rgba(244, 67, 54, 0.3);
        }
        
        .reset-button {
            background: #FF9800;
            color: white;
            border: none;
        }
        
        .reset-button:hover {
            background: #FFA726;
        }
        
        .calibration-button {
            background: #2196F3;
            color: white;
            border: none;
        }
        
        .calibration-button:hover {
            background: #42A5F5;
        }
        
        /* ===== STATUS BANNER ===== */
        .status-banner {
            border-radius: 8px;
            padding: 14px 20px;
            margin-bottom: 20px;
        }
        
        .severity-info {
            background: #E3F2FD;
            border-left: 4px solid #2196F3;
            color: #1565C0;
        }
        
        .severity-warning {
            background: #FFF3E0;
            border-left: 4px solid #FF9800;
            color: #E65100;
        }
        
        .severity-error {
            background: #FFEBEE;
            border-left: 4px solid #F44336;
            color: #C62828;
        }
        
        /* ===== ACTIVE INDICATOR ===== */
        .active-indicator {
            font-size: 16px;
            font-weight: 600;
            padding: 10px 20px;
            background: #ECEFF1;
            border-radius: 6px;
            color: #37474F;
        }
    )");
    
    // Apply to display
    Gtk::StyleContext::add_provider_for_display(
        Gdk::Display::get_default(),
        cssProvider_,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
}

}  // namespace app::view