#include "DashboardPage.h"

namespace app::view {

DashboardPage::DashboardPage()
    : Gtk::Box(Gtk::Orientation::VERTICAL) {
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

void DashboardPage::onActuatorCardChanged(const presenter::ActuatorCardViewModel& vm) {
    // Marshal to GTK main thread
    Glib::signal_idle().connect_once([this, vm]() {
        updateActuatorCard(vm);
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
    Glib::signal_idle().connect_once([this, errorMessage]() {
        auto dialog = Gtk::MessageDialog(*this, errorMessage, false, 
                                         Gtk::MessageType::ERROR, 
                                         Gtk::ButtonsType::OK);
        dialog.set_title("Error");
        dialog.run();
    });
}

// ============================================================================
// UI Construction
// ============================================================================

void DashboardPage::buildUI() {
    set_spacing(20);
    set_margin(20);
    
    buildStatusZone();
    buildWorkUnitSection();
    buildEquipmentCardsSection();
    buildActuatorCardsSection();
    buildControlPanelSection();
}

void DashboardPage::buildWorkUnitSection() {
    auto* frame = Gtk::make_managed<Gtk::Frame>("Current Work Unit");
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    box->set_margin(15);
    
    // Work unit ID
    auto* idBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    idBox->append(*Gtk::make_managed<Gtk::Operation>("ID:"));
    workUnitWidgets_.workUnitIdOperation = Gtk::make_managed<Gtk::Operation>("-");
    workUnitWidgets_.workUnitIdOperation->set_selectable(true);
    idBox->append(*workUnitWidgets_.workUnitIdOperation);
    box->append(*idBox);
    
    // Product ID
    auto* productBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    productBox->append(*Gtk::make_managed<Gtk::Operation>("Product:"));
    workUnitWidgets_.productIdOperation = Gtk::make_managed<Gtk::Operation>("-");
    productBox->append(*workUnitWidgets_.productIdOperation);
    box->append(*productBox);
    
    // Product description
    workUnitWidgets_.productDescOperation = Gtk::make_managed<Gtk::Operation>("");
    workUnitWidgets_.productDescOperation->set_wrap(true);
    workUnitWidgets_.productDescOperation->set_xalign(0.0);
    box->append(*workUnitWidgets_.productDescOperation);
    
    // Progress bar
    workUnitWidgets_.progressBar = Gtk::make_managed<Gtk::ProgressBar>();
    workUnitWidgets_.progressBar->set_show_text(true);
    box->append(*workUnitWidgets_.progressBar);
    
    // Status operation
    workUnitWidgets_.statusOperation = Gtk::make_managed<Gtk::Operation>("Ready");
    box->append(*workUnitWidgets_.statusOperation);
    
    frame->set_child(*box);
    append(*frame);
}

void DashboardPage::buildEquipmentCardsSection() {
    auto* frame = Gtk::make_managed<Gtk::Frame>("Equipment Status");
    auto* grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_margin(15);
    grid->set_row_spacing(15);
    grid->set_column_spacing(15);
    
    // Create placeholder cards (will be populated by Presenter)
    for (uint32_t i = 0; i < 4; ++i) {
        EquipmentCard card;
        card.equipmentId = i + 1;
        
        // Card container
        card.cardBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
        card.cardBox->set_size_request(200, -1);
        card.cardBox->add_css_class("equipment-card");
        
        // Status image
        card.statusImage = Gtk::make_managed<Gtk::Image>();
        card.statusImage->set_size_request(64, 64);
        card.cardBox->append(*card.statusImage);
        
        // Status operation
        card.statusOperation = Gtk::make_managed<Gtk::Operation>("Offline");
        card.statusOperation->add_css_class("status-operation");
        card.cardBox->append(*card.statusOperation);
        
        // Consumables operation
        card.consumablesOperation = Gtk::make_managed<Gtk::Operation>("-");
        card.consumablesOperation->set_wrap(true);
        card.cardBox->append(*card.consumablesOperation);
        
        // Enable/disable switch
        card.enabledSwitch = Gtk::make_managed<Gtk::Switch>();
        card.enabledSwitch->set_halign(Gtk::Align::CENTER);
        card.enabledSwitch->signal_state_set().connect([this, equipmentId = card.equipmentId](bool state) {
            onEquipmentSwitchToggled(equipmentId, state);
            return false;  // Propagate signal
        });
        card.cardBox->append(*card.enabledSwitch);
        
        // Add to grid
        grid->attach(*card.cardBox, i % 2, i / 2);
        
        equipmentCards_.push_back(card);
    }
    
    frame->set_child(*grid);
    append(*frame);
}

void DashboardPage::buildActuatorCardsSection() {
    auto* frame = Gtk::make_managed<Gtk::Frame>("Automated Actuators");
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 15);
    box->set_margin(15);
    box->set_homogeneous(true);
    
    // Create 2 actuator cards
    for (uint32_t i = 0; i < 2; ++i) {
        ActuatorCard card;
        card.actuatorId = i;
        
        // Card container
        card.cardBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
        card.cardBox->add_css_class("actuator-card");
        
        // Status image
        card.statusImage = Gtk::make_managed<Gtk::Image>();
        card.statusImage->set_size_request(80, 80);
        card.cardBox->append(*card.statusImage);
        
        // Status operation
        card.statusOperation = Gtk::make_managed<Gtk::Operation>("Offline");
        card.statusOperation->add_css_class("status-operation");
        card.cardBox->append(*card.statusOperation);
        
        // Mode operation (Auto/Manual)
        card.modeOperation = Gtk::make_managed<Gtk::Operation>("Manual Mode");
        card.modeOperation->add_css_class("mode-operation");
        card.cardBox->append(*card.modeOperation);
        
        // Alert operation
        card.alertOperation = Gtk::make_managed<Gtk::Operation>("");
        card.alertOperation->add_css_class("alert-operation");
        card.alertOperation->set_wrap(true);
        card.cardBox->append(*card.alertOperation);
        
        box->append(*card.cardBox);
        actuatorCards_.push_back(card);
    }
    
    frame->set_child(*box);
    append(*frame);
}

void DashboardPage::buildControlPanelSection() {
    auto* frame = Gtk::make_managed<Gtk::Frame>("Control Panel");
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 15);
    box->set_margin(15);
    box->set_halign(Gtk::Align::CENTER);
    
    // Active indicator
    controlPanelWidgets_.activeIndicator = Gtk::make_managed<Gtk::Operation>("System Idle");
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
    
    statusZoneWidgets_.messageOperation = Gtk::make_managed<Gtk::Operation>("");
    statusZoneWidgets_.messageOperation->set_wrap(true);
    statusZoneWidgets_.messageOperation->set_xalign(0.0);
    statusZoneWidgets_.bannerBox->append(*statusZoneWidgets_.messageOperation);
    
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
    if (presenter_) {
        presenter_->onResetRestartClicked();
    }
}

void DashboardPage::onCalibrationButtonClicked() {
    if (presenter_) {
        presenter_->onCalibrationClicked();
    }
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
    workUnitWidgets_.workUnitIdOperation->set_text(vm.workUnitId);
    workUnitWidgets_.productIdOperation->set_text(vm.productId);
    workUnitWidgets_.productDescOperation->set_text(vm.productDescription);
    
    workUnitWidgets_.progressBar->set_fraction(vm.progress);
    workUnitWidgets_.progressBar->set_text(
        std::to_string(vm.completedOperations) + " / " + 
        std::to_string(vm.totalOperations) + " operations"
    );
    
    workUnitWidgets_.statusOperation->set_text(vm.statusMessage);
    
    // Update styling based on error state
    if (vm.hasErrors) {
        workUnitWidgets_.statusOperation->add_css_class("error-status");
    } else {
        workUnitWidgets_.statusOperation->remove_css_class("error-status");
    }
}

void DashboardPage::updateEquipmentCard(const presenter::EquipmentCardViewModel& vm) {
    // Find card by equipment ID
    auto it = std::find_if(equipmentCards_.begin(), equipmentCards_.end(),
        [&vm](const EquipmentCard& card) { return card.equipmentId == vm.equipmentId; }
    );
    
    if (it == equipmentCards_.end()) return;
    
    auto& card = *it;
    
    // Determine image path based on status
    std::string imagePath;
    switch (vm.status) {
        case presenter::EquipmentCardStatus::Online:
            imagePath = "assets/img/equipment/equipment-online.svg";
            break;
        case presenter::EquipmentCardStatus::Processing:
            imagePath = "assets/img/equipment/equipment-processing.svg";
            break;
        case presenter::EquipmentCardStatus::Error:
            imagePath = "assets/img/equipment/equipment-error.svg";
            break;
        case presenter::EquipmentCardStatus::Offline:
            imagePath = "assets/img/equipment/equipment-offline.svg";
            break;
        default:
            imagePath = "assets/img/equipment/equipment-offline.svg";
            break;
    }
    
    // Update status image
    card.statusImage->set_from_file(imagePath);
    
    // Update status operation
    std::string statusText;
    switch (vm.status) {
        case presenter::EquipmentCardStatus::Online: statusText = "Online"; break;
        case presenter::EquipmentCardStatus::Processing: statusText = "Processing"; break;
        case presenter::EquipmentCardStatus::Error: statusText = "Error"; break;
        case presenter::EquipmentCardStatus::Offline: statusText = "Offline"; break;
        default: statusText = "Unknown"; break;
    }
    card.statusOperation->set_text(statusText);
    
    // Update consumables
    card.consumablesOperation->set_text(vm.consumables);
    
    // Update switch (block signal to avoid loop)
    card.enabledSwitch->set_active(vm.enabled);
    card.enabledSwitch->set_sensitive(!vm.forceDisabled);  // Disable if system forces it
}

void DashboardPage::updateActuatorCard(const presenter::ActuatorCardViewModel& vm) {
    if (vm.actuatorId >= actuatorCards_.size()) return;
    
    auto& card = actuatorCards_[vm.actuatorId];
    
    // Determine image path based on status
    std::string imagePath;
    switch (vm.status) {
        case presenter::ActuatorCardStatus::Working:
            imagePath = "assets/img/actuators/actuator-working.svg";
            break;
        case presenter::ActuatorCardStatus::Idle:
            imagePath = "assets/img/actuators/actuator-idle.svg";
            break;
        case presenter::ActuatorCardStatus::Error:
            imagePath = "assets/img/actuators/actuator-error.svg";
            break;
        default:
            imagePath = "assets/img/actuators/actuator-idle.svg";
            break;
    }
    
    // Update status image
    card.statusImage->set_from_file(imagePath);
    
    // Update status operation
    card.statusOperation->set_text(vm.statusMessage);
    
    // Update mode
    card.modeOperation->set_text(vm.autoMode ? "Auto Mode" : "Manual Mode");
    
    // Update alert
    if (vm.hasAlert) {
        card.alertOperation->set_text("⚠ " + vm.alertMessage);
        card.alertOperation->set_visible(true);
    } else {
        card.alertOperation->set_visible(false);
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
    statusZoneWidgets_.messageOperation->set_text(vm.message);
    
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
    
    // Load CSS from string (in production, load from file)
    cssProvider_->load_from_data(R"(
        .equipment-card {
            border: 1px solid #ccc;
            border-radius: 8px;
            padding: 15px;
            background: white;
        }
        
        .actuator-card {
            border: 2px solid #2196F3;
            border-radius: 10px;
            padding: 20px;
            background: #f5f5f5;
        }
        
        .control-button {
            font-weight: bold;
            font-size: 14px;
        }
        
        .start-button {
            background: #4CAF50;
            color: white;
        }
        
        .stop-button {
            background: #f44336;
            color: white;
        }
        
        .status-banner {
            border-radius: 5px;
            padding: 10px;
        }
        
        .severity-info {
            background: #2196F3;
            color: white;
        }
        
        .severity-warning {
            background: #FF9800;
            color: white;
        }
        
        .severity-error {
            background: #f44336;
            color: white;
        }
        
        .active-indicator {
            font-size: 18px;
            font-weight: bold;
            padding: 10px;
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