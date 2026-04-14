#include "DashboardPresenter.h"
#include "src/model/SimulatedModel.h"

namespace app {

DashboardPresenter::DashboardPresenter() {
}

void DashboardPresenter::initialize() {
    auto& model = model::SimulatedModel::instance();
    
    // Subscribe to Model signals
    model.onEquipmentStatusChanged([this](const model::SimulatedModel::EquipmentStatus& status) {
        handleEquipmentStatusUpdate(status.equipmentId, status.status);
    });
    
    model.onActuatorStatusChanged([this](const model::SimulatedModel::ActuatorStatus& status) {
        handleActuatorStatusUpdate(status.actuatorId, status.status);
    });
    
    model.onQualityCheckpointChanged([this](const model::SimulatedModel::QualityCheckpoint& checkpoint) {
        handleQualityCheckpointUpdate(checkpoint.checkpointId, checkpoint.status);
    });
    
    model.onWorkUnitChanged([this](const model::SimulatedModel::WorkUnit& wu) {
        handleNewWorkUnit(wu.workUnitId);
    });
    
    model.onSystemStateChanged([this](model::SimulatedModel::SystemState state) {
        handleSystemStateChanged(static_cast<int>(state));
    });
}

// User action handlers
void DashboardPresenter::onStartClicked() {
    auto& model = model::SimulatedModel::instance();
    model.startProduction();
}

void DashboardPresenter::onStopClicked() {
    auto& model = model::SimulatedModel::instance();
    model.stopProduction();
}

void DashboardPresenter::onResetRestartClicked() {
    auto& model = model::SimulatedModel::instance();
    model.resetSystem();
}

void DashboardPresenter::onCalibrationClicked() {
    auto& model = model::SimulatedModel::instance();
    model.startCalibration();
}

void DashboardPresenter::onEquipmentToggled(uint32_t equipmentId, bool enabled) {
    auto& model = model::SimulatedModel::instance();
    model.setEquipmentEnabled(equipmentId, enabled);
}

// Model signal handlers
void DashboardPresenter::handleNewWorkUnit(const std::string& workUnitId) {
    auto vm = buildWorkUnitVM(workUnitId);
    notifyWorkUnitChanged(vm);
}

void DashboardPresenter::handleEquipmentStatusUpdate(uint32_t equipmentId, int status) {
    auto vm = buildEquipmentVM(equipmentId, status);
    notifyEquipmentCardChanged(vm);
}

void DashboardPresenter::handleActuatorStatusUpdate(uint32_t actuatorId, int status) {
    auto vm = buildActuatorVM(actuatorId, status);
    notifyActuatorCardChanged(vm);
}

void DashboardPresenter::handleQualityCheckpointUpdate(uint32_t checkpointId, int status) {
    auto vm = buildQualityCheckpointVM(checkpointId, status);
    notifyQualityCheckpointChanged(vm);
}

void DashboardPresenter::handleSystemStateChanged(int newState) {
    auto vm = buildControlPanelVM();
    notifyControlPanelChanged(vm);
}

// ViewModel builders
presenter::WorkUnitViewModel DashboardPresenter::buildWorkUnitVM(const std::string& workUnitId) {
    auto& model = model::SimulatedModel::instance();
    auto& wu = model.getWorkUnit();

    presenter::WorkUnitViewModel vm;
    vm.workUnitId = wu.workUnitId;
    vm.productId = wu.productId;
    vm.productDescription = wu.description;
    vm.completedOperations = wu.completedOperations;
    vm.totalOperations = wu.totalOperations;
    vm.progress = wu.totalOperations > 0
        ? static_cast<float>(wu.completedOperations) / wu.totalOperations
        : 0.0f;
    vm.statusMessage = wu.completedOperations < wu.totalOperations
        ? "Processing operation " + std::to_string(wu.completedOperations + 1) + "..."
        : "Complete";
    vm.isTestMode = false;
    vm.hasErrors = false;

    return vm;
}

presenter::EquipmentCardViewModel DashboardPresenter::buildEquipmentVM(uint32_t equipmentId, int status) {
    presenter::EquipmentCardViewModel vm;
    vm.equipmentId = equipmentId;
    
    switch (status) {
        case 0:  // Offline
            vm.status = presenter::EquipmentCardStatus::Offline;
            vm.consumables = "Not connected";
            vm.enabled = false;
            break;
        case 1:  // Online
            vm.status = presenter::EquipmentCardStatus::Online;
            vm.consumables = "Supply level: 85%";
            vm.enabled = true;
            break;
        case 2:  // Processing
            vm.status = presenter::EquipmentCardStatus::Processing;
            vm.consumables = "Supply level: 60%";
            vm.enabled = true;
            break;
        case 3:  // Error
            vm.status = presenter::EquipmentCardStatus::Error;
            vm.consumables = "Low supply (12%)";
            vm.enabled = false;
            break;
    }
    
    return vm;
}

presenter::ActuatorCardViewModel DashboardPresenter::buildActuatorVM(uint32_t actuatorId, int status) {
    presenter::ActuatorCardViewModel vm;
    vm.actuatorId = actuatorId;
    
    switch (status) {
        case 0:  // Idle
            vm.status = presenter::ActuatorCardStatus::Idle;
            vm.statusMessage = "Idle - At home position";
            vm.autoMode = true;
            vm.atHomePosition = true;
            vm.hasAlert = false;
            break;
        case 1:  // Working
            vm.status = presenter::ActuatorCardStatus::Working;
            vm.statusMessage = "Working - Position X:150 Y:200";
            vm.autoMode = true;
            vm.atHomePosition = false;
            vm.hasAlert = false;
            break;
        case 2:  // Error
            vm.status = presenter::ActuatorCardStatus::Error;
            vm.statusMessage = "Error - Position fault";
            vm.autoMode = true;
            vm.atHomePosition = false;
            vm.hasAlert = true;
            vm.alertMessage = "Position timeout detected";
            break;
    }
    
    return vm;
}

presenter::QualityCheckpointViewModel DashboardPresenter::buildQualityCheckpointVM(uint32_t checkpointId, int status) {
    auto& model = model::SimulatedModel::instance();
    auto& cp = model.getQualityCheckpoint(checkpointId);

    presenter::QualityCheckpointViewModel vm;
    vm.checkpointId = checkpointId;
    vm.checkpointName = cp.name;
    vm.unitsInspected = cp.unitsInspected;
    vm.defectsFound = cp.defectsFound;
    vm.passRate = cp.passRate;
    vm.targetPassRate = 95.0f;
    vm.lastDefect = cp.lastDefect;

    if (cp.passRate >= 95.0f)
        vm.status = presenter::QualityCheckpointStatus::Passing;
    else if (cp.passRate >= 90.0f)
        vm.status = presenter::QualityCheckpointStatus::Warning;
    else
        vm.status = presenter::QualityCheckpointStatus::Critical;

    return vm;
}

presenter::ControlPanelViewModel DashboardPresenter::buildControlPanelVM() {
    auto& model = model::SimulatedModel::instance();
    auto state = model.getState();
    
    presenter::ControlPanelViewModel vm;
    
    using State = model::SimulatedModel::SystemState;
    
    switch (state) {
        case State::IDLE:
            vm.startEnabled = true;
            vm.stopEnabled = false;
            vm.resetRestartEnabled = true;
            vm.calibrationEnabled = true;
            vm.activeButton = presenter::ActiveControl::None;
            break;
        case State::RUNNING:
            vm.startEnabled = false;
            vm.stopEnabled = true;
            vm.resetRestartEnabled = false;
            vm.calibrationEnabled = false;
            vm.activeButton = presenter::ActiveControl::Start;
            break;
        case State::ERROR:
            vm.startEnabled = false;
            vm.stopEnabled = false;
            vm.resetRestartEnabled = true;
            vm.calibrationEnabled = false;
            vm.activeButton = presenter::ActiveControl::None;
            break;
        case State::CALIBRATION:
            vm.startEnabled = false;
            vm.stopEnabled = true;
            vm.resetRestartEnabled = false;
            vm.calibrationEnabled = false;
            vm.activeButton = presenter::ActiveControl::Calibration;
            break;
    }
    
    return vm;
}

// Notification helpers
void DashboardPresenter::notifyWorkUnitChanged(const presenter::WorkUnitViewModel& vm) {
    notifyAll([&vm](ViewObserver* obs) {
        obs->onWorkUnitChanged(vm);
    });
}

void DashboardPresenter::notifyEquipmentCardChanged(const presenter::EquipmentCardViewModel& vm) {
    notifyAll([&vm](ViewObserver* obs) {
        obs->onEquipmentCardChanged(vm);
    });
}

void DashboardPresenter::notifyActuatorCardChanged(const presenter::ActuatorCardViewModel& vm) {
    notifyAll([&vm](ViewObserver* obs) {
        obs->onActuatorCardChanged(vm);
    });
}

void DashboardPresenter::notifyQualityCheckpointChanged(const presenter::QualityCheckpointViewModel& vm) {
    notifyAll([&vm](ViewObserver* obs) {
        obs->onQualityCheckpointChanged(vm);
    });
}

void DashboardPresenter::notifyControlPanelChanged(const presenter::ControlPanelViewModel& vm) {
    notifyAll([&vm](ViewObserver* obs) {
        obs->onControlPanelChanged(vm);
    });
}

}  // namespace app
