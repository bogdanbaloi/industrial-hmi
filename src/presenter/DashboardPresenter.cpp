#include "DashboardPresenter.h"
#include "src/auth/AuditEvent.h"
#include "src/auth/AuditLogger.h"
#include "src/auth/Session.h"
#include "src/auth/User.h"
#include "src/model/SimulatedModel.h"
#include "src/config/config_defaults.h"
#include "src/core/i18n.h"

#include <format>

// Helper: call a logger method only if the optional logger_ is set.
// Tests leave it null; production injects via BasePresenter::setLogger.
#define LOG_IF(LEVEL, ...) do { if (logger_) logger_->LEVEL(__VA_ARGS__); } while (0)

namespace app {

namespace {
// Decode the raw int status codes flowing in from the Model so trace/
// debug output reads as "Processing" instead of "2" -- saves everyone
// from cross-referencing ProductionTypes.h when reading the log.
const char* equipmentStatusName(int s) {
    switch (s) {
        case 0: return "Offline";
        case 1: return "Online";
        case 2: return "Processing";
        case 3: return "Error";
        default: return "Unknown";
    }
}
const char* actuatorStatusName(int s) {
    switch (s) {
        case 0: return "Idle";
        case 1: return "Working";
        case 2: return "Error";
        default: return "Unknown";
    }
}
const char* qualityStatusName(int s) {
    switch (s) {
        case 0: return "Passing";
        case 1: return "Warning";
        case 2: return "Critical";
        default: return "Unknown";
    }
}
const char* systemStateName(int s) {
    switch (s) {
        case 0: return "IDLE";
        case 1: return "RUNNING";
        case 2: return "ERROR";
        case 3: return "CALIBRATION";
        default: return "UNKNOWN";
    }
}
}  // namespace

DashboardPresenter::DashboardPresenter()
    : DashboardPresenter(model::SimulatedModel::instance()) {}

DashboardPresenter::DashboardPresenter(model::ProductionModel& model)
    : model_(model) {}

void DashboardPresenter::initialize() {
    LOG_IF(info, "DashboardPresenter initializing - subscribing to model signals");

    // Subscribe to Model signals
    model_.onEquipmentStatusChanged([this](const model::EquipmentStatus& status) {
        handleEquipmentStatusUpdate(status.equipmentId, status.status);
    });

    model_.onActuatorStatusChanged([this](const model::ActuatorStatus& status) {
        handleActuatorStatusUpdate(status.actuatorId, status.status);
    });

    model_.onQualityCheckpointChanged([this](const model::QualityCheckpoint& checkpoint) {
        handleQualityCheckpointUpdate(checkpoint.checkpointId, checkpoint.status);
    });

    model_.onWorkUnitChanged([this](const model::WorkUnit& wu) {
        handleNewWorkUnit(wu.workUnitId);
    });

    model_.onSystemStateChanged([this](model::SystemState state) {
        handleSystemStateChanged(state);
    });
}

namespace {

// Emit one audit row for a user-initiated action. No-op when audit /
// session are not wired (tests, auth-disabled builds). Captured by
// reference so the helper stays free of allocation overhead on the
// fast path.
void emitAudit(app::auth::AuditLogger* audit,
               app::auth::Session* session,
               std::string_view category,
               std::string_view action,
               std::string_view details,
               std::string_view outcome = app::auth::result::kSuccess) {
    if (audit == nullptr || session == nullptr) return;
    const auto userOpt = session->currentUser();
    const auto user    = userOpt.value_or(app::auth::User{});
    app::auth::AuditEvent e;
    e.username = user.username;
    e.role     = app::auth::roleName(user.role);
    e.category = category;
    e.action   = action;
    e.details  = details;
    e.result   = outcome;
    audit->record(e);
}

// Defense-in-depth role gate. UI buttons are already disabled per
// role in DashboardPage::applyRole, but a future caller (keyboard
// shortcut, programmatic test path, etc.) shouldn't be able to
// bypass the policy by going through the presenter directly.
//
// Returns true to proceed with the action, false to refuse. A refusal
// records an audit FAILURE row + logs a warning so an admin sees
// every unauthorised attempt.
//
// Pass-through when no session is wired (auth disabled): the legacy
// dev path keeps full surface so existing tests / no-auth deployments
// behave unchanged.
template <typename Predicate>
bool checkRole(app::auth::AuditLogger* audit,
               app::auth::Session* session,
               app::core::Logger* logger,
               Predicate allowed,
               std::string_view category,
               std::string_view action,
               std::string_view details = "") {
    if (session == nullptr) return true;
    const auto userOpt = session->currentUser();
    if (!userOpt.has_value()) {
        // Mid-logout / un-authenticated: refuse without audit (no
        // attribution available).
        return false;
    }
    if (allowed(userOpt->role)) {
        return true;
    }
    if (logger != nullptr) {
        logger->warn("Presenter: action {} refused -- role {} lacks "
                     "permission",
                     action, app::auth::roleName(userOpt->role));
    }
    emitAudit(audit, session, category, action, details,
              app::auth::result::kFailure);
    return false;
}

}  // namespace

// User action handlers
void DashboardPresenter::onStartClicked() {
    if (!checkRole(audit_, session_, logger_,
                   app::auth::canStartStop,
                   app::auth::category::kProduction, "START")) return;
    LOG_IF(info,"User action: Start production");
    emitAudit(audit_, session_, app::auth::category::kProduction,
              "START", "");
    model_.startProduction();
}

void DashboardPresenter::onStopClicked() {
    if (!checkRole(audit_, session_, logger_,
                   app::auth::canStartStop,
                   app::auth::category::kProduction, "STOP")) return;
    LOG_IF(info,"User action: Stop production");
    emitAudit(audit_, session_, app::auth::category::kProduction,
              "STOP", "");
    model_.stopProduction();
}

void DashboardPresenter::onResetRestartClicked() {
    if (!checkRole(audit_, session_, logger_,
                   app::auth::canResetSystem,
                   app::auth::category::kProduction, "RESET")) return;
    LOG_IF(info,"User action: Reset system");
    emitAudit(audit_, session_, app::auth::category::kProduction,
              "RESET", "");
    model_.resetSystem();
}

void DashboardPresenter::onCalibrationClicked() {
    if (!checkRole(audit_, session_, logger_,
                   app::auth::canCalibrate,
                   app::auth::category::kProduction, "CALIBRATE")) return;
    LOG_IF(info,"User action: Start calibration");
    emitAudit(audit_, session_, app::auth::category::kProduction,
              "CALIBRATE", "");
    model_.startCalibration();
}

void DashboardPresenter::onEquipmentToggled(uint32_t equipmentId, bool enabled) {
    const auto details = std::format("equipment id {}", equipmentId);
    if (!checkRole(audit_, session_, logger_,
                   // Operator+ can toggle; Maintenance / Admin too.
                   [](app::auth::Role r) {
                       return app::auth::canStartStop(r);
                   },
                   app::auth::category::kEquipment,
                   enabled ? "ENABLE" : "DISABLE", details)) return;
    LOG_IF(info,"User action: Equipment {} toggled -> {}",
               equipmentId, enabled ? "enabled" : "disabled");
    emitAudit(audit_, session_, app::auth::category::kEquipment,
              enabled ? "ENABLE" : "DISABLE", details);
    model_.setEquipmentEnabled(equipmentId, enabled);
}

// Model signal handlers
void DashboardPresenter::handleNewWorkUnit(const std::string& workUnitId) {
    LOG_IF(trace,"Model event: work unit changed ({})", workUnitId);
    auto vm = buildWorkUnitVM(workUnitId);
    notifyWorkUnitChanged(vm);
}

void DashboardPresenter::handleEquipmentStatusUpdate(uint32_t equipmentId, int status) {
    LOG_IF(trace, "Model event: equipment {} -> {}",
           equipmentId, equipmentStatusName(status));
    auto vm = buildEquipmentVM(equipmentId, status);
    notifyEquipmentCardChanged(vm);

    // Raise/clear a sidebar alert for this equipment. Keyed by id so
    // repeated updates stay at one row in the Alerts panel.
    if (alertCenter_) {
        const auto key = std::string("equipment-") + std::to_string(equipmentId);
        if (status == 0 /* Offline */ || status == 3 /* Error */) {
            presenter::AlertViewModel a;
            a.key      = key;
            a.severity = (status == 3)
                             ? presenter::AlertSeverity::Critical
                             : presenter::AlertSeverity::Warning;
            // Install a retranslate callback that captures the raw id
            // and the status-kind flag, so a later language switch can
            // re-render the title in the new locale (including for rows
            // that have already moved to history).
            const bool isError = (status == 3);
            a.retranslate = [equipmentId, isError](presenter::AlertViewModel& vm) {
                auto titleFmt = isError
                                    ? std::string_view{_("Equipment {} error")}
                                    : std::string_view{_("Equipment {} offline")};
                vm.title   = std::vformat(titleFmt,
                                          std::make_format_args(equipmentId));
                vm.message = _("Line unavailable for production.");
            };
            // Initial render with the currently active locale.
            a.retranslate(a);
            alertCenter_->raise(a);
        } else {
            alertCenter_->clear(key);
        }
    }
}

void DashboardPresenter::handleActuatorStatusUpdate(uint32_t actuatorId, int status) {
    LOG_IF(trace, "Model event: actuator {} -> {}",
           actuatorId, actuatorStatusName(status));
    auto vm = buildActuatorVM(actuatorId, status);
    notifyActuatorCardChanged(vm);
}

void DashboardPresenter::handleQualityCheckpointUpdate(uint32_t checkpointId, int status) {
    LOG_IF(trace, "Model event: quality checkpoint {} -> {}",
           checkpointId, qualityStatusName(status));
    auto vm = buildQualityCheckpointVM(checkpointId, status);
    notifyQualityCheckpointChanged(vm);

    // Surface quality deviations in the sidebar. We use the VM we just
    // built (it already has the derived Passing/Warning/Critical enum +
    // pass rate) so the alert phrasing stays consistent with the card.
    if (alertCenter_) {
        const auto key = std::string("quality-") + std::to_string(checkpointId);
        using Status = presenter::QualityCheckpointStatus;
        if (vm.status == Status::Passing) {
            alertCenter_->clear(key);
        } else {
            presenter::AlertViewModel a;
            a.key      = key;
            a.severity = (vm.status == Status::Critical)
                             ? presenter::AlertSeverity::Critical
                             : presenter::AlertSeverity::Warning;
            // Capture the raw numbers + checkpoint name so the callback
            // can re-render in any future locale (and for history rows).
            const bool isCritical     = (vm.status == Status::Critical);
            const std::string cpName  = vm.checkpointName;
            const float passRate      = vm.passRate;
            const float targetPassRate = vm.targetPassRate;
            a.retranslate = [isCritical, cpName, passRate, targetPassRate]
                            (presenter::AlertViewModel& vmm) {
                auto titleFmt = isCritical
                                    ? std::string_view{_("{} critical")}
                                    : std::string_view{_("{} below target")};
                vmm.title   = std::vformat(titleFmt,
                                           std::make_format_args(cpName));
                vmm.message = std::vformat(
                    _("Pass rate {:.1f}% (target {:.0f}%)"),
                    std::make_format_args(passRate, targetPassRate));
            };
            a.retranslate(a);
            alertCenter_->raise(a);
        }
    }
}

void DashboardPresenter::handleSystemStateChanged(model::SystemState newState) {
    const int newStateInt = static_cast<int>(newState);
    LOG_IF(debug, "Model event: system state -> {}", systemStateName(newStateInt));

    // ISA-18.2 wire-up for the safe-state path (REQ-STATE-003 + REQ-ALARM-001).
    // Entering ERROR raises a Critical "system-error" alarm carrying the
    // fault reason from the model; leaving ERROR (via Reset) clears the
    // alarm. The alarm uses the same key on every entry so re-faulting
    // updates the existing card rather than stacking duplicates.
    if (alertCenter_ != nullptr) {
        const bool nowError  = (newState == model::SystemState::ERROR);
        const bool wasError  = (prevSystemState_ == model::SystemState::ERROR);
        constexpr std::string_view kSystemErrorKey = "system-error";
        if (nowError && !wasError) {
            presenter::AlertViewModel a;
            a.key      = std::string(kSystemErrorKey);
            a.severity = presenter::AlertSeverity::Critical;
            std::string reason = model_.lastFaultReason();
            a.retranslate = [reason](presenter::AlertViewModel& vm) {
                vm.title   = _("System fault");
                vm.message = reason.empty()
                                 ? std::string{_("Production locked out.")}
                                 : reason;
            };
            a.retranslate(a);
            alertCenter_->raise(a);
        } else if (!nowError && wasError) {
            alertCenter_->clear(kSystemErrorKey);
        }
        prevSystemState_ = newState;
    }

    auto vm = buildControlPanelVM();
    notifyControlPanelChanged(vm);
    signalSystemStateChanged_.emit(newStateInt);
}

// ViewModel builders
presenter::WorkUnitViewModel DashboardPresenter::buildWorkUnitVM(const std::string& workUnitId) {
    auto wu = model_.getWorkUnit();

    presenter::WorkUnitViewModel vm;
    vm.workUnitId = wu.workUnitId;
    vm.productId = wu.productId;
    vm.productDescription = wu.description;
    vm.completedOperations = wu.completedOperations;
    vm.totalOperations = wu.totalOperations;
    vm.throughputUph = wu.throughputUnitsPerHour;
    vm.oeePct = model_.oeeSnapshot().oeePct;
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
    auto cp = model_.getQualityCheckpoint(checkpointId);

    presenter::QualityCheckpointViewModel vm;
    vm.checkpointId = checkpointId;
    vm.checkpointName = cp.name;
    vm.unitsInspected = cp.unitsInspected;
    vm.defectsFound = cp.defectsFound;
    vm.passRate = cp.passRate;
    // Target comes from the active product's recipe (set via
    // ProductionModel::loadProduct, stored on the checkpoint). Falls
    // back to the global default for checkpoints with no recipe loaded
    // (passRateTarget defaults to that value on a fresh checkpoint).
    vm.targetPassRate = cp.passRateTarget;
    vm.lastDefect = cp.lastDefect;

    // The quality enum is recomputed on every tick, so WARN/ERROR here
    // would flood the log while a checkpoint stays below threshold. Keep
    // these as TRACE; the Alert panel is the right surface for
    // persistently-critical states.
    if (cp.passRate >= config::defaults::kQualityPassThreshold) {
        vm.status = presenter::QualityCheckpointStatus::Passing;
    } else if (cp.passRate >= config::defaults::kQualityWarningThreshold) {
        vm.status = presenter::QualityCheckpointStatus::Warning;
        LOG_IF(trace,"Quality checkpoint {} ({}) WARNING: {:.1f}%",
                    checkpointId, cp.name, cp.passRate);
    } else {
        vm.status = presenter::QualityCheckpointStatus::Critical;
        LOG_IF(trace,"Quality checkpoint {} ({}) CRITICAL: {:.1f}%",
                    checkpointId, cp.name, cp.passRate);
    }

    return vm;
}

presenter::ControlPanelViewModel DashboardPresenter::buildControlPanelVM() {
    auto state = model_.getState();

    presenter::ControlPanelViewModel vm;

    using State = model::SystemState;

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
